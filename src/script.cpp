// Implementation of the bounded script-count parser; see
// include/stackfall/script.hpp for why the range is explicit.

#include "stackfall/script.hpp"

namespace stackfall {

bool parseScriptCount(const char* text, int* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  // Accumulate in a value wider than the accepted range, so a long run of
  // digits can be rejected for its magnitude instead of wrapping.
  std::uint64_t value = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::uint64_t>(*p - '0');
    // Bail out as soon as the count is provably too large; the clamp also
    // makes overflow of the accumulator impossible.
    if (value > static_cast<std::uint64_t>(kMaxScriptCount)) {
      return false;
    }
  }
  *out = static_cast<int>(value);
  return true;
}

}  // namespace stackfall
