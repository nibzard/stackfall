// Headless suite for the game state machine and the deterministic tick
// (task T007, milestone M1). The acceptance criteria are specification
// section 11, items 4, 9, 10, 11, 12, 14, 15, and 16:
//
//   4.  Left/right movement stops at walls for every orientation.
//   9.  Gravity moves exactly when scheduled; pause freezes timers; restart
//       resets them.
//   10. A grounded piece locks after 30 ticks, not before, with at most 15
//       lock-delay resets.
//   11. Hard drop travels to the lowest legal position and locks in the same
//       tick.
//   12. Soft-drop and hard-drop points match the cells actually moved.
//   14. Hold works once per piece, respawns in orientation 0, and is
//       re-enabled only after lock.
//   15. Failed spawn and lock with hidden-row cells end the game
//       deterministically.
//   16. Score and level boundaries, including the level 20 cap and the
//       pre-clear level multiplier.
//
// Timing tests count ticks, never wall-clock time, and every setup that needs
// a stack or a chosen piece goes through GameTestPeer, so the public Game
// interface stays read-only for the renderer.

#include <array>
#include <cstdint>
#include <cstdio>

#include "game_test_peer.hpp"
#include "stackfall/board.hpp"
#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/randomizer.hpp"
#include "stackfall/scoring.hpp"
#include "stackfall/types.hpp"
#include "test_util.hpp"

// GameTestPeer, the route into the private state of Game, lives in
// game_test_peer.hpp and is shared with the regression suite.

namespace {

using stackfall::Board;
using stackfall::Cell;
using stackfall::Game;
using stackfall::InputFrame;
using stackfall::Piece;
using stackfall::PieceId;
using stackfall::Rotation;
using stackfall::Scene;

constexpr std::uint64_t kSeed = 20261102ULL;

// ---------------------------------------------------------------------------
// Input builders, so a frame reads as its intent instead of eight booleans.
// ---------------------------------------------------------------------------

InputFrame holdLeft() {
  InputFrame f;
  f.leftHeld = true;
  return f;
}

InputFrame holdRight() {
  InputFrame f;
  f.rightHeld = true;
  return f;
}

InputFrame holdBoth() {
  InputFrame f;
  f.leftHeld = true;
  f.rightHeld = true;
  return f;
}

InputFrame holdDown() {
  InputFrame f;
  f.downHeld = true;
  return f;
}

InputFrame pressPause() {
  InputFrame f;
  f.pausePressed = true;
  return f;
}

InputFrame pressHardDrop() {
  InputFrame f;
  f.hardDropPressed = true;
  return f;
}

InputFrame pressRotateCW() {
  InputFrame f;
  f.rotateCWPressed = true;
  return f;
}

InputFrame pressHold() {
  InputFrame f;
  f.holdPressed = true;
  return f;
}

void runTicks(Game& g, const InputFrame& f, int count) {
  for (int i = 0; i < count; ++i) {
    g.tick(f);
  }
}

// ---------------------------------------------------------------------------
// Comparison and setup helpers.
// ---------------------------------------------------------------------------

bool samePiece(const Piece& a, const Piece& b) {
  return a.id == b.id && a.rotation == b.rotation && a.x == b.x && a.y == b.y;
}

// Two games are the same round when every field a hash would fold in matches,
// public and private alike.
bool sameState(const Game& a, const Game& b) {
  return a.score() == b.score() && a.lines() == b.lines() &&
         a.level() == b.level() && a.scene() == b.scene() &&
         a.tickCount() == b.tickCount() &&
         a.board().occupancy() == b.board().occupancy() &&
         a.board().cellTypes() == b.board().cellTypes() &&
         a.queue() == b.queue() && a.hasHoldPiece() == b.hasHoldPiece() &&
         (!a.hasHoldPiece() || a.holdPieceId() == b.holdPieceId()) &&
         a.holdLocked() == b.holdLocked() &&
         a.hasActivePiece() == b.hasActivePiece() &&
         (!a.hasActivePiece() || samePiece(a.activePiece(), b.activePiece())) &&
         stackfall::GameTestPeer::gravityTimer(a) ==
             stackfall::GameTestPeer::gravityTimer(b) &&
         stackfall::GameTestPeer::groundedTicks(a) ==
             stackfall::GameTestPeer::groundedTicks(b) &&
         stackfall::GameTestPeer::lockResets(a) ==
             stackfall::GameTestPeer::lockResets(b) &&
         stackfall::GameTestPeer::shiftDir(a) ==
             stackfall::GameTestPeer::shiftDir(b) &&
         stackfall::GameTestPeer::shiftTicks(a) ==
             stackfall::GameTestPeer::shiftTicks(b);
}

// Settles every column of row y except gapColumn.
void fillRowExcept(Game& g, int y, int gapColumn,
                   PieceId id = PieceId::I) {
  Board& board = stackfall::GameTestPeer::board(g);
  for (int x = 0; x < stackfall::kBoardWidth; ++x) {
    if (x != gapColumn) {
      board.settleCell(x, y, id);
    }
  }
}

// A vertical I whose single column is `column`, top cell at row top.
Piece verticalI(int column, int top) {
  // Clockwise I cells sit in local column 2, rows 0..3.
  return Piece{PieceId::I, Rotation::Clockwise, column - 2, top};
}

// Blocks every spawn cell of `id` with settled cells.
void blockSpawnCells(Game& g, PieceId id) {
  Board& board = stackfall::GameTestPeer::board(g);
  for (const Cell& cell : stackfall::boardCells(stackfall::makeSpawnPiece(id))) {
    board.settleCell(cell.x, cell.y, PieceId::J);
  }
}

int minCellX(const Piece& piece) {
  const std::array<Cell, stackfall::kCellsPerPiece> cells =
      stackfall::boardCells(piece);
  int minX = cells[0].x;
  for (const Cell& cell : cells) {
    if (cell.x < minX) {
      minX = cell.x;
    }
  }
  return minX;
}

int maxCellX(const Piece& piece) {
  const std::array<Cell, stackfall::kCellsPerPiece> cells =
      stackfall::boardCells(piece);
  int maxX = cells[0].x;
  for (const Cell& cell : cells) {
    if (cell.x > maxX) {
      maxX = cell.x;
    }
  }
  return maxX;
}

int maxCellY(const Piece& piece) {
  const std::array<Cell, stackfall::kCellsPerPiece> cells =
      stackfall::boardCells(piece);
  int maxY = cells[0].y;
  for (const Cell& cell : cells) {
    if (cell.y > maxY) {
      maxY = cell.y;
    }
  }
  return maxY;
}

// ---------------------------------------------------------------------------
// Fresh round and determinism.
// ---------------------------------------------------------------------------

void test_start_opens_a_fresh_round() {
  Game g;
  SC_CHECK(g.scene() == Scene::Boot);
  SC_CHECK(!g.hasActivePiece());
  g.tick(InputFrame::idle());  // an un-started game ignores input
  SC_CHECK(g.scene() == Scene::Boot);

  g.start(kSeed);
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.score() == 0);
  SC_CHECK(g.lines() == 0);
  SC_CHECK(g.level() == 1);
  SC_CHECK(g.tickCount() == 0);
  SC_CHECK(g.seed() == kSeed);
  SC_CHECK(g.board().isEmpty());
  SC_CHECK(g.board().isConsistent());
  SC_CHECK(!g.hasHoldPiece());
  SC_CHECK(!g.holdLocked());
  SC_CHECK(g.hasActivePiece());

