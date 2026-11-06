// Headless reference replay for the unattended harness
// (docs/AGENT_TESTING.md, T023).
//
// This tool computes what the rules core must do with a session script, with
// no SDL, no window, and no main loop of src/main.cpp. It parses the same
// script language the game binary's --input-script driver parses, feeds the
// same SDL-free InputAdapter and Game, and writes checkpoint records through
// the same observationLine() formatter, one record per `check` command after
// the tick that command's frame ran - exactly where the game binary flushes
// its records.
//
// The harness compares the two streams field by field. The comparison is the
// acceptance: a clean process exit or a screenshot file proves nothing about
// gameplay, while a byte-equal stream proves the real SDL event path, the
// fixed-step loop wiring, and the developer hooks together produced the
// states the rules core prescribes.
//
// The deliberate copies in this file are the point, not a shortcut:
//
//   * the script parser mirrors the grammar of src/main.cpp without sharing
//     its code, so a parsing defect there cannot hide here;
//   * the key and pad tables below are transcribed from specification
//     section 5, not from src/input_sdl.cpp, so a wrong or missing mapping
//     in the SDL layer shows up as a record mismatch;
//   * the hold gate mirrors the platform half of the HOLD preference (task
//     T044, specification section 5): the game binary drops every hold edge
//     while that preference is off, before the round can see it, so this
//     replay must drop the same edges to stay a mirror. The gate's state
//     comes from the session's settings file - the one the harness passes
//     next to the script - and from the same options-row select the game
//     applies, so a relaunch that starts with HOLD off replays identically.
//
//   * the orientation mirrors the platform half of the ORIENTATION row (tasks
//     T055 and T068): the game binary turns the running session on the select
//     edge itself - the layout, the input tables, and every held level and
//     pending edge together - so this replay swaps its own key and pad tables
//     and clears its adapter on the same edge. A session that toggles the row
//     mid-script therefore replays each half against the table that was in
//     force when it pressed, which is what makes the turn comparable at all.
//
//   * the record persistence mirrors the platform half of the high score
//     (task T052, specification sections 4.7 and 9): the game binary saves
//     the running score the moment it passes the stored record and feeds
//     the best record back into the session between rounds, so the `high`
//     field of a checkpoint depends on both. This replay tracks the same
//     saved record in memory, seeded from the record file the harness names
//     with --high-score, and feeds it back at the same round boundaries. It
//     never writes the file: the record belongs to the game process the
//     harness runs after this one.
//
// Link target: stackfall_core only (CMake target stackfall_reference), so
// this stays a headless rules tool on every machine that builds the tests.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/observe.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/fixture.hpp"
#include "stackfall/script.hpp"
#include "stackfall/storage.hpp"

