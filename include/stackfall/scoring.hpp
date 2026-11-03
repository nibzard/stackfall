#pragma once

// Level, gravity, and points rules of the STACKFALL rules core (specification
// sections 4.5 and 4.6). Everything here is fixed-width integer arithmetic, so
// a score or a level is the same number on every build machine and on the
// device.

#include <array>
#include <cstdint>

namespace stackfall {

// Levels run 1..20; speed stops increasing at level 20 but scoring continues
// (specification section 4.6).
inline constexpr int kMinLevel = 1;
inline constexpr int kMaxLevel = 20;

// Ten cleared lines raise the level by one.
inline constexpr int kLinesPerLevel = 10;

// Manual drop points (specification section 4.6).
inline constexpr std::uint64_t kSoftDropPointsPerCell = 1;
inline constexpr std::uint64_t kHardDropPointsPerCell = 2;

// The gravity table of section 4.5: simulation ticks one cell takes at each
// level, 48 at level 1 down to 1 at level 20. Index 0 is level 1.
const std::array<int, kMaxLevel>& gravityTable();

// Ticks per cell at `level`, clamped into [kMinLevel, kMaxLevel], so a caller
// never indexes the table out of range.
int gravityTicksForLevel(int level);

// The level a round sits at after `totalLines` cleared lines:
// min(20, 1 + floor(totalLines / 10)) (specification section 4.6).
int levelForLines(std::uint64_t totalLines);

// Base points of one lock that clears `linesCleared` rows: 0, 100, 300, 500,
// and 800 for zero through four rows. Any other count scores 0, because a
// piece can never clear more than four rows.
std::uint64_t clearBasePoints(int linesCleared);

// Points of one clear at `level`, which must be the level before the clear.
// The caller passes the pre-clear level (specification section 4.6).
std::uint64_t clearPoints(int linesCleared, int level);

// Adds `points` to `total` and saturates instead of wrapping, so the unsigned
// 64-bit score the formatter prints never ran over (specification section 4.6).
std::uint64_t addScore(std::uint64_t total, std::uint64_t points);

}  // namespace stackfall