  // The first piece comes out of the seeded queue, and the queue itself is
  // the queue of a Randomizer started with the same seed.
  stackfall::Randomizer reference(kSeed);
  const PieceId first = reference.nextPiece();
  SC_CHECK(g.activePiece().id == first);
  SC_CHECK(samePiece(g.activePiece(), stackfall::makeSpawnPiece(first)));
  SC_CHECK(g.queue() == reference.queue());
  SC_CHECK(g.board().canPlace(g.activePiece()));
}

void test_same_seed_same_input_same_round() {
  const InputFrame script[8] = {holdDown(),  holdLeft(),  InputFrame::idle(),
                                holdRight(), pressRotateCW(), holdDown(),
                                InputFrame::idle(), pressHardDrop()};
  Game a;
  Game b;
  a.start(kSeed);
  b.start(kSeed);
  for (int tick = 0; tick < 400; ++tick) {
    const InputFrame& f = script[tick % 8];
    a.tick(f);
    b.tick(f);
    SC_CHECK(a.score() == b.score());
    SC_CHECK(a.scene() == b.scene());
    SC_CHECK(a.board().occupancy() == b.board().occupancy());
  }
  SC_CHECK(sameState(a, b));
}

// ---------------------------------------------------------------------------
// Gravity, section 11 item 9 (first half).
// ---------------------------------------------------------------------------

void test_gravity_table_is_pinned() {
  const std::array<int, stackfall::kMaxLevel> pinned = {
      {48, 43, 38, 33, 28, 23, 18, 13, 8, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1}};
  SC_CHECK(stackfall::gravityTable() == pinned);
  for (int level = 1; level <= stackfall::kMaxLevel; ++level) {
    SC_CHECK(stackfall::gravityTicksForLevel(level) ==
             pinned[static_cast<std::size_t>(level - 1)]);
  }
  // Out-of-range levels read the nearest entry instead of indexing away.
  SC_CHECK(stackfall::gravityTicksForLevel(0) == pinned.front());
  SC_CHECK(stackfall::gravityTicksForLevel(21) == pinned.back());
}

// Counts the idle ticks one cell of fall takes at a given level.
int ticksPerFall(Game& g) {
  const int startY = g.activePiece().y;
  int ticks = 0;
  while (g.activePiece().y == startY && g.hasActivePiece() &&
         g.scene() == Scene::Playing) {
    g.tick(InputFrame::idle());
    ++ticks;
    SC_CHECK(ticks < 200);  // a guard against a runaway loop
  }
  return ticks;
}

void test_gravity_moves_exactly_when_scheduled() {
  const int levels[] = {1, 2, 5, 10, 13, 19, 20};
  for (const int level : levels) {
    Game g;
    g.start(kSeed + static_cast<std::uint64_t>(level));
    stackfall::GameTestPeer::setLines(
        g, static_cast<std::uint64_t>(stackfall::kLinesPerLevel * (level - 1)));
    SC_CHECK(g.level() == level);
    // A horizontal I high on the board has open space below it.
    stackfall::GameTestPeer::setPiece(
        g, Piece{PieceId::I, Rotation::Spawn, 3, 2});
    const int expected =
        stackfall::gravityTicksForLevel(level);
    SC_CHECK(ticksPerFall(g) == expected);
    SC_CHECK(g.activePiece().y == 3);
  }

  // The timer restarts after a drop, so the second cell takes the same
  // stretch of ticks at level 1.
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::I, Rotation::Spawn, 3, 2});
  SC_CHECK(ticksPerFall(g) == 48);
  SC_CHECK(ticksPerFall(g) == 48);
}