namespace {

// ---------------------------------------------------------------------------
// The section 5 tables (task T066). Before the customization screen these
// were four transcribed arrays; since it they are data this replay reads from
// the same controls.dat the game reads and resolves through the shared
// vocabulary of stackfall/bindings.hpp, so the canonical control names below
// are the same words the file, the script language, and the screens use.
//
// The deliberate independence survives in a different place: the defaults this
// replay falls back on are the section 5 tables of defaultControlBindings(),
// and the rotated portrait roles come from the same function the game's SDL
// layer uses, so a wrong mapping in src/input_sdl.cpp still shows up as a
// record mismatch while a wrong binding table cannot hide here.
// ---------------------------------------------------------------------------

const stackfall::GameAction kNone = stackfall::GameAction::kCount;

// True when the word names a key the script may press: any rebindable
// control of the keyboard list, or "m", the fixed sound shortcut of the
// platform layer, which carries no rules action at all.
bool scriptKeyKnown(const std::string& name) {
  return name == "m" ||
         stackfall::keyboardControlFromName(name.c_str()) !=
             stackfall::kNoControl;
}

// One keyboard control id, or -1 when the word names none. "m" resolves to no
// control, so a press of it changes the round nothing, exactly as the game's
// script driver accepts it.
int scriptKeyboardControl(const std::string& name) {
  if (name == "m") {
    return stackfall::kNoControl;
  }
  return stackfall::keyboardControlFromName(name.c_str());
}

// One controller control id, or -1 when the word names none.
int scriptControllerControl(const std::string& name) {
  return stackfall::controllerControlFromName(name.c_str());
}

// Feeds one control press or release of one device into the adapter through
// the binding tables in force: the gameplay action the control performs, then
// the fixed menu role it carries, exactly as mapKeyboardScancode() and
// mapControllerButton() do inside the game's SDL layer.
void applyScriptControl(stackfall::InputAdapter& adapter, int source,
                        stackfall::DeviceKind device, int control,
                        bool pressed, const stackfall::ControlBindings& bindings,
                        bool portraitTables) {
  const stackfall::Orientation frame =
      portraitTables ? stackfall::Orientation::Portrait
                     : stackfall::Orientation::Landscape;
  const stackfall::ControlTable& table = bindings.table(device, frame);
  const int action = stackfall::actionUsingControl(table, control);
  if (action >= 0) {
    adapter.setButton(source, stackfall::kRebindableActions[action], pressed);
  }
  const stackfall::GameAction role = stackfall::menuRoleOfControl(device,
                                                                 control, frame);
  if (role != kNone) {
    adapter.setButton(source, role, pressed);
  }
}

// ---------------------------------------------------------------------------
// Script commands, mirroring the grammar of src/main.cpp.
// ---------------------------------------------------------------------------

struct Command {
  enum class Kind : std::uint8_t {
    Key,
    Pad,
    Stick,
    Wait,
    Stall,
    Detach,
    Attach,
    Restart,
    Fixture,
    Check,
    Shot,
    Quit,
    Echo
  };
  Kind kind;
  bool pressed = false;
  int value = 0;
  int axisValue = 0;
  std::string name;
  std::string text;
};

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
      return false;
    }
    value = value * 10 + static_cast<std::uint64_t>(*p - '0');
  }
  *out = value;
  return true;
}

bool parseScriptLine(const std::string& raw, std::vector<Command>* commands,
                     int lineNumber) {
  const std::size_t hash = raw.find('#');
  std::string line = raw.substr(0, hash);
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r' ||
                           line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  if (line.empty()) {
    return true;
  }
  const std::size_t verbEnd = line.find_first_of(" \t");
  const std::string verb = line.substr(0, verbEnd);
  const std::string rest =
      verbEnd == std::string::npos ? "" : line.substr(verbEnd + 1);
  const std::size_t nameEnd = rest.find_first_of(" \t");
  const std::string name =
      rest.empty() || nameEnd == 0 ? "" : rest.substr(0, nameEnd);

  Command command;
  const auto fail = [&](const char* why) {
    std::fprintf(stderr, "reference script line %d (%s): %s\n", lineNumber,
                 raw.c_str(), why);
    return false;
  };
  if (verb == "key" || verb == "pad") {
    const std::string tail =
        nameEnd == std::string::npos ? "" : rest.substr(nameEnd + 1);
    const std::size_t tailEnd = tail.find_first_of(" \t");
    const std::string state = tail.substr(0, tailEnd);
    if (state != "down" && state != "up") {
      return fail("key/pad needs down or up");
    }
    command.pressed = state == "down";
    command.name = name;
    if (verb == "key") {
      if (!scriptKeyKnown(name)) {
        return fail("unknown key name");
      }
      command.kind = Command::Kind::Key;
    } else {
      if (scriptControllerControl(name) == stackfall::kNoControl) {
        return fail("unknown pad button name");
      }
      command.kind = Command::Kind::Pad;
    }
  } else if (verb == "stick") {
    // "stick <x|y> <value>": the axis words are the two readings of the left
    // stick, resolved through the same deadzoned translation the game's SDL
    // layer applies, so the two streams stay comparable (task T86).
    const bool isX = name == "x";
    const bool isY = name == "y";
    if (!isX && !isY) {
      return fail("unknown stick axis name");
    }
    const std::string tail =
        nameEnd == std::string::npos ? "" : rest.substr(nameEnd + 1);
    const std::size_t tailEnd = tail.find_first_of(" \t");
    const std::string state = tail.substr(0, tailEnd);
    if (!stackfall::parseScriptAxisValue(state.c_str(), &command.axisValue)) {
      return fail("stick needs a value from -32768 to 32767");
    }
    command.kind = Command::Kind::Stick;
    command.value = isX ? 0 : 1;
  } else if (verb == "wait" || verb == "stall") {
    // The one shared rule of the two mirrored parsers: the count runs
    // through the core's bounded parser, so neither copy can turn a huge
    // number into a truncated delay (T025 fuzz pass; tests/test_script.cpp
    // pins the exact range both sides accept).
    if (!stackfall::parseScriptCount(name.c_str(), &command.value)) {
      return fail("wait/stall needs a number from 0 to 3600000");
    }
    command.kind = verb == "wait" ? Command::Kind::Wait : Command::Kind::Stall;
  } else if (verb == "detach") {
    command.kind = Command::Kind::Detach;
  } else if (verb == "attach") {
    command.kind = Command::Kind::Attach;
  } else if (verb == "restart") {
    command.kind = Command::Kind::Restart;
  } else if (verb == "quit") {
    command.kind = Command::Kind::Quit;
  } else if (verb == "fixture" || verb == "check" || verb == "shot") {
    if (name.empty() || nameEnd != std::string::npos) {
      return fail("fixture/check/shot needs one name");
    }
    command.name = name;
    command.kind = verb == "fixture" ? Command::Kind::Fixture
                  : verb == "check"  ? Command::Kind::Check
                                     : Command::Kind::Shot;
  } else if (verb == "echo") {
    command.kind = Command::Kind::Echo;
    command.text = rest;
  } else {
    return fail("unknown command");
  }
  commands->push_back(std::move(command));
  return true;
}

