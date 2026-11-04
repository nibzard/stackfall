// Canonical tetromino shape tables for STACKFALL (specification section
// 4.2). Each orientation stores the four occupied cells of the piece's
// square rotation matrix in local coordinates: (x, y) with y counted from
// the top of the matrix.
//
// JLSTZ and O live in a 3x3 matrix and I in a 4x4 matrix, as SRS requires.
// The R, 2, and L rows below are the canonical spawn drawing turned by one,
// two, and three 90 degree steps inside that matrix. The tests derive those
// states independently from the rotation formula, so a mistyped cell fails
// the suite. O keeps its spawn cells in every state because it never rotates
// to a different footprint and never kicks.
//
// The kick tables at the bottom carry the same guarantee for offsets: the
// tests hold a verbatim copy of the reference table and compare it against
// these entries, so a mistyped or misnegated offset fails the suite.

#include "stackfall/pieces.hpp"

namespace stackfall {

namespace {

using Cells = std::array<Cell, kCellsPerPiece>;

// The orientation tables, indexed [piece id - 1][rotation state]. Piece
// order matches PieceId: I, O, T, S, Z, J, L. Rotation order is 0, R, 2, L.
const Cells kShapeTables[kPieceCount][kRotationCount] = {
    {// I: spawn row 1; after quarter turns column 2, row 2, column 1.
     {{{0, 1}, {1, 1}, {2, 1}, {3, 1}}},
     {{{2, 0}, {2, 1}, {2, 2}, {2, 3}}},
     {{{0, 2}, {1, 2}, {2, 2}, {3, 2}}},
     {{{1, 0}, {1, 1}, {1, 2}, {1, 3}}}},
    {// O: the same 2x2 block in every state; it never moves or kicks.
     {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
     {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
     {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}},
     {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}}},
    {// T: nub up at spawn, then right, down, and left.
     {{{1, 0}, {0, 1}, {1, 1}, {2, 1}}},
     {{{1, 0}, {2, 1}, {1, 1}, {1, 2}}},
     {{{0, 1}, {1, 1}, {2, 1}, {1, 2}}},
     {{{1, 0}, {0, 1}, {1, 1}, {1, 2}}}},
    {// S: top row right at spawn, then turned inside the matrix.
     {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}},
     {{{1, 0}, {1, 1}, {2, 1}, {2, 2}}},
     {{{1, 1}, {2, 1}, {0, 2}, {1, 2}}},
     {{{0, 0}, {0, 1}, {1, 1}, {1, 2}}}},
    {// Z: top row left at spawn, then turned inside the matrix.
     {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},
     {{{2, 0}, {1, 1}, {2, 1}, {1, 2}}},
     {{{0, 1}, {1, 1}, {1, 2}, {2, 2}}},
     {{{1, 0}, {0, 1}, {1, 1}, {0, 2}}}},
    {// J: hook in the top-left at spawn, then turned inside the matrix.
     {{{0, 0}, {0, 1}, {1, 1}, {2, 1}}},
     {{{1, 0}, {2, 0}, {1, 1}, {1, 2}}},
     {{{0, 1}, {1, 1}, {2, 1}, {2, 2}}},
     {{{1, 0}, {1, 1}, {1, 2}, {0, 2}}}},
    {// L: foot in the top-right at spawn, then turned inside the matrix.
     {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}},
     {{{1, 0}, {1, 1}, {1, 2}, {2, 2}}},
     {{{0, 1}, {1, 1}, {2, 1}, {0, 2}}},
     {{{0, 0}, {1, 0}, {1, 1}, {1, 2}}}},
};

// Every piece spawns with its matrix corner at this column, which centers
// the I at columns 3..6 and the O at columns 4..5 (specification 4.2).
constexpr int kSpawnOriginX = 3;

// The I drawing first occupies its local row 1, so its matrix starts one
// row higher than the others; both choices put the topmost occupied cell
// of the piece at visible row y = 0, the immediate-spawn rule approved on
// 2026-11-03 (task T029). Every piece is on screen the tick it appears,
// and the hidden rows stay free for the rotations of section 4.4.
constexpr int kSpawnOriginYStandard = 0;
constexpr int kSpawnOriginYI = -1;