void test_soft_drop_beats_gravity_and_scores_cells() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::I, Rotation::Spawn, 3, 0});
  // The I spawn drawing occupies board row y + 1, so it lands at y = 18.
  runTicks(g, holdDown(), 18);
  SC_CHECK(g.activePiece().y == 18);
  SC_CHECK(g.score() == 18);  // one point per successful cell
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(stackfall::GameTestPeer::gravityTimer(g) == 0);

  // Held soft drop cannot pass the floor: no further points, and the piece
  // stays in play until the lock delay expires. The tick a piece lands on
  // counts as its first grounded tick.
  runTicks(g, holdDown(), 10);
  SC_CHECK(g.activePiece().y == 18);
  SC_CHECK(g.score() == 18);
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 11);
  runTicks(g, holdDown(), 19);  // grounded ticks 12..30: the lock fires
  SC_CHECK(g.board().settledCellCount() == 4);
  SC_CHECK(g.score() == 18);  // the lock itself adds nothing

  // One attempted cell per tick while held, never two, even at level 1 where
  // natural gravity would be far slower.
  Game h;
  h.start(kSeed + 1);
  stackfall::GameTestPeer::setPiece(h,
                                    Piece{PieceId::O, Rotation::Spawn, 4, 0});
  for (int i = 0; i < 10; ++i) {
    h.tick(holdDown());
    SC_CHECK(h.activePiece().y == i + 1);
  }
  SC_CHECK(h.score() == 10);
}

// ---------------------------------------------------------------------------
// Hard drop, section 11 items 11 and 12.
// ---------------------------------------------------------------------------

void test_hard_drop_lands_and_locks_in_the_same_tick() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 10});
  const std::array<PieceId, stackfall::kPreviewCount> queueBefore = g.queue();
  g.tick(pressHardDrop());

  // One tick placed the piece on the floor, locked it, and spawned the next.
  SC_CHECK(g.board().isSettled(5, 18));
  SC_CHECK(g.board().isSettled(6, 18));
  SC_CHECK(g.board().isSettled(5, 19));
  SC_CHECK(g.board().isSettled(6, 19));
  SC_CHECK(g.board().settledCellCount() == 4);
  SC_CHECK(g.board().cellCodeAt(5, 19) ==
           static_cast<std::uint8_t>(PieceId::O));
  SC_CHECK(g.score() == 16);  // eight cells travelled, two points each
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.activePiece().id == queueBefore[0]);
  SC_CHECK(!g.holdLocked());  // the lock re-enabled hold
  SC_CHECK(g.tickCount() == 1);
}

void test_hard_drop_of_a_grounded_piece_scores_nothing() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 18});
  g.tick(pressHardDrop());
  SC_CHECK(g.score() == 0);  // zero cells travelled
  SC_CHECK(g.board().settledCellCount() == 4);
}

void test_soft_and_hard_drop_point_accounting() {
  // Three soft cells, then a hard drop over the remaining distance.
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::T, Rotation::Spawn, 3, 0});
  runTicks(g, holdDown(), 3);
  SC_CHECK(g.activePiece().y == 3);
  g.tick(pressHardDrop());
  // T spawn cells end at local row 1, so the piece rests with y = 18: the
  // hard drop covered 15 cells.
  SC_CHECK(g.board().settledCellCount() == 4);
  SC_CHECK(g.score() == 3 + 2 * 15);
  SC_CHECK(g.lines() == 0);
}

// ---------------------------------------------------------------------------
// DAS and ARR, section 4.5.
// ---------------------------------------------------------------------------

void test_das_and_arr_repeat() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::T, Rotation::Spawn, 3, 0});

  g.tick(holdRight());  // the press tick moves immediately
  SC_CHECK(g.activePiece().x == 4);
  SC_CHECK(stackfall::GameTestPeer::shiftTicks(g) == 0);

  runTicks(g, holdRight(), 9);  // ticks 2..10 charge the DAS
  SC_CHECK(g.activePiece().x == 4);
  SC_CHECK(stackfall::GameTestPeer::shiftTicks(g) == 9);

  g.tick(holdRight());  // tick 11: ten ticks charged, the first repeat fires
  SC_CHECK(g.activePiece().x == 5);

  g.tick(holdRight());  // tick 12: still inside the ARR gap
  SC_CHECK(g.activePiece().x == 5);

  g.tick(holdRight());  // tick 13: two ticks later, the next repeat
  SC_CHECK(g.activePiece().x == 6);
  runTicks(g, holdRight(), 2);
  SC_CHECK(g.activePiece().x == 7);  // tick 15

  // x = 7 puts the T against the right wall; repeats keep failing there.
  runTicks(g, holdRight(), 25);
  SC_CHECK(g.activePiece().x == 7);
  SC_CHECK(maxCellX(g.activePiece()) == stackfall::kBoardWidth - 1);
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.board().settledCellCount() == 0);
}

