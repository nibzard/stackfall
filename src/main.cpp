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
// tick numbering independent of wall-clock batching. The stability pass of
// task T025 adds --frame-times, which records the raw duration of every frame
// of a real-time paced run for the median and percentile measurements. The
// bounded-image pass of task T054 adds --screenshot-budget, which caps the
// bytes of checkpoint images one session writes: past the cap the session
// skips the images and keeps writing every observation record.
// Every one of them is
// inert unless a harness passes it. The driver behind them - the script
// parser and runner, the virtual gamepad, the checkpoint capture, and the
// evidence streams - lives in src/developer_session.cpp (task T048); this
// file calls it before the event poll and after the render and keeps the
// timing, the simulation, and the rendering itself.
//
// High-score persistence (tasks T012 and T052) runs through src/storage.cpp:
// the store resolves the launcher's conf/ directory from STACKFALL_SAVE_DIR
// or XDG_DATA_HOME, the session loads the record before the title screen
// draws it, a round that sets a record saves it at game over, and the
// platform layer also writes the record the moment a running score passes
// the stored best - the tick loop below - so an abandoned round, a
// window close, or a power loss keeps every point up to the piece in play.
//
// Sound (task T013) runs through src/audio_sdl.cpp: the loop observes every
// simulation tick with a CueDetector, a working device plays the five
// synthesized cues of specification section 6.3, and the M key or the
// controller Select button switches the mute, whose starting state comes from
// settings.dat beside the high-score record. That file carries the six
// player preferences of tasks T039 and T044 - sound, ghost, grid, hold, next
// preview count, and grayscale - which a session loads and applies at
// startup through one shared call; the options screen of tasks T040 and T044
// changes any of them and each change applies that same call before it
// writes the whole file at once. A missing audio device logs one
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
//
// Resource ownership (task T047) follows one rule: every platform resource of
// the session - the log file, the SDL library, the window, the renderer, the
// controller set, the developer automation of src/developer_session.cpp (the
// virtual pad, the script, and the evidence files), and the audio device -
// belongs to one scoped owner declared in main, and the owners
// release in the reverse order of their declaration. That reverse order is the
// EXIT order of specification section 4.7: the audio device closes before
// SDL_Quit through declaration order alone, every SDL handle returns before
// the library shuts down, and the session log closes last, so the shutdown
// lines still land in log.txt. A startup step that fails leaves through one
// `return`, and the owners built so far clean up after it, so a new failure
// path needs no hand-written cleanup block.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include <filesystem>
#include <string>

#include <SDL.h>

#include "stackfall/audio_sdl.hpp"
#include "stackfall/developer_session.hpp"
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
using stackfall::DeveloperSession;
using stackfall::Game;
using stackfall::HighScoreStore;
using stackfall::LoadResult;
using stackfall::Renderer;
using stackfall::SaveStatus;
using stackfall::Scene;
using stackfall::SettingsStore;
using stackfall::SettingsValues;
using stackfall::SdlInput;
using stackfall::SoundCue;
using stackfall::kCanvasHeight;
using stackfall::kCanvasWidth;
using stackfall::kLogFileEnv;
using stackfall::kSoundCueCount;
using stackfall::observationSceneName;
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

// Owns the session log for the whole process (task T047). The constructor
// opens the sink before anything that can fail; the destructor closes it after
// every other owner of the session has released its resource and written its
// last line, because this owner is the first object of main and destruction
// runs in reverse order. file() hands the open stream to the owners that write
// their own lines into it, the developer automation of
// src/developer_session.cpp among them; they never close it.
class SessionLog {
 public:
  explicit SessionLog(const std::string& path) { openSessionLog(path); }

  ~SessionLog() { closeSessionLog(); }

  SessionLog(const SessionLog&) = delete;
  SessionLog& operator=(const SessionLog&) = delete;

  std::FILE* file() const { return g_sessionLog; }
};

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
  const char* frameTimes;      // nullptr: no per-frame time record (T025)
  bool tickPerFrame;           // developer: one tick per frame, pacing off
  bool hasScreenshotBudget;    // developer: --screenshot-budget was passed
  std::uint64_t screenshotBudget;  // 0: the session writes without a cap
};

