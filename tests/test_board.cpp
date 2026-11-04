// Headless suite for the board storage, collision, lock, and line clears
// (task T005, milestone M1). The acceptance criteria are specification
// section 11, items 7, 8, and 17:
//
//   7.  Collision with settled cells blocks movement; the active piece is
//       not committed before it locks.
//   8.  One, two, three, and four full lines are cleared correctly and the
//       colors shift with the occupancy rows.
//   17. A full row has mask 0x03FF, and colors and occupancy stay consistent
//       over hundreds of randomized cases.
//
// The randomized cases run against an independent reference model that keeps
// whole color rows and rebuilds the board bottom-aligned after every clear,
// so the model shares no code and no algorithm with Board::clearFullRows.

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "stackfall/board.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/types.hpp"
#include "test_util.hpp"

namespace {

using stackfall::Board;
using stackfall::Cell;
using stackfall::Piece;
using stackfall::PieceId;
using stackfall::Rotation;

constexpr int kRows = stackfall::kBoardRows;
constexpr int kCols = stackfall::kBoardWidth;

using ColorGrid = std::array<std::array<std::uint8_t, kCols>, kRows>;

// ---------------------------------------------------------------------------
// Reference model: a plain 22 x 10 grid of piece codes, 0 for empty. Storage
// row r holds board row r - 2, exactly like the board arrays.
// ---------------------------------------------------------------------------

int modelRow(int y) { return y + stackfall::kHiddenRows; }

bool modelRowFull(const ColorGrid& model, int row) {
  for (int x = 0; x < kCols; ++x) {
    if (model[row][x] == 0) {
      return false;
    }
  }
  return true;
}

int countFullModelRows(const ColorGrid& model) {
  int full = 0;
  for (int row = 0; row < kRows; ++row) {
    if (modelRowFull(model, row)) {
      ++full;
    }
  }
  return full;
}

// The occupancy mask a model row must produce: bit x per settled column.
std::uint16_t modelRowMask(const ColorGrid& model, int row) {
  std::uint16_t mask = 0;
  for (int x = 0; x < kCols; ++x) {
    if (model[row][x] != 0) {
      mask = static_cast<std::uint16_t>(mask | (1u << x));
    }
  }
  return mask;
}

// Removes every full row by rebuilding the grid bottom-aligned from the
// surviving rows in their original order. This is deliberately unlike the
// in-place compaction of Board::clearFullRows, so the two must agree.
int modelClearRows(ColorGrid& model) {
  std::vector<std::array<std::uint8_t, kCols>> survivors;
  for (int row = 0; row < kRows; ++row) {
    if (!modelRowFull(model, row)) {
      survivors.push_back(model[row]);
    }
  }
  const int cleared = kRows - static_cast<int>(survivors.size());

  ColorGrid result{};
  int row = kRows - 1;
  for (std::size_t i = survivors.size(); i > 0; --i, --row) {
    result[row] = survivors[i - 1];
  }
  model = result;
  return cleared;
}

// Compares a board against the model: every occupancy mask and every color
// code must match, and the board must satisfy its own invariant.
bool boardMatchesModel(const Board& board, const ColorGrid& model,
                       const char* what) {
  bool ok = true;
  for (int row = 0; row < kRows; ++row) {
    if (board.occupancy()[row] != modelRowMask(model, row)) {
      std::fprintf(stderr,
                   "  context: %s, occupancy row %d is %04x, model %04x\n",
                   what, row, board.occupancy()[row],
                   modelRowMask(model, row));
      ok = false;
    }
    for (int x = 0; x < kCols; ++x) {
      if (board.cellTypes()[row][x] != model[row][x]) {
        std::fprintf(stderr,
                     "  context: %s, color cell (x=%d,row=%d) is %u, "
                     "model %u\n",
                     what, x, row, board.cellTypes()[row][x], model[row][x]);
        ok = false;
      }
    }
  }
  if (!board.isConsistent()) {
    std::fprintf(stderr, "  context: %s, board reports an inconsistency\n",
                 what);
    ok = false;
  }
  return ok;
}

// Writes one cell into the model and the board together.
bool settleTogether(Board& board, ColorGrid& model, int x, int y, PieceId id) {
  const int row = modelRow(y);
  if (x < 0 || x >= kCols || row < 0 || row >= kRows || model[row][x] != 0) {
    return false;
  }
  model[row][x] = static_cast<std::uint8_t>(id);
  return board.settleCell(x, y, id);
}

// Locks a piece into the model and the board together.
void lockTogether(Board& board, ColorGrid& model, const Piece& piece) {
  for (const Cell& cell : stackfall::boardCells(piece)) {
    model[modelRow(cell.y)][cell.x] = static_cast<std::uint8_t>(piece.id);
  }
  board.lockPiece(piece);
}

// ---------------------------------------------------------------------------
// Deterministic test generator. PCG32 arrives with task T006; the suite only
// needs reproducible integers, so it carries this small splitmix64.
// ---------------------------------------------------------------------------

class Random {
 public:
  explicit Random(std::uint64_t seed) : state_{seed} {}

