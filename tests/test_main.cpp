// Driver for the headless test suite.
// When a new suite lands, declare its run function here and add one call.
//
// The suite doubles as the developer test CLI of specification section 4.3:
// --seed <integer> makes the regression suite replay its pinned scenarios at
// that seed, so a run stays reproducible and refresh values are printable on
// any machine. Unknown options warn and are ignored, exactly as they do in
// the game binary, so a script cannot break the suite.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "test_util.hpp"

void run_sanity_tests();
// Milestone M1 suites, in specification order:
void run_pieces_tests();
void run_board_tests();
void run_randomizer_tests();
void run_game_tests();
void run_regression_tests(bool hasCustomSeed, std::uint64_t customSeed);
// Milestone M2 suites. The SDL-free input adapter and the SDL-free glyph
// table belong here; the SDL paths above them are the separate
// stackfall_input_tests and stackfall_render_tests targets.
void run_input_tests();
void run_font_tests();
// The section 4.7 screens of task T011, driven the way the platform layer
// drives them: openTitle plus tick(TickInput).
void run_screens_tests();
// The atomic high-score persistence of task T012 (section 11 item 20). The
// store is plain std::filesystem code, so it belongs in this headless suite.
void run_storage_tests();
// The developer fixture loader and checkpoint record formatter of the
// unattended harness (task T023, docs/AGENT_TESTING.md): core code with no
// SDL, so the strict parse rules and the record format stay pinned here.
void run_fixture_tests();
// The bounded script-count rule shared by the two mirrored script parsers
// (task T025's fuzz repair): core code with no SDL.
void run_script_tests();

namespace {

struct Options {
  bool hasSeed;
  std::uint64_t seed;
  bool help;
};

void printUsage() {
  std::printf(
      "Usage: stackfall_tests [--seed N]\n"
      "  --seed N  also replay every pinned scenario at seed N and print the\n"
      "            refresh values of the pinned hashes\n"
      "  --help    show this text\n");
}

// Parses --seed <integer>. A missing or malformed value warns and is ignored,
// so the pinned suites still run.
Options parseOptions(int argc, char** argv) {
  Options options{false, 0, false};
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--seed") == 0) {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "Warning: --seed needs a value; ignored\n");
        continue;
      }
      ++i;
      const char* value = argv[i];
      bool digitsOnly = value[0] != '\0';
      for (const char* p = value; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
          digitsOnly = false;
        }
      }
      if (!digitsOnly) {
        std::fprintf(stderr,
                     "Warning: --seed needs a number, got \"%s\"; ignored\n",
                     value);
        continue;
      }
      errno = 0;
      const unsigned long long parsed = std::strtoull(value, nullptr, 10);
      if (errno == ERANGE) {
        std::fprintf(stderr,
                     "Warning: --seed value \"%s\" is too large; ignored\n",
                     value);
        continue;
      }
      options.hasSeed = true;
      options.seed = static_cast<std::uint64_t>(parsed);
    } else if (std::strcmp(arg, "--help") == 0) {
      options.help = true;
    } else {
      std::fprintf(stderr, "Warning: unknown option \"%s\"; ignored\n", arg);
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parseOptions(argc, argv);
  if (options.help) {
    printUsage();
    return EXIT_SUCCESS;
  }

  run_sanity_tests();
  run_pieces_tests();
  run_board_tests();
  run_randomizer_tests();
  run_game_tests();
  run_regression_tests(options.hasSeed, options.seed);
  run_input_tests();
  run_font_tests();
  run_screens_tests();
  run_storage_tests();
  run_fixture_tests();
  run_script_tests();

  stackfall_test::print_summary();
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