void printUsage() {
  std::printf(
      "Usage: stackfall_reference --seed N --script FILE [--out FILE]\n"
      "                           [--fixture-dir DIR] [--settings FILE]\n"
      "                           [--controls FILE] [--high-score FILE]\n"
      "  Replays a session script through the rules core and writes one\n"
      "  checkpoint record per `check` command, in the format of the game\n"
      "  binary's --observations file. Headless: no SDL.\n"
      "  --settings FILE reads the session's settings.dat for the hold and\n"
      "  orientation preferences the platform layer owns; a missing file is\n"
      "  the fresh-installation default of every preference, and portrait\n"
      "  swaps the key and pad tables for the rotated ones of task T055. A\n"
      "  select on the ORIENTATION row swaps them again inside the replay,\n"
      "  because the game turns the session at once (task T068).\n"
      "  --controls FILE reads the session's controls.dat for the custom\n"
      "  bindings the platform layer owns (task T066); a missing file is the\n"
      "  section 5 default tables and a damaged one recovers to them under\n"
      "  the rule the game applies. A capture on the customization screen\n"
      "  rebinds this replay's own tables on the same edge the game applies.\n"
      "  --high-score FILE reads the session's highscore.dat for the record\n"
      "  the game binary loads, saves mid-round, and feeds back between\n"
      "  rounds (task T052); a missing file is the record 0 of a fresh\n"
      "  installation. The replay never writes it.\n");
}

// The options row of the HOLD preference (specification section 4.7): SOUND,
// GHOST, GRID, HOLD, NEXT PIECES, GRAYSCALE, ORIENTATION (task T055).
constexpr int kOptionsHoldRow = 3;
constexpr int kOptionsOrientationRow = 6;

}  // namespace

