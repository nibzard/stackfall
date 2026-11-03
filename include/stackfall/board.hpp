#pragma once

// The playing field of the STACKFALL rules core (specification sections 4.1,
// 4.2, 4.5, and 4.6): a 10 x 20 visible field with two hidden rows above it,
// stored as 22 row masks plus a parallel color array.
//
// Storage follows section 4.1 exactly. `occupancy_` holds one uint16_t per
// row and uses the ten column bits 0..9, so a filled row is kFullRowMask.
// `cellType_` holds one byte per cell, 0 for empty and 1..7 for the piece
// codes of PieceId. The storage index of row y is y + kHiddenRows.
//
// Two rules govern every access:
//   * `occupancy_` is authoritative for collision. Collision code reads the
//     masks only; the color array never decides a move.
//   * No code reads or writes a row outside y = -2..19. Every query checks
//     the bounds of a cell before it touches a stored row.
//
// The two arrays describe the same cells at all times. isConsistent() checks
// that invariant; the mutators assert it in debug builds and the test suite
// checks it after every operation (section 4.1).

#include <array>
#include <cstdint>

#include "stackfall/pieces.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// What a lock wrote, for the caller's bookkeeping (specification section
// 4.5): the four committed cells and how many of them landed in the hidden
// rows y < 0. The game layer ends the round on a nonzero hidden cell count
// (lock-out).
struct LockReport {
  std::array<Cell, kCellsPerPiece> cells;
  int hiddenCells;
};

class Board {
 public:
  Board();

  // -------------------------------------------------------------------------
  // Storage observers. rowMask(y) and cellCodeAt(x, y) answer for any
  // coordinates: a row outside y = -2..19 reads as empty, because such a row
  // has no storage and no contents.
  // -------------------------------------------------------------------------
  const std::array<std::uint16_t, kBoardRows>& occupancy() const;
  const std::array<std::array<std::uint8_t, kBoardWidth>, kBoardRows>&
  cellTypes() const;
  std::uint16_t rowMask(int y) const;
  std::uint8_t cellCodeAt(int x, int y) const;
  bool isSettled(int x, int y) const;
  bool rowIsFull(int y) const;
  int fullRowCount() const;
  int settledCellCount() const;
  bool isEmpty() const;

  // -------------------------------------------------------------------------
  // Movement validity, specification section 4.2. A piece state is valid
  // when every occupied cell lies in one of the ten columns, lies in
  // y = -2..19, and misses every settled occupancy bit. The bounds are
  // checked before any stored row is read.
  // -------------------------------------------------------------------------
  bool canPlaceCell(const Cell& cell) const;
  bool canPlace(const std::array<Cell, kCellsPerPiece>& cells) const;
  bool canPlace(const Piece& piece) const;

  // Rows the piece can still fall while staying valid; 0 when it is grounded.
  // The ghost preview and the hard drop of section 4.5 both use this query.
  int maxDrop(const Piece& piece) const;

  // -------------------------------------------------------------------------
  // Lock and line clears, specification sections 4.5 and 4.6.
  // -------------------------------------------------------------------------

  // Commits the four cells of an active piece to the board and records the
  // piece code in the color array. The caller locks only a piece state that
  // satisfies canPlace; the debug build asserts that precondition and the
  // release build skips an off-board cell instead of writing out of range.
  LockReport lockPiece(const Piece& piece);

  // Removes every full row at once, shifts the remaining rows down with
  // their colors, and empties the rows that appear at the top. Returns the
  // number of removed rows.
  int clearFullRows();

  // -------------------------------------------------------------------------
  // Low-level writers and invariants.
  // -------------------------------------------------------------------------

  // Sets one cell and its piece code directly. This is the cell writer
  // behind lockPiece; the game layer goes through lockPiece, and tests use
  // this to build stacks that no single piece could produce. Returns false
  // and changes nothing when the cell lies outside the board or is already
  // settled.
  bool settleCell(int x, int y, PieceId id);

  // Empties every row of both arrays.
  void clear();

  // True when both arrays describe the same cells and every color code is a
  // valid piece code: each set occupancy bit has a nonzero cellType byte,
  // each clear bit has byte 0, and no mask uses a bit above the ten columns.
  bool isConsistent() const;

 private:
  std::array<std::uint16_t, kBoardRows> occupancy_;
  std::array<std::array<std::uint8_t, kBoardWidth>, kBoardRows> cellType_;
};

}  // namespace stackfall