void test_opposite_directions_cancel() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::T, Rotation::Spawn, 3, 0});

  runTicks(g, holdLeft(), 3);  // the press tick moved once, to x = 2
  SC_CHECK(g.activePiece().x == 2);

  runTicks(g, holdBoth(), 12);  // both held: no movement at all
  SC_CHECK(g.activePiece().x == 2);
  SC_CHECK(stackfall::GameTestPeer::shiftDir(g) == 0);
  SC_CHECK(stackfall::GameTestPeer::shiftTicks(g) == 0);

  // Releasing left while right stays down is a fresh right press: it moves
  // immediately and restarts the charge.
  g.tick(holdRight());
  SC_CHECK(g.activePiece().x == 3);
  SC_CHECK(stackfall::GameTestPeer::shiftTicks(g) == 0);
}

void test_direction_switch_resets_the_charge() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::T, Rotation::Spawn, 3, 0});

  runTicks(g, holdLeft(), 9);  // x = 2 after the press tick, 8 ticks charged
  SC_CHECK(g.activePiece().x == 2);
  SC_CHECK(stackfall::GameTestPeer::shiftTicks(g) == 8);

  // Switching direction on the next tick moves immediately rather than
  // finishing the old charge.
  g.tick(holdRight());
  SC_CHECK(g.activePiece().x == 3);
  SC_CHECK(stackfall::GameTestPeer::shiftDir(g) == 1);
  SC_CHECK(stackfall::GameTestPeer::shiftTicks(g) == 0);

  // Releasing both directions parks the charge until a new press.
  g.tick(InputFrame::idle());
  SC_CHECK(g.activePiece().x == 3);
  SC_CHECK(stackfall::GameTestPeer::shiftDir(g) == 0);
}

// ---------------------------------------------------------------------------
// Walls, section 11 item 4.
// ---------------------------------------------------------------------------

void test_movement_stops_at_walls_in_every_orientation() {
  for (const PieceId id : stackfall::kAllPieceIds) {
    for (const Rotation rotation : stackfall::kAllRotations) {
      // Left wall: 40 held-left ticks reach the wall at DAS/ARR speed, and
      // level 1 gravity moves nothing inside 40 ticks.
      Game left;
      left.start(kSeed);
      stackfall::GameTestPeer::setPiece(left, Piece{id, rotation, 3, 0});
      runTicks(left, holdLeft(), 40);
      const Piece& atLeft = left.activePiece();
      SC_CHECK(minCellX(atLeft) == 0);
      SC_CHECK(left.hasActivePiece());
      const int parkedX = atLeft.x;
      runTicks(left, holdLeft(), 10);
      SC_CHECK(left.activePiece().x == parkedX);  // repeats fail at the wall
      SC_CHECK(minCellX(left.activePiece()) == 0);

      // Right wall, the same way.
      Game right;
      right.start(kSeed);
      stackfall::GameTestPeer::setPiece(right, Piece{id, rotation, 3, 0});
      runTicks(right, holdRight(), 40);
      const Piece& atRight = right.activePiece();
      SC_CHECK(maxCellX(atRight) == stackfall::kBoardWidth - 1);
      SC_CHECK(right.hasActivePiece());
      const int parkedRightX = atRight.x;
      runTicks(right, holdRight(), 10);
      SC_CHECK(right.activePiece().x == parkedRightX);
      SC_CHECK(maxCellX(right.activePiece()) == stackfall::kBoardWidth - 1);

      // Every cell of every orientation stayed inside the ten columns.
      for (const Cell& cell : stackfall::boardCells(atLeft)) {
        SC_CHECK(stackfall::inBoardX(cell.x));
      }
      for (const Cell& cell : stackfall::boardCells(atRight)) {
        SC_CHECK(stackfall::inBoardX(cell.x));
      }
      // Nothing locked: the piece never touched the floor.
      SC_CHECK(left.board().settledCellCount() == 0);
      SC_CHECK(right.board().settledCellCount() == 0);
    }
  }
}

// ---------------------------------------------------------------------------
// Lock delay, section 11 item 10.
// ---------------------------------------------------------------------------

void test_ground_locks_after_thirty_ticks() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 18});
  SC_CHECK(maxCellY(g.activePiece()) == stackfall::kBoardHeight - 1);

  runTicks(g, InputFrame::idle(), 29);
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.board().settledCellCount() == 0);  // not one tick early
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 29);
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 0);

  g.tick(InputFrame::idle());  // the thirtieth grounded tick locks
  SC_CHECK(g.board().settledCellCount() == 4);
  SC_CHECK(g.hasActivePiece());  // the next piece is already in play
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 0);
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 0);
}

void test_lock_delay_resets_cap_at_fifteen() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 2, 18});

  // Alternating directions makes every tick a fresh press, so every tick
  // moves the grounded piece once and spends one lock-delay reset.
  for (int tick = 0; tick < 15; ++tick) {
    g.tick(tick % 2 == 0 ? holdLeft() : holdRight());
    SC_CHECK(g.hasActivePiece());
    SC_CHECK(stackfall::GameTestPeer::lockResets(g) == tick + 1);
    SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 1);
  }

  // The sixteenth successful move changes nothing: the budget is spent, so
  // the grounded streak keeps growing.
  g.tick(holdLeft());
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.activePiece().x == 1);  // the move itself still worked
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 15);
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 2);

  // Further moves keep working and the piece still locks. The streak stood at
  // one after the sixteenth tick, so the thirtieth grounded tick is tick 44.
  for (int tick = 0; tick < 27; ++tick) {  // ticks 17..43: streak 3..29
    g.tick(tick % 2 == 0 ? holdRight() : holdLeft());
    SC_CHECK(g.hasActivePiece());
    SC_CHECK(g.board().settledCellCount() == 0);
    SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 15);
  }
  g.tick(holdRight());  // tick 44: the lock fires mid-move-sequence
  SC_CHECK(g.board().settledCellCount() == 4);
  // The counter belongs to the piece, so the spawned piece starts at zero.
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 0);
}

