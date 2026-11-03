// SDL2 platform layer for STACKFALL, tasks T002, T009, T010, T011, T012,
// T013, and T014.
//
// This file owns everything the rules core must never see: the window and
// renderer, game controller discovery, the EXIT shutdown path, and the
// fixed-step 60 Hz main loop of specification section 7. Input translation
// lives next door in src/input_sdl.cpp: the loop polls every event through
// SdlInput, then runs the due simulation ticks, and each tick consumes one
// TickInput from the adapter, so a press edge fires exactly once even when a
// frame catches up through several ticks (specification section 5, section
// 11 item 19). Each tick goes to the session path Game::tick(TickInput), so
// the section 4.7 screens of T011 run in the same loop as the round: the
// session opens at the title menu, Start Game opens a round, and Quit leaves
// through the same clean EXIT path as a window close. Every rendered frame
// goes through the read-only procedural Renderer of src/renderer_sdl.cpp,
// which draws the current screen from rectangles, lines, and the 5x7 glyph
// table alone. The Q key stays a developer shortcut for an immediate quit.
//
// The developer options --input-log, --input-script, --virtual-controller,
// and --seed exist for the unattended verification of docs/AGENT_TESTING.md:
// they record and drive input without a human. Normal startup ignores all of
// them. The unattended harness of task T023 adds five more: --save-dir points
// the two persistent stores and the session log at an explicit directory,
// --observations names the structured checkpoint record file,
// --screenshot-dir receives one BMP per named checkpoint, --fixture-dir
// resolves the `fixture` script command, and --tick-per-frame runs exactly one
// simulation tick per frame with pacing off, which makes a scripted session's
// tick numbering independent of wall-clock batching. Every one of them is
// inert unless a harness passes it.
//
// High-score persistence (task T012) runs through src/storage.cpp: the store
// resolves the launcher's conf/ directory from STACKFALL_SAVE_DIR or
// XDG_DATA_HOME, the session loads the record before the title screen draws
// it, and a round that sets a record saves it at game over.
//
// Sound (task T013) runs through src/audio_sdl.cpp: the loop observes every
// simulation tick with a CueDetector, a working device plays the five
// synthesized cues of specification section 6.3, and the M key or the
// controller Select button switches the mute, whose starting state comes from
// settings.dat beside the high-score record. A missing audio device logs one
// line and changes nothing else about the session.
//
// Logging (task T014, specification section 9) goes to stdout and, whenever a
// location resolves, to log.txt itself: the sink opens before anything that
// can fail, so the startup block and every failure below leave their line on
// disk even when stdout went to a terminal. STACKFALL_LOG_FILE names the file
// exactly; without it the file lands beside the high-score record in the save
// directory - the --save-dir option's directory when a harness passed one
// (task T033), the environment's directory otherwise - a place a launcher's
// own stdout redirection into the game directory of section 10.3 never
// touches, so the two sinks cannot corrupt one file. The startup block names
// the version, the build architecture, and
// the SDL version; a renderer setup that cannot produce a backend at all -
// including an SDL_RENDER_DRIVER the installed SDL does not carry - prints
// one actionable error and leaves through a clean exit; a video subsystem
// that will not start exits through the same clean path; a controller
// subsystem that will not start leaves the keyboard playable; and the loop's
// fallback frame limiter holds 60 Hz pacing whenever presentation does not
// block.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include <filesystem>
#include <string>
#include <vector>

#include <SDL.h>

#include "stackfall/audio_sdl.hpp"
#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/input_sdl.hpp"
#include "stackfall/observe.hpp"
#include "stackfall/renderer_sdl.hpp"
#include "stackfall/storage.hpp"
#include "stackfall/version.hpp"

namespace {

// Simulation constants (specification sections 3 and 6.1). The canvas size
// and every layout constant live in stackfall/renderer_sdl.hpp, so the loop
// and the renderer share one geometry.
constexpr int kSimulationHz = 60;
// A frame delta above kStallWarnMs is an unusual stall and gets logged; a
// delta above kMaxFrameMs comes from a resume or suspend and is clamped so
// the loop never tries to catch up through minutes of dead time
// (specification section 7).
constexpr Uint64 kStallWarnMs = 50;
constexpr Uint64 kMaxFrameMs = 250;

// The RG503 has one gamepad; a few spare slots keep desktop testing with
// several controllers harmless.
constexpr int kMaxControllers = 4;

using stackfall::AudioDriver;
using stackfall::CueDetector;
using stackfall::CueSet;
using stackfall::Game;
using stackfall::HighScoreStore;
using stackfall::LoadResult;
using stackfall::Renderer;
using stackfall::SaveStatus;
using stackfall::Scene;
using stackfall::SettingsStore;
using stackfall::SdlInput;
using stackfall::SoundCue;
using stackfall::kCanvasHeight;
using stackfall::kCanvasWidth;
using stackfall::kLogFileEnv;
using stackfall::kSoundCueCount;
using stackfall::saveStatusName;
using stackfall::soundCueName;

// ---------------------------------------------------------------------------
// Session log (task T014, specification section 9).
//
// Every line this platform layer (src/main.cpp) prints reaches stdout and,
// once a location resolved, log.txt. The sink opens before anything that can
// fail, so the startup banner and the failure paths of this file (SDL_Init,
// the window, every renderer attempt, a save that did not write, a
// controller that left) all leave their entry on disk - every entry section 9
// requires lives here. The SDL adapters keep a few informational lines of
// their own on stdout (the audio device state, mapping notes, focus loss);
// the launcher's stdout redirection of section 9 carries those into the game
// directory's log.txt, while the game's own file lives in the save directory
// unless STACKFALL_LOG_FILE names another one, so the two never collide.
// ---------------------------------------------------------------------------

// The log file of this process, or nullptr while the session logs to stdout
// alone.
std::FILE* g_sessionLog = nullptr;

// Writes one line to `stream` and the log file. A va_list cannot be walked
// twice, so the file reads a copy.
void logLine(std::FILE* stream, const char* format, std::va_list args) {
  std::va_list copy;
  va_copy(copy, args);
  std::vfprintf(stream, format, args);
  if (g_sessionLog != nullptr) {
    std::vfprintf(g_sessionLog, format, copy);
    std::fflush(g_sessionLog);  // a terminated run has still written it all
  }
  va_end(copy);
}

// One information line: stdout and log.txt.
void logf(const char* format, ...) {
  std::va_list args;
  va_start(args, format);
  logLine(stdout, format, args);
  va_end(args);
}

// One error line: stderr and log.txt, so a failed run leaves its actionable
// error on disk beside the lines that led to it.
void logErrorf(const char* format, ...) {
  std::va_list args;
  va_start(args, format);
  logLine(stderr, format, args);
  va_end(args);
}

// Opens this session's log.txt, replacing any earlier run's file. An empty
// path means no location resolved and the session logs to stdout alone; a
// path that cannot be opened costs the file, never the game.
void openSessionLog(const std::string& path) {
  if (path.empty()) {
    logf("Log: stdout only; set %s or a save directory to keep log.txt\n",
         kLogFileEnv);
    return;
  }
  // The parent directories appear on demand, exactly as they do for a save,
  // so a fresh STACKFALL_LOG_FILE needs no prepared directory.
  const std::filesystem::path file(path);
  if (file.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
  }
  g_sessionLog = std::fopen(path.c_str(), "w");
  if (g_sessionLog == nullptr) {
    logErrorf("Warning: cannot open log file \"%s\"; logging to stdout only\n",
              path.c_str());
    return;
  }
  logf("Log: stdout and %s\n", path.c_str());
}

void closeSessionLog() {
  if (g_sessionLog != nullptr) {
    std::fclose(g_sessionLog);
    g_sessionLog = nullptr;
  }
}

// ---------------------------------------------------------------------------
// Command line.
// ---------------------------------------------------------------------------

struct Options {
  Uint64 frameLimit;  // 0 means run until the user quits
  bool help;
  bool hasSeed;
  std::uint64_t seed;
  const char* inputLog;        // nullptr: no per-tick input log
  const char* inputScript;     // nullptr: no scripted input
  bool virtualController;      // attach the developer virtual pad
  const char* saveDir;         // nullptr: resolve the save dir from environment
  const char* observations;    // nullptr: no checkpoint records
  const char* screenshotDir;   // nullptr: checkpoint shots log as unavailable
  const char* fixtureDir;      // nullptr: `fixture` commands cannot resolve
  bool tickPerFrame;           // developer: one tick per frame, pacing off
};

void printUsage() {
  // Usage is not a log entry: --help answers on stdout and never opens a file.
  std::printf(
      "Usage: stackfall [--frames N] [--seed N] [--input-log FILE]\n"
      "                 [--input-script FILE] [--virtual-controller]\n"
      "                 [--save-dir DIR] [--observations FILE]\n"
      "                 [--screenshot-dir DIR] [--fixture-dir DIR]\n"
      "                 [--tick-per-frame]\n"
      "  --frames N            render N frames, then exit (0 = until quit)\n"
      "  --seed N              start the round from seed N (deterministic)\n"
      "  --input-log FILE      record one line per simulation tick\n"
      "  --input-script FILE   run a scripted input session (developer)\n"
      "  --virtual-controller  attach the developer virtual gamepad\n"
      "  --save-dir DIR        write the high score, settings, and log here\n"
      "  --observations FILE   write one record per checkpoint command\n"
      "  --screenshot-dir DIR  write one BMP per checkpoint command\n"
      "  --fixture-dir DIR     resolve `fixture` script commands here\n"
      "  --tick-per-frame      one simulation tick per frame, pacing off\n"
      "  --help                show this text\n");
}

// Parses a whole non-negative decimal string into `out`; false on anything
// else, so option values and script values share one strict parser.
bool parseUnsigned(const char* text, std::uint64_t* out) {
  if (text[0] == '\0') {
    return false;
  }
  std::uint64_t value = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    if (value > (~std::uint64_t{0} - 9) / 10) {
      return false;  // overflow
    }
    value = value * 10 + static_cast<std::uint64_t>(*p - '0');
  }
  *out = value;
  return true;
}