// ---------------------------------------------------------------------------
// Wall-kick tables, specification section 4.4. Reference:
// https://www.tetris.wiki/SRS. The reference counts +y upward; the grid
// counts +y downward, so every dy below is the negated reference value; the
// tests hold a verbatim copy of each reference row and compare it against
// these entries. Layout: [rotation state turned from][0 = clockwise,
// 1 = counter-clockwise], five candidates per transition in canonical trial
// order, the first being the in-place test.
// ---------------------------------------------------------------------------
using KickRow = std::array<Kick, kKickCandidates>;

// Builds one table row from the five candidates in grid form, so the tables
// below stay readable as plain offset lists.
constexpr KickRow kickRow(Kick a, Kick b, Kick c, Kick d, Kick e) {
  return KickRow{{a, b, c, d, e}};
}

// J, L, S, T, and Z share this table.
const KickRow kJlstzKickTables[kRotationCount][2] = {
    {kickRow({0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}),   // 0->R
     kickRow({0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2})},     // 0->L
    {kickRow({0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}),     // R->2
     kickRow({0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2})},    // R->0
    {kickRow({0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}),      // 2->L
     kickRow({0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2})},  // 2->R
    {kickRow({0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}),  // L->0
     kickRow({0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2})}  // L->2
};

// The I tetromino has its own table.
const KickRow kIKickTables[kRotationCount][2] = {
    {kickRow({0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2}),    // 0->R
     kickRow({0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1})},   // 0->L
    {kickRow({0, 0}, {-1, 0}, {2, 0}, {-1, -2}, {2, 1}),    // R->2
     kickRow({0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2})},   // R->0
    {kickRow({0, 0}, {2, 0}, {-1, 0}, {2, -1}, {-1, 2}),    // 2->L
     kickRow({0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1})},   // 2->R
    {kickRow({0, 0}, {1, 0}, {-2, 0}, {1, 2}, {-2, -1}),    // L->0
     kickRow({0, 0}, {-2, 0}, {1, 0}, {-2, 1}, {1, -2})}    // L->2
};

// O never kicks (specification sections 4.4 and 11 item 2): the single
// candidate the piece tries is the in-place test.
const KickRow kOKickTable =
    kickRow({0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0});

}  // namespace

const std::array<Cell, kCellsPerPiece>& pieceCells(PieceId id,
                                                   Rotation rotation) {
  return kShapeTables[static_cast<int>(id) - 1][static_cast<int>(rotation)];
}

int pieceMatrixSize(PieceId id) {
  return id == PieceId::I ? 4 : 3;
}

int spawnX(PieceId id) {
  (void)id;
  return kSpawnOriginX;
}

int spawnY(PieceId id) {
  return id == PieceId::I ? kSpawnOriginYI : kSpawnOriginYStandard;
}

Piece makeSpawnPiece(PieceId id) {
  return Piece{id, Rotation::Spawn, spawnX(id), spawnY(id)};
}

std::array<Cell, kCellsPerPiece> boardCells(const Piece& piece) {
  const Cells& local = pieceCells(piece.id, piece.rotation);
  std::array<Cell, kCellsPerPiece> cells = {};
  for (std::size_t i = 0; i < local.size(); ++i) {
    cells[i] = Cell{piece.x + local[i].x, piece.y + local[i].y};
  }
  return cells;
}

std::size_t kickCandidateCount(PieceId id) {
  return id == PieceId::O ? 1 : kKickCandidates;
}

const std::array<Kick, kKickCandidates>& wallKicks(PieceId id, Rotation from,
                                                   bool clockwise) {
  if (id == PieceId::O) {
    return kOKickTable;
  }
  const std::size_t state = static_cast<std::size_t>(from);
  const std::size_t direction = clockwise ? 0 : 1;
  return id == PieceId::I ? kIKickTables[state][direction]
                          : kJlstzKickTables[state][direction];
}

char pieceLetter(PieceId id) {
  switch (id) {
    case PieceId::I:
      return 'I';
    case PieceId::O:
      return 'O';
    case PieceId::T:
      return 'T';
    case PieceId::S:
      return 'S';
    case PieceId::Z:
      return 'Z';
    case PieceId::J:
      return 'J';
    case PieceId::L:
      return 'L';
  }
  return '?';
}

bool pieceIdFromLetter(char letter, PieceId* id) {
  for (const PieceId candidate : kAllPieceIds) {
    if (pieceLetter(candidate) == letter) {
      *id = candidate;
      return true;
    }
  }
  return false;
}

}  // namespace stackfall