void test_failed_input_never_resets_lock_delay() {
  Game g;
  g.start(kSeed);
  // x = -1 puts the O against the left wall: cells in columns 0 and 1.
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, -1, 18});

  runTicks(g, holdLeft(), 29);  // every move fails at the wall
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.activePiece().x == -1);
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 0);
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 29);

  g.tick(holdLeft());
  SC_CHECK(g.board().settledCellCount() == 4);  // locked on tick 30
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 0);
}

void test_airborne_move_clears_the_grounded_streak() {
  // A piece that slides off a ledge becomes airborne: the streak clears and
  // the reset budget stays spent (specification section 4.5).
  Game g;
  g.start(kSeed);
  // A ledge three columns wide: a T resting on it stays grounded until its
  // whole bottom row clears the ledge, because one supported cell is enough
  // to block the downward move.
  Board& cells = stackfall::GameTestPeer::board(g);
  for (int x = 0; x < 3; ++x) {
    cells.settleCell(x, stackfall::kBoardHeight - 1, PieceId::J);
  }
  // The T spawn drawing keeps its bottom row in board row y + 1, so it rests
  // at y = 17 on the ledge.
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::T, Rotation::Spawn, 0, 17});
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 0);

  runTicks(g, InputFrame::idle(), 3);  // grounded on the ledge
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 3);

  // Two grounded slides (the press tick and the first DAS repeat) each spend
  // a lock-delay reset; the third slide lifts the piece clear of the ledge.
  runTicks(g, holdRight(), 13);
  SC_CHECK(g.activePiece().x == 3);
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(g) == 0);
  SC_CHECK(stackfall::GameTestPeer::lockResets(g) == 2);

  // It falls again on the next natural gravity step, which proves the move
  // really lifted it.
  runTicks(g, InputFrame::idle(), stackfall::gravityTicksForLevel(1));
  SC_CHECK(g.activePiece().y == 18);
}

// ---------------------------------------------------------------------------
// Hold, section 11 item 14.
// ---------------------------------------------------------------------------

void test_hold_once_per_piece() {
  Game g;
  g.start(kSeed);
  const PieceId first = g.activePiece().id;
  const std::array<PieceId, stackfall::kPreviewCount> queueBefore = g.queue();

  g.tick(pressHold());
  SC_CHECK(g.holdLocked());
  SC_CHECK(g.hasHoldPiece());
  SC_CHECK(g.holdPieceId() == first);
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(samePiece(g.activePiece(),
                     stackfall::makeSpawnPiece(queueBefore[0])));

  // The hold drew one piece, so the queue matches a Randomizer that drew two.
  stackfall::Randomizer reference(kSeed);
  reference.nextPiece();
  reference.nextPiece();
  SC_CHECK(g.queue() == reference.queue());

  // A second hold on the same piece changes nothing.
  const Piece stayed = g.activePiece();
  const std::array<PieceId, stackfall::kPreviewCount> queueStayed = g.queue();
  g.tick(pressHold());
  SC_CHECK(g.holdLocked());
  SC_CHECK(samePiece(g.activePiece(), stayed));
  SC_CHECK(g.queue() == queueStayed);

  // A lock re-enables hold and the next hold works again.
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 10});
  g.tick(pressHardDrop());
  SC_CHECK(!g.holdLocked());
  const PieceId beforeSecondHold = g.activePiece().id;
  g.tick(pressHold());
  SC_CHECK(g.holdLocked());
  SC_CHECK(g.holdPieceId() == beforeSecondHold);
  SC_CHECK(samePiece(g.activePiece(),
                     stackfall::makeSpawnPiece(first)));
}

void test_hold_swap_resets_rotation_and_position() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setHoldSlot(g, PieceId::T);
  // A moved and rotated piece, nothing like its spawn state.
  stackfall::GameTestPeer::setPiece(
      g, Piece{PieceId::J, Rotation::HalfTurn, 6, 10});
  g.tick(pressHold());
  SC_CHECK(g.holdPieceId() == PieceId::J);
  SC_CHECK(samePiece(g.activePiece(),
                     stackfall::makeSpawnPiece(PieceId::T)));
  SC_CHECK(g.activePiece().rotation == Rotation::Spawn);
}

void test_hold_blocked_spawn_ends_the_round() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setHoldSlot(g, PieceId::I);
  blockSpawnCells(g, PieceId::I);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 10});
  g.tick(pressHold());
  SC_CHECK(g.scene() == Scene::GameOver);
  SC_CHECK(!g.hasActivePiece());
  SC_CHECK(g.holdPieceId() == PieceId::O);  // the swap still stored the piece
  SC_CHECK(g.board().settledCellCount() == 4);  // only the blocked spawn cells
}

// ---------------------------------------------------------------------------
// Top-out, section 11 item 15.
// ---------------------------------------------------------------------------