// Parses the developer options. Unknown options warn and are ignored, so a
// launcher environment cannot break startup. Scripted sessions need exact
// values, so a missing option value warns and drops the option.
Options parseOptions(int argc, char** argv) {
  Options options{0,   false, false, 0,    nullptr, nullptr,
                  false, nullptr, nullptr, nullptr, nullptr, false};
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const bool needsValue = std::strcmp(arg, "--frames") == 0 ||
                            std::strcmp(arg, "--seed") == 0 ||
                            std::strcmp(arg, "--input-log") == 0 ||
                            std::strcmp(arg, "--input-script") == 0 ||
                            std::strcmp(arg, "--save-dir") == 0 ||
                            std::strcmp(arg, "--observations") == 0 ||
                            std::strcmp(arg, "--screenshot-dir") == 0 ||
                            std::strcmp(arg, "--fixture-dir") == 0;
    if (needsValue) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Warning: %s needs a value; ignored\n", arg);
        continue;
      }
      ++i;
      const char* value = argv[i];
      std::uint64_t parsed = 0;
      if ((std::strcmp(arg, "--frames") == 0 ||
           std::strcmp(arg, "--seed") == 0) &&
          parseUnsigned(value, &parsed)) {
        if (std::strcmp(arg, "--frames") == 0) {
          options.frameLimit = parsed;
        } else {
          options.hasSeed = true;
          options.seed = parsed;
        }
      } else if (std::strcmp(arg, "--frames") == 0 ||
                 std::strcmp(arg, "--seed") == 0) {
        std::fprintf(stderr,
                     "Warning: %s needs a number, got \"%s\"; ignored\n", arg,
                     value);
      } else if (std::strcmp(arg, "--input-log") == 0) {
        options.inputLog = value;
      } else if (std::strcmp(arg, "--input-script") == 0) {
        options.inputScript = value;
      } else if (std::strcmp(arg, "--save-dir") == 0) {
        options.saveDir = value;
      } else if (std::strcmp(arg, "--observations") == 0) {
        options.observations = value;
      } else if (std::strcmp(arg, "--screenshot-dir") == 0) {
        options.screenshotDir = value;
      } else {
        options.fixtureDir = value;
      }
    } else if (std::strcmp(arg, "--help") == 0) {
      options.help = true;
    } else if (std::strcmp(arg, "--virtual-controller") == 0) {
      options.virtualController = true;
    } else if (std::strcmp(arg, "--tick-per-frame") == 0) {
      options.tickPerFrame = true;
    } else {
      std::fprintf(stderr, "Warning: unknown option \"%s\"; ignored\n", arg);
    }
  }
  return options;
}

// A seed for an unscripted round: mixed from the performance counter, in the
// platform layer only, because the core must stay clock-free
// (specification section 7).
std::uint64_t deriveSeed() {
  std::uint64_t s = static_cast<std::uint64_t>(SDL_GetPerformanceCounter()) *
                    0x9E3779B97F4A7C15ULL;
  s ^= s >> 30;
  s *= 0xBF58476D1CE4E5B9ULL;
  s ^= s >> 27;
  s *= 0x94D049BB133111EBULL;
  return s ^ (s >> 31);
}

// ---------------------------------------------------------------------------
// Game controller set. Controllers open and close at any time; the keyboard
// is the fallback whenever no controller is open (specification sections 5
// and 9). SDL queues an SDL_CONTROLLERDEVICEADDED event for every controller
// already attached when the subsystem starts, so the added handler skips
// devices that discovery already opened.
// ---------------------------------------------------------------------------

struct ControllerSlot {
  SDL_GameController* handle;
  SDL_JoystickID instance;
};

class ControllerSet {
 public:
  // Opens and logs every attached game controller, and names joysticks that
  // carry no controller mapping (a missing SDL_GAMECONTROLLERCONFIG on a new
  // device shows up here). Returns how many controllers are open.
  int discover() {
    const int joysticks = SDL_NumJoysticks();
    for (int device = 0; device < joysticks; ++device) {
      if (SDL_IsGameController(device) == SDL_TRUE) {
        open(device);
      } else {
        const char* name = SDL_JoystickNameForIndex(device);
        logf(
            "joystick \"%s\" has no game controller mapping; keyboard "
            "fallback stays active\n",
            name != nullptr ? name : "?");
      }
    }
    if (openCount() == 0) {
      logf("No game controller detected; keyboard fallback active\n");
    }
    return openCount();
  }

  // SDL_CONTROLLERDEVICEADDED carries a device index.
  void onAdded(int deviceIndex) {
    const SDL_JoystickID instance =
        SDL_JoystickGetDeviceInstanceID(deviceIndex);
    for (int i = 0; i < count_; ++i) {
      if (slots_[i].instance == instance) {
        return;  // discovery already opened this device
      }
    }
    open(deviceIndex);
  }

  // SDL_CONTROLLERDEVICEREMOVED carries an instance id. A disconnect must
  // never crash the game (specification section 5).
  void onRemoved(SDL_JoystickID instance) {
    for (int i = 0; i < count_; ++i) {
      if (slots_[i].instance != instance) {
        continue;
      }
      const char* name = SDL_GameControllerName(slots_[i].handle);
      logf("Game controller \"%s\" (instance %d) removed\n",
           name != nullptr ? name : "?", static_cast<int>(instance));
      SDL_GameControllerClose(slots_[i].handle);
      slots_[i] = slots_[count_ - 1];
      --count_;
      if (count_ == 0) {
        logf("No game controller left; keyboard fallback active\n");
      }
      return;
    }
  }

  int openCount() const { return count_; }

  void closeAll() {
    for (int i = 0; i < count_; ++i) {
      SDL_GameControllerClose(slots_[i].handle);
    }
    count_ = 0;
  }

