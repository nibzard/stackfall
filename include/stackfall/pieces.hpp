#pragma once

// Tetromino definitions for the STACKFALL rules core (specification sections
// 4.2 and 4.4): the seven canonical pieces, their four SRS orientation
// states, their spawn positions, and the SRS wall-kick tables together with
// the rotation attempt rule. Every offset and position uses the grid
// convention of the rules core: x grows to the right and y grows downward.

#include <array>

#include "stackfall/types.hpp"

namespace stackfall {

// Every tetromino occupies exactly four cells.
inline constexpr std::size_t kCellsPerPiece = 4;

// A piece in play: identity, rotation state, and the board position of the
// top-left corner of its rotation matrix. x grows to the right and y grows
// downward.
struct Piece {
  PieceId id;
  Rotation rotation;
  int x;
  int y;
};

// The four occupied cells of one orientation, in matrix-local coordinates
// (0 <= x, y < pieceMatrixSize(id)).
const std::array<Cell, kCellsPerPiece>& pieceCells(PieceId id,
                                                   Rotation rotation);

// Side length of the square rotation matrix: 4 for I and 3 for O and JLSTZ,
// as required by the SRS rotation system (specification section 4.2).
int pieceMatrixSize(PieceId id);

// Spawn origin of a piece: the matrix corner sits at column 3 for every
// piece, at row -1 for I and at row 0 for the others. This centers the I
// at board columns 3..6 and the O at columns 4..5, and puts the topmost
// occupied cell of every piece at visible row y = 0, so each piece is on
// screen the tick it spawns (specification section 4.2).
int spawnX(PieceId id);
int spawnY(PieceId id);

// A fresh piece in its spawn state.
Piece makeSpawnPiece(PieceId id);

// The four board cells occupied by a piece state.
std::array<Cell, kCellsPerPiece> boardCells(const Piece& piece);

// Uppercase letter for a piece id, for logs, tests, and error messages.
char pieceLetter(PieceId id);

// The piece id of one uppercase letter I, O, T, S, Z, J, or L, for scripts and
// fixture files. Returns false for every other input.
bool pieceIdFromLetter(char letter, PieceId* id);

// ---------------------------------------------------------------------------
// SRS wall kicks (specification section 4.4).
// ---------------------------------------------------------------------------

// One candidate wall-kick offset in grid coordinates: dx grows to the right
// and dy grows downward, like every other coordinate of the rules core.
struct Kick {
  int dx;
  int dy;
};

constexpr bool operator==(const Kick& a, const Kick& b) {
  return a.dx == b.dx && a.dy == b.dy;
}

constexpr bool operator!=(const Kick& a, const Kick& b) {
  return !(a == b);
}

// SRS tries at most five candidate positions per rotation: the basic
// rotation plus four offsets.
inline constexpr std::size_t kKickCandidates = 5;

// Number of candidates a piece tries: five for I and JLSTZ, and one for O,
// because O never kicks (specification section 4.4).
std::size_t kickCandidateCount(PieceId id);

// The candidate offsets for one quarter turn out of `from`, clockwise or
// counter-clockwise, in canonical trial order. The first entry is always the
// in-place {0, 0} test and only the first kickCandidateCount(id) entries are
// meaningful. The values are grid offsets: they equal the reference table of
// https://www.tetris.wiki/SRS with the y component negated, because the
// reference counts +y upward while the grid counts +y downward.
const std::array<Kick, kKickCandidates>& wallKicks(PieceId id, Rotation from,
                                                   bool clockwise);

// Result of one rotation attempt.
struct RotationResult {
  Piece piece;   // the accepted candidate, or the unchanged input piece
  Kick kick;     // offset of the accepted candidate, {0, 0} on rejection
  bool rotated;  // true when a candidate was accepted
};

// Attempts one 90 degree rotation of `piece`. The candidates are the piece
// in the target rotation state at each offset of wallKicks(...) in order;
// the first candidate accepted by `isValid` wins. When no candidate is
// valid, the rotation is rejected and the piece stays unchanged
// (specification section 4.2). `isValid` receives candidate piece states
// only and must apply the section 4.2 validity rule: every occupied cell
// inside the ten columns and y = -2..19 and free of settled cells, with the
// bounds checked before any board row is read or written.
template <typename IsValid>
RotationResult rotatePiece(const Piece& piece, bool clockwise,
                           IsValid&& isValid) {
  const Rotation to =
      clockwise ? rotateCw(piece.rotation) : rotateCcw(piece.rotation);
  const std::array<Kick, kKickCandidates>& kicks =
      wallKicks(piece.id, piece.rotation, clockwise);
  const std::size_t count = kickCandidateCount(piece.id);
  for (std::size_t i = 0; i < count; ++i) {
    const Kick& kick = kicks[i];
    const Piece candidate{piece.id, to, piece.x + kick.dx, piece.y + kick.dy};
    if (isValid(candidate)) {
      return RotationResult{candidate, kick, true};
    }
  }
  return RotationResult{piece, Kick{0, 0}, false};
}

}  // namespace stackfall