void test_block_out_on_failed_spawn() {
  Game g;
  g.start(kSeed);
  const PieceId upcoming = g.queue()[0];
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 10});
  blockSpawnCells(g, upcoming);
  g.tick(pressHardDrop());

  // The lock itself worked; the spawn that followed it failed, and the round
  // ended inside this tick.
  SC_CHECK(g.scene() == Scene::GameOver);
  SC_CHECK(!g.hasActivePiece());
  SC_CHECK(g.board().settledCellCount() == 8);  // four blockers plus the O
  SC_CHECK(g.score() == 16);
  SC_CHECK(g.lines() == 0);

  // A finished round ignores input from here on.
  const std::uint64_t frozenScore = g.score();
  runTicks(g, holdDown(), 50);
  SC_CHECK(g.scene() == Scene::GameOver);
  SC_CHECK(g.score() == frozenScore);
  SC_CHECK(g.board().settledCellCount() == 8);
}

void test_lock_out_on_hidden_row_cells() {
  Game g;
  g.start(kSeed);
  const std::array<PieceId, stackfall::kPreviewCount> queueBefore = g.queue();
  // Supports one row below the field: a piece landing on them keeps cells in
  // the hidden rows.
  Board& cells = stackfall::GameTestPeer::board(g);
  cells.settleCell(5, 1, PieceId::J);
  cells.settleCell(6, 1, PieceId::J);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, -2});
  g.tick(pressHardDrop());

  SC_CHECK(g.scene() == Scene::GameOver);
  SC_CHECK(!g.hasActivePiece());
  SC_CHECK(g.board().isSettled(5, -1));
  SC_CHECK(g.board().isSettled(6, -1));
  SC_CHECK(g.board().isSettled(5, 0));
  SC_CHECK(g.board().isSettled(6, 0));
  SC_CHECK(g.score() == 2);  // one cell travelled
  SC_CHECK(g.lines() == 0);  // the round ended before any clear
  SC_CHECK(g.queue() == queueBefore);  // nothing spawned afterwards
  SC_CHECK(g.board().isConsistent());
}

void test_top_out_is_deterministic() {
  // Soft drop held forever stacks the board until the round ends.
  Game a;
  a.start(424242ULL);
  int ticks = 0;
  while (a.scene() == Scene::Playing) {
    a.tick(holdDown());
    ++ticks;
    SC_CHECK(ticks < 20000);
  }
  SC_CHECK(a.scene() == Scene::GameOver);

  Game b;
  b.start(424242ULL);
  for (int i = 0; i < ticks; ++i) {
    b.tick(holdDown());
  }
  SC_CHECK(b.scene() == Scene::GameOver);
  SC_CHECK(sameState(a, b));

  // A different seed also ends, on its own schedule.
  Game c;
  c.start(987651ULL);
  int otherTicks = 0;
  while (c.scene() == Scene::Playing) {
    c.tick(holdDown());
    ++otherTicks;
    SC_CHECK(otherTicks < 20000);
  }
  SC_CHECK(c.scene() == Scene::GameOver);
}

// ---------------------------------------------------------------------------
// Clear scoring and levels, section 11 item 16.
// ---------------------------------------------------------------------------

void test_clear_scoring_for_every_quantity() {
  const struct {
    int rows;
    std::uint64_t base;
  } cases[] = {{1, 100}, {2, 300}, {3, 500}, {4, 800}};
  for (const auto& c : cases) {
    Game g;
    g.start(kSeed);
    for (int i = 0; i < c.rows; ++i) {
      fillRowExcept(g, stackfall::kBoardHeight - 1 - i, 3);
    }
    stackfall::GameTestPeer::setPiece(g, verticalI(3, 8));
    g.tick(pressHardDrop());

    SC_CHECK(g.lines() == static_cast<std::uint64_t>(c.rows));
    SC_CHECK(g.level() == 1);
    SC_CHECK(g.score() == 16 + c.base);  // drop points plus the level 1 clear
    SC_CHECK(g.board().fullRowCount() == 0);
    SC_CHECK(g.board().isConsistent());
    // Four locked cells minus the ten cells each cleared row carried away.
    SC_CHECK(g.board().settledCellCount() == 4 - c.rows);
    SC_CHECK(g.hasActivePiece());
  }
}

void test_clear_scores_at_the_pre_clear_level() {
  // Nine lines: level 1. The single clear scores 100 and lifts the round to
  // level 2 after the fact.
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setLines(g, 9);
  fillRowExcept(g, stackfall::kBoardHeight - 1, 3);
  stackfall::GameTestPeer::setPiece(g, verticalI(3, 8));
  g.tick(pressHardDrop());
  SC_CHECK(g.lines() == 10);
  SC_CHECK(g.level() == 2);
  SC_CHECK(g.score() == 16 + 100 * 1);

  // Ten lines: level 2, and the same single clear now scores 200.
  Game h;
  h.start(kSeed);
  stackfall::GameTestPeer::setLines(h, 10);
  fillRowExcept(h, stackfall::kBoardHeight - 1, 3);
  stackfall::GameTestPeer::setPiece(h, verticalI(3, 8));
  h.tick(pressHardDrop());
  SC_CHECK(h.lines() == 11);
  SC_CHECK(h.level() == 2);
  SC_CHECK(h.score() == 16 + 100 * 2);

  // A four-line clear at the level cap.
  Game f;
  f.start(kSeed);
  stackfall::GameTestPeer::setLines(f, 190);
  for (int i = 0; i < 4; ++i) {
    fillRowExcept(f, stackfall::kBoardHeight - 1 - i, 3);
  }
  stackfall::GameTestPeer::setPiece(f, verticalI(3, 8));
  f.tick(pressHardDrop());
  SC_CHECK(f.lines() == 194);
  SC_CHECK(f.level() == 20);
  SC_CHECK(f.score() == 16 + 800 * 20);
}