 private:
  void open(int deviceIndex) {
    if (count_ >= kMaxControllers) {
      logf("Game controller limit of %d reached; device %d ignored\n",
           kMaxControllers, deviceIndex);
      return;
    }
    SDL_GameController* handle = SDL_GameControllerOpen(deviceIndex);
    if (handle == nullptr) {
      logf("Game controller device %d failed to open: %s\n", deviceIndex,
           SDL_GetError());
      return;
    }
    const SDL_JoystickID instance =
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(handle));
    const char* name = SDL_GameControllerName(handle);
    logf("Game controller \"%s\" (instance %d) opened\n",
         name != nullptr ? name : "?", static_cast<int>(instance));
    slots_[count_] = ControllerSlot{handle, instance};
    ++count_;
  }

  ControllerSlot slots_[kMaxControllers];
  int count_ = 0;
};

// ---------------------------------------------------------------------------
// Scripted input for recorded desktop sessions (docs/AGENT_TESTING.md, T023).
// A script is a list of commands, one per line, '#' starts a comment:
//
//   key <name> down|up    push a keyboard event into the SDL queue
//   pad <name> down|up    press a virtual pad button
//   wait <ticks>          hold the script until that many ticks ran
//   stall <ms>            delay the frame, forcing a catch-up batch
//   detach                detach the virtual controller
//   restart               restart the round with the same seed
//   fixture <name>        load a board fixture from --fixture-dir
//   check <name>          record one observation and one screenshot
//   shot <name>           record one screenshot, no observation
//   quit                  leave the main loop cleanly
//   echo <text>           log a marker line
//
// Commands run before the event poll of each frame, in order, until the next
// pending `wait` completes. Key and pad presses travel the real SDL event
// path; nothing here touches the adapter directly.
//
// `check` and `shot` only mark the checkpoint here. The main loop flushes the
// marks after the frame's simulation ticks and its render, so the observation
// record and the screenshot describe exactly the state the frame drew - the
// same state the headless reference replay of tests/harness records after its
// tick, which is what makes the two streams comparable.
// ---------------------------------------------------------------------------

struct ScriptContext {
  SDL_Joystick* virtualJoystick = nullptr;  // opened handle for pad commands
  int virtualDevice = -1;                   // device index for detach
  Game* game = nullptr;                     // for restart and fixtures
  CueDetector* cues = nullptr;              // a restart resets the snapshot
  std::FILE* inputLog = nullptr;            // markers land next to the ticks
  const char* fixtureDir = nullptr;         // resolves `fixture` commands
  // Checkpoints the loop flushes after the current frame renders.
  std::vector<std::string> pendingChecks;
  std::vector<std::string> pendingShots;
};