  std::uint64_t next() {
    state_ += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  int below(int bound) { return static_cast<int>(next() % bound); }

 private:
  std::uint64_t state_;
};

// ---------------------------------------------------------------------------
// Storage and observers.
// ---------------------------------------------------------------------------

void checkEmptyBoard() {
  Board board;
  SC_CHECK(board.isEmpty());
  SC_CHECK(board.isConsistent());
  SC_CHECK(board.settledCellCount() == 0);
  SC_CHECK(board.fullRowCount() == 0);
  SC_CHECK(board.clearFullRows() == 0);
  SC_CHECK(board.isEmpty());

  for (int y = -stackfall::kHiddenRows; y < stackfall::kBoardHeight; ++y) {
    SC_CHECK(board.rowMask(y) == 0);
    SC_CHECK(!board.rowIsFull(y));
    for (int x = 0; x < kCols; ++x) {
      SC_CHECK(!board.isSettled(x, y));
      SC_CHECK(board.cellCodeAt(x, y) == 0);
    }
  }
}

void checkOutOfBoundsQueriesStayInsideStorage() {
  Board board;

  // Coordinates outside the board read as empty instead of reaching into the
  // arrays (specification section 4.1).
  const int badX[] = {-1, -3, 10, 15};
  for (int x : badX) {
    SC_CHECK(!board.isSettled(x, 5));
    SC_CHECK(board.cellCodeAt(x, 5) == 0);
  }
  const int badY[] = {-3, -5, 20, 25};
  for (int y : badY) {
    SC_CHECK(board.rowMask(y) == 0);
    SC_CHECK(!board.rowIsFull(y));
    SC_CHECK(!board.isSettled(4, y));
    SC_CHECK(board.cellCodeAt(4, y) == 0);
  }

  // Writes outside the board are refused and leave the board empty.
  SC_CHECK(!board.settleCell(-1, 5, PieceId::T));
  SC_CHECK(!board.settleCell(10, 5, PieceId::T));
  SC_CHECK(!board.settleCell(4, -3, PieceId::T));
  SC_CHECK(!board.settleCell(4, 20, PieceId::T));
  SC_CHECK(board.isEmpty());
  SC_CHECK(board.isConsistent());

  // A cell already settled is not written twice; occupancy stays the single
  // authority, so the first code survives.
  SC_CHECK(board.settleCell(3, 7, PieceId::J));
  SC_CHECK(!board.settleCell(3, 7, PieceId::L));
  SC_CHECK(board.cellCodeAt(3, 7) == static_cast<std::uint8_t>(PieceId::J));
  SC_CHECK(board.settledCellCount() == 1);
  SC_CHECK(board.isConsistent());
}

void checkFullRowMaskValue() {
  // Specification section 11, item 17, first half: a full row is 0x03FF.
  SC_CHECK(stackfall::kFullRowMask == 0x03FF);

  Board board;
  for (int x = 0; x < kCols; ++x) {
    SC_CHECK(board.settleCell(x, 12, PieceId::I));
    // Nine columns filled is not a full row yet.
    if (x < kCols - 1) {
      SC_CHECK(board.rowMask(12) != stackfall::kFullRowMask);
      SC_CHECK(!board.rowIsFull(12));
    }
  }
  SC_CHECK(board.rowMask(12) == stackfall::kFullRowMask);
  SC_CHECK(board.rowIsFull(12));
  SC_CHECK(board.fullRowCount() == 1);
  SC_CHECK(board.settledCellCount() == kCols);
  SC_CHECK(board.isConsistent());
}

// ---------------------------------------------------------------------------
// Movement validity: walls, floor, hidden rows, and the fall distance.
// ---------------------------------------------------------------------------

void checkPlacementAgainstBounds() {
  Board board;

  // Left wall: the O cells sit in local columns 1 and 2, so origin x = -2
  // puts a cell in column -1, while x = -1 still fits against the wall.
  SC_CHECK(board.canPlace(Piece{PieceId::O, Rotation::Spawn, -1, 5}));
  SC_CHECK(!board.canPlace(Piece{PieceId::O, Rotation::Spawn, -2, 5}));
  // Right wall: the O at x = 8 would put a cell in column 10.
  SC_CHECK(!board.canPlace(Piece{PieceId::O, Rotation::Spawn, 8, 5}));
  SC_CHECK(board.canPlace(Piece{PieceId::O, Rotation::Spawn, 7, 5}));
  // Floor: the O occupies two rows, so y = 19 leaves a cell in row 20.
  SC_CHECK(board.canPlace(Piece{PieceId::O, Rotation::Spawn, 4, 18}));
  SC_CHECK(!board.canPlace(Piece{PieceId::O, Rotation::Spawn, 4, 19}));
  // Hidden rows are legal storage. Only the occupied cells decide, so the I
  // at origin y = -3 still fits (its cells sit in row -2), while the T at
  // origin y = -3 does not (its nub would sit in row -3).
  SC_CHECK(board.canPlace(stackfall::makeSpawnPiece(PieceId::I)));
  SC_CHECK(board.canPlace(stackfall::makeSpawnPiece(PieceId::T)));
  SC_CHECK(board.canPlace(Piece{PieceId::I, Rotation::Spawn, 3, -3}));
  SC_CHECK(board.canPlace(Piece{PieceId::T, Rotation::Spawn, 3, -2}));
  SC_CHECK(!board.canPlace(Piece{PieceId::I, Rotation::Spawn, 3, -4}));
  SC_CHECK(!board.canPlace(Piece{PieceId::T, Rotation::Spawn, 3, -3}));
  SC_CHECK(!board.canPlace(Piece{PieceId::T, Rotation::Spawn, 3, 20}));

  // The cell form of the rule agrees with the piece form.
  const std::array<Cell, 4> inside = {
      {Cell{0, 0}, Cell{9, 19}, Cell{4, -2}, Cell{5, -1}}};
  SC_CHECK(board.canPlace(inside));
  const std::array<Cell, 4> offWall = {
      {Cell{0, 0}, Cell{10, 0}, Cell{2, 0}, Cell{3, 0}}};
  SC_CHECK(!board.canPlace(offWall));
  const std::array<Cell, 4> offFloor = {
      {Cell{0, 19}, Cell{1, 20}, Cell{2, 0}, Cell{3, 0}}};
  SC_CHECK(!board.canPlace(offFloor));
  SC_CHECK(!board.canPlaceCell(Cell{-1, 0}));
  SC_CHECK(!board.canPlaceCell(Cell{0, -3}));
  SC_CHECK(!board.canPlaceCell(Cell{0, 20}));
  SC_CHECK(board.canPlaceCell(Cell{9, 19}));
  SC_CHECK(board.canPlaceCell(Cell{0, -2}));

  // Fall distance on an empty board: the O lands with its low row on row 19
  // and the spawn I falls until its single row sits on row 19.
  SC_CHECK(board.maxDrop(Piece{PieceId::O, Rotation::Spawn, 4, 0}) == 18);
  SC_CHECK(board.maxDrop(stackfall::makeSpawnPiece(PieceId::I)) == 19);
  SC_CHECK(board.maxDrop(stackfall::makeSpawnPiece(PieceId::O)) == 18);
  SC_CHECK(board.maxDrop(Piece{PieceId::O, Rotation::Spawn, 4, 18}) == 0);

  // No query commits anything: the board stays empty throughout.
  SC_CHECK(board.isEmpty());
}

// Specification section 11, item 7: collision with settled cells blocks
// movement, and the active piece is not committed before it locks.
void checkSettledCellsBlockMovement() {
  Board board;
  ColorGrid model{};

  // A floor across row 19 except column 5, one cell at (2, 18), and one
  // cell at (6, 10) for the horizontal case.
  for (int x = 0; x < kCols; ++x) {
    if (x != 5) {
      SC_CHECK(settleTogether(board, model, x, 19, PieceId::Z));
    }
  }
  SC_CHECK(settleTogether(board, model, 2, 18, PieceId::J));
  SC_CHECK(settleTogether(board, model, 6, 10, PieceId::L));
  SC_CHECK(boardMatchesModel(board, model, "stack built"));

  // A vertical I over the floor is grounded at once; over the gap in column
  // 5 it falls exactly one more row.
  const Piece overFloor{PieceId::I, Rotation::Clockwise, 2, 15};  // column 4
  const Piece overGap{PieceId::I, Rotation::Clockwise, 3, 15};    // column 5
  SC_CHECK(board.canPlace(overFloor));
  SC_CHECK(board.canPlace(overGap));
  SC_CHECK(board.maxDrop(overFloor) == 0);
  SC_CHECK(board.maxDrop(overGap) == 1);
  SC_CHECK(board.canPlace(
      Piece{overGap.id, overGap.rotation, overGap.x, overGap.y + 1}));
  SC_CHECK(!board.canPlace(
      Piece{overGap.id, overGap.rotation, overGap.x, overGap.y + 2}));

  // Horizontal movement stops at the settled cell at (6, 10).
  const Piece beside{PieceId::O, Rotation::Spawn, 3, 10};  // columns 4 and 5
  SC_CHECK(board.canPlace(beside));
  SC_CHECK(board.canPlace(
      Piece{beside.id, beside.rotation, beside.x - 1, beside.y}));
  SC_CHECK(!board.canPlace(
      Piece{beside.id, beside.rotation, beside.x + 1, beside.y}));

  // A piece state that overlaps settled cells is invalid even though every
  // cell is inside the board: the T at (0, 18) meets the floor at (0, 19).
  SC_CHECK(!board.canPlace(Piece{PieceId::T, Rotation::Spawn, 0, 18}));

  // A rotation whose every candidate meets a settled cell or leaves the
  // board is rejected and leaves the piece unchanged. The J at spawn fits;
  // the settled cell at (2, 18) blocks its second candidate, one added cell
  // each blocks the first and the third, and the last two leave row 19.
  ColorGrid crowd = model;
  crowd[modelRow(15)][2] = static_cast<std::uint8_t>(PieceId::S);  // kick 3
  crowd[modelRow(16)][4] = static_cast<std::uint8_t>(PieceId::S);  // kick 1
  Board crowded;
  for (int row = 0; row < kRows; ++row) {
    for (int x = 0; x < kCols; ++x) {
      if (crowd[row][x] != 0) {
        SC_CHECK(crowded.settleCell(
            x, row - stackfall::kHiddenRows,
            static_cast<PieceId>(crowd[row][x])));
      }
    }
  }
  const Piece jPiece{PieceId::J, Rotation::Spawn, 2, 16};
  SC_CHECK(crowded.canPlace(jPiece));
  const stackfall::RotationResult jTurn = stackfall::rotatePiece(
      jPiece, true, [&crowded](const Piece& candidate) {
        return crowded.canPlace(candidate);
      });
  SC_CHECK(!jTurn.rotated);
  SC_CHECK(jTurn.kick == (stackfall::Kick{0, 0}));
  SC_CHECK(jTurn.piece.id == jPiece.id);
  SC_CHECK(jTurn.piece.rotation == jPiece.rotation);
  SC_CHECK(jTurn.piece.x == jPiece.x && jTurn.piece.y == jPiece.y);

  // A piece state that overlaps settled cells is invalid in every rotation.
  for (Rotation rotation : stackfall::kAllRotations) {
    SC_CHECK(!board.canPlace(Piece{PieceId::I, rotation, 0, 19}));
    SC_CHECK(!board.canPlace(Piece{PieceId::T, rotation, 1, 19}));
  }

  // Nothing above changed the board.
  SC_CHECK(boardMatchesModel(board, model, "after blocked moves"));
  SC_CHECK(board.settledCellCount() == kCols - 1 + 2);
}

void checkActivePieceNotCommittedBeforeLock() {
  Board board;
  ColorGrid model{};

  // Move, turn, and drop the same active piece for many steps. The board
  // must stay exactly as empty as the model, which records no active piece
  // until the lock (specification section 4.2).
  Piece piece = stackfall::makeSpawnPiece(PieceId::T);
  Random random(0xB0A2D001);
  for (int step = 0; step < 200; ++step) {
    const int action = random.below(4);
    Piece candidate = piece;
    if (action == 0) {
      candidate.x -= 1;
    } else if (action == 1) {
      candidate.x += 1;
    } else if (action == 2) {
      const stackfall::RotationResult turned = stackfall::rotatePiece(
          piece, random.below(2) == 0,
          [&board](const Piece& p) { return board.canPlace(p); });
      candidate = turned.piece;
    } else {
      candidate.y += 1;
    }
    if (board.canPlace(candidate)) {
      piece = candidate;
    }
    // However the step went, no cell of the active piece reached storage.
    SC_CHECK(board.isEmpty());
    SC_CHECK(board.isConsistent());
  }

  // The lock writes exactly the four cells of the piece, each with its code.
  lockTogether(board, model, piece);
  SC_CHECK(boardMatchesModel(board, model, "after the first lock"));
  SC_CHECK(board.settledCellCount() == 4);
  for (const Cell& cell : stackfall::boardCells(piece)) {
    SC_CHECK(board.isSettled(cell.x, cell.y));
    SC_CHECK(board.cellCodeAt(cell.x, cell.y) ==
             static_cast<std::uint8_t>(piece.id));
  }

  // The same piece state no longer fits: its own cells are settled now.
  SC_CHECK(!board.canPlace(piece));
}

void checkLockReportsHiddenCells() {
  Board board;

  // The I at spawn fills visible row 0 under the immediate-spawn rule of
  // task T029, so its lock hides no cell; the piece lifted one row into the
  // hidden rows by a kick is the one that reports four.
  const stackfall::LockReport iReport =
      board.lockPiece(stackfall::makeSpawnPiece(PieceId::I));
  SC_CHECK(iReport.hiddenCells == 0);
  SC_CHECK(board.rowMask(0) == 0x0078);  // bits 3..6, columns 3..6
  SC_CHECK(board.rowMask(-1) == 0);
  board.clear();
  const stackfall::LockReport liftedI =
      board.lockPiece(Piece{PieceId::I, Rotation::Spawn, 3, -2});
  SC_CHECK(liftedI.hiddenCells == 4);
  SC_CHECK(board.rowMask(-1) == 0x0078);
  SC_CHECK(board.rowMask(-2) == 0);

  // A T locked at y = 0 keeps all four cells inside the visible field.
  board.clear();
  const stackfall::LockReport tReport =
      board.lockPiece(Piece{PieceId::T, Rotation::Spawn, 3, 0});
  SC_CHECK(tReport.hiddenCells == 0);
  SC_CHECK(board.rowMask(0) == (1u << 4));
  SC_CHECK(board.settledCellCount() == 4);

  // A T locked at y = -1 leaves its nub in the hidden row -1.
  board.clear();
  const stackfall::LockReport hidden =
      board.lockPiece(Piece{PieceId::T, Rotation::Spawn, 3, -1});
  SC_CHECK(hidden.hiddenCells == 1);
  SC_CHECK(board.isSettled(4, -1));
  SC_CHECK(board.rowMask(-1) == (1u << 4));
  SC_CHECK(board.isConsistent());
}

// ---------------------------------------------------------------------------
// Line clears (item 8).
// ---------------------------------------------------------------------------

// Runs one clear case: rebuild the board from the picture through the public
// cell writer, lock the completing piece, clear, and compare against the
// model after every step.
void checkClearCase(const char* what, const ColorGrid& picture,
                    const Piece& completing) {
  Board board;
  ColorGrid model{};
  for (int row = 0; row < kRows; ++row) {
    for (int x = 0; x < kCols; ++x) {
      if (picture[row][x] != 0) {
        model[row][x] = picture[row][x];
        SC_CHECK(board.settleCell(
            x, row - stackfall::kHiddenRows,
            static_cast<PieceId>(picture[row][x])));
      }
    }
  }
  SC_CHECK(boardMatchesModel(board, model, what));
  SC_CHECK(board.canPlace(completing));

  const int cellsBefore = board.settledCellCount();
  lockTogether(board, model, completing);
  SC_CHECK(board.fullRowCount() == countFullModelRows(model));

  const int cleared = board.clearFullRows();
  const int modelCleared = modelClearRows(model);
  if (cleared != modelCleared) {
    std::fprintf(stderr, "  context: %s cleared %d rows, model %d\n", what,
                 cleared, modelCleared);
  }
  SC_CHECK(cleared == modelCleared);
  SC_CHECK(boardMatchesModel(board, model, what));

  // Cells leave the board only inside removed rows.
  SC_CHECK(board.settledCellCount() == cellsBefore + 4 - 10 * modelCleared);
  SC_CHECK(board.fullRowCount() == 0);
  SC_CHECK(board.isConsistent());
}

// Fills the nine columns around `gap` of board row y with one code.
void fillRowExcept(ColorGrid& picture, int y, int gap, std::uint8_t code) {
  for (int x = 0; x < kCols; ++x) {
    if (x != gap) {
      picture[modelRow(y)][x] = code;
    }
  }
}

// Rebuilds a board from a picture and returns it, for the cases that check
// single cells of the result by hand.
Board boardFromPicture(const ColorGrid& picture) {
  Board board;
  for (int row = 0; row < kRows; ++row) {
    for (int x = 0; x < kCols; ++x) {
      if (picture[row][x] != 0) {
        SC_CHECK(board.settleCell(x, row - stackfall::kHiddenRows,
                                  static_cast<PieceId>(picture[row][x])));
      }
    }
  }
  return board;
}

void checkOneTwoAndThreeLineClears() {
  // Single: row 19 misses column 9 and a vertical I in that column fills the
  // gap. Rows above carry distinct codes and positions, so the one-row shift
  // is visible.
  {
    ColorGrid picture{};
    fillRowExcept(picture, 19, 9, 1);
    picture[modelRow(18)][0] = 2;
    picture[modelRow(18)][3] = 3;
    picture[modelRow(0)][5] = 4;
    picture[modelRow(1)][5] = 7;
    picture[modelRow(4)][9] = 6;

    // I turned clockwise from (7, 16): cells (9,16)..(9,19).
    checkClearCase("single clear", picture,
                   Piece{PieceId::I, Rotation::Clockwise, 7, 16});
  }

  // Double: rows 18 and 19 miss column 0 and the vertical J fills it.
  {
    ColorGrid picture{};
    fillRowExcept(picture, 18, 0, 1);
    fillRowExcept(picture, 19, 0, 2);
    picture[modelRow(15)][4] = 3;
    picture[modelRow(17)][9] = 4;

    // J turned clockwise from (-1, 17): cells (0,17),(1,17),(0,18),(0,19).
    checkClearCase("double clear", picture,
                   Piece{PieceId::J, Rotation::Clockwise, -1, 17});
  }

  // Triple: rows 17, 18, and 19 miss column 6 and the vertical I fills it.
  {
    ColorGrid picture{};
    fillRowExcept(picture, 17, 6, 1);
    fillRowExcept(picture, 18, 6, 2);
    fillRowExcept(picture, 19, 6, 3);
    picture[modelRow(10)][1] = 4;
    picture[modelRow(16)][5] = 5;

    // I turned clockwise from (4, 16): cells (6,16)..(6,19).
    checkClearCase("triple clear", picture,
                   Piece{PieceId::I, Rotation::Clockwise, 4, 16});
  }
}

void checkFourLineClear() {
  // The classic four-line clear: four rows miss the same column and one
  // vertical I completes all of them at once.
  ColorGrid picture{};
  fillRowExcept(picture, 16, 4, 1);
  fillRowExcept(picture, 17, 4, 2);
  fillRowExcept(picture, 18, 4, 3);
  fillRowExcept(picture, 19, 4, 5);
  // One surviving cell above the field, so the four-row shift shows up.
  picture[modelRow(12)][7] = 6;

  // I turned clockwise from (2, 16): cells (4,16)..(4,19).
  checkClearCase("four-line clear", picture,
                 Piece{PieceId::I, Rotation::Clockwise, 2, 16});
}

// Full rows that do not sit next to each other, and full hidden rows: the
// removal is simultaneous, so every surviving row drops by the number of
// full rows below it, never row by row.
void checkNonAdjacentAndHiddenRowClears() {
  {
    // Rows 19 and 17 full, row 18 not.
    ColorGrid picture{};
    for (int x = 0; x < kCols; ++x) {
      picture[modelRow(19)][x] = 1;
      picture[modelRow(17)][x] = 2;
    }
    picture[modelRow(18)][4] = 3;
    picture[modelRow(16)][9] = 4;
    picture[modelRow(0)][0] = 5;

    Board board = boardFromPicture(picture);
    SC_CHECK(board.fullRowCount() == 2);
    SC_CHECK(board.clearFullRows() == 2);

    // Row 18 has one full row below it and drops by one onto the floor.
    // Row 16 has two full rows below it and drops by two. Row 0 also drops
    // by two, and the two rows at the top are empty afterwards.
    SC_CHECK(board.settledCellCount() == 3);
    SC_CHECK(board.isSettled(4, 19));
    SC_CHECK(board.cellCodeAt(4, 19) == 3);
    SC_CHECK(board.isSettled(9, 18));
    SC_CHECK(board.cellCodeAt(9, 18) == 4);
    SC_CHECK(board.isSettled(0, 2));
    SC_CHECK(board.cellCodeAt(0, 2) == 5);
    SC_CHECK(board.rowMask(0) == 0);
    SC_CHECK(board.rowMask(1) == 0);
    SC_CHECK(board.isConsistent());
  }

  {
    // Both hidden rows full: they are stored rows, so they clear too. Every
    // visible row sits below them and therefore does not move at all.
    ColorGrid picture{};
    for (int x = 0; x < kCols; ++x) {
      picture[modelRow(-2)][x] = 1;
      picture[modelRow(-1)][x] = 2;
    }
    picture[modelRow(0)][6] = 3;

    Board board = boardFromPicture(picture);
    SC_CHECK(board.fullRowCount() == 2);
    SC_CHECK(board.rowIsFull(-2));
    SC_CHECK(board.rowIsFull(-1));
    SC_CHECK(board.clearFullRows() == 2);
    SC_CHECK(board.settledCellCount() == 1);
    SC_CHECK(board.isSettled(6, 0));
    SC_CHECK(board.cellCodeAt(6, 0) == 3);
    SC_CHECK(!board.isSettled(6, -2));
    SC_CHECK(board.rowMask(-2) == 0);
    SC_CHECK(board.rowMask(-1) == 0);
    SC_CHECK(!board.isEmpty());
    SC_CHECK(board.isConsistent());
  }
}

// A lock that completes no row clears nothing and keeps every cell.
void checkLockWithoutClear() {
  Board board;
  ColorGrid model{};

  lockTogether(board, model, Piece{PieceId::S, Rotation::Spawn, 4, 10});
  SC_CHECK(board.fullRowCount() == 0);
  SC_CHECK(board.clearFullRows() == 0);
  SC_CHECK(boardMatchesModel(board, model, "lock without a clear"));
  SC_CHECK(board.settledCellCount() == 4);
}

// ---------------------------------------------------------------------------
// Randomized consistency (item 17).
// ---------------------------------------------------------------------------

// Hundreds of random raw fills, each followed by a clear, compared against
// the reference model.
void checkRandomFillsStayConsistent() {
  Random random(0x5EEDF111);
  int clearsChecked = 0;
  const int kIterations = 400;
  for (int iteration = 0; iteration < kIterations; ++iteration) {
    Board board;
    ColorGrid model{};

    // Fill the bottom rows to a random density per row; a row that reaches
    // all ten columns comes out full.
    const int rowsToFill = 1 + random.below(kRows);
    for (int row = kRows - rowsToFill; row < kRows; ++row) {
      const int wanted = random.below(kCols + 1);
      int placed = 0;
      while (placed < wanted) {
        const int x = random.below(kCols);
        if (model[row][x] == 0) {
          const PieceId id =
              stackfall::kAllPieceIds[random.below(stackfall::kPieceCount)];
          SC_CHECK(settleTogether(board, model, x,
                                  row - stackfall::kHiddenRows, id));
          ++placed;
        }
      }
    }
    SC_CHECK(boardMatchesModel(board, model, "random fill"));

    const int expectedFull = countFullModelRows(model);
    SC_CHECK(board.fullRowCount() == expectedFull);

    const int cleared = board.clearFullRows();
    const int modelCleared = modelClearRows(model);
    if (cleared != modelCleared) {
      std::fprintf(stderr,
                   "  context: random fill %d cleared %d rows, model %d\n",
                   iteration, cleared, modelCleared);
    }
    SC_CHECK(cleared == modelCleared);
    SC_CHECK(boardMatchesModel(board, model, "random fill after clear"));
    clearsChecked += cleared;
  }
  // The loop must actually have exercised row removals in bulk: 400 random
  // boards of this shape remove well over 350 rows in total.
  SC_CHECK(clearsChecked > 350);
}

// Hundreds of random lock-and-clear rounds on a small number of boards,
// mirroring play: every piece falls to its landing row before it locks.
void checkRandomLocksStayConsistent() {
  Random random(0x10C05EED);
  Board board;
  ColorGrid model{};

  int locks = 0;
  int clearedTotal = 0;
  int resets = 0;
  const int entryRows[] = {0, 3, 7, 12};
  for (int step = 0; step < 600; ++step) {
    const PieceId id =
        stackfall::kAllPieceIds[random.below(stackfall::kPieceCount)];
    const Rotation rotation =
        static_cast<Rotation>(random.below(stackfall::kRotationCount));

    // Pick the state that lands deepest: for every column, take the first
    // entry row that fits and let the piece fall. Stacking tight this way
    // completes rows often enough to exercise clears in bulk.
    bool placed = false;
    Piece piece{id, rotation, 0, 0};
    int deepestLanding = -1;
    for (int x = -2; x <= kCols; ++x) {
      for (int y : entryRows) {
        const Piece candidate{id, rotation, x, y};
        if (!board.canPlace(candidate)) {
          continue;
        }
        const int landing = y + board.maxDrop(candidate);
        if (landing > deepestLanding) {
          deepestLanding = landing;
          piece = candidate;
          piece.y = landing;
          placed = true;
        }
        break;  // the lowest fitting entry row of this column is enough
      }
    }
    if (!placed) {
      // The board is full: start a fresh round, exactly as a game would.
      board.clear();
      model = ColorGrid{};
      ++resets;
      continue;
    }

    SC_CHECK(board.canPlace(piece));
    SC_CHECK(board.maxDrop(piece) == 0);

    lockTogether(board, model, piece);
    ++locks;
    SC_CHECK(boardMatchesModel(board, model, "random lock"));

    const int modelCleared = modelClearRows(model);
    const int cleared = board.clearFullRows();
    if (cleared != modelCleared) {
      std::fprintf(stderr,
                   "  context: random lock step %d cleared %d rows, "
                   "model %d\n",
                   step, cleared, modelCleared);
    }
    SC_CHECK(cleared == modelCleared);
    SC_CHECK(boardMatchesModel(board, model, "random lock after clear"));
    clearedTotal += cleared;
  }

  // The sequence must have locked many pieces and removed many rows.
  SC_CHECK(locks > 300);
  SC_CHECK(clearedTotal > 30);
  SC_CHECK(resets > 0);
  SC_CHECK(board.isConsistent());

  // A full reset empties both arrays together.
  board.clear();
  SC_CHECK(board.isEmpty());
  SC_CHECK(board.isConsistent());
  SC_CHECK(board.settledCellCount() == 0);
}

}  // namespace

void run_board_tests() {
  checkEmptyBoard();
  checkOutOfBoundsQueriesStayInsideStorage();
  checkFullRowMaskValue();
  checkPlacementAgainstBounds();
  checkSettledCellsBlockMovement();
  checkActivePieceNotCommittedBeforeLock();
  checkLockReportsHiddenCells();
  checkOneTwoAndThreeLineClears();
  checkFourLineClear();
  checkNonAdjacentAndHiddenRowClears();
  checkLockWithoutClear();
  checkRandomFillsStayConsistent();
  checkRandomLocksStayConsistent();
}