int main(int argc, char** argv) {
  bool hasSeed = false;
  std::uint64_t seed = 0;
  const char* scriptPath = nullptr;
  const char* outPath = nullptr;
  const char* fixtureDir = nullptr;
  const char* settingsPath = nullptr;
  const char* controlsPath = nullptr;
  const char* highScorePath = nullptr;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const bool needsValue = std::strcmp(arg, "--seed") == 0 ||
                            std::strcmp(arg, "--script") == 0 ||
                            std::strcmp(arg, "--out") == 0 ||
                            std::strcmp(arg, "--fixture-dir") == 0 ||
                            std::strcmp(arg, "--settings") == 0 ||
                            std::strcmp(arg, "--controls") == 0 ||
                            std::strcmp(arg, "--high-score") == 0;
    if (needsValue) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s needs a value\n", arg);
        return EXIT_FAILURE;
      }
      ++i;
      const char* value = argv[i];
      if (std::strcmp(arg, "--seed") == 0) {
        if (!parseUnsigned(value, &seed)) {
          std::fprintf(stderr, "--seed needs a number\n");
          return EXIT_FAILURE;
        }
        hasSeed = true;
      } else if (std::strcmp(arg, "--script") == 0) {
        scriptPath = value;
      } else if (std::strcmp(arg, "--out") == 0) {
        outPath = value;
      } else if (std::strcmp(arg, "--settings") == 0) {
        settingsPath = value;
      } else if (std::strcmp(arg, "--controls") == 0) {
        controlsPath = value;
      } else if (std::strcmp(arg, "--high-score") == 0) {
        highScorePath = value;
      } else {
        fixtureDir = value;
      }
    } else if (std::strcmp(arg, "--help") == 0) {
      printUsage();
      return EXIT_SUCCESS;
    } else {
      std::fprintf(stderr, "Unknown option \"%s\"\n", arg);
      printUsage();
      return EXIT_FAILURE;
    }
  }
  if (!hasSeed || scriptPath == nullptr) {
    printUsage();
    return EXIT_FAILURE;
  }

  std::FILE* scriptFile = std::fopen(scriptPath, "r");
  if (scriptFile == nullptr) {
    std::fprintf(stderr, "Cannot open script \"%s\"\n", scriptPath);
    return EXIT_FAILURE;
  }
  std::vector<Command> commands;
  bool ok = true;
  {
    std::string line;
    char buffer[256];
    int lineNumber = 0;
    while (std::fgets(buffer, sizeof(buffer), scriptFile) != nullptr) {
      line = buffer;
      ++lineNumber;
      if (!parseScriptLine(line, &commands, lineNumber)) {
        ok = false;
      }
    }
  }
  std::fclose(scriptFile);
  if (!ok) {
    return EXIT_FAILURE;
  }

  std::FILE* out = stdout;
  if (outPath != nullptr) {
    out = std::fopen(outPath, "w");
    if (out == nullptr) {
      std::fprintf(stderr, "Cannot open output \"%s\"\n", outPath);
      return EXIT_FAILURE;
    }
  }

  // The session this tool mirrors: the same openTitle seed, the same adapter,
  // and one simulation tick per pumped frame, which is what the game binary
  // does under --tick-per-frame.
  stackfall::Game game;
  game.openTitle(seed);
  stackfall::InputAdapter adapter;
  constexpr int kPadSource = 1;  // the one virtual pad a script may use

  // The hold gate the platform layer owns (task T044, specification section
  // 5): HOLD off drops every hold edge before the round sees it. Its starting
  // state is the session's settings file, exactly as the game binary reads it
  // at startup, and a select on the HOLD row of the options screen flips it,
  // exactly as src/main.cpp's applyOptionsSelect does on the same edge.
  //
  // The orientation the platform layer applies (tasks T055 and T068): portrait
  // swaps the two script tables below for the rotated ones, exactly as
  // src/main.cpp's applyOrientation() swaps the tables of SdlInput. The
  // session opens on the orientation of the settings file, and a select on
  // the ORIENTATION row turns the running session at once - the same tick
  // that consumed the select edge swaps these tables and forgets every held
  // level and pending edge of the adapter, exactly as SdlInput's
  // setOrientation() does - so a script that plays either side of the turn
  // replays against the tables that were in force when it pressed.
  bool holdEnabled = true;
  bool portraitTables = false;
  stackfall::SettingsResult loaded;
  if (settingsPath != nullptr) {
    // The harness names the settings file itself; the store wants its
    // directory, exactly like the record file below.
    const std::filesystem::path settingsFile(settingsPath);
    const stackfall::SettingsStore settings(
        settingsFile.has_parent_path() ? settingsFile.parent_path().string()
                                       : std::string());
    loaded = settings.load();
    holdEnabled = loaded.holdOn;
    std::fprintf(stderr, "reference: hold preference %s at startup (%s)\n",
                 holdEnabled ? "on" : "off",
                 stackfall::saveStatusName(loaded.status));
  }
  portraitTables = loaded.orientation == stackfall::Orientation::Portrait;
  std::fprintf(stderr, "reference: orientation %s at startup (%s)\n",
               portraitTables ? "portrait" : "landscape",
               settingsPath != nullptr
                   ? stackfall::saveStatusName(loaded.status)
                   : "not found");

  // The binding tables the platform layer owns (task T066): the same
  // controls.dat the game reads, the same recovery to the section 5 defaults,
  // and the same mid-session edits a capture applies. The frame in force is
  // `portraitTables` above, exactly as in the game, and the customization
  // screen below edits the table of that frame alone.
  stackfall::ControlBindings bindings = stackfall::defaultControlBindings();
  if (controlsPath != nullptr) {
    const std::filesystem::path controlsFile(controlsPath);
    const stackfall::ControlsStore controls(
        controlsFile.has_parent_path() ? controlsFile.parent_path().string()
                                       : std::string());
    const stackfall::ControlsResult loadedControls = controls.load();
    bindings = loadedControls;
    std::fprintf(stderr, "reference: %s bindings at startup (%s)\n",
                 stackfall::controlBindingsAreDefault(bindings) ? "default"
                                                                : "custom",
                 stackfall::saveStatusName(loadedControls.status));
  }
  // The customization screen's capture state: the row whose capture is armed,
  // or -1 while none is, and the capture a pump just took.
  int captureRow = -1;
  bool captureArrived = false;
  stackfall::DeviceKind captureDevice = stackfall::DeviceKind::Keyboard;
  int captureControl = stackfall::kNoControl;

  // The record persistence mirror of task T052 (specification sections 4.7
  // and 9): `savedRecord` holds what the game binary's store holds, seeded
  // from the record file the harness names and raised by every score that
  // passes it, and the feedback below feeds it back between rounds exactly
  // as src/main.cpp does. The round-boundary state walks the same rule: a
  // round is in play across Playing, Paused, and the controls screen over a
  // frozen round, and a round whose clock restarts while one is already in
  // play was replaced by a restart. Nothing here writes: the record file
  // belongs to the game process the harness runs after this replay.
  std::uint64_t savedRecord = 0;
  if (highScorePath != nullptr) {
    const std::filesystem::path recordFile(highScorePath);
    const stackfall::HighScoreStore records(
        recordFile.has_parent_path() ? recordFile.parent_path().string()
                                     : std::string());
    const stackfall::LoadResult loaded = records.load();
    game.setHighScore(loaded.highScore);
    savedRecord = loaded.highScore;
    std::fprintf(stderr, "reference: record %llu at startup (%s)\n",
                 static_cast<unsigned long long>(loaded.highScore),
                 stackfall::saveStatusName(loaded.status));
  }
  bool roundActive = false;
  std::uint64_t roundTicks = 0;
  // The scene the previous simulated tick ran on, the state the
  // return-to-play gate above reads.
  stackfall::Scene sceneOfPreviousTick = game.scene();

  std::size_t index = 0;
  bool waiting = false;
  std::uint64_t waitUntil = 0;
  std::uint64_t ticks = 0;
  bool running = true;
  bool padAttached = true;
  // The thumbstick of the one pad a script may use (task T86): the D-pad
  // control each axis last deflected to, or kNoControl at rest. The game's
  // SDL layer derives the same transitions from real axis events, so this
  // mirror derives them from the values the script names.
  int stickArms[2] = {stackfall::kNoControl, stackfall::kNoControl};
  // The game forgets the stick's deflected arms wherever it forgets the
  // held levels and pending edges - a binding change and the orientation
  // turn - so this mirror does the same on the same edges.
  const auto forgetStickArms = [&stickArms]() {
    stickArms[0] = stackfall::kNoControl;
    stickArms[1] = stackfall::kNoControl;
  };
  while (running) {
    // Pump: commands in order until the next pending wait completes. A quit
    // only asks the loop to leave; the rest of this frame - its tick and its
    // checkpoints - still runs, exactly like the game binary, whose SDL_QUIT
    // lands in the poll before the very same frame's simulation.
    bool quitRequested = false;
    std::vector<std::string> pendingChecks;
    while (index < commands.size()) {
      const Command& command = commands[index];
      if (command.kind == Command::Kind::Wait) {
        if (!waiting) {
          waitUntil = ticks + static_cast<std::uint64_t>(command.value);
          waiting = true;
        }
        if (ticks < waitUntil) {
          break;
        }
        waiting = false;
        ++index;
        continue;
      }
      switch (command.kind) {
        case Command::Kind::Key:
        case Command::Kind::Pad: {
          if (command.kind == Command::Kind::Pad && !padAttached) {
            break;  // the detached pad no longer exists; the game ignores it
          }
          const bool keyboard = command.kind == Command::Kind::Key;
          const int control = keyboard
                                  ? scriptKeyboardControl(command.name)
                                  : scriptControllerControl(command.name);
          if (control == stackfall::kNoControl) {
            break;  // an unmapped control changes nothing, as in the game
          }
          if (!command.pressed) {
            // A release never captures; it only clears the level the press
            // raised, which is also what the game's event path does.
            applyScriptControl(adapter,
                               keyboard ? stackfall::kKeyboardInputSource
                                        : kPadSource,
                               keyboard ? stackfall::DeviceKind::Keyboard
                                        : stackfall::DeviceKind::Controller,
                               control, false, bindings, portraitTables);
            break;
          }
          if (captureRow >= 0) {
            // The customization screen is capturing (task T066): the press
            // names a control and stops here, never reaching the adapter, so
            // it can neither move a cursor nor act on the round.
            captureArrived = true;
            captureDevice = keyboard ? stackfall::DeviceKind::Keyboard
                                      : stackfall::DeviceKind::Controller;
            captureControl = control;
            break;
          }
          applyScriptControl(adapter,
                             keyboard ? stackfall::kKeyboardInputSource
                                      : kPadSource,
                             keyboard ? stackfall::DeviceKind::Keyboard
                                      : stackfall::DeviceKind::Controller,
                             control, true, bindings, portraitTables);
          break;
        }
        case Command::Kind::Stick: {
          if (!padAttached) {
            break;  // the detached pad no longer exists; the game ignores it
          }
          const auto axis = static_cast<stackfall::StickAxis>(command.value);
          const int arm =
              stackfall::controllerControlOfStickAxis(axis, command.axisValue);
          if (arm == stickArms[command.value]) {
            break;  // the same arm, or the same rest: no transition
          }
          if (stickArms[command.value] != stackfall::kNoControl) {
            applyScriptControl(adapter, kPadSource,
                               stackfall::DeviceKind::Controller,
                               stickArms[command.value], false, bindings,
                               portraitTables);
          }
          stickArms[command.value] = arm;
          if (arm == stackfall::kNoControl) {
            break;
          }
          if (captureRow >= 0) {
            // A deflection names no control, so a capture never takes it and
            // it never reaches the adapter either (task T86).
            break;
          }
          applyScriptControl(adapter, kPadSource,
                             stackfall::DeviceKind::Controller, arm, true,
                             bindings, portraitTables);
          break;
        }
        case Command::Kind::Stall:
          break;  // a tick-per-frame session has no catch-up to force
        case Command::Kind::Detach:
          adapter.dropSource(kPadSource);
          padAttached = false;
          // The stick state dies with the pad, exactly as the game's
          // forgetStickOf() does on controller removal.
          forgetStickArms();
          break;
        case Command::Kind::Attach:
          // The replug of task T066: a fresh handle with a fresh instance id,
          // so the game's adapter opens a new source and this replay simply
          // accepts pad commands again.
          padAttached = true;
          break;
        case Command::Kind::Restart:
          game.start(game.seed());
          break;
        case Command::Kind::Fixture: {
          if (fixtureDir == nullptr) {
            std::fprintf(stderr,
                         "fixture command without --fixture-dir; ignored\n");
            break;
          }
          const std::string path =
              std::string(fixtureDir) + "/" + command.name + ".board.txt";
          stackfall::BoardFixture fixture;
          std::string error;
          if (!stackfall::loadBoardFixture(path, &fixture, &error)) {
            std::fprintf(stderr, "fixture %s unavailable: %s\n",
                         command.name.c_str(), error.c_str());
            break;
          }
          if (!game.applyBoardFixture(fixture)) {
            std::fprintf(stderr, "fixture %s rejected\n",
                         command.name.c_str());
          }
          break;
        }
        case Command::Kind::Check:
          pendingChecks.push_back(command.name);
          break;
        case Command::Kind::Shot:
          break;  // the reference draws nothing; the game saves the image
        case Command::Kind::Quit:
          quitRequested = true;
          break;
        case Command::Kind::Echo:
          std::fprintf(out, "echo %s\n", command.text.c_str());
          break;
        case Command::Kind::Wait:
          break;  // handled above
      }
      ++index;
    }

    // A capture that arrived in this frame's pump (task T066): the game binary
    // applies it right after its event poll and before this frame's ticks, so
    // this replay does the same. The conflict rule, the same-control
    // cancellation, and the single-control assignment are the rules of
    // src/main.cpp's capture handler, restated here so the two cannot drift.
    if (captureArrived) {
      captureArrived = false;
      const int action = captureRow;
      captureRow = -1;
      const stackfall::Orientation frame =
          portraitTables ? stackfall::Orientation::Portrait
                         : stackfall::Orientation::Landscape;
      stackfall::ControlTable& table = bindings.table(captureDevice, frame);
      const int holder = stackfall::actionUsingControl(table, captureControl);
      if (holder == action) {
        std::fprintf(stderr, "reference: capture cancelled, binding kept\n");
      } else if (holder >= 0) {
        std::fprintf(stderr,
                     "reference: capture rejected, control already bound\n");
      } else {
        table[action][0] = captureControl;
        table[action][1] = stackfall::kNoControl;
        adapter.clearHeldAndPending();
        forgetStickArms();
        std::fprintf(stderr, "reference: binding applied\n");
      }
    }

    // One tick per pump, then the checkpoint records, mirroring the game
    // loop's poll-tick-render-flush order under --tick-per-frame.
    stackfall::TickInput tickInput = adapter.takeTickInput();
    // The scene before the tick owns the select edge, so the press that opens
    // the options screen never toggles a row - the same rule the game binary
    // applies before it reads the highlighted row.
    const stackfall::Scene sceneBeforeTick = game.scene();
    // The return-to-play gate of the platform layer (task T067): a press a
    // menu already owns may not act on the round that opens next, because
    // D-pad up moves the menu cursor and hard drops in one press. The game
    // binary spends every gameplay edge of the first tick a round owns
    // before the core sees it; this mirror spends the same edges on the same
    // tick, so a script that queues a press across a restart - the one
    // transition that happens outside a tick - replays identically on both
    // sides.
    if (sceneBeforeTick == stackfall::Scene::Playing &&
        sceneOfPreviousTick != stackfall::Scene::Playing) {
      tickInput.game.rotateCWPressed = false;
      tickInput.game.rotateCCWPressed = false;
      tickInput.game.hardDropPressed = false;
      tickInput.game.holdPressed = false;
      tickInput.game.pausePressed = false;
    }
    sceneOfPreviousTick = sceneBeforeTick;
    if (!holdEnabled) {
      tickInput.game.holdPressed = false;  // the platform's HOLD gate
    }
    game.tick(tickInput);
    if (sceneBeforeTick == stackfall::Scene::Options &&
        tickInput.menu.selectPressed &&
        game.optionsSelection() == kOptionsHoldRow) {
      holdEnabled = !holdEnabled;
      std::fprintf(stderr, "reference: hold preference %s (options row %d)\n",
                   holdEnabled ? "on" : "off", kOptionsHoldRow);
    }
    // The customization screen's rows (task T066): a select on one of the
    // eight action rows arms a capture for the next press, and a select on
    // RESTORE DEFAULTS returns all four tables to the section 5 defaults.
    // Both clear the adapter exactly as the game's setControlBindings() does,
    // because the tables changed under whatever the script was holding.
    if (sceneBeforeTick == stackfall::Scene::Rebind &&
        tickInput.menu.selectPressed &&
        game.scene() == stackfall::Scene::Rebind) {
      const int row = game.rebindSelection();
      if (row == stackfall::kRebindRestoreRow) {
        bindings = stackfall::defaultControlBindings();
        adapter.clearHeldAndPending();
        forgetStickArms();
        std::fprintf(stderr, "reference: bindings restored to defaults\n");
      } else if (row >= 0 && row < stackfall::kRebindableActionCount) {
        captureRow = row;
        std::fprintf(stderr, "reference: capture armed on row %d\n", row);
      }
    }
    // Leaving the customization screen ends any capture still armed, so a
    // press on another screen can never change a binding.
    if (sceneBeforeTick == stackfall::Scene::Rebind &&
        game.scene() != stackfall::Scene::Rebind && captureRow >= 0) {
      captureRow = -1;
      std::fprintf(stderr, "reference: capture cancelled on leaving\n");
    }
    if (sceneBeforeTick == stackfall::Scene::Options &&
        tickInput.menu.selectPressed &&
        game.optionsSelection() == kOptionsOrientationRow) {
      // The turn the game binary applies on the same edge (task T068): the
      // tables swap at once and the adapter forgets every held level and
      // pending edge, so a control the script pressed before the turn cannot
      // act after it under either table.
      portraitTables = !portraitTables;
      if (captureRow >= 0) {
        captureRow = -1;
        std::fprintf(stderr, "reference: capture cancelled by the turn\n");
      }
      adapter.clearHeldAndPending();
      forgetStickArms();
      std::fprintf(stderr,
                   "reference: orientation %s (options row %d, applied in "
                   "this session)\n",
                   portraitTables ? "portrait" : "landscape",
                   kOptionsOrientationRow);
    }
    // The persistence mirror, tick by tick: the saved record rises the
    // moment the running score passes it (the game's save; this replay only
    // tracks the value), and the round boundaries below feed it back into
    // the session between rounds, never mid-round, so every checkpoint's
    // `high` field names what the game binary must show.
    if (game.score() > savedRecord) {
      savedRecord = game.score();
    }
    const stackfall::Scene sceneAfterTick = game.scene();
    if (roundActive && sceneAfterTick != stackfall::Scene::Playing &&
        sceneAfterTick != stackfall::Scene::Paused &&
        sceneAfterTick != stackfall::Scene::PauseControls) {
      roundActive = false;  // game over or Quit to Title ended the round
      game.setHighScore(savedRecord);
    } else if (sceneAfterTick == stackfall::Scene::Playing && !roundActive) {
      roundActive = true;  // Start Game or Play Again began a round
    } else if (roundActive && sceneAfterTick == stackfall::Scene::Playing &&
               game.tickCount() < roundTicks) {
      game.setHighScore(savedRecord);  // Restart replaced the round
    }
    roundTicks = game.tickCount();
    ++ticks;
    for (const std::string& name : pendingChecks) {
      const std::string record =
          stackfall::observationLine(name, game) + " shot=none";
      std::fprintf(out, "%s\n", record.c_str());
    }
    // A confirmed Quit on the title menu ends the session the same way the
    // game binary's loop ends: after this frame's tick, render, and flush.
    if (quitRequested || game.quitRequested()) {
      running = false;
    }
    if (ticks > 1000000) {
      std::fprintf(stderr, "Reference replay exceeded 1000000 ticks\n");
      return EXIT_FAILURE;
    }
  }

  std::fprintf(out, "reference ticks=%llu\n",
               static_cast<unsigned long long>(ticks));
  if (outPath != nullptr) {
    std::fclose(out);
  }
  return EXIT_SUCCESS;
}
