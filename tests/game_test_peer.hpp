#pragma once

// GameTestPeer: the declared test route into the private state of Game. The
// declaration lives in game.hpp; the test suites own this definition, so every
// suite that needs private access includes this header and the program keeps
// exactly one definition. Production code has no route to it, so the public
// Game interface stays read-only for the renderer (specification sections 5
// and 7).

#include "stackfall/board.hpp"
#include "stackfall/game.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/randomizer.hpp"
#include "stackfall/scoring.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

struct GameTestPeer {
  static Board& board(Game& g) { return g.board_; }

  // Puts a chosen piece in play with fresh per-piece counters.
  static void setPiece(Game& g, const Piece& piece) {
    g.active_ = piece;
    g.hasActive_ = true;
    g.gravityTimer_ = 0;
    g.groundedTicks_ = 0;
    g.lockResets_ = 0;
  }

  static void setHoldSlot(Game& g, PieceId id) {
    g.holdId_ = id;
    g.hasHold_ = true;
  }

  static void setHoldLocked(Game& g, bool locked) { g.holdLocked_ = locked; }

  // Sets the cleared-line total and the level it implies.
  static void setLines(Game& g, std::uint64_t lines) {
    g.lines_ = lines;
    g.level_ = levelForLines(lines);
  }

  // Sets the private tick counters of section 4.5 in one call, so the
  // regression suite can prove that the state hash folds them in.
  static void setTickCounters(Game& g, int gravityTimer, int groundedTicks,
                              int lockResets, int shiftDir, int shiftTicks) {
    g.gravityTimer_ = gravityTimer;
    g.groundedTicks_ = groundedTicks;
    g.lockResets_ = lockResets;
    g.shiftDir_ = shiftDir;
    g.shiftTicks_ = shiftTicks;
  }

  static int gravityTimer(const Game& g) { return g.gravityTimer_; }
  static int groundedTicks(const Game& g) { return g.groundedTicks_; }
  static int lockResets(const Game& g) { return g.lockResets_; }
  static int shiftDir(const Game& g) { return g.shiftDir_; }
  static int shiftTicks(const Game& g) { return g.shiftTicks_; }

  // The piece source of the round, for tests and the state hash of the
  // regression suite. Randomizer itself exposes its queue, draw count, and
  // generator state.
  static const Randomizer& randomizer(const Game& g) { return g.randomizer_; }
};

}  // namespace stackfall
