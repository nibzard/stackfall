// Headless suite for the board types, the seven tetromino shape tables, and
// the SRS rotation with wall kicks (tasks T003 and T004, milestone M1). The
// T003 acceptance criterion is specification section 11, item 1: every piece
// occupies exactly four cells in each of its four orientations; further
// checks pin the canonical spawn drawings, the SRS quarter-turn property of
// the orientation tables, and the spawn geometry of section 4.2. The T004
// acceptance criteria are section 11, items 2, 3, 5, and 6: O never moves or
// kicks, I and JLSTZ orientations match the SRS reference, rotation against
// walls applies a valid kick or fails without moving, and rotation above the
// hidden-row boundary never leaves y = -2..19.

#include <array>
#include <cstdio>

#include "stackfall/pieces.hpp"
#include "stackfall/types.hpp"
#include "test_util.hpp"

namespace {

using stackfall::Cell;
using stackfall::PieceId;
using stackfall::Rotation;

// Expected spawn cells of each piece, transcribed one by one from the
// canonical drawings in specification section 4.2, in matrix-local
// coordinates.
struct SpawnDrawing {
  PieceId id;
  std::array<Cell, 4> cells;
};

const std::array<SpawnDrawing, 7> kSpawnDrawings = {{
    {PieceId::I, {{{0, 1}, {1, 1}, {2, 1}, {3, 1}}}},
    {PieceId::O, {{{1, 0}, {2, 0}, {1, 1}, {2, 1}}}},
    {PieceId::T, {{{1, 0}, {0, 1}, {1, 1}, {2, 1}}}},
    {PieceId::S, {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}}},
    {PieceId::Z, {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}}},
    {PieceId::J, {{{0, 0}, {0, 1}, {1, 1}, {2, 1}}}},
    {PieceId::L, {{{2, 0}, {0, 1}, {1, 1}, {2, 1}}}},
}};

// SC_CHECK plus one context line that names the piece and rotation under
// test, so a failure names the exact table entry.
void checkPiece(bool ok, PieceId id, Rotation rotation, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "  context: piece %c, rotation %d, %s\n",
                 stackfall::pieceLetter(id), static_cast<int>(rotation),
                 what);
  }
  SC_CHECK(ok);
}