void test_level_boundaries() {
  const struct {
    std::uint64_t lines;
    int level;
  } cases[] = {{0, 1},  {1, 1},   {9, 1},    {10, 2},   {11, 2},
               {19, 2}, {20, 3},  {109, 11}, {189, 19}, {190, 20},
               {199, 20}, {200, 20}, {100000, 20}};
  for (const auto& c : cases) {
    SC_CHECK(stackfall::levelForLines(c.lines) == c.level);
  }

  // Scoring continues past the cap while speed stops increasing: a round deep
  // past level 20 still takes one tick per cell and pays the level 20 rate.
  SC_CHECK(stackfall::gravityTicksForLevel(
               stackfall::levelForLines(500)) == 1);
  SC_CHECK(stackfall::clearPoints(1, stackfall::levelForLines(500)) == 100 * 20);
  SC_CHECK(stackfall::clearBasePoints(0) == 0);
  SC_CHECK(stackfall::clearBasePoints(5) == 0);  // no piece clears five rows
}

// ---------------------------------------------------------------------------
// Pause and restart, section 11 item 9 (second half).
// ---------------------------------------------------------------------------

void test_pause_freezes_every_counter() {
  Game a;
  a.start(kSeed);
  Game b;
  b.start(kSeed);
  runTicks(a, holdDown(), 12);
  runTicks(b, holdDown(), 12);
  SC_CHECK(sameState(a, b));

  b.tick(pressPause());
  SC_CHECK(b.scene() == Scene::Paused);
  const Piece frozen = b.activePiece();
  const std::uint64_t frozenScore = b.score();
  const int frozenGravity = stackfall::GameTestPeer::gravityTimer(b);

  // Held input and edges while paused change nothing at all.
  InputFrame busy = holdDown();
  busy.hardDropPressed = true;
  busy.pausePressed = false;
  runTicks(b, busy, 120);
  SC_CHECK(b.scene() == Scene::Paused);
  SC_CHECK(samePiece(b.activePiece(), frozen));
  SC_CHECK(b.score() == frozenScore);
  SC_CHECK(stackfall::GameTestPeer::gravityTimer(b) == frozenGravity);
  SC_CHECK(stackfall::GameTestPeer::groundedTicks(b) == 0);
  SC_CHECK(b.board().settledCellCount() == 0);
  SC_CHECK(b.tickCount() == 12);

  b.tick(pressPause());  // resume; the tick only switches the scene
  SC_CHECK(b.scene() == Scene::Playing);
  SC_CHECK(sameState(a, b));

  // The paused stretch left no trace: both rounds stay the same game.
  for (int i = 0; i < 200; ++i) {
    const InputFrame f = i % 3 == 0 ? holdDown() : InputFrame::idle();
    a.tick(f);
    b.tick(f);
  }
  SC_CHECK(sameState(a, b));
}

void test_restart_resets_the_round() {
  Game g;
  g.start(kSeed);
  // Soft drop stacks pieces until at least one of them locks.
  runTicks(g, holdDown(), 80);
  SC_CHECK(g.board().settledCellCount() > 0);
  g.tick(pressPause());
  SC_CHECK(g.scene() == Scene::Paused);
  g.tick(pressHold());      // paused: ignored
  g.tick(pressHardDrop());  // paused: ignored
  SC_CHECK(g.scene() == Scene::Paused);

  g.start(kSeed + 7);  // restart while paused begins a fresh round
  Game fresh;
  fresh.start(kSeed + 7);
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(sameState(g, fresh));

  // The restarted round plays exactly like the fresh one.
  for (int i = 0; i < 120; ++i) {
    const InputFrame f = i % 2 == 0 ? holdDown() : InputFrame::idle();
    g.tick(f);
    fresh.tick(f);
  }
  SC_CHECK(sameState(g, fresh));
}

// ---------------------------------------------------------------------------
// Input processing order, section 5.
// ---------------------------------------------------------------------------

void test_pause_beats_every_other_action() {
  Game g;
  g.start(kSeed);
  const Piece before = g.activePiece();
  InputFrame f = pressPause();
  f.hardDropPressed = true;
  f.leftHeld = true;
  f.downHeld = true;
  f.rotateCWPressed = true;
  g.tick(f);
  SC_CHECK(g.scene() == Scene::Paused);
  SC_CHECK(samePiece(g.activePiece(), before));
  SC_CHECK(g.board().settledCellCount() == 0);
  SC_CHECK(g.score() == 0);
  SC_CHECK(g.tickCount() == 0);

  g.tick(pressPause());  // resume
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.board().settledCellCount() == 0);  // the queued drop never ran
  SC_CHECK(samePiece(g.activePiece(), before));
}

void test_hold_suppresses_the_other_actions_of_its_tick() {
  Game g;
  g.start(kSeed);
  const std::array<PieceId, stackfall::kPreviewCount> queueBefore = g.queue();
  InputFrame f = pressHold();
  f.rotateCWPressed = true;
  f.hardDropPressed = true;
  f.leftHeld = true;
  f.downHeld = true;
  g.tick(f);
  SC_CHECK(g.holdLocked());
  SC_CHECK(g.hasHoldPiece());
  // The returned piece is untouched: spawn state, no fall, no lock, no points.
  SC_CHECK(samePiece(g.activePiece(),
                     stackfall::makeSpawnPiece(queueBefore[0])));
  SC_CHECK(g.board().settledCellCount() == 0);
  SC_CHECK(g.score() == 0);
}

