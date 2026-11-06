// Bounded numeric arguments of the session script language
// (docs/AGENT_TESTING.md, task T023), shared by the two parsers that mirror
// each other: the game binary's script driver and the headless reference
// replay. Task T025's fuzz pass found that a `wait` or `stall` count above
// the 32-bit range truncated into a negative delay, hanging the process
// inside SDL_Delay for days, so the accepted range is one explicit rule
// pinned by the headless suite instead of an implicit cast in two places.

#pragma once

#include <cstdint>

namespace stackfall {

// The largest count a script may name: 3,600,000 ticks are 100 minutes of
// simulation at 60 Hz and 3,600,000 ms are one hour of forced stall, far
// above every recorded session, and far below any value whose truncation
// could surprise a 32-bit delay.
constexpr int kMaxScriptCount = 3600000;

// Parses the numeric argument of `wait <ticks>` and `stall <ms>`: strict
// decimal digits only, no sign, no spaces, and at most kMaxScriptCount.
// Returns false and leaves `out` untouched on any other input, so a fuzzed
// or hand-edited script cannot reach a delay with a value nobody vetted.
bool parseScriptCount(const char* text, int* out);

// The widest axis reading a script may name: the largest value of a signed
// 16-bit SDL axis, 32767, and its smallest, -32768. A `stick` command names
// the exact value the virtual pad reports, so the deadzone boundary of
// specification section 5 stays testable from a session script.
constexpr int kMaxScriptAxisValue = 32767;
constexpr int kMinScriptAxisValue = -32768;

// Parses the numeric argument of `stick <axis> <value>`: an optional minus
// sign, strict decimal digits, and a value inside the signed 16-bit SDL axis
// range. Returns false and leaves `out` untouched on any other input, so a
// fuzzed count cannot become a surprising axis reading either.
bool parseScriptAxisValue(const char* text, int* out);

}  // namespace stackfall
