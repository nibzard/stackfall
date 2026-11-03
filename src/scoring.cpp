// Level, gravity, and points rules for the STACKFALL rules core
// (specification sections 4.5 and 4.6).

#include "stackfall/scoring.hpp"

#include <cassert>

namespace stackfall {

namespace {

// Ticks one cell takes at each level, level 1 first. The table is pinned by
// the test suite against the values printed in section 4.5, so a mistyped
// entry fails the suite.
constexpr std::array<int, kMaxLevel> kGravityTicks = {
    {48, 43, 38, 33, 28, 23, 18, 13, 8, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1}};

}  // namespace

const std::array<int, kMaxLevel>& gravityTable() {
  return kGravityTicks;
}

int gravityTicksForLevel(int level) {
  // Levels outside 1..20 read the nearest entry: level 20 is the cap of
  // section 4.6 and level 0 never occurs in a round.
  if (level <= kMinLevel) {
    return kGravityTicks.front();
  }
  if (level >= kMaxLevel) {
    return kGravityTicks.back();
  }
  return kGravityTicks[static_cast<std::size_t>(level - 1)];
}

int levelForLines(std::uint64_t totalLines) {
  const std::uint64_t level = 1 + totalLines / kLinesPerLevel;
  return static_cast<int>(level > kMaxLevel ? kMaxLevel : level);
}

std::uint64_t clearBasePoints(int linesCleared) {
  switch (linesCleared) {
    case 1:
      return 100;
    case 2:
      return 300;
    case 3:
      return 500;
    case 4:
      return 800;
    default:
      // Zero rows, or a count no single piece can clear.
      return 0;
  }
}

std::uint64_t clearPoints(int linesCleared, int level) {
  assert(level >= kMinLevel && level <= kMaxLevel &&
         "clearPoints needs the pre-clear level of a round");
  return clearBasePoints(linesCleared) * static_cast<std::uint64_t>(level);
}

std::uint64_t addScore(std::uint64_t total, std::uint64_t points) {
  // Unsigned addition wraps when the sum passes 2^64; saturation keeps the
  // printed score monotonic instead.
  const std::uint64_t sum = total + points;
  return sum < total ? ~std::uint64_t{0} : sum;
}

}  // namespace stackfall