void test_rotation_then_move_then_hard_drop_in_one_tick() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::T, Rotation::Spawn, 3, 0});
  InputFrame f = pressRotateCW();
  f.leftHeld = true;
  f.hardDropPressed = true;
  g.tick(f);

  // Rotated, shifted left once, dropped, and locked: all inside this tick.
  SC_CHECK(g.board().settledCellCount() == 4);
  SC_CHECK(g.board().isSettled(3, 17));
  SC_CHECK(g.board().isSettled(3, 18));
  SC_CHECK(g.board().isSettled(4, 18));
  SC_CHECK(g.board().isSettled(3, 19));
  SC_CHECK(g.score() == 2 * 17);
  SC_CHECK(g.hasActivePiece());
}

void test_ghost_piece_follows_the_landing_row() {
  Game g;
  g.start(kSeed);
  stackfall::GameTestPeer::setPiece(g, Piece{PieceId::O, Rotation::Spawn, 4, 6});
  const Piece ghost = g.ghostPiece();
  SC_CHECK(ghost.x == 4);
  SC_CHECK(ghost.rotation == Rotation::Spawn);
  SC_CHECK(ghost.y == 18);
  SC_CHECK(maxCellY(ghost) == stackfall::kBoardHeight - 1);

  // The ghost of a grounded piece is the piece itself.
  stackfall::GameTestPeer::setPiece(g,
                                    Piece{PieceId::O, Rotation::Spawn, 4, 18});
  SC_CHECK(samePiece(g.ghostPiece(), g.activePiece()));
}

// ---------------------------------------------------------------------------
// Long randomized session: invariants and replay determinism.
// ---------------------------------------------------------------------------

std::uint32_t fuzzStep(std::uint32_t& state) {
  // A plain numerical script source, independent of the game's PCG32.
  state = state * 1664525u + 1013904223u;
  return state >> 8;
}

InputFrame fuzzFrame(std::uint32_t& state) {
  const std::uint32_t bits = fuzzStep(state);
  InputFrame f;
  f.leftHeld = (bits & 0x01u) != 0;
  f.rightHeld = (bits & 0x02u) != 0;
  f.downHeld = (bits & 0x04u) != 0;
  f.rotateCWPressed = (bits & 0x08u) != 0;
  f.rotateCCWPressed = (bits & 0x10u) != 0;
  f.hardDropPressed = (bits & 0x20u) != 0;
  f.holdPressed = (bits & 0x40u) != 0;
  f.pausePressed = (bits & 0x80u) != 0;
  return f;
}

void test_long_random_session_stays_consistent() {
  std::uint32_t state = 1;
  Game a;
  a.start(987654321ULL);
  Game b;
  b.start(987654321ULL);
  bool ended = false;
  std::uint64_t ticksAtEnd = 0;

  for (int tick = 0; tick < 3000; ++tick) {
    const InputFrame f = fuzzFrame(state);
    a.tick(f);
    b.tick(f);

    SC_CHECK(a.board().isConsistent());
    if (a.scene() == Scene::Playing) {
      SC_CHECK(a.hasActivePiece());
      // The active piece is never inside a wall or a settled cell.
      SC_CHECK(a.board().canPlace(a.activePiece()));
      for (const Cell& cell : stackfall::boardCells(a.activePiece())) {
        SC_CHECK(stackfall::inBoard(cell));
      }
    }
    if (a.scene() == Scene::GameOver && !ended) {
      ended = true;
      ticksAtEnd = a.tickCount();
    }
    if (ended) {
      // A finished round is frozen forever.
      SC_CHECK(a.scene() == Scene::GameOver);
      SC_CHECK(a.tickCount() == ticksAtEnd);
    }
  }
  SC_CHECK(ended);  // the session reached a top-out
  SC_CHECK(sameState(a, b));
}

}  // namespace

void run_game_tests() {
  std::printf("test_game: state machine and deterministic tick\n");
  test_start_opens_a_fresh_round();
  test_same_seed_same_input_same_round();
  test_gravity_table_is_pinned();
  test_gravity_moves_exactly_when_scheduled();
  test_soft_drop_beats_gravity_and_scores_cells();
  test_hard_drop_lands_and_locks_in_the_same_tick();
  test_hard_drop_of_a_grounded_piece_scores_nothing();
  test_soft_and_hard_drop_point_accounting();
  test_das_and_arr_repeat();
  test_opposite_directions_cancel();
  test_direction_switch_resets_the_charge();
  test_movement_stops_at_walls_in_every_orientation();
  test_ground_locks_after_thirty_ticks();
  test_lock_delay_resets_cap_at_fifteen();
  test_failed_input_never_resets_lock_delay();
  test_airborne_move_clears_the_grounded_streak();
  test_hold_once_per_piece();
  test_hold_swap_resets_rotation_and_position();
  test_hold_blocked_spawn_ends_the_round();
  test_block_out_on_failed_spawn();
  test_lock_out_on_hidden_row_cells();
  test_top_out_is_deterministic();
  test_clear_scoring_for_every_quantity();
  test_clear_scores_at_the_pre_clear_level();
  test_level_boundaries();
  test_pause_freezes_every_counter();
  test_restart_resets_the_round();
  test_pause_beats_every_other_action();
  test_hold_suppresses_the_other_actions_of_its_tick();
  test_rotation_then_move_then_hard_drop_in_one_tick();
  test_ghost_piece_follows_the_landing_row();
  test_long_random_session_stays_consistent();
}