void printUsage() {
  // Usage is not a log entry: --help answers on stdout and never opens a file.
  std::printf(
      "Usage: stackfall [--frames N] [--seed N] [--input-log FILE]\n"
      "                 [--input-script FILE] [--virtual-controller]\n"
      "                 [--save-dir DIR] [--observations FILE]\n"
      "                 [--screenshot-dir DIR] [--fixture-dir DIR]\n"
      "                 [--screenshot-budget BYTES] [--frame-times FILE]\n"
      "                 [--tick-per-frame]\n"
      "  --frames N            render N frames, then exit (0 = until quit)\n"
      "  --seed N              start the round from seed N (deterministic)\n"
      "  --input-log FILE      record one line per simulation tick\n"
      "  --input-script FILE   run a scripted input session (developer)\n"
      "  --virtual-controller  attach the developer virtual gamepad\n"
      "  --save-dir DIR        write the high score, settings, and log here\n"
      "  --observations FILE   write one record per checkpoint command\n"
      "  --screenshot-dir DIR  write one BMP per checkpoint command\n"
      "  --screenshot-budget BYTES\n"
      "                        cap this session's checkpoint image bytes;\n"
      "                        past the cap the session skips images and\n"
      "                        keeps every record (default %llu; 0 = no cap)\n"
      "  --fixture-dir DIR     resolve `fixture` script commands here\n"
      "  --frame-times FILE    record one microsecond frame duration per\n"
      "                        frame of a real-time paced run (T025)\n"
      "  --tick-per-frame      one simulation tick per frame, pacing off\n"
      "  --help                show this text\n",
      static_cast<unsigned long long>(stackfall::kDefaultScreenshotBudget));
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
  // Zero keeps every option off: no frame cap, no seed, no evidence file, no
  // budget override, so the developer session keeps its own defaults.
  Options options{};
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const bool needsValue = std::strcmp(arg, "--frames") == 0 ||
                            std::strcmp(arg, "--seed") == 0 ||
                            std::strcmp(arg, "--input-log") == 0 ||
                            std::strcmp(arg, "--input-script") == 0 ||
                            std::strcmp(arg, "--save-dir") == 0 ||
                            std::strcmp(arg, "--observations") == 0 ||
                            std::strcmp(arg, "--screenshot-dir") == 0 ||
                            std::strcmp(arg, "--fixture-dir") == 0 ||
                            std::strcmp(arg, "--screenshot-budget") == 0 ||
                            std::strcmp(arg, "--frame-times") == 0;
    if (needsValue) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Warning: %s needs a value; ignored\n", arg);
        continue;
      }
      ++i;
      const char* value = argv[i];
      const bool numericOption = std::strcmp(arg, "--frames") == 0 ||
                                 std::strcmp(arg, "--seed") == 0 ||
                                 std::strcmp(arg, "--screenshot-budget") == 0;
      std::uint64_t parsed = 0;
      if (numericOption && parseUnsigned(value, &parsed)) {
        if (std::strcmp(arg, "--frames") == 0) {
          options.frameLimit = parsed;
        } else if (std::strcmp(arg, "--seed") == 0) {
          options.hasSeed = true;
          options.seed = parsed;
        } else {
          options.hasScreenshotBudget = true;
          options.screenshotBudget = parsed;
        }
      } else if (numericOption) {
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
      } else if (std::strcmp(arg, "--frame-times") == 0) {
        options.frameTimes = value;
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
// devices that discovery already opened. The set owns every handle it opened
// (task T047): its destructor closes them, so the exit paths need no manual
// sweep.
// ---------------------------------------------------------------------------

struct ControllerSlot {
  SDL_GameController* handle;
  SDL_JoystickID instance;
};

class ControllerSet {
 public:
  ControllerSet() = default;

  // Closes every controller this set opened. Safe when none are open.
  ~ControllerSet() { closeAll(); }

  ControllerSet(const ControllerSet&) = delete;
  ControllerSet& operator=(const ControllerSet&) = delete;

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

 private:
  void closeAll() {
    for (int i = 0; i < count_; ++i) {
      SDL_GameControllerClose(slots_[i].handle);
    }
    count_ = 0;
  }

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
// SDL, window, and renderer.
// ---------------------------------------------------------------------------

// Owns the SDL library lifetime of the process (task T047). Declared before
// every owner of an SDL resource below, so destruction - which runs in the
// reverse order of declaration - has released each of them before SDL_Quit
// runs: the audio device first, then the virtual pad, the controller set, the
// renderer, and the window. SDL_Quit is safe to call after any SDL_Init
// outcome, so an early startup failure shares this one cleanup.
class SdlLibrary {
 public:
  SdlLibrary() = default;

  ~SdlLibrary() { SDL_Quit(); }

  SdlLibrary(const SdlLibrary&) = delete;
  SdlLibrary& operator=(const SdlLibrary&) = delete;
};

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

// Owns the application window. The constructor takes the window parameters and
// the caller checks valid(); the failure message belongs to main, because it
// is the step that decides a window is fatal (specification section 9).
class SdlWindow {
 public:
  SdlWindow(const char* title, int width, int height) {
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height,
                               SDL_WINDOW_RESIZABLE);
  }

  ~SdlWindow() {
    if (window_ != nullptr) {
      SDL_DestroyWindow(window_);
    }
  }

  SdlWindow(const SdlWindow&) = delete;
  SdlWindow& operator=(const SdlWindow&) = delete;

  bool valid() const { return window_ != nullptr; }
  SDL_Window* get() const { return window_; }

 private:
  SDL_Window* window_ = nullptr;
};

// Owns the SDL renderer. The constructor runs the fallback chain of
// createRenderer(), which logs every attempt, and leaves the reason of the
// last failure in `lastError` for the caller's error message.
class SdlRenderer {
 public:
  SdlRenderer(SDL_Window* window, const char** lastError) {
    renderer_ = createRenderer(window, lastError);
  }

  ~SdlRenderer() {
    if (renderer_ != nullptr) {
      SDL_DestroyRenderer(renderer_);
    }
  }

  SdlRenderer(const SdlRenderer&) = delete;
  SdlRenderer& operator=(const SdlRenderer&) = delete;

  bool valid() const { return renderer_ != nullptr; }
  SDL_Renderer* get() const { return renderer_; }

 private:
  SDL_Renderer* renderer_ = nullptr;
};

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

// One crash-safe write of a running round's record (task T052). The loop
// calls this the moment the score passes the saved record, and the store's
// atomic temporary-file, flush, and rename write makes every one of these
// saves safe to interrupt, so an abandoned round, a window close, or a
// power loss costs at most the points of the piece in play. The input-log
// line carries the tick, so evidence can prove the save ran while the
// round was still going; the failure line is the high score's own, so a
// failed record write counts exactly like a failed game-over save. A
// session without a save directory saves nothing here: its load already
// reported that persistence is off.
void saveRunningRecord(const HighScoreStore& store, std::uint64_t score,
                       std::uint64_t tick, std::FILE* inputLog) {
  if (!store.hasDirectory()) {
    return;
  }
  const SaveStatus status = store.save(score);
  if (status == SaveStatus::Ok) {
    logf("Record %llu saved to %s\n",
         static_cast<unsigned long long>(score), store.filePath().c_str());
  } else {
    logf("Saving the high score %llu failed: %s\n",
         static_cast<unsigned long long>(score), saveStatusName(status));
  }
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "record save=%llu tick=%llu status=%s\n",
                 static_cast<unsigned long long>(score),
                 static_cast<unsigned long long>(tick),
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
// Sound (task T013, specification section 6.3) and the six player
// preferences of the options screen (tasks T040 and T044).
// ---------------------------------------------------------------------------

// The one place that maps the session's preference values onto their owners:
// the audio mute and the renderer's option gates. Startup calls it right
// after the settings load and every change route calls it right after
// flipping its value, so the owners always agree with `prefs` and a future
// preference needs exactly one new line here (task T049, refactor-review
// item 4). Muting never touches the audio device, so this is equally safe
// with no device at all (specification section 6.3).
void applyPreferences(const SettingsValues& prefs, AudioDriver& audio,
                      Renderer& renderer) {
  audio.setMuted(!prefs.soundOn);
  renderer.setSoundOn(prefs.soundOn);
  renderer.setShowGhost(prefs.ghostOn);
  renderer.setShowGrid(prefs.gridOn);
  renderer.setShowHold(prefs.holdOn);
  renderer.setNextPreviews(prefs.nextPreviews);
  renderer.setGrayscale(prefs.grayscaleOn);
}

// Every preference change writes the whole settings file through one atomic
// save, so a session interrupted right after a change keeps every value and
// the five preferences that did not change ride along unchanged. `what` names
// the preference for the failure line, and `inputLogLine` is the per-tick
// input log's evidence line with the new value and the save status.
void savePreference(const SettingsStore& settings, const SettingsValues& prefs,
                    const char* what, const char* inputLogLine,
                    std::FILE* inputLog) {
  const SaveStatus status = settings.save(prefs);
  if (status != SaveStatus::Ok) {
    logf("Saving the %s setting failed: %s\n", what, saveStatusName(status));
  }
  if (inputLog != nullptr) {
    std::fprintf(inputLog, "%s status=%s\n", inputLogLine,
                 saveStatusName(status));
  }
}

// The sound toggle is platform-layer state, so it never travels through the
// rules adapter: the keyboard M key and the controller Select button switch
// it here, directly in the event loop, and the options screen reaches the
// same function on its SOUND row (task T040). The value flips here and
// `applyPreferences` moves it into the mute and the renderer's row label.
void toggleSound(AudioDriver& audio, const SettingsStore& settings,
                 SettingsValues& prefs, Renderer& renderer,
                 std::FILE* inputLog) {
  prefs.soundOn = !prefs.soundOn;
  applyPreferences(prefs, audio, renderer);
  char line[48];
  std::snprintf(line, sizeof(line), "sound %s",
                prefs.soundOn ? "unmuted" : "muted");
  savePreference(settings, prefs, "sound", line, inputLog);
}

// The ghost toggle of the options screen (task T040), the same platform-layer
// pattern as the sound: flip the session value, apply it, and write the file
// at once.
void toggleGhost(AudioDriver& audio, const SettingsStore& settings,
                 SettingsValues& prefs, Renderer& renderer,
                 std::FILE* inputLog) {
  prefs.ghostOn = !prefs.ghostOn;
  applyPreferences(prefs, audio, renderer);
  char line[48];
  std::snprintf(line, sizeof(line), "ghost %s",
                prefs.ghostOn ? "shown" : "hidden");
  savePreference(settings, prefs, "ghost", line, inputLog);
}

// The grid toggle of the options screen (task T044) reuses the renderer gate
// the developer options already had: off removes the faint grid lines, on
// restores them, and no piece pixel changes either way.
void toggleGrid(AudioDriver& audio, const SettingsStore& settings,
                SettingsValues& prefs, Renderer& renderer,
                std::FILE* inputLog) {
  prefs.gridOn = !prefs.gridOn;
  applyPreferences(prefs, audio, renderer);
  char line[48];
  std::snprintf(line, sizeof(line), "grid %s",
                prefs.gridOn ? "shown" : "hidden");
  savePreference(settings, prefs, "grid", line, inputLog);
}

// The hold toggle of the options screen (task T044): off makes the loop drop
// every mapped hold edge before the round sees it - so no hold swap happens
// and no hold lock is spent - and hides the Hold preview; on restores the
// once-per-piece rule of the core untouched.
void toggleHold(AudioDriver& audio, const SettingsStore& settings,
                SettingsValues& prefs, Renderer& renderer,
                std::FILE* inputLog) {
  prefs.holdOn = !prefs.holdOn;
  applyPreferences(prefs, audio, renderer);
  char line[48];
  std::snprintf(line, sizeof(line), "hold %s",
                prefs.holdOn ? "enabled" : "disabled");
  savePreference(settings, prefs, "hold", line, inputLog);
}

// The NEXT PIECES row of the options screen (task T044): select cycles
// 1, 2, 3, 4, 5, and back to 1. The count changes only how many queue
// previews the renderer draws; the queue and the randomizer behind it stay
// byte-identical.
void cycleNextPreviews(AudioDriver& audio, const SettingsStore& settings,
                       SettingsValues& prefs, Renderer& renderer,
                       std::FILE* inputLog) {
  prefs.nextPreviews = prefs.nextPreviews >= stackfall::kSettingsNextMax
                           ? stackfall::kSettingsNextMin
                           : prefs.nextPreviews + 1;
  applyPreferences(prefs, audio, renderer);
  char line[48];
  std::snprintf(line, sizeof(line), "next %d", prefs.nextPreviews);
  savePreference(settings, prefs, "next", line, inputLog);
}

// The grayscale toggle of the options screen (task T044): on draws the play
// scene's pieces, previews, and ghost in grays while the text and outlines
// keep their colors; off restores the normal palette exactly.
void toggleGrayscale(AudioDriver& audio, const SettingsStore& settings,
                     SettingsValues& prefs, Renderer& renderer,
                     std::FILE* inputLog) {
  prefs.grayscaleOn = !prefs.grayscaleOn;
  applyPreferences(prefs, audio, renderer);
  char line[48];
  std::snprintf(line, sizeof(line), "grayscale %s",
                prefs.grayscaleOn ? "on" : "off");
  savePreference(settings, prefs, "grayscale", line, inputLog);
}

// A on one row of the options screen changes that row's value. The rules core
// keeps the highlighted row and changes nothing itself; this platform half
// applies the change (tasks T040 and T044, specification section 7).
void applyOptionsSelect(Renderer& renderer, AudioDriver& audio,
                        const SettingsStore& settings, SettingsValues& prefs,
                        int row, std::FILE* inputLog) {
  switch (row) {
    case 0:
      toggleSound(audio, settings, prefs, renderer, inputLog);
      break;
    case 1:
      toggleGhost(audio, settings, prefs, renderer, inputLog);
      break;
    case 2:
      toggleGrid(audio, settings, prefs, renderer, inputLog);
      break;
    case 3:
      toggleHold(audio, settings, prefs, renderer, inputLog);
      break;
    case 4:
      cycleNextPreviews(audio, settings, prefs, renderer, inputLog);
      break;
    default:
      toggleGrayscale(audio, settings, prefs, renderer, inputLog);
      break;
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
bool pollEvents(SDL_Renderer* sdlRenderer, Renderer& renderer,
                ControllerSet& controllers, SdlInput& input,
                AudioDriver& audio, const SettingsStore& settings,
                SettingsValues& prefs, std::FILE* inputLog) {
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
          toggleSound(audio, settings, prefs, renderer, inputLog);
        }
        break;
      case SDL_KEYDOWN:
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_q) {
          return false;
        }
        if (event.key.repeat == 0 && event.key.keysym.sym == SDLK_m) {
          toggleSound(audio, settings, prefs, renderer, inputLog);
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
//
// The developer automation of the session - the script driver, the virtual
// pad, and the checkpoint capture - sits behind `developer` (task T048), so
// the loop's own list names only what it drives itself: the renderers, the
// controller set, the input adapter, the round with its two stores, the
// preferences, and the sound. The evidence streams the loop writes through
// (`developer.inputLog()`, `developer.frameTimes()`) belong to the same
// owner, which is what took them off this list.
int runMainLoop(Renderer& renderer, SDL_Renderer* sdlRenderer,
                ControllerSet& controllers, SdlInput& input, Game& game,
                const HighScoreStore& highScores, const SettingsStore& settings,
                SettingsValues& prefs, AudioDriver& audio,
                CueDetector& cueDetector, DeveloperSession& developer,
                bool tickPerFrame, Uint64 frameLimit) {
  std::FILE* const inputLog = developer.inputLog();
  std::FILE* const frameTimes = developer.frameTimes();
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

  // The persisted record this session owns (task T052, specification
  // sections 4.7 and 9). It seeds from the load that opened the session -
  // game.highScore() holds exactly that value here - and rises with every
  // mid-round save below, in the platform layer alone: the rules core never
  // learns of it, so its gameplay, its hashes, and its input timing stay as
  // they were. `roundActive` says whether a round is in play (Playing,
  // Paused, or the controls screen over a frozen round), and `roundTicks`
  // is the round's tick count after the previous tick; together they name
  // the round boundaries the feedback below waits for.
  std::uint64_t savedRecord = game.highScore();
  bool roundActive = false;
  std::uint64_t roundTicks = 0;

  while (running) {
    // Scripted input first, so its queued events reach this frame's poll.
    if (developer.hasScript()) {
      developer.pumpScript(ticksSimulated);
    }

    // Events next, before any simulation (specification section 5).
    running = pollEvents(sdlRenderer, renderer, controllers, input, audio,
                         settings, prefs, inputLog);

    int batch = 0;
    if (tickPerFrame) {
      batch = 1;
      accumulator = 0;
    } else {
      const Uint64 now = SDL_GetPerformanceCounter();
      Uint64 delta = now - previous;
      previous = now;
      // The frame-time record of the T025 measurements: one line per frame
      // with the raw, unclamped interval from the previous frame's start, in
      // microseconds, so median and percentile math happens outside the loop.
      // It is a paced-run instrument; the deterministic mode below has no
      // frame pacing to measure and writes nothing.
      if (frameTimes != nullptr) {
        const Uint64 countsPerUs = frequency / 1000000;
        std::fprintf(frameTimes, "%llu\n",
                     countsPerUs == 0
                         ? static_cast<unsigned long long>(
                               countsToMs(delta, frequency) * 1000)
                         : static_cast<unsigned long long>(delta /
                                                           countsPerUs));
      }
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
      stackfall::TickInput tickInput = input.takeTickInput();
      // Hold disabled on the options screen ignores every mapped hold input
      // (controller Y or left shoulder, keyboard C or left shift): the edge
      // is dropped here, before the round can see it, so no hold swap happens
      // and no hold lock is spent (task T044, specification section 5). The
      // input log records the frame the tick really consumed.
      if (!prefs.holdOn) {
        tickInput.game.holdPressed = false;
      }
      if (inputLog != nullptr) {
        logTick(inputLog, ticksSimulated, slot, batch, tickInput);
      }
      // The scene before the tick owns the select edge: the A press that
      // opens the options screen belongs to the title, and only a select
      // pressed while the screen already showed changes a value, so the
      // opening press never toggles one (task T040).
      const Scene sceneBeforeTick = game.scene();
      game.tick(tickInput);
      if (sceneBeforeTick == Scene::Options && tickInput.menu.selectPressed) {
        applyOptionsSelect(renderer, audio, settings, prefs,
                           game.optionsSelection(), inputLog);
      }
      // The record persists the moment the running score passes the best
      // the store holds (task T052): every save runs through the atomic
      // temporary-file, flush, and rename write, so a quit to the title, a
      // window close, or a power loss keeps every point up to the piece in
      // play instead of waiting for a game over that never came.
      if (game.score() > savedRecord) {
        savedRecord = game.score();
        saveRunningRecord(highScores, savedRecord, ticksSimulated, inputLog);
      }
      // Round boundaries feed the persisted best back into the session,
      // between rounds only (specification section 4.7): a round that ends
      // by game over, Quit to Title, or Restart leaves the record the next
      // round must beat in the game's own high score, while a running round
      // keeps the record it began with, so the core's newRecord flag of
      // endRound() and the game-over screen still compare against that
      // record and Play Again plays against the best so far. A restart is
      // the one boundary that ends one round and begins the next in a
      // single tick: the fresh round's clock restarts, which is how the
      // platform layer tells it from a resume.
      const Scene sceneAfterTick = game.scene();
      if (roundActive && sceneAfterTick != Scene::Playing &&
          sceneAfterTick != Scene::Paused &&
          sceneAfterTick != Scene::PauseControls) {
        roundActive = false;  // game over or Quit to Title ended the round
        game.setHighScore(savedRecord);
      } else if (sceneAfterTick == Scene::Playing && !roundActive) {
        roundActive = true;  // Start Game or Play Again began a round
      } else if (roundActive && sceneAfterTick == Scene::Playing &&
                 game.tickCount() < roundTicks) {
        game.setHighScore(savedRecord);  // Restart replaced the round
      }
      roundTicks = game.tickCount();
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
      logf("Scene %s -> %s at tick %llu\n", observationSceneName(lastScene),
           observationSceneName(game.scene()),
           static_cast<unsigned long long>(ticksSimulated));
      if (inputLog != nullptr) {
        std::fprintf(inputLog, "scene %s -> %s tick=%llu\n",
                     observationSceneName(lastScene),
                     observationSceneName(game.scene()),
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
    // describe the state that was drawn (docs/AGENT_TESTING.md, T023). The
    // call is a no-op unless a script marked checkpoints this frame.
    developer.captureCheckpoints(game, sdlRenderer);

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
  // (task T014, specification section 9). Its owner is the first object of
  // the session, so it closes after everything below has gone quiet.
  const SessionLog sessionLog(stackfall::logFilePath(saveDirectory));

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

  int exitCode = EXIT_FAILURE;
  {
    // Every platform resource of the session, as scoped owners in creation
    // order (task T047). The block's end releases them in the reverse order,
    // which is the EXIT order of specification section 4.7: the audio device
    // first, then the developer session's evidence streams and virtual pad,
    // the controller set, the renderer, and the window, and SDL_Quit
    // after all of them. A startup step that fails leaves through one
    // `return`, and the owners built so far release what the session had.
    const SdlLibrary sdl;

    // Video failing is fatal and logged; the game is nothing without a window.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      logErrorf("SDL_Init(SDL_INIT_VIDEO) failed: %s\n", SDL_GetError());
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

    const SdlWindow window("STACKFALL", kCanvasWidth, kCanvasHeight);
    if (!window.valid()) {
      logErrorf("SDL_CreateWindow failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    const char* videoDriver = SDL_GetCurrentVideoDriver();
    logf("Window: %dx%d, video driver \"%s\"\n", kCanvasWidth, kCanvasHeight,
         videoDriver != nullptr ? videoDriver : "?");

    const char* rendererError = "no attempt was made";
    const SdlRenderer renderer(window.get(), &rendererError);
    if (!renderer.valid()) {
      logErrorf(
          "Every renderer creation attempt failed: %s\n"
          "Set SDL_RENDER_DRIVER to one of the drivers listed above "
          "(for example software), or switch video drivers with "
          "SDL_VIDEODRIVER (for example SDL_VIDEODRIVER=dummy for a "
          "headless run)\n",
          rendererError);
      return EXIT_FAILURE;
    }
    if (!stackfall::applyCanvas(renderer.get())) {
      logErrorf("Setting the %dx%d canvas failed: %s\n", kCanvasWidth,
                kCanvasHeight, SDL_GetError());
    }

    // The procedural renderer of task T010 draws every frame from this one
    // wrapper; it reads the round and never writes it (specification section
    // 7).
    Renderer sceneRenderer(renderer.get());

    // The PortMaster environment carries controller mappings in
    // SDL_GAMECONTROLLERCONFIG (specification section 5); load them before
    // controller discovery so the lines apply to the first scan.
    SdlInput input;
    input.loadEnvironmentMappings();

    // Declaration order, not creation order, sets the release order: the
    // developer automation is declared after the controller set, so its
    // virtual pad releases first, exactly as the exit path always did. The
    // pad is still attached before discovery runs, because discovery opens it
    // as one more controller and logs it like a real one. Everything else the
    // developer session owns - the script, the evidence streams, and the
    // capture - opens below, at the steps that always created it.
    ControllerSet controllers;
    DeveloperSession developer(sessionLog.file());
    if (options.virtualController || options.inputScript != nullptr) {
      developer.attachPad();
    }
    if (controllersUsable) {
      controllers.discover();
    } else {
      logf("No game controller subsystem; keyboard fallback active\n");
    }

    developer.openInputLog(options.inputLog);
    std::FILE* const inputLog = developer.inputLog();

    // The structured checkpoint records and the screenshot directory of the
    // unattended harness (docs/AGENT_TESTING.md, T023). Both stay closed and
    // absent in a normal run. The image budget of task T054 arrives only when
    // the option named one; without it the session keeps the 256 MiB default,
    // and 0 removes the cap.
    developer.openObservations(options.observations);
    developer.setScreenshotDir(options.screenshotDir);
    if (options.hasScreenshotBudget) {
      developer.setScreenshotBudget(options.screenshotBudget);
    }
    developer.setFixtureDir(options.fixtureDir);

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

    // Sound (task T013, specification section 6.3) and the six player
    // preferences of the options screen (tasks T039, T040, and T044). The
    // settings file beside the record supplies every starting state - each
    // stays at its default unless a valid file changes it - and one
    // `applyPreferences` call moves all of them into their owners, the same
    // call every later change routes through, so the play scene gates the
    // ghost, the grid, the Hold preview, and the preview count from the first
    // frame and the options screen shows every row's value. The device open
    // may fail without costing anything else: the driver reports silence and
    // the session plays on. The driver is declared after every other owner
    // above, so its destructor closes the device before any of them and long
    // before SDL_Quit (specification section 4.7).
    const SettingsStore settings(saveDirectory);
    AudioDriver audio;
    CueDetector cueDetector;
    const stackfall::SettingsResult preferences = settings.load();
    SettingsValues prefs = preferences;
    applyPreferences(prefs, audio, sceneRenderer);
    if (settings.hasDirectory()) {
      logf("Settings: sound %s, ghost preview %s, grid %s, hold %s, next "
           "previews %d, grayscale %s (%s) from %s\n",
           prefs.soundOn ? "on" : "off", prefs.ghostOn ? "on" : "off",
           prefs.gridOn ? "on" : "off", prefs.holdOn ? "on" : "off",
           prefs.nextPreviews, prefs.grayscaleOn ? "on" : "off",
           saveStatusName(preferences.status), settings.filePath().c_str());
    } else {
      logf("No save directory: the player settings stay in memory this "
           "session\n");
    }
    if (inputLog != nullptr) {
      std::fprintf(inputLog, "sound start=%s\n", prefs.soundOn ? "on" : "off");
      std::fprintf(inputLog, "ghost start=%s\n", prefs.ghostOn ? "on" : "off");
      std::fprintf(inputLog, "grid start=%s\n", prefs.gridOn ? "on" : "off");
      std::fprintf(inputLog, "hold start=%s\n", prefs.holdOn ? "on" : "off");
      std::fprintf(inputLog, "next start=%d\n", prefs.nextPreviews);
      std::fprintf(inputLog, "grayscale start=%s\n",
                   prefs.grayscaleOn ? "on" : "off");
    }
    audio.open();
    logf("Sound toggle: M key or controller Select\n");

    // The script binds the round and the cue detector itself, the two
    // application objects its `restart` and `fixture` commands act on; no
    // wider context travels with them (task T048).
    if (options.inputScript != nullptr) {
      if (!developer.loadScript(options.inputScript, &game, &cueDetector)) {
        // A recorded session must not run halfway by accident. The owners of
        // this block release everything the session had already built; no
        // clean-shutdown line belongs to a session that never ran.
        return EXIT_FAILURE;
      }
    }

    // The frame-time record of the T025 measurement runs, opened beside the
    // other structured streams and after the script, so a rejected script
    // leaves no file behind. It is the developer session's last stream to
    // open and its first to close.
    developer.openFrameTimes(options.frameTimes);

    exitCode = runMainLoop(sceneRenderer, renderer.get(), controllers, input,
                           game, highScores, settings, prefs, audio,
                           cueDetector, developer, options.tickPerFrame,
                           options.frameLimit);
  }

  // EXIT state (specification section 4.7): the block above released every
  // resource in the reverse order of creation - the audio device before
  // SDL_Quit, the script-released pad closed exactly once - and the session
  // log is still open, so the ordinary exit leaves its line in log.txt. A
  // startup failure returned early and prints nothing here.
  if (exitCode == EXIT_SUCCESS) {
    logf("Clean shutdown\n");
  }
  return exitCode;
}
