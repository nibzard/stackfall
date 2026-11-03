// Headless suite for the bounded script-count parser of the session script
// language (include/stackfall/script.hpp, task T025's fuzz repair). The
// crashing inputs of the fuzz pass are pinned here: before the shared
// parser existed, a `wait` or `stall` count above the 32-bit range
// truncated into a negative or multi-day delay that hung the process
// inside SDL_Delay - `stall 6442450944` never returned. Both script
// parsers (the game binary's driver and the headless reference replay) now
// route their counts through this one rule, so the headless suite pins the
// exact accepted range.

#include "test_util.hpp"

#include "stackfall/script.hpp"

namespace {

void run_bounded_range_tests() {
  // Valid counts round through: zero, one, every-day session values, and
  // the boundary itself.
  int value = -1;
  SC_CHECK(stackfall::parseScriptCount("0", &value));
  SC_CHECK(value == 0);
  SC_CHECK(stackfall::parseScriptCount("1", &value));
  SC_CHECK(value == 1);
  SC_CHECK(stackfall::parseScriptCount("60", &value));
  SC_CHECK(value == 60);
  SC_CHECK(stackfall::parseScriptCount("36000", &value));
  SC_CHECK(value == 36000);
  SC_CHECK(stackfall::parseScriptCount("4000000", &value) == false);
  const char* boundary = "3600000";
  SC_CHECK(stackfall::parseScriptCount(boundary, &value));
  SC_CHECK(value == stackfall::kMaxScriptCount);
  SC_CHECK(value == 3600000);
}

void run_crashing_inputs_tests() {
  // One line per input the fuzz pass (or the manual probe that preceded
  // it) showed must never become a delay. `out` must stay untouched on a
  // rejection, so a rejected line can never fall back to a stale value.
  int value = 7;
  const char* crashes[] = {
      // 2^31: truncates to INT_MIN, a negative SDL_Delay argument.
      "2147483648",
      // 2^32: truncates to zero - surprising, not a delay anyone asked for.
      "4294967296",
      // 2^32 + 2^31 - 3: truncates to 2147483645 ms, a 24.8-day delay.
      "6442450941",
      // 2^32 + 2^31: truncates to INT_MIN.
      "6442450944",
      // The 64-bit limit and one digit past it.
      "18446744073709551615",
      "99999999999999999999999999",
  };
  for (const char* text : crashes) {
    SC_CHECK(stackfall::parseScriptCount(text, &value) == false);
    SC_CHECK(value == 7);
  }
  // One past the accepted boundary, in two spellings.
  SC_CHECK(stackfall::parseScriptCount("3600001", &value) == false);
  SC_CHECK(stackfall::parseScriptCount("03600001", &value) == false);
  SC_CHECK(value == 7);
}

void run_malformed_inputs_tests() {
  // Everything that is not bare decimal digits is rejected: the empty
  // string, signs, whitespace, radix prefixes, decimals, and text.
  int value = 7;
  const char* malformed[] = {
      "", "-", "+1", "-1", " 1", "1 ", "1\t", "0x10", "1e3", "1.5", "1,000",
      "one", "down", "007abc", "1;", ";1",
  };
  for (const char* text : malformed) {
    SC_CHECK(stackfall::parseScriptCount(text, &value) == false);
    SC_CHECK(value == 7);
  }
  // A null pointer is a rejected argument, never a crash.
  SC_CHECK(stackfall::parseScriptCount(nullptr, &value) == false);
  // Leading zeros stay decimal: "007" is seven, not octal.
  SC_CHECK(stackfall::parseScriptCount("007", &value));
  SC_CHECK(value == 7);
}

}  // namespace

void run_script_tests() {
  run_bounded_range_tests();
  run_crashing_inputs_tests();
  run_malformed_inputs_tests();
}
