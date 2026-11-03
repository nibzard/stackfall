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
// The two deliberate copies in this file are the point, not a shortcut:
//
//   * the script parser mirrors the grammar of src/main.cpp without sharing
//     its code, so a parsing defect there cannot hide here;
//   * the key and pad tables below are transcribed from specification
//     section 5, not from src/input_sdl.cpp, so a wrong or missing mapping
//     in the SDL layer shows up as a record mismatch.
//
// Link target: stackfall_core only (CMake target stackfall_reference), so
// this stays a headless rules tool on every machine that builds the tests.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/observe.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/fixture.hpp"

namespace {

// ---------------------------------------------------------------------------
// The section 5 tables, transcribed from the specification.
// ---------------------------------------------------------------------------

struct ScriptBinding {
  const char* name;
  stackfall::GameAction primary;
  stackfall::GameAction secondary;  // kNoGameAction: none
};

const stackfall::GameAction kNone = stackfall::GameAction::kCount;

// Keyboard: arrows move and soft drop, X or Up rotate clockwise, Z rotates
// counter-clockwise, Space hard drops, C or left Shift holds, Enter or Esc
// pauses, plus the menu roles of Enter, Esc, Up, and Down. M switches the
// sound, which is platform-layer state outside the rules core, so its entry
// carries no action and the replay accepts the key the way the game's script
// driver does without changing the round.
const ScriptBinding kKeyTable[] = {
    {"left", stackfall::GameAction::Left, kNone},
    {"right", stackfall::GameAction::Right, kNone},
    {"down", stackfall::GameAction::Down, stackfall::GameAction::MenuDown},
    {"up", stackfall::GameAction::RotateCW, stackfall::GameAction::MenuUp},
    {"x", stackfall::GameAction::RotateCW, kNone},
    {"z", stackfall::GameAction::RotateCCW, kNone},
    {"space", stackfall::GameAction::HardDrop, kNone},
    {"c", stackfall::GameAction::Hold, kNone},
    {"lshift", stackfall::GameAction::Hold, kNone},
    {"enter", stackfall::GameAction::Pause, stackfall::GameAction::MenuSelect},
    {"esc", stackfall::GameAction::Pause, stackfall::GameAction::MenuBack},
    {"m", kNone, kNone},
};

// Virtual pad: D-pad move and soft drop, A clockwise, B counter-clockwise,
// X hard drop, Y or left shoulder hold, Start pause, and the menu roles of A,
// B, and the D-pad.
const ScriptBinding kPadTable[] = {
    {"a", stackfall::GameAction::RotateCW, stackfall::GameAction::MenuSelect},
    {"b", stackfall::GameAction::RotateCCW, stackfall::GameAction::MenuBack},
    {"x", stackfall::GameAction::HardDrop, kNone},
    {"y", stackfall::GameAction::Hold, kNone},
    {"l1", stackfall::GameAction::Hold, kNone},
    {"start", stackfall::GameAction::Pause, kNone},
    {"up", stackfall::GameAction::MenuUp, kNone},
    {"down", stackfall::GameAction::Down, stackfall::GameAction::MenuDown},
    {"left", stackfall::GameAction::Left, kNone},
    {"right", stackfall::GameAction::Right, kNone},
};

const ScriptBinding* findBinding(const ScriptBinding* table, std::size_t count,
                                 const std::string& name) {
  for (std::size_t i = 0; i < count; ++i) {
    if (name == table[i].name) {
      return &table[i];
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Script commands, mirroring the grammar of src/main.cpp.
// ---------------------------------------------------------------------------

struct Command {
  enum class Kind : std::uint8_t {
    Key,
    Pad,
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
  bool pressed = false;
  int value = 0;
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
  std::uint64_t number = 0;
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
      if (findBinding(kKeyTable, sizeof(kKeyTable) / sizeof(kKeyTable[0]),
                      name) == nullptr) {
        return fail("unknown key name");
      }
      command.kind = Command::Kind::Key;
    } else {
      if (findBinding(kPadTable, sizeof(kPadTable) / sizeof(kPadTable[0]),
                      name) == nullptr) {
        return fail("unknown pad button name");
      }
      command.kind = Command::Kind::Pad;
    }
  } else if (verb == "wait" || verb == "stall") {
    if (!parseUnsigned(name.c_str(), &number)) {
      return fail("wait/stall needs a number");
    }
    command.kind = verb == "wait" ? Command::Kind::Wait : Command::Kind::Stall;
    command.value = static_cast<int>(number);
  } else if (verb == "detach") {
    command.kind = Command::Kind::Detach;
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
      "                           [--fixture-dir DIR]\n"
      "  Replays a session script through the rules core and writes one\n"
      "  checkpoint record per `check` command, in the format of the game\n"
      "  binary's --observations file. Headless: no SDL.\n");
}

}  // namespace

int main(int argc, char** argv) {
  bool hasSeed = false;
  std::uint64_t seed = 0;
  const char* scriptPath = nullptr;
  const char* outPath = nullptr;
  const char* fixtureDir = nullptr;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const bool needsValue = std::strcmp(arg, "--seed") == 0 ||
                            std::strcmp(arg, "--script") == 0 ||
                            std::strcmp(arg, "--out") == 0 ||
                            std::strcmp(arg, "--fixture-dir") == 0;
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

  std::size_t index = 0;
  bool waiting = false;
  std::uint64_t waitUntil = 0;
  std::uint64_t ticks = 0;
  bool running = true;
  bool padAttached = true;
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
          const ScriptBinding* binding = findBinding(
              command.kind == Command::Kind::Key ? kKeyTable : kPadTable,
              command.kind == Command::Kind::Key
                  ? sizeof(kKeyTable) / sizeof(kKeyTable[0])
                  : sizeof(kPadTable) / sizeof(kPadTable[0]),
              command.name);
          if (binding == nullptr) {
            break;  // an unmapped control changes nothing, as in the game
          }
          if (binding->primary == kNone) {
            break;  // a platform-only key (the sound toggle) has no rule
          }
          const int source =
              command.kind == Command::Kind::Key
                  ? stackfall::kKeyboardInputSource
                  : kPadSource;
          adapter.setButton(source, binding->primary, command.pressed);
          if (binding->secondary != kNone) {
            adapter.setButton(source, binding->secondary, command.pressed);
          }
          break;
        }
        case Command::Kind::Stall:
          break;  // a tick-per-frame session has no catch-up to force
        case Command::Kind::Detach:
          adapter.dropSource(kPadSource);
          padAttached = false;
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

    // One tick per pump, then the checkpoint records, mirroring the game
    // loop's poll-tick-render-flush order under --tick-per-frame.
    const stackfall::TickInput tickInput = adapter.takeTickInput();
    game.tick(tickInput);
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