class InputScript {
 public:
  bool load(const char* path, ScriptContext* context) {
    context_ = context;
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
      logErrorf("Cannot open input script \"%s\"\n", path);
      return false;
    }
    std::string line;
    char buffer[256];
    bool ok = true;
    while (std::fgets(buffer, sizeof(buffer), file) != nullptr) {
      line = buffer;
      if (!parseLine(line) && ok) {
        ok = false;  // report the first bad line, keep reading for the report
      }
    }
    std::fclose(file);
    return ok;
  }

  // Runs the commands due before this frame's event poll.
  void pump(std::uint64_t tickCount) {
    while (index_ < commands_.size()) {
      const Command& command = commands_[index_];
      if (command.kind == Command::Kind::Wait) {
        if (!waiting_) {
          waitUntil_ = tickCount + static_cast<std::uint64_t>(command.value);
          waiting_ = true;
        }
        if (tickCount < waitUntil_) {
          return;
        }
        waiting_ = false;
        ++index_;
        continue;
      }
      run(command);
      ++index_;
    }
  }

 private:
  struct Command {
    enum class Kind : std::uint8_t {
      KeyDown,
      KeyUp,
      PadDown,
      PadUp,
      Wait,
      Stall,
      Detach,
      Restart,
      Fixture,
      Check,
      Shot,
      Quit,
      Echo
    };
    Kind kind;
    int value = 0;    // scancode, button, ticks, or milliseconds
    std::string text;  // echo marker, checkpoint, or fixture name
  };

  // The script key table: names for the mapped scancodes of section 5.
  struct KeyName {
    const char* name;
    SDL_Scancode scancode;
  };
  static const KeyName* findKey(const char* name) {
    static const KeyName keys[] = {
        {"left", SDL_SCANCODE_LEFT},
        {"right", SDL_SCANCODE_RIGHT},
        {"up", SDL_SCANCODE_UP},
        {"down", SDL_SCANCODE_DOWN},
        {"x", SDL_SCANCODE_X},
        {"z", SDL_SCANCODE_Z},
        {"space", SDL_SCANCODE_SPACE},
        {"c", SDL_SCANCODE_C},
        {"lshift", SDL_SCANCODE_LSHIFT},
        {"enter", SDL_SCANCODE_RETURN},
        {"esc", SDL_SCANCODE_ESCAPE},
        {"m", SDL_SCANCODE_M},
    };
    for (const KeyName& key : keys) {
      if (std::strcmp(key.name, name) == 0) {
        return &key;
      }
    }
    return nullptr;
  }

  static void pushKeyEvent(SDL_Scancode scancode, bool pressed) {
    SDL_Event event;
    SDL_zero(event);
    event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.repeat = 0;
    event.key.keysym.scancode = scancode;
    event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
    SDL_PushEvent(&event);
  }

  void marker(const char* text) {
    logf("script: %s\n", text);
    if (context_ != nullptr && context_->inputLog != nullptr) {
      std::fprintf(context_->inputLog, "script %s\n", text);
      std::fflush(context_->inputLog);
    }
  }

  void run(const Command& command) {
    switch (command.kind) {
      case Command::Kind::KeyDown:
      case Command::Kind::KeyUp: {
        const bool pressed = command.kind == Command::Kind::KeyDown;
        pushKeyEvent(static_cast<SDL_Scancode>(command.value), pressed);
        break;
      }
      case Command::Kind::PadDown:
      case Command::Kind::PadUp: {
        if (context_ == nullptr || context_->virtualJoystick == nullptr) {
          marker("pad command without a virtual controller; ignored");
          break;
        }
        const bool pressed = command.kind == Command::Kind::PadDown;
        SDL_JoystickSetVirtualButton(context_->virtualJoystick, command.value,
                                     pressed ? 1 : 0);
        // Virtual buttons set state; SDL emits the event only on the next
        // update. Apply it now, so a tap (down and up in one frame) still
        // produces both events, exactly like a quick real press.
        SDL_JoystickUpdate();
        break;
      }
      case Command::Kind::Wait:
        break;  // handled in pump()
      case Command::Kind::Stall:
        logf("script: stalling %d ms for a catch-up frame\n", command.value);
        SDL_Delay(command.value);
        break;
      case Command::Kind::Detach:
        if (context_ != nullptr && context_->virtualJoystick != nullptr) {
          SDL_JoystickClose(context_->virtualJoystick);
          context_->virtualJoystick = nullptr;
          stackfall::detachVirtualController(context_->virtualDevice);
          context_->virtualDevice = -1;
        }
        break;
      case Command::Kind::Restart:
        if (context_ != nullptr && context_->game != nullptr) {
          context_->game->start(context_->game->seed());
          // The round changed outside a simulation tick, so the cue detector
          // must forget its snapshot: the emptied board is a restart, not a
          // clear.
          if (context_->cues != nullptr) {
            context_->cues->reset();
          }
          marker("restart");
        }
        break;
      case Command::Kind::Fixture: {
        if (context_ == nullptr || context_->game == nullptr) {
          break;
        }
        if (context_->fixtureDir == nullptr) {
          marker("fixture command without --fixture-dir; ignored");
          break;
        }
        const std::string path = std::string(context_->fixtureDir) + "/" +
                                 command.text + ".board.txt";
        stackfall::BoardFixture fixture;
        std::string error;
        if (!stackfall::loadBoardFixture(path, &fixture, &error)) {
          marker(("fixture " + command.text + " unavailable: " + error).c_str());
          break;
        }
        if (!context_->game->applyBoardFixture(fixture)) {
          marker(("fixture " + command.text + " rejected: the round is not "
                  "playing or the board is inconsistent").c_str());
          break;
        }
        if (context_->cues != nullptr) {
          // A replaced board is not a line clear, exactly like a restart.
          context_->cues->reset();
        }
        const std::string activeText =
            fixture.hasActive
                ? std::string(1, stackfall::pieceLetter(fixture.active))
                : std::string("kept");
        // The fixture line is evidence of a labeled board state, not a session
        // event: stdout and the input log below carry it beside the tick
        // stream, and log.txt stays a log.
        std::printf(
            "Fixture \"%s\" applied from %s (provenance: %s; active %s)\n",
            fixture.name.c_str(), path.c_str(),
            fixture.provenance.empty() ? "unlabeled"
                                       : fixture.provenance.c_str(),
            activeText.c_str());
        if (context_->inputLog != nullptr) {
          std::fprintf(context_->inputLog, "fixture %s path=%s\n",
                       command.text.c_str(), path.c_str());
          std::fflush(context_->inputLog);
        }
        break;
      }
      case Command::Kind::Check:
        if (context_ != nullptr) {
          context_->pendingChecks.push_back(command.text);
        }
        break;
      case Command::Kind::Shot:
        if (context_ != nullptr) {
          context_->pendingShots.push_back(command.text);
        }
        break;
      case Command::Kind::Quit: {
        // Through the queue, like a window close: the loop leaves through
        // its ordinary exit path.
        SDL_Event event;
        SDL_zero(event);
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        marker("quit");
        break;
      }
      case Command::Kind::Echo:
        marker(command.text.c_str());
        break;
    }
  }

  // Parses one script line into commands_. Returns false and reports the
  // line on a syntax error, because a recorded session must not run halfway
  // by accident.
  bool parseLine(const std::string& raw) {
    const size_t hash = raw.find('#');
    std::string line = raw.substr(0, hash);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' ||
                             line.back() == ' ' || line.back() == '\t')) {
      line.pop_back();
    }
    if (line.empty()) {
      return true;
    }
    const size_t verbEnd = line.find_first_of(" \t");
    const std::string verb = line.substr(0, verbEnd);
    const std::string rest =
        verbEnd == std::string::npos ? "" : line.substr(verbEnd + 1);
    const size_t nameEnd = rest.find_first_of(" \t");
    const std::string name =
        rest.empty() || nameEnd == 0 ? "" : rest.substr(0, nameEnd);
    const std::string tail =
        nameEnd == std::string::npos ? "" : rest.substr(nameEnd + 1);
    const size_t tailEnd = tail.find_first_of(" \t");
    const std::string state = tail.substr(0, tailEnd);

    Command command;
    std::uint64_t number = 0;
    if (verb == "key" || verb == "pad") {
      // "key <name> down|up" and "pad <name> down|up".
      const bool down = state == "down";
      const bool up = state == "up";
      if (!down && !up) {
        return reportSyntax(raw, "key/pad needs down or up");
      }
      if (verb == "key") {
        const KeyName* key = findKey(name.c_str());
        if (key == nullptr) {
          return reportSyntax(raw, "unknown key name");
        }
        command.kind = down ? Command::Kind::KeyDown : Command::Kind::KeyUp;
        command.value = key->scancode;
      } else {
        const int button = stackfall::virtualPadButtonFromName(name.c_str());
        if (button < 0) {
          return reportSyntax(raw, "unknown pad button name");
        }
        command.kind = down ? Command::Kind::PadDown : Command::Kind::PadUp;
        command.value = button;
      }
    } else if (verb == "wait" || verb == "stall") {
      if (!parseUnsigned(name.c_str(), &number)) {
        return reportSyntax(raw, "wait/stall needs a number");
      }
      command.kind =
          verb == "wait" ? Command::Kind::Wait : Command::Kind::Stall;
      command.value = static_cast<int>(number);
    } else if (verb == "detach") {
      command.kind = Command::Kind::Detach;
    } else if (verb == "restart") {
      command.kind = Command::Kind::Restart;
    } else if (verb == "quit") {
      command.kind = Command::Kind::Quit;
    } else if (verb == "fixture" || verb == "check" || verb == "shot") {
      if (name.empty() || nameEnd != std::string::npos) {
        return reportSyntax(raw, "fixture/check/shot needs one name");
      }
      command.text = name;
      command.kind = verb == "fixture" ? Command::Kind::Fixture
                      : verb == "check" ? Command::Kind::Check
                                        : Command::Kind::Shot;
    } else if (verb == "echo") {
      command.kind = Command::Kind::Echo;
      command.text = rest;
    } else {
      return reportSyntax(raw, "unknown command");
    }
    commands_.push_back(std::move(command));
    return true;
  }

  static bool reportSyntax(const std::string& raw, const char* why) {
    // The line keeps its newline in the log file too, so one report cannot
    // run into the next entry.
    std::string line = raw;
    if (line.empty() || line.back() != '\n') {
      line += '\n';
    }
    logErrorf("Input script error (%s): %s", why, line.c_str());
    return false;
  }

  ScriptContext* context_ = nullptr;
  std::vector<Command> commands_;
  size_t index_ = 0;
  bool waiting_ = false;
  std::uint64_t waitUntil_ = 0;
};

// ---------------------------------------------------------------------------
// Window and renderer.
// ---------------------------------------------------------------------------

// The render driver names this build of SDL carries, comma-separated, so a
// failure message can tell the operator exactly what SDL_RENDER_DRIVER may
// contain here.
std::string renderDriverNames() {
  std::string names;
  const int count = SDL_GetNumRenderDrivers();
  for (int index = 0; index < count; ++index) {
    SDL_RendererInfo info;
    if (SDL_GetRenderDriverInfo(index, &info) != 0 || info.name == nullptr) {
      continue;
    }
    if (!names.empty()) {
      names += ", ";
    }
    names += info.name;
  }
  return names.empty() ? std::string("none") : names;
}

// True when `name` matches one of the render drivers this SDL carries.
// SDL compares driver names without case, so this check does too.
bool renderDriverKnown(const char* name) {
  const int count = SDL_GetNumRenderDrivers();
  for (int index = 0; index < count; ++index) {
    SDL_RendererInfo info;
    if (SDL_GetRenderDriverInfo(index, &info) == 0 && info.name != nullptr &&
        SDL_strcasecmp(name, info.name) == 0) {
      return true;
    }
  }
  return false;
}

// Tries an accelerated vsync renderer, then an accelerated one, then any
// backend SDL will give us, including software (specification section 9).
// Logs every attempt so a forced failure run shows which fallback won, and
// leaves the reason of the last failure in `lastError` for the caller's
// error message.
//
// One case is a failure before the first attempt: SDL_RENDER_DRIVER set to a
// name SDL does not carry. SDL would quietly ignore the variable and hand out
// whatever backend still matches the flags, but an operator who set it asked
// for that one driver, so the honest answer is the actionable error of a
// failed renderer setup, with the names that would have worked.
SDL_Renderer* createRenderer(SDL_Window* window, const char** lastError) {
  const char* forcedDriver = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
  if (forcedDriver != nullptr && forcedDriver[0] != '\0' &&
      !renderDriverKnown(forcedDriver)) {
    logf(
        "Renderer attempt \"SDL_RENDER_DRIVER=%s\" failed: SDL carries no "
        "render driver of that name\n",
        forcedDriver);
    logf("Render drivers available here: %s\n", renderDriverNames().c_str());
    *lastError = "SDL_RENDER_DRIVER names no render driver this SDL carries";
    return nullptr;
  }
  const struct {
    const char* description;
    Uint32 flags;
  } attempts[] = {
      {"accelerated vsync",
       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC},
      {"accelerated", SDL_RENDERER_ACCELERATED},
      {"any available backend", 0},
  };
  for (const auto& attempt : attempts) {
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, attempt.flags);
    if (renderer != nullptr) {
      SDL_RendererInfo info;
      if (SDL_GetRendererInfo(renderer, &info) == 0) {
        logf("Renderer: %s (%s%s)\n", info.name,
             info.flags & SDL_RENDERER_ACCELERATED ? "accelerated"
                                                   : "not accelerated",
             info.flags & SDL_RENDERER_PRESENTVSYNC ? ", vsync" : "");
      } else {
        logf("Renderer: selected (%s)\n", attempt.description);
      }
      return renderer;
    }
    *lastError = SDL_GetError();
    logf("Renderer attempt \"%s\" failed: %s\n", attempt.description,
         *lastError);
  }
  return nullptr;
}

