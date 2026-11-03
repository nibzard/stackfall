// Board storage, collision, locking, and line clears for the STACKFALL rules
// core (specification sections 4.1, 4.2, 4.5, and 4.6).
//
// Every stored row is a ten-bit occupancy mask plus a parallel color row, and
// the two move together: a line clear copies both arrays for a surviving row,
// so a settled cell keeps its piece code through any number of shifts. The
// mutators run the isConsistent() invariant check under assert(), so debug
// builds and the test suite both catch a divergence between the arrays
// (specification section 4.1).

#include "stackfall/board.hpp"

#include <cassert>

namespace stackfall {

Board::Board() {
  clear();
}

// ---------------------------------------------------------------------------
// Storage observers.
// ---------------------------------------------------------------------------

const std::array<std::uint16_t, kBoardRows>& Board::occupancy() const {
  return occupancy_;
}

const std::array<std::array<std::uint8_t, kBoardWidth>, kBoardRows>&
Board::cellTypes() const {
  return cellType_;
}

std::uint16_t Board::rowMask(int y) const {
  // A row outside the stored range has no contents, so it reads as empty
  // instead of indexing the arrays out of bounds.
  if (!inBoardY(y)) {
    return 0;
  }
  return occupancy_[boardRowIndex(y)];
}

std::uint8_t Board::cellCodeAt(int x, int y) const {
  if (!inBoardX(x) || !inBoardY(y)) {
    return 0;
  }
  return cellType_[boardRowIndex(y)][x];
}

bool Board::isSettled(int x, int y) const {
  // The bounds come first: a shift by a negative column count would be
  // undefined, and a cell outside the board is never settled.
  if (!inBoardX(x) || !inBoardY(y)) {
    return false;
  }
  return (occupancy_[boardRowIndex(y)] >> x & 1u) != 0;
}

bool Board::rowIsFull(int y) const {
  return rowMask(y) == kFullRowMask;
}

int Board::fullRowCount() const {
  int full = 0;
  for (int y = -kHiddenRows; y < kBoardHeight; ++y) {
    if (rowIsFull(y)) {
      ++full;
    }
  }
  return full;
}

int Board::settledCellCount() const {
  int cells = 0;
  for (const std::uint16_t mask : occupancy_) {
    for (int x = 0; x < kBoardWidth; ++x) {
      cells += static_cast<int>((mask >> x & 1u) != 0);
    }
  }
  return cells;
}

bool Board::isEmpty() const {
  for (const std::uint16_t mask : occupancy_) {
    if (mask != 0) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Movement validity, specification section 4.2.
// ---------------------------------------------------------------------------

bool Board::canPlaceCell(const Cell& cell) const {
  // The bounds come first, so a cell outside the ten columns or outside
  // y = -2..19 is refused before any stored row is read (section 4.1).
  if (!inBoard(cell)) {
    return false;
  }
  return (occupancy_[boardRowIndex(cell.y)] >> cell.x & 1u) == 0;
}

bool Board::canPlace(const std::array<Cell, kCellsPerPiece>& cells) const {
  for (const Cell& cell : cells) {
    if (!canPlaceCell(cell)) {
      return false;
    }
  }
  return true;
}

bool Board::canPlace(const Piece& piece) const {
  return canPlace(boardCells(piece));
}

int Board::maxDrop(const Piece& piece) const {
  int distance = 0;
  // The loop ends at the floor at the latest, because a piece pressed onto
  // row 19 fails canPlace one row further down.
  while (canPlace(Piece{piece.id, piece.rotation, piece.x, piece.y + distance + 1})) {
    ++distance;
  }
  return distance;
}

// ---------------------------------------------------------------------------
// Lock and line clears, specification sections 4.5 and 4.6.
// ---------------------------------------------------------------------------

LockReport Board::lockPiece(const Piece& piece) {
  // Section 4.2 keeps an active piece out of occupancy, so the caller locks
  // only a state that already passed canPlace. The debug build asserts it;
  // the release build keeps a violated precondition from writing outside the
  // arrays by skipping an off-board cell.
  assert(canPlace(piece) && "lockPiece called on a blocked piece state");

  const std::array<Cell, kCellsPerPiece> cells = boardCells(piece);
  LockReport report{cells, 0};
  for (const Cell& cell : cells) {
    if (!inBoard(cell)) {
      continue;
    }
    const int row = boardRowIndex(cell.y);
    occupancy_[row] = static_cast<std::uint16_t>(occupancy_[row] | (1u << cell.x));
    cellType_[row][cell.x] = static_cast<std::uint8_t>(piece.id);
    if (cell.y < 0) {
      ++report.hiddenCells;
    }
  }
  assert(isConsistent());
  return report;
}

int Board::clearFullRows() {
  // One pass from the floor upwards keeps every surviving row in its order
  // and drops each one by the number of full rows below it, which is the
  // simultaneous removal of section 4.6. Occupancy and colors move as a
  // pair, so a surviving cell keeps its piece code.
  int write = kBoardRows - 1;
  for (int read = kBoardRows - 1; read >= 0; --read) {
    if (occupancy_[read] == kFullRowMask) {
      continue;  // the row is removed
    }
    occupancy_[write] = occupancy_[read];
    cellType_[write] = cellType_[read];
    --write;
  }
  // Every surviving row stepped write down once from the floor, so the rows
  // that never received a survivor are exactly the removed ones.
  const int cleared = write + 1;

  // The rows that appear at the top start empty in both arrays.
  for (int row = write; row >= 0; --row) {
    occupancy_[row] = 0;
    cellType_[row].fill(0);
  }
  assert(isConsistent());
  return cleared;
}

// ---------------------------------------------------------------------------
// Low-level writers and invariants.
// ---------------------------------------------------------------------------

bool Board::settleCell(int x, int y, PieceId id) {
  if (!inBoardX(x) || !inBoardY(y)) {
    return false;
  }
  const int row = boardRowIndex(y);
  if ((occupancy_[row] >> x & 1u) != 0) {
    return false;  // already settled; occupancy stays authoritative
  }
  occupancy_[row] = static_cast<std::uint16_t>(occupancy_[row] | (1u << x));
  cellType_[row][x] = static_cast<std::uint8_t>(id);
  assert(isConsistent());
  return true;
}

void Board::clear() {
  occupancy_.fill(0);
  for (std::array<std::uint8_t, kBoardWidth>& row : cellType_) {
    row.fill(0);
  }
}

bool Board::isConsistent() const {
  for (int row = 0; row < kBoardRows; ++row) {
    // Only the ten column bits of a mask may be set.
    if ((occupancy_[row] & static_cast<std::uint16_t>(~kFullRowMask)) != 0) {
      return false;
    }
    for (int x = 0; x < kBoardWidth; ++x) {
      const bool occupied = (occupancy_[row] >> x & 1u) != 0;
      const std::uint8_t code = cellType_[row][x];
      if (occupied != (code != 0)) {
        return false;
      }
      // A color code is 0 or one of the seven piece codes 1..7.
      if (code > static_cast<std::uint8_t>(kPieceCount)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace stackfall
