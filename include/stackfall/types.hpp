#pragma once

// Board-wide constants and types for the STACKFALL rules core
// (specification section 4.1). Everything here is plain integer data with no
// platform dependency, so the headless tests and the device build share the
// exact same definitions.

#include <array>
#include <cstdint>

namespace stackfall {

// ---------------------------------------------------------------------------
// Board constants. The visible field is 10 x 20 with two hidden rows above
// it. Rows exist for y = -kHiddenRows .. kBoardHeight - 1, so the storage
// index of a row is y + kHiddenRows.
// ---------------------------------------------------------------------------
inline constexpr int kBoardWidth = 10;   // W: logical columns x = 0..9
inline constexpr int kBoardHeight = 20;  // H: visible rows y = 0..19
inline constexpr int kHiddenRows = 2;    // HIDDEN: hidden rows y = -2..-1
inline constexpr int kBoardRows = kBoardHeight + kHiddenRows;  // 22 stored rows

// A completely filled board row. Row masks keep the ten occupancy bits of a
// column inside a uint16_t.
inline constexpr std::uint16_t kFullRowMask = 0x03FF;

// One occupied square. In board coordinates x = 0..9 and y = -2..19; in a
// piece rotation matrix x and y are local to that matrix.
struct Cell {
  int x;
  int y;
};

constexpr bool operator==(const Cell& a, const Cell& b) {
  return a.x == b.x && a.y == b.y;
}

constexpr bool operator!=(const Cell& a, const Cell& b) {
  return !(a == b);
}

// Cell helpers. A cell lies on the board only when its column is one of the
// ten board columns and its row is one of the 22 stored rows.
constexpr bool inBoardX(int x) {
  return x >= 0 && x < kBoardWidth;
}

constexpr bool inBoardY(int y) {
  return y >= -kHiddenRows && y < kBoardHeight;
}

constexpr bool inBoard(const Cell& cell) {
  return inBoardX(cell.x) && inBoardY(cell.y);
}

// Storage index of row y inside the occupancy and cellType arrays.
constexpr int boardRowIndex(int y) {
  return y + kHiddenRows;
}

// ---------------------------------------------------------------------------
// Piece identity. The numeric values are the block codes stored in the
// cellType color array, so 0 stays reserved for "empty" and the ids run
// 1..7 (specification section 4.1).
// ---------------------------------------------------------------------------
enum class PieceId : std::uint8_t {
  I = 1,
  O = 2,
  T = 3,
  S = 4,
  Z = 5,
  J = 6,
  L = 7,
};

inline constexpr int kPieceCount = 7;

// The seven piece ids in specification order I, O, T, S, Z, J, L.
inline constexpr std::array<PieceId, kPieceCount> kAllPieceIds = {
    {PieceId::I, PieceId::O, PieceId::T, PieceId::S, PieceId::Z, PieceId::J,
     PieceId::L}};

// ---------------------------------------------------------------------------
// Screen orientation (task T055). The player picks one on the options screen
// and the platform layer turns the running session on that select (task
// T068), so the value also travels through the settings file the rules core
// already owns and a relaunch opens on it. Landscape is the default and the
// recovery value of a damaged file.
//
// Portrait turns the RG503 90 degrees clockwise in the player's hands: the
// D-pad end moves to the top of the grip, the action cluster to the bottom,
// and the scene rotates the other way so it stands upright in that grip. The
// pinned rotation and the remapped controls live in notes/design-review.md.
// ---------------------------------------------------------------------------
enum class Orientation : std::uint8_t {
  Landscape = 0,
  Portrait = 1,
};

inline constexpr Orientation kDefaultOrientation = Orientation::Landscape;

// ---------------------------------------------------------------------------
// Rotation states (specification section 4.2): spawn is "0", one clockwise
// quarter turn is "R", the half turn is "2", and one counter-clockwise
// quarter turn is "L". The numeric order is the SRS state order.
// ---------------------------------------------------------------------------
enum class Rotation : std::uint8_t {
  Spawn = 0,
  Clockwise = 1,
  HalfTurn = 2,
  CounterClockwise = 3,
};

inline constexpr int kRotationCount = 4;

// The four rotation states in SRS order 0, R, 2, L.
inline constexpr std::array<Rotation, kRotationCount> kAllRotations = {
    {Rotation::Spawn, Rotation::Clockwise, Rotation::HalfTurn,
     Rotation::CounterClockwise}};

// The rotation state reached by one 90 degree turn.
constexpr Rotation rotateCw(Rotation rotation) {
  return static_cast<Rotation>((static_cast<int>(rotation) + 1) %
                               kRotationCount);
}

constexpr Rotation rotateCcw(Rotation rotation) {
  return static_cast<Rotation>((static_cast<int>(rotation) + kRotationCount -
                                1) % kRotationCount);
}

}  // namespace stackfall