// The build architecture, spelled out from compiler macros so the startup log
// names the machine the binary was built for on every toolchain: the AArch64
// handheld and an x86_64 desktop both identify themselves without a runtime
// query (task T014).
#if defined(__x86_64__) || defined(_M_X64)
constexpr char kBuildArch[] = "x86_64";
#elif defined(__aarch64__)
constexpr char kBuildArch[] = "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
constexpr char kBuildArch[] = "arm";
#elif defined(__i386__) || defined(_M_IX86)
constexpr char kBuildArch[] = "i386";
#else
constexpr char kBuildArch[] = "unknown";
#endif

// Converts a performance-counter delta to whole milliseconds. Dividing by
// counts-per-millisecond first keeps the result far from uint64 overflow even
// after a suspend lasting months.
Uint64 countsToMs(Uint64 counts, Uint64 frequency) {
  const Uint64 countsPerMs = frequency / 1000;
  if (countsPerMs == 0) {
    return 0;
  }
  return counts / countsPerMs;
}

// The fallback frame limiter (task T014, specification section 7).
// SDL_RenderPresent blocks for a vsync renderer, and that alone holds the
// loop at the 60 FPS render target. When presentation returns at once - a
// software renderer, or the dummy driver of the test runs - nothing blocks,
// and the loop would spin through thousands of frames per second while the
// accumulator still advances the round at 60 Hz. The limiter holds each
// frame to its 1/60 s slot, but only when the work of the frame finished
// early: a present that already blocked for most of the period is left
// alone, so the two pacing mechanisms never stack into a slower frame rate.
//
// `frameSlot` is the counter time this frame started at; on return it holds
// the slot of the next frame. A stall that left the cadence a full frame
// behind restarts the cadence at now, so a recovered loop resumes at 60 Hz
// instead of bursting through a backlog of unspent slots.
void paceFrame(Uint64& frameSlot, Uint64 framePeriodCounts, Uint64 frequency,
               bool& limiterLogged) {
  const Uint64 target = frameSlot + framePeriodCounts;
  const Uint64 finished = SDL_GetPerformanceCounter();
  if (finished < target) {
    const Uint64 remainingMs = countsToMs(target - finished, frequency);
    // Below 2 ms the sleep would overshoot the slot; a present that blocked
    // for almost the whole period ends here, and the small remainder is
    // left to the next frame.
    if (remainingMs >= 2) {
      if (!limiterLogged) {
        limiterLogged = true;
        logf(
            "Fallback frame limiter engaged: presentation returned early; "
            "pacing at %d Hz\n",
            kSimulationHz);
      }
      SDL_Delay(static_cast<Uint32>(remainingMs - 1));
      while (SDL_GetPerformanceCounter() < target) {
        SDL_Delay(1);
      }
    }
  }
  frameSlot += framePeriodCounts;
  const Uint64 now = SDL_GetPerformanceCounter();
  if (now > frameSlot + framePeriodCounts) {
    frameSlot = now;
  }
}

// ---------------------------------------------------------------------------
// Input log of a recorded session: one line per simulation tick.
// ---------------------------------------------------------------------------

const char* sceneName(Scene scene) {
  switch (scene) {
    case Scene::Boot:
      return "boot";
    case Scene::Title:
      return "title";
    case Scene::Controls:
      return "controls";
    case Scene::Playing:
      return "playing";
    case Scene::Paused:
      return "paused";
    case Scene::GameOver:
      return "gameover";
  }
  return "?";
}

void logTick(std::FILE* log, std::uint64_t tick, int slot, int batch,
             const stackfall::TickInput& input) {
  const stackfall::InputFrame& f = input.game;
  std::fprintf(log,
               "tick=%llu slot=%d/%d L=%d R=%d D=%d cw=%d ccw=%d drop=%d "
               "hold=%d pause=%d menu=%d%d%d%d\n",
               static_cast<unsigned long long>(tick), slot, batch, f.leftHeld,
               f.rightHeld, f.downHeld, f.rotateCWPressed, f.rotateCCWPressed,
               f.hardDropPressed, f.holdPressed, f.pausePressed,
               input.menu.upPressed, input.menu.downPressed,
               input.menu.selectPressed, input.menu.backPressed);
}

// ---------------------------------------------------------------------------
// High-score persistence (task T012, specification sections 9 and 10.3).
// ---------------------------------------------------------------------------

// Loads the stored record into the session. Every outcome is logged, because
// a save that silently fails is a lost record: a missing, invalid, or partial
// file recovers to high score 0 and an environment without a save directory
// disables persistence instead of guessing a writable location.
void loadStoredHighScore(const HighScoreStore& store, Game& game,
                         std::FILE* inputLog) {
  const LoadResult record = store.load();
  game.setHighScore(record.highScore);
  if (store.hasDirectory()) {
    logf("High score %llu (%s) from %s\n",
         static_cast<unsigned long long>(record.highScore),
         saveStatusName(record.status), store.filePath().c_str());
  } else {
    logf("No save directory: set %s or XDG_DATA_HOME; the high score "
         "will not persist this session\n",
         stackfall::kSaveDirEnv);
  }
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "highscore load=%llu status=%s\n",
                 static_cast<unsigned long long>(record.highScore),
                 saveStatusName(record.status));
  }
}

// Saves the record when a round sets one (specification section 4.7): the
// game-over screen already raised the session high score, so the platform
// layer writes that value through the atomic save and reports the outcome.
void saveHighScore(const HighScoreStore& store, const Game& game,
                   std::FILE* inputLog) {
  const SaveStatus status = store.save(game.highScore());
  if (status == SaveStatus::Ok) {
    logf("New high score %llu saved to %s\n",
         static_cast<unsigned long long>(game.highScore()),
         store.filePath().c_str());
  } else {
    logf("Saving the high score %llu failed: %s\n",
         static_cast<unsigned long long>(game.highScore()),
         saveStatusName(status));
  }
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "highscore save=%llu status=%s\n",
                 static_cast<unsigned long long>(game.highScore()),
                 saveStatusName(status));
  }
}

// One rendered frame: the letterbox base paints the bars of a window that is
// not the canvas (task T032, specification section 6.1), the procedural
// renderer draws the whole round in the section 6.2 order, and the present
// call shows it (task T010).
void renderFrame(Renderer& renderer, const Game& game,
                 SDL_Renderer* sdlRenderer) {
  stackfall::beginFrame(sdlRenderer);
  renderer.draw(game);
  SDL_RenderPresent(sdlRenderer);
}

// ---------------------------------------------------------------------------
// Checkpoint capture (docs/AGENT_TESTING.md, T023). A checkpoint saves the
// frame the loop just rendered, exactly as presented: readOutputPixels copies
// the whole renderer output - the letterbox bars of a non-native window
// included (task T032) - into a fresh surface and SDL_SaveBMP writes the
// lossless BMP, both from core SDL2, so no image library and no asset path is
// involved. The screenshot names the checkpoint, never a frame number, so
// evidence stays comparable across runs.
// ---------------------------------------------------------------------------