// Order-independent comparison of two four-cell sets.
bool sameCellSet(const std::array<Cell, 4>& a, const std::array<Cell, 4>& b) {
  for (const Cell& ca : a) {
    bool found = false;
    for (const Cell& cb : b) {
      if (ca == cb) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

void checkBoardConstants() {
  SC_CHECK(stackfall::kBoardWidth == 10);
  SC_CHECK(stackfall::kBoardHeight == 20);
  SC_CHECK(stackfall::kHiddenRows == 2);
  SC_CHECK(stackfall::kBoardRows == 22);
  SC_CHECK(stackfall::kFullRowMask == 0x03FF);

  // Row y lives at storage index y + 2 across the whole stored range.
  SC_CHECK(stackfall::boardRowIndex(-2) == 0);
  SC_CHECK(stackfall::boardRowIndex(-1) == 1);
  SC_CHECK(stackfall::boardRowIndex(0) == 2);
  SC_CHECK(stackfall::boardRowIndex(19) == 21);

  SC_CHECK(stackfall::inBoardX(0));
  SC_CHECK(stackfall::inBoardX(9));
  SC_CHECK(!stackfall::inBoardX(-1));
  SC_CHECK(!stackfall::inBoardX(10));

  SC_CHECK(stackfall::inBoardY(-2));
  SC_CHECK(stackfall::inBoardY(19));
  SC_CHECK(!stackfall::inBoardY(-3));
  SC_CHECK(!stackfall::inBoardY(20));

  SC_CHECK(stackfall::inBoard(Cell{0, -2}));
  SC_CHECK(stackfall::inBoard(Cell{9, 19}));
  SC_CHECK(!stackfall::inBoard(Cell{-1, 5}));
  SC_CHECK(!stackfall::inBoard(Cell{5, -3}));
  SC_CHECK(!stackfall::inBoard(Cell{10, 5}));
  SC_CHECK(!stackfall::inBoard(Cell{5, 20}));

  SC_CHECK((Cell{2, 3} == Cell{2, 3}));
  SC_CHECK((Cell{2, 3} != Cell{3, 2}));
}

void checkPieceIdCodes() {
  // The ids are the cellType block codes 1..7; code 0 stays "empty".
  SC_CHECK(static_cast<int>(PieceId::I) == 1);
  SC_CHECK(static_cast<int>(PieceId::O) == 2);
  SC_CHECK(static_cast<int>(PieceId::T) == 3);
  SC_CHECK(static_cast<int>(PieceId::S) == 4);
  SC_CHECK(static_cast<int>(PieceId::Z) == 5);
  SC_CHECK(static_cast<int>(PieceId::J) == 6);
  SC_CHECK(static_cast<int>(PieceId::L) == 7);

  // The id list covers every code exactly once, in specification order.
  bool seen[8] = {false, false, false, false, false, false, false, false};
  for (PieceId id : stackfall::kAllPieceIds) {
    const int code = static_cast<int>(id);
    SC_CHECK(code >= 1 && code <= 7);
    SC_CHECK(!seen[code]);
    seen[code] = true;
  }
  for (int code = 1; code <= 7; ++code) {
    SC_CHECK(seen[code]);
  }

  SC_CHECK(stackfall::pieceLetter(PieceId::I) == 'I');
  SC_CHECK(stackfall::pieceLetter(PieceId::O) == 'O');
  SC_CHECK(stackfall::pieceLetter(PieceId::T) == 'T');
  SC_CHECK(stackfall::pieceLetter(PieceId::S) == 'S');
  SC_CHECK(stackfall::pieceLetter(PieceId::Z) == 'Z');
  SC_CHECK(stackfall::pieceLetter(PieceId::J) == 'J');
  SC_CHECK(stackfall::pieceLetter(PieceId::L) == 'L');
}

void checkRotationStates() {
  // The numeric order is the SRS state order 0, R, 2, L.
  SC_CHECK(static_cast<int>(Rotation::Spawn) == 0);
  SC_CHECK(static_cast<int>(Rotation::Clockwise) == 1);
  SC_CHECK(static_cast<int>(Rotation::HalfTurn) == 2);
  SC_CHECK(static_cast<int>(Rotation::CounterClockwise) == 3);

  bool seen[4] = {false, false, false, false};
  for (Rotation rotation : stackfall::kAllRotations) {
    const int state = static_cast<int>(rotation);
    SC_CHECK(state >= 0 && state < 4);
    SC_CHECK(!seen[state]);
    seen[state] = true;
  }
  for (int state = 0; state < 4; ++state) {
    SC_CHECK(seen[state]);
  }

  // Quarter turns step through the states and wrap around.
  SC_CHECK(stackfall::rotateCw(Rotation::Spawn) == Rotation::Clockwise);
  SC_CHECK(stackfall::rotateCw(Rotation::Clockwise) == Rotation::HalfTurn);
  SC_CHECK(stackfall::rotateCw(Rotation::HalfTurn) == Rotation::CounterClockwise);
  SC_CHECK(stackfall::rotateCw(Rotation::CounterClockwise) == Rotation::Spawn);
  SC_CHECK(stackfall::rotateCcw(Rotation::Spawn) == Rotation::CounterClockwise);
  SC_CHECK(stackfall::rotateCcw(Rotation::CounterClockwise) == Rotation::HalfTurn);
  SC_CHECK(stackfall::rotateCcw(Rotation::HalfTurn) == Rotation::Clockwise);
  SC_CHECK(stackfall::rotateCcw(Rotation::Clockwise) == Rotation::Spawn);
}

void checkMatrixSizes() {
  SC_CHECK(stackfall::pieceMatrixSize(PieceId::I) == 4);
  for (PieceId id : stackfall::kAllPieceIds) {
    if (id == PieceId::I) {
      continue;
    }
    // JLSTZ occupy an embedded 3x3 rotation system; O shares that size and
    // simply never rotates (specification section 4.2).
    SC_CHECK(stackfall::pieceMatrixSize(id) == 3);
  }
}

// Acceptance check, specification section 11, item 1: each of the seven
// pieces has exactly four occupied cells in each of its four orientations.
// The count comes from a marking grid, so a duplicated or out-of-matrix
// cell cannot hide behind the fixed array length.
void checkFourCellsPerOrientation() {
  for (PieceId id : stackfall::kAllPieceIds) {
    const int size = stackfall::pieceMatrixSize(id);
    for (Rotation rotation : stackfall::kAllRotations) {
      const std::array<Cell, 4> cells = stackfall::pieceCells(id, rotation);

      bool marked[4][4] = {{false, false, false, false},
                           {false, false, false, false},
                           {false, false, false, false},
                           {false, false, false, false}};
      bool insideMatrix = true;
      bool allDistinct = true;
      for (const Cell& cell : cells) {
        if (cell.x < 0 || cell.x >= size || cell.y < 0 || cell.y >= size) {
          insideMatrix = false;
          continue;
        }
        if (marked[cell.y][cell.x]) {
          allDistinct = false;
        }
        marked[cell.y][cell.x] = true;
      }

      int occupied = 0;
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          if (marked[y][x]) {
            ++occupied;
          }
        }
      }

      checkPiece(insideMatrix, id, rotation,
                 "cell outside the rotation matrix");
      checkPiece(allDistinct, id, rotation, "duplicated cell");
      checkPiece(occupied == 4, id, rotation,
                 "orientation does not occupy exactly four cells");
    }
  }
}

void checkSpawnDrawings() {
  for (const SpawnDrawing& drawing : kSpawnDrawings) {
    const std::array<Cell, 4> actual =
        stackfall::pieceCells(drawing.id, Rotation::Spawn);
    checkPiece(sameCellSet(drawing.cells, actual), drawing.id,
               Rotation::Spawn,
               "spawn state differs from the canonical drawing");
  }
}

// The R, 2, and L tables must equal the spawn drawing turned by quarter
// rotations inside the square matrix. The test derives the expected cells
// with the rotation formula instead of comparing against stored data, so
// the check is independent of the implementation tables. O is exempt: it
// keeps its spawn cells in every orientation (specification section 11,
// item 2).
void checkOrientationsFollowQuarterTurns() {
  for (PieceId id : stackfall::kAllPieceIds) {
    const std::array<Cell, 4> spawn =
        stackfall::pieceCells(id, Rotation::Spawn);

    if (id == PieceId::O) {
      // O never rotates to a different footprint: every state keeps the
      // spawn cells, which is why it also never kicks.
      for (int step = 1; step <= 3; ++step) {
        const Rotation state = static_cast<Rotation>(step);
        checkPiece(sameCellSet(spawn, stackfall::pieceCells(id, state)), id,
                   state, "O changed its cells through rotation");
      }
      continue;
    }

    const int size = stackfall::pieceMatrixSize(id);
    std::array<Cell, 4> turned = spawn;
    for (int step = 1; step <= 3; ++step) {
      // One clockwise quarter turn inside a size x size matrix:
      // (x, y) -> (size - 1 - y, x).
      for (Cell& cell : turned) {
        cell = Cell{size - 1 - cell.y, cell.x};
      }
      const Rotation state = static_cast<Rotation>(step);
      checkPiece(sameCellSet(turned, stackfall::pieceCells(id, state)), id,
                 state, "state differs from the turned spawn drawing");
    }
  }
}

// Spawn geometry, specification section 4.2: matrix origin x = 3 for every
// piece; the I starts at matrix y = -1 and the others at y = 0; the topmost
// occupied cell of every piece sits at visible row y = 0, the
// immediate-spawn rule of task T029, so no piece waits in the hidden rows;
// the I spans columns 3..6 and the O spans columns 4..5.
void checkSpawnGeometry() {
  for (PieceId id : stackfall::kAllPieceIds) {
    SC_CHECK(stackfall::spawnX(id) == 3);
    SC_CHECK(stackfall::spawnY(id) == (id == PieceId::I ? -1 : 0));

    const stackfall::Piece piece = stackfall::makeSpawnPiece(id);
    SC_CHECK(piece.id == id);
    SC_CHECK(piece.rotation == Rotation::Spawn);
    SC_CHECK(piece.x == 3);
    SC_CHECK(piece.y == stackfall::spawnY(id));

    const std::array<Cell, 4> cells = stackfall::boardCells(piece);
    int minX = cells[0].x;
    int maxX = cells[0].x;
    int minY = cells[0].y;
    int maxY = cells[0].y;
    bool allOnBoard = true;
    bool boardCellsMatchOffsets = true;
    const std::array<Cell, 4>& offsets =
        stackfall::pieceCells(id, Rotation::Spawn);
    for (std::size_t i = 0; i < cells.size(); ++i) {
      minX = minX < cells[i].x ? minX : cells[i].x;
      maxX = maxX > cells[i].x ? maxX : cells[i].x;
      minY = minY < cells[i].y ? minY : cells[i].y;
      maxY = maxY > cells[i].y ? maxY : cells[i].y;
      allOnBoard = allOnBoard && stackfall::inBoard(cells[i]);
      boardCellsMatchOffsets = boardCellsMatchOffsets &&
                               (cells[i] == Cell{piece.x + offsets[i].x,
                                                 piece.y + offsets[i].y});
    }

    SC_CHECK(allOnBoard);
    SC_CHECK(boardCellsMatchOffsets);
    // The top of every piece is the first visible row, so the renderer has
    // cells to draw on the spawn tick itself (task T029).
    SC_CHECK(minY == 0);

    if (id == PieceId::I) {
      // The four I cells fill row y = 0 across columns 3..6 and the piece
      // holds no other row, so it is fully on screen at once.
      SC_CHECK(minX == 3);
      SC_CHECK(maxX == 6);
      SC_CHECK(maxY == 0);
    } else if (id == PieceId::O) {
      // The O is a 2x2 block over columns 4..5 and rows 0..1.
      SC_CHECK(minX == 4);
      SC_CHECK(maxX == 5);
      SC_CHECK(maxY == 1);
    } else {
      // Every JLSTZ piece spans columns 3..5 and rows 0..1 at spawn.
      SC_CHECK(minX == 3);
      SC_CHECK(maxX == 5);
      SC_CHECK(maxY == 1);
    }
  }
}

// ---------------------------------------------------------------------------
// SRS rotation and wall kicks (task T004). Reference:
// https://www.tetris.wiki/SRS, reviewed November 2026.
// ---------------------------------------------------------------------------

// A kick candidate exactly as printed in the reference tables: x grows to
// the right and y grows UPWARD. The rules core negates y, so the comparisons
// below read Kick{reference.dx, -reference.dy}; that comparison is the only
// place where the two conventions meet (specification section 4.4).
struct ReferenceKick {
  int dx;
  int dy;
};

// One row of a reference kick table: the transition and its five tests.
// Clockwise turns are the 0->R, R->2, 2->L, and L->0 rows; counter-clockwise
// turns are 0->L, L->2, 2->R, and R->0.
struct ReferenceKickRow {
  Rotation from;
  bool clockwise;
  std::array<ReferenceKick, 5> tests;
};

// Builders that keep the reference literals below free of std::array brace
// nesting.
constexpr std::array<ReferenceKick, 5> refKicks(ReferenceKick a,
                                                ReferenceKick b,
                                                ReferenceKick c,
                                                ReferenceKick d,
                                                ReferenceKick e) {
  return std::array<ReferenceKick, 5>{{a, b, c, d, e}};
}

const std::array<ReferenceKickRow, 8> kJlstzReferenceKicks = {{
    // 0->R
    {Rotation::Spawn, true,
     refKicks({0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2})},
    // R->0
    {Rotation::Clockwise, false,
     refKicks({0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2})},
    // R->2
    {Rotation::Clockwise, true,
     refKicks({0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2})},
    // 2->R
    {Rotation::HalfTurn, false,
     refKicks({0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2})},
    // 2->L
    {Rotation::HalfTurn, true,
     refKicks({0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2})},
    // L->2
    {Rotation::CounterClockwise, false,
     refKicks({0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2})},
    // L->0
    {Rotation::CounterClockwise, true,
     refKicks({0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2})},
    // 0->L
    {Rotation::Spawn, false,
     refKicks({0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2})},
}};

const std::array<ReferenceKickRow, 8> kIReferenceKicks = {{
    // 0->R
    {Rotation::Spawn, true,
     refKicks({0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2})},
    // R->0
    {Rotation::Clockwise, false,
     refKicks({0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2})},
    // R->2
    {Rotation::Clockwise, true,
     refKicks({0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1})},
    // 2->R
    {Rotation::HalfTurn, false,
     refKicks({0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1})},
    // 2->L
    {Rotation::HalfTurn, true,
     refKicks({0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2})},
    // L->2
    {Rotation::CounterClockwise, false,
     refKicks({0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2})},
    // L->0
    {Rotation::CounterClockwise, true,
     refKicks({0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1})},
    // 0->L
    {Rotation::Spawn, false,
     refKicks({0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1})},
}};

// The 28 SRS orientation states as matrix-local cell sets, transcribed from
// the rotation series of the reference page. These literals are independent
// of the quarter-turn formula used by the T003 checks, so a systematically
// mis-turned table cannot pass both.
struct OrientationReference {
  PieceId id;
  std::array<std::array<Cell, 4>, 4> states;  // 0, R, 2, L
};

// Builds one orientation state from its four cells.
constexpr std::array<Cell, 4> cells4(Cell a, Cell b, Cell c, Cell d) {
  return std::array<Cell, 4>{{a, b, c, d}};
}

// Builds the four states of one piece.
constexpr std::array<std::array<Cell, 4>, 4> orientations(
    const std::array<Cell, 4>& s0, const std::array<Cell, 4>& s1,
    const std::array<Cell, 4>& s2, const std::array<Cell, 4>& s3) {
  return std::array<std::array<Cell, 4>, 4>{{s0, s1, s2, s3}};
}

const std::array<OrientationReference, 7> kSrsOrientationReference = {{
    {PieceId::I,
     orientations(cells4({0, 1}, {1, 1}, {2, 1}, {3, 1}),
                  cells4({2, 0}, {2, 1}, {2, 2}, {2, 3}),
                  cells4({0, 2}, {1, 2}, {2, 2}, {3, 2}),
                  cells4({1, 0}, {1, 1}, {1, 2}, {1, 3}))},
    {PieceId::O,
     orientations(cells4({1, 0}, {2, 0}, {1, 1}, {2, 1}),
                  cells4({1, 0}, {2, 0}, {1, 1}, {2, 1}),
                  cells4({1, 0}, {2, 0}, {1, 1}, {2, 1}),
                  cells4({1, 0}, {2, 0}, {1, 1}, {2, 1}))},
    {PieceId::T,
     orientations(cells4({1, 0}, {0, 1}, {1, 1}, {2, 1}),
                  cells4({1, 0}, {1, 1}, {1, 2}, {2, 1}),
                  cells4({0, 1}, {1, 1}, {2, 1}, {1, 2}),
                  cells4({1, 0}, {0, 1}, {1, 1}, {1, 2}))},
    {PieceId::S,
     orientations(cells4({1, 0}, {2, 0}, {0, 1}, {1, 1}),
                  cells4({1, 0}, {1, 1}, {2, 1}, {2, 2}),
                  cells4({1, 1}, {2, 1}, {0, 2}, {1, 2}),
                  cells4({0, 0}, {0, 1}, {1, 1}, {1, 2}))},
    {PieceId::Z,
     orientations(cells4({0, 0}, {1, 0}, {1, 1}, {2, 1}),
                  cells4({2, 0}, {1, 1}, {2, 1}, {1, 2}),
                  cells4({0, 1}, {1, 1}, {1, 2}, {2, 2}),
                  cells4({1, 0}, {0, 1}, {1, 1}, {0, 2}))},
    {PieceId::J,
     orientations(cells4({0, 0}, {0, 1}, {1, 1}, {2, 1}),
                  cells4({1, 0}, {2, 0}, {1, 1}, {1, 2}),
                  cells4({0, 1}, {1, 1}, {2, 1}, {2, 2}),
                  cells4({1, 0}, {1, 1}, {1, 2}, {0, 2}))},
    {PieceId::L,
     orientations(cells4({2, 0}, {0, 1}, {1, 1}, {2, 1}),
                  cells4({1, 0}, {1, 1}, {1, 2}, {2, 2}),
                  cells4({0, 1}, {1, 1}, {2, 1}, {0, 2}),
                  cells4({0, 0}, {1, 0}, {1, 1}, {1, 2}))},
}};

// The section 4.2 validity rule restricted to the board bounds: every
// occupied cell must sit in one of the ten columns and in y = -2..19.
bool fitsOnEmptyBoard(const stackfall::Piece& piece) {
  for (const Cell& cell : stackfall::boardCells(piece)) {
    if (!stackfall::inBoard(cell)) {
      return false;
    }
  }
  return true;
}

// A minimal stand-in for the T005 board: a settled-cell set plus the
// validity rule of section 4.2. The bounds check comes first, so a row
// outside y = -2..19 is refused before the settled cells are ever consulted
// — the storage-access discipline of specification section 4.1. The counters
// let the tests observe that discipline.
class MockStack {
 public:
  void block(int x, int y) { settled_[y + stackfall::kHiddenRows][x] = true; }

  bool fits(const stackfall::Piece& piece) const {
    ++queries_;
    for (const Cell& cell : stackfall::boardCells(piece)) {
      if (!stackfall::inBoard(cell)) {
        ++rejectedByBounds_;
        return false;  // row storage is never reached for out-of-board cells
      }
      if (settled_[cell.y + stackfall::kHiddenRows][cell.x]) {
        return false;
      }
    }
    return true;
  }

  int queries() const { return queries_; }
  int rejectedByBounds() const { return rejectedByBounds_; }

 private:
  bool settled_[stackfall::kBoardRows][stackfall::kBoardWidth] = {};
  mutable int queries_ = 0;
  mutable int rejectedByBounds_ = 0;
};

// Section 11, item 3, orientation half: I horizontal/vertical states and all
// JLSTZ states match the SRS reference rotation series.
void checkSrsReferenceOrientations() {
  for (const OrientationReference& reference : kSrsOrientationReference) {
    for (int state = 0; state < 4; ++state) {
      const Rotation rotation = static_cast<Rotation>(state);
      checkPiece(sameCellSet(reference.states[state],
                             stackfall::pieceCells(reference.id, rotation)),
                 reference.id, rotation,
                 "orientation differs from the SRS reference state");
    }
  }
}

// Section 11, item 3, kick half, plus the encoding rule of section 4.4: the
// stored tables equal the reference rows with the y component negated, in
// the same trial order. Also pins the candidate counts and the row-sharing
// symmetry of the reference.
void checkKickTablesMatchReference() {
  const std::array<PieceId, 5> jlstz = {
      {PieceId::J, PieceId::L, PieceId::S, PieceId::T, PieceId::Z}};
  for (PieceId id : jlstz) {
    for (const ReferenceKickRow& row : kJlstzReferenceKicks) {
      const std::array<stackfall::Kick, 5>& table =
          stackfall::wallKicks(id, row.from, row.clockwise);
      for (std::size_t i = 0; i < table.size(); ++i) {
        const stackfall::Kick expected{row.tests[i].dx, -row.tests[i].dy};
        checkPiece(table[i] == expected, id, row.from,
                   "kick candidate differs from the negated reference value");
      }
      checkPiece(table[0] == (stackfall::Kick{0, 0}), id, row.from,
                 "first candidate is not the in-place test");
    }
  }
  for (const ReferenceKickRow& row : kIReferenceKicks) {
    const std::array<stackfall::Kick, 5>& table =
        stackfall::wallKicks(PieceId::I, row.from, row.clockwise);
    for (std::size_t i = 0; i < table.size(); ++i) {
      const stackfall::Kick expected{row.tests[i].dx, -row.tests[i].dy};
      checkPiece(table[i] == expected, PieceId::I, row.from,
                 "kick candidate differs from the negated reference value");
    }
    checkPiece(table[0] == (stackfall::Kick{0, 0}), PieceId::I, row.from,
               "first candidate is not the in-place test");
  }

  // Five candidates for I and every JLSTZ piece; O tries the in-place test
  // only, which is why it can never kick.
  SC_CHECK(stackfall::kickCandidateCount(PieceId::I) ==
           stackfall::kKickCandidates);
  for (PieceId id : jlstz) {
    SC_CHECK(stackfall::kickCandidateCount(id) == stackfall::kKickCandidates);
  }
  SC_CHECK(stackfall::kickCandidateCount(PieceId::O) == 1);
  SC_CHECK(stackfall::wallKicks(PieceId::O, Rotation::Spawn, true)[0] ==
           (stackfall::Kick{0, 0}));
  SC_CHECK(stackfall::wallKicks(PieceId::O, Rotation::HalfTurn, false)[0] ==
           (stackfall::Kick{0, 0}));

  // Transitions that share a reference row must share an encoded row.
  auto rowEquals = [](PieceId id, Rotation aFrom, bool aCw, Rotation bFrom,
                      bool bCw) {
    const std::array<stackfall::Kick, 5>& a =
        stackfall::wallKicks(id, aFrom, aCw);
    const std::array<stackfall::Kick, 5>& b =
        stackfall::wallKicks(id, bFrom, bCw);
    for (std::size_t i = 0; i < a.size(); ++i) {
      if (a[i] != b[i]) {
        return false;
      }
    }
    return true;
  };
  for (PieceId id : jlstz) {
    // 0->R == 2->R, R->0 == R->2, 2->L == 0->L, L->2 == L->0.
    SC_CHECK(rowEquals(id, Rotation::Spawn, true, Rotation::HalfTurn, false));
    SC_CHECK(rowEquals(id, Rotation::Clockwise, true, Rotation::Clockwise,
                       false));
    SC_CHECK(rowEquals(id, Rotation::HalfTurn, true, Rotation::Spawn, false));
    SC_CHECK(rowEquals(id, Rotation::CounterClockwise, true,
                       Rotation::CounterClockwise, false));
  }
  // 0->R == L->2, R->0 == 2->L, R->2 == 0->L, 2->R == L->0.
  SC_CHECK(rowEquals(PieceId::I, Rotation::Spawn, true,
                     Rotation::CounterClockwise, false));
  SC_CHECK(rowEquals(PieceId::I, Rotation::Clockwise, false,
                     Rotation::HalfTurn, true));
  SC_CHECK(rowEquals(PieceId::I, Rotation::Clockwise, true, Rotation::Spawn,
                     false));
  SC_CHECK(rowEquals(PieceId::I, Rotation::HalfTurn, false,
                     Rotation::CounterClockwise, true));
}

// Section 11, item 2: O never changes its cells or its position through
// rotation and never kicks, wherever it stands.
void checkOStaysStationary() {
  // Origins that press the O against the left wall, the right wall, the
  // floor, and the hidden rows, plus two mid-field states.
  const std::array<stackfall::Piece, 6> starts = {{
      stackfall::makeSpawnPiece(PieceId::O),
      {PieceId::O, Rotation::Spawn, -1, -2},
      {PieceId::O, Rotation::Spawn, 7, 0},
      {PieceId::O, Rotation::Spawn, 3, 18},
      {PieceId::O, Rotation::HalfTurn, 0, 5},
      {PieceId::O, Rotation::CounterClockwise, 4, 9},
  }};
  for (const stackfall::Piece& start : starts) {
    SC_CHECK(fitsOnEmptyBoard(start));
    for (bool clockwise : {true, false}) {
      const Rotation to = clockwise ? stackfall::rotateCw(start.rotation)
                                    : stackfall::rotateCcw(start.rotation);
      const stackfall::RotationResult result =
          stackfall::rotatePiece(start, clockwise, fitsOnEmptyBoard);
      checkPiece(result.rotated, PieceId::O, start.rotation,
                 "O rotation rejected although the piece is on the board");
      checkPiece(result.kick == (stackfall::Kick{0, 0}), PieceId::O,
                 start.rotation, "O applied a kick");
      checkPiece(result.piece.rotation == to, PieceId::O, start.rotation,
                 "O rotation state did not advance by one turn");
      checkPiece(result.piece.x == start.x && result.piece.y == start.y,
                 PieceId::O, start.rotation, "O moved through rotation");
      checkPiece(sameCellSet(stackfall::boardCells(start),
                             stackfall::boardCells(result.piece)),
                 PieceId::O, start.rotation,
                 "O changed its cells through rotation");
    }
  }

  // Exactly one candidate is tried: the in-place test.
  int offered = 0;
  stackfall::rotatePiece(
      starts[0], true, [&offered](const stackfall::Piece&) {
        ++offered;
        return false;
      });
  SC_CHECK(offered == 1);

  // Under a completely full board even the in-place test fails, and the
  // piece stays exactly as it was.
  MockStack full;
  for (int y = -2; y <= 19; ++y) {
    for (int x = 0; x <= 9; ++x) {
      full.block(x, y);
    }
  }
  const stackfall::Piece spawn = stackfall::makeSpawnPiece(PieceId::O);
  for (bool clockwise : {true, false}) {
    const stackfall::RotationResult result = stackfall::rotatePiece(
        spawn, clockwise,
        [&full](const stackfall::Piece& candidate) { return full.fits(candidate); });
    SC_CHECK(!result.rotated);
    SC_CHECK(result.kick == (stackfall::Kick{0, 0}));
    SC_CHECK(result.piece.id == spawn.id);
    SC_CHECK(result.piece.rotation == spawn.rotation);
    SC_CHECK(result.piece.x == spawn.x);
    SC_CHECK(result.piece.y == spawn.y);
  }
}

// Section 4.2 rejection rule: a rotation without a valid candidate leaves
// the piece completely unchanged — identity, rotation state, and position.
void checkRejectedRotationLeavesPieceUnchanged() {
  MockStack full;
  for (int y = -2; y <= 19; ++y) {
    for (int x = 0; x <= 9; ++x) {
      full.block(x, y);
    }
  }
  auto fits = [&full](const stackfall::Piece& candidate) {
    return full.fits(candidate);
  };
  for (PieceId id : stackfall::kAllPieceIds) {
    for (Rotation from : stackfall::kAllRotations) {
      const std::array<stackfall::Piece, 3> starts = {{
          {id, from, stackfall::spawnX(id), stackfall::spawnY(id) + 4},
          {id, from, 0, 6},
          {id, from, 6, 12},
      }};
      for (const stackfall::Piece& start : starts) {
        SC_CHECK(fitsOnEmptyBoard(start));
        for (bool clockwise : {true, false}) {
          const stackfall::RotationResult result =
              stackfall::rotatePiece(start, clockwise, fits);
          checkPiece(!result.rotated, id, from,
                     "rotation accepted although every candidate is blocked");
          checkPiece(result.kick == (stackfall::Kick{0, 0}), id, from,
                     "rejected rotation reports a kick");
          checkPiece(result.piece.id == start.id &&
                         result.piece.rotation == from &&
                         result.piece.x == start.x &&
                         result.piece.y == start.y,
                     id, from, "rejected rotation changed the piece");
        }
      }
    }
  }
}

// Section 4.4 trial rule: the candidates are offered in table order and the
// first valid one wins.
void checkFirstValidCandidateWins() {
  const stackfall::Piece start{PieceId::T, Rotation::Spawn, 3, 5};
  const std::array<stackfall::Kick, 5>& kicks =
      stackfall::wallKicks(PieceId::T, Rotation::Spawn, true);
  const Rotation to = stackfall::rotateCw(Rotation::Spawn);

  // Record every candidate the rotation offers.
  std::array<stackfall::Piece, 8> offered = {};
  int offeredCount = 0;
  stackfall::rotatePiece(
      start, true,
      [&offered, &offeredCount](const stackfall::Piece& candidate) {
        if (offeredCount < 8) {
          offered[offeredCount] = candidate;
        }
        ++offeredCount;
        return false;
      });
  SC_CHECK(offeredCount ==
           static_cast<int>(stackfall::kickCandidateCount(PieceId::T)));
  for (std::size_t i = 0; i < stackfall::kickCandidateCount(PieceId::T); ++i) {
    const stackfall::Piece& candidate = offered[i];
    SC_CHECK(candidate.id == PieceId::T);
    SC_CHECK(candidate.rotation == to);
    SC_CHECK(candidate.x == start.x + kicks[i].dx);
    SC_CHECK(candidate.y == start.y + kicks[i].dy);
  }

  // Accept only the fourth candidate, grid (0, +2), printed as (0, -2) in
  // the reference. The rotation must land exactly there.
  auto acceptsOnlyFourth = [&start](const stackfall::Piece& candidate) {
    return candidate.x == start.x && candidate.y == start.y + 2;
  };
  const stackfall::RotationResult result =
      stackfall::rotatePiece(start, true, acceptsOnlyFourth);
  SC_CHECK(result.rotated);
  SC_CHECK(result.kick == (stackfall::Kick{0, 2}));
  SC_CHECK(result.piece.id == PieceId::T);
  SC_CHECK(result.piece.rotation == to);
  SC_CHECK(result.piece.x == start.x);
  SC_CHECK(result.piece.y == start.y + 2);
}

// Section 11, item 5, pinned cases: rotations against the left wall, the
// right wall, and the floor apply exactly the kick the reference prescribes.
// Each start is a legal in-play piece pressed against an edge of the field.
struct WallCase {
  PieceId id;
  Rotation from;
  bool clockwise;
  int x;
  int y;
  stackfall::Kick expectedKick;
  const char* what;
};

const std::array<WallCase, 6> kWallCases = {{
    // I vertical on the right wall, R->2: one column left (ref (-1, 0)).
    {PieceId::I, Rotation::Clockwise, true, 7, 0, {-1, 0}, "I right wall"},
    // I vertical on the left wall, R->0: two columns right (ref (+2, 0)).
    {PieceId::I, Rotation::Clockwise, false, -2, 0, {2, 0}, "I left wall"},
    // J vertical on the left wall, R->0: one column right (ref (+1, 0)).
    {PieceId::J, Rotation::Clockwise, false, -1, 0, {1, 0}, "J left wall"},
    // L on the right wall, L->0: one column left (ref (-1, 0)).
    {PieceId::L, Rotation::CounterClockwise, true, 8, 0, {-1, 0}, "L right wall"},
    // T on the floor, 0->R: one column left, one row up (ref (-1, +1)).
    {PieceId::T, Rotation::Spawn, true, 3, 18, {-1, -1}, "T floor kick"},
    // I on the floor, 0->R: one column right, two rows up (ref (+1, +2)).
    {PieceId::I, Rotation::Spawn, true, 3, 18, {1, -2}, "I floor kick"},
}};

void checkPinnedWallAndFloorKicks() {
  for (const WallCase& one : kWallCases) {
    const stackfall::Piece start{one.id, one.from, one.x, one.y};
    if (!fitsOnEmptyBoard(start)) {
      std::fprintf(stderr, "  context: bad case %s\n", one.what);
    }
    SC_CHECK(fitsOnEmptyBoard(start));

    const stackfall::RotationResult result =
        stackfall::rotatePiece(start, one.clockwise, fitsOnEmptyBoard);
    checkPiece(result.rotated, one.id, one.from, one.what);
    checkPiece(result.kick == one.expectedKick, one.id, one.from, one.what);
    const Rotation to = one.clockwise ? stackfall::rotateCw(one.from)
                                      : stackfall::rotateCcw(one.from);
    checkPiece(result.piece.rotation == to, one.id, one.from, one.what);
    checkPiece(result.piece.x == one.x + one.expectedKick.dx, one.id, one.from,
               one.what);
    checkPiece(result.piece.y == one.y + one.expectedKick.dy, one.id, one.from,
               one.what);
    checkPiece(fitsOnEmptyBoard(result.piece), one.id, one.from, one.what);
  }
}

// Section 11, item 5, exhaustive form: for every legal piece state on an
// empty board, both quarter turns either apply exactly the first fitting
// candidate of the table or leave the piece untouched. The expected index
// is recomputed here independently of rotatePiece.
void checkRotationSweepOnEmptyBoard() {
  for (PieceId id : stackfall::kAllPieceIds) {
    for (Rotation from : stackfall::kAllRotations) {
      for (int clockwiseInt = 0; clockwiseInt < 2; ++clockwiseInt) {
        const bool clockwise = clockwiseInt != 0;
        const std::array<stackfall::Kick, 5>& kicks =
            stackfall::wallKicks(id, from, clockwise);
        const int count =
            static_cast<int>(stackfall::kickCandidateCount(id));
        const Rotation to = clockwise ? stackfall::rotateCw(from)
                                      : stackfall::rotateCcw(from);
        for (int x = -3; x <= 10; ++x) {
          for (int y = -2; y <= 19; ++y) {
            const stackfall::Piece start{id, from, x, y};
            if (!fitsOnEmptyBoard(start)) {
              continue;
            }

            int expected = -1;
            for (int i = 0; i < count; ++i) {
              const stackfall::Piece candidate{
                  id, to, x + kicks[i].dx, y + kicks[i].dy};
              if (fitsOnEmptyBoard(candidate)) {
                expected = i;
                break;
              }
            }

            const stackfall::RotationResult result =
                stackfall::rotatePiece(start, clockwise, fitsOnEmptyBoard);
            if (expected >= 0) {
              checkPiece(result.rotated, id, from,
                         "rotation rejected although a candidate fits");
              checkPiece(result.kick == kicks[expected], id, from,
                         "first fitting candidate not taken");
              checkPiece(result.piece.rotation == to, id, from,
                         "wrong rotation state after success");
              checkPiece(result.piece.x == x + kicks[expected].dx &&
                             result.piece.y == y + kicks[expected].dy,
                         id, from, "kick offset not applied to the origin");
              checkPiece(fitsOnEmptyBoard(result.piece), id, from,
                         "accepted candidate is off the board");
            } else {
              checkPiece(!result.rotated, id, from,
                         "rotation accepted without a fitting candidate");
              checkPiece(result.kick == (stackfall::Kick{0, 0}), id, from,
                         "rejected rotation reports a kick");
              checkPiece(result.piece.rotation == from &&
                             result.piece.x == x && result.piece.y == y,
                         id, from, "rejected rotation moved the piece");
            }
          }
        }
      }
    }
  }
}

// Section 11, item 6: rotations above the hidden-row boundary stay inside
// y = -2..19. MockStack checks the bounds before it consults its settled
// rows, mirroring the storage rule of section 4.1, so candidates that would
// leave the stored rows are refused before any row access.
void checkRotationAtTopBoundary() {
  // Every piece rotates in place when it spawns at visible row 0 (the I
  // matrix reaches the hidden rows, but its occupied row is row 0 too).
  for (PieceId id : stackfall::kAllPieceIds) {
    const stackfall::Piece start = stackfall::makeSpawnPiece(id);
    SC_CHECK(fitsOnEmptyBoard(start));
    for (bool clockwise : {true, false}) {
      const stackfall::RotationResult result =
          stackfall::rotatePiece(start, clockwise, fitsOnEmptyBoard);
      checkPiece(result.rotated, id, start.rotation,
                 "spawn rotation rejected at the top of the field");
      checkPiece(result.kick == (stackfall::Kick{0, 0}), id, start.rotation,
                 "spawn rotation kicked");
      checkPiece(fitsOnEmptyBoard(result.piece), id, start.rotation,
                 "spawn rotation left the stored rows");
    }
  }

  // A settled cell on the nub of the basic rotation forces the second
  // candidate: the piece kicks one column left inside the hidden rows and
  // stays within y >= -2. Only (5, -1) is blocked, a cell that belongs to
  // the first candidate alone.
  MockStack side;
  side.block(5, -1);
  const stackfall::Piece topT{PieceId::T, Rotation::Spawn, 3, -2};
  const stackfall::RotationResult kicked = stackfall::rotatePiece(
      topT, true, [&side](const stackfall::Piece& candidate) {
        return side.fits(candidate);
      });
  checkPiece(kicked.rotated, PieceId::T, Rotation::Spawn,
             "top rotation rejected although a kick fits");
  checkPiece(kicked.kick == (stackfall::Kick{-1, 0}), PieceId::T,
             Rotation::Spawn, "unexpected top kick");
  checkPiece(fitsOnEmptyBoard(kicked.piece), PieceId::T, Rotation::Spawn,
             "top kick left the stored rows");

  // An upward kick near the top: the third candidate of 0->R is one column
  // left and one row up (reference (-1, +1)), which reaches row -2 but
  // never above it. One cell unique to candidate 1 and one unique to
  // candidate 2 force that kick.
  MockStack shoulder;
  shoulder.block(5, 0);  // candidate 1 only
  shoulder.block(3, 1);  // candidate 2 only
  const stackfall::Piece highT{PieceId::T, Rotation::Spawn, 3, -1};
  const stackfall::RotationResult up = stackfall::rotatePiece(
      highT, true, [&shoulder](const stackfall::Piece& candidate) {
        return shoulder.fits(candidate);
      });
  checkPiece(up.rotated, PieceId::T, Rotation::Spawn,
             "upward kick near the top rejected");
  checkPiece(up.kick == (stackfall::Kick{-1, -1}), PieceId::T,
             Rotation::Spawn, "expected the one-left one-up kick");
  checkPiece(fitsOnEmptyBoard(up.piece), PieceId::T, Rotation::Spawn,
             "upward kick left the stored rows");

  // Every candidate blocked, one of them above row -2: the out-of-board
  // candidate is refused by the bounds check alone — before MockStack ever
  // touches its settled rows — and the rotation fails without moving. One
  // cell unique to each in-board candidate is enough.
  MockStack crowding;
  crowding.block(5, -1);  // candidate 1 only
  crowding.block(3, -2);  // candidate 2 only
  crowding.block(5, 1);   // candidate 4 only
  crowding.block(3, 2);   // candidate 5 only
  const stackfall::RotationResult blocked = stackfall::rotatePiece(
      topT, true, [&crowding](const stackfall::Piece& candidate) {
        return crowding.fits(candidate);
      });
  checkPiece(!blocked.rotated, PieceId::T, Rotation::Spawn,
             "rotation accepted although every candidate is blocked");
  checkPiece(blocked.kick == (stackfall::Kick{0, 0}), PieceId::T,
             Rotation::Spawn, "blocked rotation reports a kick");
  checkPiece(blocked.piece.rotation == Rotation::Spawn && blocked.piece.x == 3 &&
                 blocked.piece.y == -2,
             PieceId::T, Rotation::Spawn, "blocked rotation moved the piece");
  // Five candidates were offered; exactly the one reaching row -3 was
  // refused by the bounds check, so no out-of-range row was ever consulted.
  SC_CHECK(crowding.queries() ==
           static_cast<int>(stackfall::kickCandidateCount(PieceId::T)));
  SC_CHECK(crowding.rejectedByBounds() == 1);
}

// Four clockwise turns, and a clockwise/counter-clockwise pair, return every
// piece to its exact spawn state on an empty board.
void checkRotationCycles() {
  for (PieceId id : stackfall::kAllPieceIds) {
    stackfall::Piece piece = stackfall::makeSpawnPiece(id);
    for (int step = 0; step < 4; ++step) {
      const stackfall::RotationResult result =
          stackfall::rotatePiece(piece, true, fitsOnEmptyBoard);
      SC_CHECK(result.rotated);
      piece = result.piece;
    }
    SC_CHECK(piece.rotation == Rotation::Spawn);
    SC_CHECK(piece.x == stackfall::spawnX(id));
    SC_CHECK(piece.y == stackfall::spawnY(id));

    const stackfall::RotationResult there =
        stackfall::rotatePiece(piece, true, fitsOnEmptyBoard);
    const stackfall::RotationResult back =
        stackfall::rotatePiece(there.piece, false, fitsOnEmptyBoard);
    SC_CHECK(back.piece.id == piece.id && back.piece.rotation == piece.rotation &&
             back.piece.x == piece.x && back.piece.y == piece.y);
  }
}

}  // namespace

void run_pieces_tests() {
  checkBoardConstants();
  checkPieceIdCodes();
  checkRotationStates();
  checkMatrixSizes();
  checkFourCellsPerOrientation();
  checkSpawnDrawings();
  checkOrientationsFollowQuarterTurns();
  checkSpawnGeometry();
  checkSrsReferenceOrientations();
  checkKickTablesMatchReference();
  checkOStaysStationary();
  checkRejectedRotationLeavesPieceUnchanged();
  checkFirstValidCandidateWins();
  checkPinnedWallAndFloorKicks();
  checkRotationSweepOnEmptyBoard();
  checkRotationAtTopBoundary();
  checkRotationCycles();
}