bool saveScreenshot(SDL_Renderer* sdlRenderer, const std::string& path) {
  int width = 0;
  int height = 0;
  if (SDL_GetRendererOutputSize(sdlRenderer, &width, &height) != 0) {
    logf("Screenshot %s failed: %s\n", path.c_str(), SDL_GetError());
    return false;
  }
  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
      0, width, height, 32, SDL_PIXELFORMAT_RGB888);
  if (surface == nullptr) {
    logf("Screenshot %s failed: %s\n", path.c_str(), SDL_GetError());
    return false;
  }
  bool ok = true;
  if (!stackfall::readOutputPixels(sdlRenderer, surface)) {
    logf("Screenshot %s failed to read pixels: %s\n", path.c_str(),
         SDL_GetError());
    ok = false;
  } else if (SDL_SaveBMP(surface, path.c_str()) != 0) {
    logf("Screenshot %s failed to save: %s\n", path.c_str(), SDL_GetError());
    ok = false;
  }
  SDL_FreeSurface(surface);
  return ok;
}

// Flushes the checkpoints a script marked this frame. Called after the
// frame's ticks and its render, so the record and the image describe the
// state that was drawn; the observation names the screenshot it saved, and a
// checkpoint without a screenshot directory records `shot=none` instead of
// failing, because a rules-only session still deserves its records.
void flushCheckpoints(ScriptContext& context, const Game& game,
                      SDL_Renderer* sdlRenderer, const char* screenshotDir,
                      std::FILE* observations) {
  for (const std::string& name : context.pendingShots) {
    if (screenshotDir == nullptr) {
      logf("Checkpoint %s has no --screenshot-dir; no image\n", name.c_str());
      continue;
    }
    const std::string path =
        std::string(screenshotDir) + "/" + name + ".bmp";
    if (saveScreenshot(sdlRenderer, path)) {
      logf("Checkpoint shot %s -> %s\n", name.c_str(), path.c_str());
    }
  }
  context.pendingShots.clear();
  for (const std::string& name : context.pendingChecks) {
    std::string shotPath = "none";
    if (screenshotDir != nullptr) {
      const std::string path =
          std::string(screenshotDir) + "/" + name + ".bmp";
      if (saveScreenshot(sdlRenderer, path)) {
        shotPath = path;
        logf("Checkpoint %s -> %s\n", name.c_str(), path.c_str());
      }
    } else {
      logf("Checkpoint %s has no --screenshot-dir; no image\n", name.c_str());
    }
    const std::string record =
        stackfall::observationLine(name, game) + " shot=" + shotPath;
    // The record line is structured data, not a session event: stdout and the
    // observations file carry it, and log.txt stays a log.
    std::printf("%s\n", record.c_str());
    if (observations != nullptr) {
      std::fprintf(observations, "%s\n", record.c_str());
      std::fflush(observations);
    }
  }
  context.pendingChecks.clear();
}

// ---------------------------------------------------------------------------
// Sound (task T013, specification section 6.3).
// ---------------------------------------------------------------------------

// The sound toggle is platform-layer state, so it never travels through the
// rules adapter: the keyboard M key and the controller Select button switch it
// here, directly in the event loop, and the setting is written to settings.dat
// the moment it changes, so a session interrupted right after a toggle keeps
// the player's choice.
void toggleSound(AudioDriver& audio, const SettingsStore& settings,
                 std::FILE* inputLog) {
  audio.toggleMute();
  const SaveStatus status = settings.save(!audio.muted());
  if (status != SaveStatus::Ok) {
    logf("Saving the sound setting failed: %s\n", saveStatusName(status));
  }
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "sound %s status=%s\n",
                 audio.muted() ? "muted" : "unmuted", saveStatusName(status));
  }
}

// One line of per-cue counters, for stdout and the input log. `requested` is
// what the round raised, `played` is what a device actually sounded, and the
// difference is the mute and the missing device, so an unattended run can
// prove that a silent session still detected every event.
std::string soundCountText(const AudioDriver& audio, bool requested) {
  std::string text;
  for (int index = 0; index < kSoundCueCount; ++index) {
    const SoundCue cue = static_cast<SoundCue>(index);
    text += index == 0 ? "" : " ";
    text += soundCueName(cue);
    text += "=";
    text += std::to_string(requested ? audio.requestedCount(cue)
                                     : audio.playedCount(cue));
  }
  return text;
}

void logSoundCounts(const AudioDriver& audio, std::FILE* inputLog) {
  logf(
      "Sound cues: requested %s, played %s, %u muted, %u without a device\n",
      soundCountText(audio, true).c_str(), soundCountText(audio, false).c_str(),
      audio.mutedCount(), audio.droppedCount());
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "sfx counts %s\n",
                 soundCountText(audio, true).c_str());
  }
}

// The cue evidence of one tick. The input log keeps one line per cue, so a
// session's sound events line up with its tick lines.
void logCues(const CueSet& cues, std::uint64_t tick, std::FILE* inputLog) {
  if (inputLog == nullptr || cues.none()) {
    return;
  }
  for (const SoundCue cue : stackfall::kAllSoundCues) {
    if (cues.has(cue)) {
      std::fprintf(inputLog, "sfx %s tick=%llu\n", soundCueName(cue),
                   static_cast<unsigned long long>(tick));
    }
  }
}

// Drains the event queue into the controller set and the input adapter.
// Returns false when the program should exit: window close, or the Q key,
// which stands in for the EXIT state until the title screen of milestone M2
// offers Quit. Escape no longer exits: it pauses (specification section 5).
bool pollEvents(SDL_Renderer* sdlRenderer, ControllerSet& controllers,
                SdlInput& input, AudioDriver& audio,
                const SettingsStore& settings, std::FILE* inputLog) {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    input.handleEvent(event);
    switch (event.type) {
      case SDL_QUIT:
        return false;
      case SDL_WINDOWEVENT:
        // A resized window must letterbox again: the canvas transform is
        // recomputed here, because a resize that leaves it alone would anchor
        // the scene at the top of the new window instead of centering it
        // (specification section 6.1, design review finding
        // letterbox-scene-centered). The next frame's base paints the new
        // bars black.
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
          stackfall::applyCanvas(sdlRenderer);
          logf("Window resized to %dx%d; canvas re-applied\n",
               event.window.data1, event.window.data2);
        }
        break;
      case SDL_CONTROLLERDEVICEADDED:
        controllers.onAdded(event.cdevice.which);
        break;
      case SDL_CONTROLLERDEVICEREMOVED:
        controllers.onRemoved(event.cdevice.which);
        break;
      case SDL_CONTROLLERDEVICEREMAPPED:
        break;  // mapping changed; the next button event carries it
      case SDL_CONTROLLERBUTTONDOWN:
        // Select switches the sound, the one button of the RG503 layout that
        // no rule of section 5 claims (specification sections 5 and 6.3).
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
          toggleSound(audio, settings, inputLog);
        }
        break;
      case SDL_KEYDOWN:
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_q) {
          return false;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_m) {
          toggleSound(audio, settings, inputLog);
        }
        break;
      default:
        break;
    }
  }
  return true;
}

// The fixed-step loop of specification section 7. Each iteration runs the
// scripted input if any, polls all pending events, then consumes every due
// simulation tick with one TickInput each, and renders once.
//
// The accumulator counts performance-counter units scaled by 60, so one
// simulation tick is exactly `frequency` scaled units, that is exactly 1/60
// of a second. Pure integer arithmetic: no truncation, no drift, and no
// floating point anywhere in the timing.
//
// A stalled frame pays out several ticks at once. Every press edge polled
// before the batch lands on the batch's first tick alone, because
// takeTickInput() consumes the pending edges once (specification section 11,
// item 19).
//
// The developer mode `tickPerFrame` (the --tick-per-frame option of the
// unattended harness, T023) trades real-time pacing for an exact frame-to-
// tick mapping: every frame runs exactly one tick and the frame limiter
// stays off, so a scripted session's tick numbering - and with it every
// checkpoint record - is independent of how the wall clock batched the
// frames. The headless reference replay of tests/harness produces its
// records under the same mapping, which is what makes the two streams
// comparable byte for byte.
int runMainLoop(Renderer& renderer, SDL_Renderer* sdlRenderer,
                ControllerSet& controllers, SdlInput& input, Game& game,
                const HighScoreStore& highScores, const SettingsStore& settings,
                AudioDriver& audio, CueDetector& cueDetector,
                InputScript* script, ScriptContext* scriptContext,
                std::FILE* inputLog, std::FILE* observations,
                const char* screenshotDir, bool tickPerFrame,
                Uint64 frameLimit) {
  const Uint64 frequency = SDL_GetPerformanceFrequency();
  const Uint64 tickUnit = frequency;  // scaled units per 1/60 s tick
  const Uint64 stallCounts = frequency * kStallWarnMs / 1000;
  const Uint64 maxFrameCounts = frequency * kMaxFrameMs / 1000;
  const Uint64 framePeriodCounts = frequency / kSimulationHz;

  Uint64 previous = SDL_GetPerformanceCounter();
  Uint64 frameSlot = previous;  // the slot this frame occupies, for paceFrame
  Uint64 accumulator = 0;
  Uint64 framesRendered = 0;
  Uint64 ticksSimulated = 0;
  bool running = true;
  bool limiterLogged = false;  // the fallback limiter logs its first engage
  // The loop observes the scene from the round's entry, not from its current
  // value: a round starts out of Scene::Boot, so the first observed scene
  // logs as a boot -> playing transition through the same path as every
  // other one. Without it, a scripted session never records that the round
  // reached playing (docs/AGENT_TESTING.md relies on these markers).
  Scene lastScene = Scene::Boot;

  while (running) {
    // Scripted input first, so its queued events reach this frame's poll.
    if (script != nullptr) {
      script->pump(ticksSimulated);
    }

    // Events next, before any simulation (specification section 5).
    running = pollEvents(sdlRenderer, controllers, input, audio, settings,
                         inputLog);

    int batch = 0;
    if (tickPerFrame) {
      batch = 1;
      accumulator = 0;
    } else {
      const Uint64 now = SDL_GetPerformanceCounter();
      Uint64 delta = now - previous;
      previous = now;
      if (delta > maxFrameCounts) {
        logf(
            "Frame stall of %llu ms clamped to %llu ms; skipping catch-up\n",
            static_cast<unsigned long long>(countsToMs(delta, frequency)),
            static_cast<unsigned long long>(kMaxFrameMs));
        delta = maxFrameCounts;
      } else if (delta > stallCounts) {
        logf("Unusual frame stall of %llu ms\n",
             static_cast<unsigned long long>(countsToMs(delta, frequency)));
      }
      accumulator += delta * kSimulationHz;

      // Count the batch first, so every tick line knows its slot.
      for (Uint64 due = accumulator; due >= tickUnit; due -= tickUnit) {
        ++batch;
      }
    }
    int slot = 0;
    while (slot < batch) {
      const stackfall::TickInput tickInput = input.takeTickInput();
      if (inputLog != nullptr) {
        logTick(inputLog, ticksSimulated, slot, batch, tickInput);
      }
      game.tick(tickInput);
      // The detector reads what the tick did and the driver plays it, so a
      // cue sounds once per event exactly (specification section 6.3).
      const CueSet cues = cueDetector.observe(game);
      audio.play(cues);
      logCues(cues, ticksSimulated, inputLog);
      accumulator -= tickUnit;
      ++ticksSimulated;
      ++slot;
    }

    // Scene transitions and the game-over summary go to both logs.
    if (game.scene() != lastScene) {
      logf("Scene %s -> %s at tick %llu\n", sceneName(lastScene),
           sceneName(game.scene()),
           static_cast<unsigned long long>(ticksSimulated));
      if (inputLog != nullptr) {
        std::fprintf(inputLog, "scene %s -> %s tick=%llu\n",
                     sceneName(lastScene), sceneName(game.scene()),
                     static_cast<unsigned long long>(ticksSimulated));
      }
      if (lastScene == Scene::Title && game.scene() == Scene::Playing) {
        // Every round begins at the title: record which seed it runs on.
        logf("Round seed %llu\n",
             static_cast<unsigned long long>(game.seed()));
        if (inputLog != nullptr) {
          std::fprintf(inputLog, "round seed=%llu\n",
                       static_cast<unsigned long long>(game.seed()));
        }
      }
      lastScene = game.scene();
      if (lastScene == Scene::GameOver) {
        logf(
            "Game over: score %llu, lines %llu, level %d, high score %llu, "
            "new record %s\n",
            static_cast<unsigned long long>(game.score()),
            static_cast<unsigned long long>(game.lines()), game.level(),
            static_cast<unsigned long long>(game.highScore()),
            game.newRecord() ? "yes" : "no");
        if (inputLog != nullptr) {
          std::fprintf(inputLog,
                       "gameover score=%llu lines=%llu level=%d high=%llu "
                       "record=%d\n",
                       static_cast<unsigned long long>(game.score()),
                       static_cast<unsigned long long>(game.lines()),
                       game.level(),
                       static_cast<unsigned long long>(game.highScore()),
                       game.newRecord() ? 1 : 0);
        }
        // The record is written once, when the round ends and beats the saved
        // best (specification sections 4.7 and 9).
        if (game.newRecord()) {
          saveHighScore(highScores, game, inputLog);
        }
        if (inputLog != nullptr) {
          std::fprintf(inputLog, "sfx counts %s\n",
                       soundCountText(audio, true).c_str());
        }
      }
    }

    // Quit confirmed on the title menu: leave through the ordinary EXIT path,
    // exactly like a window close.
    if (game.quitRequested()) {
      logf("Quit requested through the title menu\n");
      if (inputLog != nullptr) {
        std::fprintf(inputLog, "quit menu\n");
      }
      running = false;
    }

    renderFrame(renderer, game, sdlRenderer);
    ++framesRendered;

    // Checkpoints flush after the render they name, so record and image
    // describe the state that was drawn (docs/AGENT_TESTING.md, T023).
    if (scriptContext != nullptr) {
      flushCheckpoints(*scriptContext, game, sdlRenderer, screenshotDir,
                       observations);
    }

    const bool limitReached = frameLimit != 0 && framesRendered >= frameLimit;
    // Pacing the last frame away would only delay the exit, so the limiter
    // runs while another frame is still coming. The deterministic harness
    // mode skips pacing entirely: its frame count is a tick count.
    if (!limitReached && running && !tickPerFrame) {
      paceFrame(frameSlot, framePeriodCounts, frequency, limiterLogged);
    }
    if (limitReached) {
      logf("Frame limit of %llu frames reached\n",
           static_cast<unsigned long long>(frameLimit));
      running = false;
    }
  }

  logf("Loop finished: %llu frames rendered, %llu ticks simulated\n",
       static_cast<unsigned long long>(framesRendered),
       static_cast<unsigned long long>(ticksSimulated));
  logSoundCounts(audio, inputLog);
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  if (options.help) {
    printUsage();
    return EXIT_SUCCESS;
  }

  // One save directory carries every file the session persists: the record,
  // the settings, and the log. The --save-dir option of the harness wins over
  // every environment hint, so a scripted session keeps all three out of the
  // player's directory; without it the same resolution reads the environment
  // (task T033).
  const std::string saveDirectory =
      options.saveDir != nullptr ? std::string(options.saveDir)
                                 : stackfall::saveDirectoryFromEnvironment();

  // The log sink opens before anything that can fail, so the startup banner
  // and every failure below leave their line in log.txt beside the record
  // (task T014, specification section 9).
  openSessionLog(stackfall::logFilePath(saveDirectory));

  SDL_version compiled;
  SDL_VERSION(&compiled);
  SDL_version linked;
  SDL_GetVersion(&linked);

  logf("STACKFALL %s\n", stackfall::kVersionString);
  logf("Architecture: %s\n", kBuildArch);
  logf("SDL compiled %u.%u.%u, linked %u.%u.%u\n", compiled.major,
       compiled.minor, compiled.patch, linked.major, linked.minor,
       linked.patch);
  logf("Simulation: %d Hz fixed step, frame delta clamp %llu ms\n",
       kSimulationHz, static_cast<unsigned long long>(kMaxFrameMs));
  if (options.tickPerFrame) {
    logf("Harness mode: one simulation tick per frame, pacing off\n");
  }

  // Video failing is fatal and logged; the game is nothing without a window.
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    logErrorf("SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
    closeSessionLog();
    return EXIT_FAILURE;
  }
  // The controller subsystem gets its own attempt, because a platform whose
  // gamepad support will not start must still run: the session logs the
  // failure and plays on the keyboard, which is the no-controller state of
  // specification section 9 (task T014). No controller event can arrive
  // afterwards, exactly as when no device exists.
  const bool controllersUsable = SDL_Init(SDL_INIT_GAMECONTROLLER) == 0;
  if (!controllersUsable) {
    logErrorf("SDL_Init(SDL_INIT_GAMECONTROLLER) failed: %s\n",
              SDL_GetError());
  }

  SDL_Window* window =
      SDL_CreateWindow("STACKFALL", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, kCanvasWidth, kCanvasHeight,
                       SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    logErrorf("SDL_CreateWindow failed: %s\n", SDL_GetError());
    SDL_Quit();
    closeSessionLog();
    return EXIT_FAILURE;
  }
  const char* videoDriver = SDL_GetCurrentVideoDriver();
  logf("Window: %dx%d, video driver \"%s\"\n", kCanvasWidth, kCanvasHeight,
       videoDriver != nullptr ? videoDriver : "?");

  const char* rendererError = "no attempt was made";
  SDL_Renderer* renderer = createRenderer(window, &rendererError);
  if (renderer == nullptr) {
    logErrorf(
        "Every renderer creation attempt failed: %s\n"
        "Set SDL_RENDER_DRIVER to one of the drivers listed above "
        "(for example software), or switch video drivers with "
        "SDL_VIDEODRIVER (for example SDL_VIDEODRIVER=dummy for a "
        "headless run)\n",
        rendererError);
    SDL_DestroyWindow(window);
    SDL_Quit();
    closeSessionLog();
    return EXIT_FAILURE;
  }
  if (!stackfall::applyCanvas(renderer)) {
    logErrorf("Setting the %dx%d canvas failed: %s\n", kCanvasWidth,
              kCanvasHeight, SDL_GetError());
  }

  // The procedural renderer of task T010 draws every frame from this one
  // wrapper; it reads the round and never writes it (specification section 7).
  Renderer sceneRenderer(renderer);

  // The PortMaster environment carries controller mappings in
  // SDL_GAMECONTROLLERCONFIG (specification section 5); load them before
  // controller discovery so the lines apply to the first scan.
  SdlInput input;
  input.loadEnvironmentMappings();

  // The developer virtual pad, for scripted sessions and manual checks.
  int virtualDevice = -1;
  SDL_Joystick* virtualJoystick = nullptr;
  if (options.virtualController || options.inputScript != nullptr) {
    virtualDevice = stackfall::attachVirtualController();
    if (virtualDevice >= 0) {
      virtualJoystick = SDL_JoystickOpen(virtualDevice);
    }
  }

  ControllerSet controllers;
  if (controllersUsable) {
    controllers.discover();
  } else {
    logf("No game controller subsystem; keyboard fallback active\n");
  }

  std::FILE* inputLog = nullptr;
  if (options.inputLog != nullptr) {
    inputLog = std::fopen(options.inputLog, "w");
    if (inputLog == nullptr) {
      logErrorf("Warning: cannot open input log \"%s\"\n", options.inputLog);
    }
  }

  // The structured checkpoint records and the screenshot directory of the
  // unattended harness (docs/AGENT_TESTING.md, T023). Both stay closed and
  // absent in a normal run.
  std::FILE* observations = nullptr;
  if (options.observations != nullptr) {
    observations = std::fopen(options.observations, "w");
    if (observations == nullptr) {
      logErrorf("Warning: cannot open observations \"%s\"\n",
                options.observations);
    }
  }
  if (options.screenshotDir != nullptr) {
    logf("Checkpoint screenshots into %s\n", options.screenshotDir);
  }

  Game game;
  const std::uint64_t seed = options.hasSeed ? options.seed : deriveSeed();
  // The session opens at the title menu of section 4.7; the first round and
  // every later fresh round draw their seeds from one deterministic stream
  // seeded here, so a scripted session with --seed replays exactly.
  game.openTitle(seed);
  logf("Title screen; session seed %llu\n",
       static_cast<unsigned long long>(seed));
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "input-log seed=%llu\n",
                 static_cast<unsigned long long>(seed));
  }

  // The saved record joins the session before the title screen renders it:
  // the launcher's conf/ directory carries it across launches
  // (specification sections 9 and 10.3, task T012). `saveDirectory` above
  // already holds the harness's --save-dir, so the record, the settings, and
  // the session log land in one directory (task T033).
  const HighScoreStore highScores(saveDirectory);
  loadStoredHighScore(highScores, game, inputLog);

  // Sound (task T013, specification section 6.3). The settings file beside the
  // record supplies the starting mute state - sound stays on unless a valid
  // file turns it off - and the device open may fail without costing anything
  // else: the driver reports silence and the session plays on.
  const SettingsStore settings(saveDirectory);
  AudioDriver audio;
  CueDetector cueDetector;
  const stackfall::SettingsResult soundSetting = settings.load();
  audio.setMuted(!soundSetting.soundOn);
  if (settings.hasDirectory()) {
    logf("Sound setting %s (%s) from %s\n", soundSetting.soundOn ? "on" : "off",
         saveStatusName(soundSetting.status), settings.filePath().c_str());
  } else {
    logf("No save directory: the sound setting stays in memory this "
         "session\n");
  }
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "sound start=%s\n",
                 soundSetting.soundOn ? "on" : "off");
  }
  audio.open();
  logf("Sound toggle: M key or controller Select\n");

  ScriptContext context;
  context.virtualJoystick = virtualJoystick;
  context.virtualDevice = virtualDevice;
  context.game = &game;
  context.cues = &cueDetector;
  context.inputLog = inputLog;
  context.fixtureDir = options.fixtureDir;
  InputScript script;
  InputScript* scriptPtr = nullptr;
  if (options.inputScript != nullptr) {
    if (!script.load(options.inputScript, &context)) {
      audio.close();
      if (inputLog != nullptr) {
        std::fclose(inputLog);
      }
      if (observations != nullptr) {
        std::fclose(observations);
      }
      if (virtualJoystick != nullptr) {
        SDL_JoystickClose(virtualJoystick);
        stackfall::detachVirtualController(virtualDevice);
      }
      controllers.closeAll();
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      SDL_Quit();
      closeSessionLog();
      return EXIT_FAILURE;
    }
    scriptPtr = &script;
  }

  const int exitCode = runMainLoop(
      sceneRenderer, renderer, controllers, input, game, highScores, settings,
      audio, cueDetector, scriptPtr, &context, inputLog, observations,
      options.screenshotDir, options.tickPerFrame, options.frameLimit);

  // EXIT state (specification section 4.7): release every resource in the
  // reverse order of creation and leave a clean process. A script may have
  // detached the virtual pad mid-session, so read its shared context. Audio
  // closes first, because section 4.7 names stopping the sound as part of a
  // clean exit.
  audio.close();
  if (inputLog != nullptr) {
    std::fclose(inputLog);
  }
  if (observations != nullptr) {
    std::fclose(observations);
  }
  if (context.virtualJoystick != nullptr) {
    SDL_JoystickClose(context.virtualJoystick);
    stackfall::detachVirtualController(context.virtualDevice);
    context.virtualJoystick = nullptr;
  }
  controllers.closeAll();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  logf("Clean shutdown\n");
  closeSessionLog();
  return exitCode;
}
