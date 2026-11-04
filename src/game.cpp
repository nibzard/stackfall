// The deterministic game state machine of the STACKFALL rules core
// (specification sections 4.5, 4.7, and 5).
//
// tick() is the only writer of the round and it runs the stages of section 5
// in order, so the same input stream always walks the same states. Locking is
// the one path that changes the board, and it runs the same steps for a hard
// drop and for an expired lock delay: commit, clear, score, re-level, spawn.

#include "stackfall/game.hpp"

#include <cassert>

#include "stackfall/scoring.hpp"

namespace stackfall {
namespace {

// One step of the SplitMix64 mixer over the session seed, in pure integer
// arithmetic inside the core. Start Game, Restart, and Play Again draw their
// seeds from this stream, so every fresh round differs while the same
// openTitle seed plus the same TickInput stream still walks the same rounds
// on every build (specification sections 4.3 and 7).
std::uint64_t advanceSeed(std::uint64_t seed) {
  seed += 0x9E3779B97F4A7C15ULL;
  seed = (seed ^ (seed >> 30)) * 0xBF58476D1CE4E5B9ULL;
  seed = (seed ^ (seed >> 27)) * 0x94D049BB133111EBULL;
  return seed ^ (seed >> 31);
}

}  // namespace

Game::Game() : randomizer_{0}, active_{makeSpawnPiece(PieceId::I)} {}

void Game::resetRound(std::uint64_t seed) {
  // Every round field returns to its opening value, so a restart is
  // indistinguishable from a fresh Game with the same seed.
  seed_ = seed;
  board_.clear();
  randomizer_.reset(seed);
  active_ = makeSpawnPiece(PieceId::I);
  hasActive_ = false;
  holdId_ = PieceId::I;
  hasHold_ = false;
  holdLocked_ = false;
  score_ = 0;
  lines_ = 0;
  level_ = kMinLevel;
  tickCount_ = 0;
  gravityTimer_ = 0;
  groundedTicks_ = 0;
  lockResets_ = 0;
  shiftDir_ = 0;
  shiftTicks_ = 0;
  pauseCursor_ = 0;
  overCursor_ = 0;
  newRecord_ = false;
}

void Game::openTitle(std::uint64_t seed) {
  nextSeed_ = seed;
  resetRound(seed);
  titleCursor_ = 0;
  optionsCursor_ = 0;
  quitRequested_ = false;
  scene_ = Scene::Title;
}

void Game::start(std::uint64_t seed) {
  resetRound(seed);
  scene_ = Scene::Playing;
  spawnNextPiece();
}

// ---------------------------------------------------------------------------
// The tick, in the input processing order of section 5.
// ---------------------------------------------------------------------------

void Game::tick(const InputFrame& input) {
  // Scenes outside Playing and Paused take no gameplay input; their menus run
  // through tick(TickInput), the session path of the section 4.7 screens.
  if (scene_ != Scene::Playing && scene_ != Scene::Paused) {
    return;
  }

  // The pause command comes first, and the tick that switches the scene
  // carries no gameplay action in either direction.
  if (input.pausePressed) {
    if (scene_ == Scene::Playing) {
      scene_ = Scene::Paused;
      pauseCursor_ = 0;  // Resume is the default item of the pause overlay
    } else {
      scene_ = Scene::Playing;
    }
    return;
  }
  if (scene_ != Scene::Playing) {
    return;  // paused: every counter stays frozen (section 4.5)
  }
  assert(hasActive_ && "a playing round always has a piece");
  ++tickCount_;

  // Hold is one action per locked piece. When it fires, every other gameplay
  // action of this tick is skipped (section 5).
  const bool holdUsed = input.holdPressed && !holdLocked_;
  if (holdUsed) {
    handleHold();
  }
  if (scene_ != Scene::Playing) {
    return;  // the hold swap blocked out
  }

  if (!holdUsed) {
    // Rotation before movement; a clockwise edge wins a tie with a
    // counter-clockwise edge on the same tick.
    if (input.rotateCWPressed) {
      tryRotate(true);
    }
    if (input.rotateCCWPressed) {
      tryRotate(false);
    }
    handleHorizontalShift(input);

    if (input.hardDropPressed) {
      // A hard drop ends the piece actions of this tick.
      handleHardDrop();
    } else if (input.downHeld) {
      // Soft drop takes priority over natural gravity (section 4.5).
      gravityTimer_ = 0;
      if (tryFall()) {
        score_ = addScore(score_, kSoftDropPointsPerCell);
      }
    } else {
      // Natural gravity: one cell when the level timer runs out, whatever the
      // attempt does to the piece.
      ++gravityTimer_;
      if (gravityTimer_ >= gravityTicksForLevel(level_)) {
        gravityTimer_ = 0;
        tryFall();
      }
    }
  }
  if (scene_ != Scene::Playing) {
    return;  // the drop locked the round out
  }

  // Ground and lock bookkeeping closes the tick. It also sees a piece that a
  // hold or a hard drop spawned during this tick, so a piece born grounded
  // starts its lock delay on its first tick.
  handleGroundAndLock();
}

// ---------------------------------------------------------------------------
// The section 4.7 screens. Each tick routes to the screen in charge, which
// picks its half of the input (stackfall/input.hpp): the menus read the menu
// edges, the pause overlay also reads the pause key, and the round reads the
// gameplay frame alone.
// ---------------------------------------------------------------------------

void Game::tick(const TickInput& input) {
  switch (scene_) {
    case Scene::Boot:
      return;  // nothing runs before the platform layer opens the title
    case Scene::Title:
      tickTitle(input.menu);
      return;
    case Scene::Controls:
      tickControls(input.menu);
      return;
    case Scene::Options:
      tickOptions(input.menu);
      return;
    case Scene::PauseControls:
      tickPauseControls(input.menu);
      return;
    case Scene::Paused:
      tickPaused(input);
      return;
    case Scene::GameOver:
      tickGameOver(input.menu);
      return;
    case Scene::Playing:
      break;
  }
  tick(input.game);
}

void Game::tickTitle(const MenuFrame& menu) {
  moveCursor(titleCursor_, kTitleItemCount, menu);
  if (!menu.selectPressed) {
    return;
  }
  switch (titleCursor_) {
    case 0:
      beginRound();  // Start Game
      break;
    case 1:
      scene_ = Scene::Controls;
      break;
    case 2:
      scene_ = Scene::Options;
      break;
    default:
      quitRequested_ = true;  // Quit: the platform layer exits
      break;
  }
}

void Game::tickControls(const MenuFrame& menu) {
  // Confirm and back both return to the title (specification section 4.7).
  // The cursor stays on Controls, so B followed by A reopens the screen.
  if (menu.selectPressed || menu.backPressed) {
    scene_ = Scene::Title;
  }
}

void Game::tickOptions(const MenuFrame& menu) {
  // Back returns to the title; the title cursor stays on the OPTIONS item,
  // so B followed by A reopens the screen on the same row. A select changes
  // nothing here: the row values are platform state the core never holds
  // (specification section 7), so the platform layer reads the highlighted
  // row and applies the change itself. The cursor wraps over the six rows
  // (task T044).
  if (menu.backPressed) {
    scene_ = Scene::Title;
    return;
  }
  moveCursor(optionsCursor_, kOptionsItemCount, menu);
}

void Game::tickPaused(const TickInput& input) {
  // Back leaves the overlay and resumes; then the D-pad moves the cursor and
  // A runs the highlighted item; the pause key alone still resumes
  // (specification sections 4.7 and 5). Everything else changes nothing: the
  // round below stays frozen exactly (section 4.5).
  if (input.menu.backPressed) {
    scene_ = Scene::Playing;
    return;
  }
  moveCursor(pauseCursor_, kPauseItemCount, input.menu);
  if (input.menu.selectPressed) {
    switch (pauseCursor_) {
      case 0:
        scene_ = Scene::Playing;  // Resume
        return;
      case 1:
        beginRound();  // Restart: a fresh seeded round
        return;
      case 2:
        // Controls: the mappings screen over the frozen round (task T037).
        // The round and every counter stay exactly as they are; back returns
        // to this overlay on the same item.
        scene_ = Scene::PauseControls;
        return;
      default:
        abandonToTitle();  // Quit to Title: the round is abandoned
        return;
    }
  }
  if (input.game.pausePressed) {
    scene_ = Scene::Playing;
  }
}

void Game::tickPauseControls(const MenuFrame& menu) {
  // Confirm and back both return to the pause overlay (specification section
  // 4.7). The cursor stays on the Controls item, so B followed by A reopens
  // the screen, and no counter of the frozen round below moves (task T037).
  if (menu.selectPressed || menu.backPressed) {
    scene_ = Scene::Paused;
  }
}

void Game::tickGameOver(const MenuFrame& menu) {
  moveCursor(overCursor_, kOverItemCount, menu);
  if (!menu.selectPressed) {
    return;
  }
  if (overCursor_ == 0) {
    beginRound();  // Play Again
  } else {
    abandonToTitle();  // Title
  }
}

void Game::moveCursor(int& cursor, int itemCount, const MenuFrame& menu) {
  if (menu.upPressed) {
    cursor = (cursor + itemCount - 1) % itemCount;
  }
  if (menu.downPressed) {
    cursor = (cursor + 1) % itemCount;
  }
}

void Game::beginRound() {
  start(nextSeed_);
  nextSeed_ = advanceSeed(nextSeed_);
}

void Game::abandonToTitle() {
  resetRound(nextSeed_);
  titleCursor_ = 0;
  scene_ = Scene::Title;
}

void Game::endRound() {
  scene_ = Scene::GameOver;
  overCursor_ = 0;
  // The record is decided once, at the end of the round: a better score
  // raises the session high score and lights the new-record note of the
  // game-over screen (specification section 4.7).
  newRecord_ = score_ > highScore_;
  if (newRecord_) {
    highScore_ = score_;
  }
}

// ---------------------------------------------------------------------------
// Hold, rotation, and movement.
// ---------------------------------------------------------------------------

void Game::handleHold() {
  // The spent hold is charged before anything can fail, and a lock is the only
  // event that re-enables it (specification sections 4.3 and 11 item 14).
  holdLocked_ = true;
  const PieceId stashed = active_.id;
  if (hasHold_) {
    const PieceId returned = holdId_;
    holdId_ = stashed;
    spawnPiece(returned);
  } else {
    holdId_ = stashed;
    hasHold_ = true;
    spawnNextPiece();
  }
}

bool Game::tryRotate(bool clockwise) {
  const bool wasGrounded = grounded();
  const RotationResult result = rotatePiece(
      active_, clockwise,
      [this](const Piece& candidate) { return board_.canPlace(candidate); });
  if (!result.rotated) {
    return false;  // every candidate failed; the piece stays unchanged
  }
  active_ = result.piece;
  applyLockDelayReset(wasGrounded);
  return true;
}

bool Game::tryMove(int dx) {
  const Piece candidate{active_.id, active_.rotation, active_.x + dx,
                        active_.y};
  if (!board_.canPlace(candidate)) {
    return false;
  }
  const bool wasGrounded = grounded();
  active_ = candidate;
  applyLockDelayReset(wasGrounded);
  return true;
}

void Game::handleHorizontalShift(const InputFrame& input) {
  const int direction = input.horizontalDirection();
  if (direction != shiftDir_) {
    // A fresh press or a reversal restarts the charge, and a fresh direction
    // moves immediately on its press tick (section 4.5). Left and right held
    // together read as no direction, which also parks the charge.
    shiftDir_ = direction;
    shiftTicks_ = 0;
    if (direction != 0) {
      tryMove(direction);
    }
    return;
  }
  if (direction == 0) {
    return;
  }
  // The direction is unchanged, so the charge grows by one tick; at kDasTicks
  // it pays out a step and keeps kArrTicks of charge, which repeats the step
  // every kArrTicks from there.
  ++shiftTicks_;
  if (shiftTicks_ >= kDasTicks) {
    shiftTicks_ -= kArrTicks;
    tryMove(direction);
  }
}

bool Game::tryFall() {
  const Piece candidate{active_.id, active_.rotation, active_.x,
                        active_.y + 1};
  if (!board_.canPlace(candidate)) {
    return false;
  }
  // The piece was airborne this tick, so its grounded streak restarts.
  active_ = candidate;
  groundedTicks_ = 0;
  return true;
}

// ---------------------------------------------------------------------------
// Hard drop, ground, and lock.
// ---------------------------------------------------------------------------

void Game::handleHardDrop() {
  const int distance = board_.maxDrop(active_);
  active_ = Piece{active_.id, active_.rotation, active_.x,
                  active_.y + distance};
  score_ = addScore(score_, kHardDropPointsPerCell *
                                static_cast<std::uint64_t>(distance));
  lockActivePiece();
}

void Game::handleGroundAndLock() {
  if (!grounded()) {
    return;
  }
  // The tick a piece lands on already counts as a grounded tick, because the
  // piece is grounded once its fall stops, so a lock lands 30 ticks after the
  // fall that grounded the piece, landing tick included.
  ++groundedTicks_;
  if (groundedTicks_ >= kLockDelayTicks) {
    lockActivePiece();
  }
}

void Game::applyLockDelayReset(bool wasGroundedBefore) {
  if (!grounded()) {
    // The action lifted the piece off its resting row: the streak clears and
    // the reset budget stays as it was (section 4.5).
    groundedTicks_ = 0;
    return;
  }
  if (wasGroundedBefore && lockResets_ < kLockDelayResets) {
    groundedTicks_ = 0;
    ++lockResets_;
  }
}

void Game::lockActivePiece() {
  const LockReport report = board_.lockPiece(active_);
  hasActive_ = false;
  // A lock re-enables the hold of section 4.3 whatever happens next.
  holdLocked_ = false;

  if (report.hiddenCells > 0) {
    // Lock-out: cells of the locked piece sit in the hidden rows, so the round
    // ends before any clear or spawn (specification section 4.5).
    endRound();
    return;
  }

  const int cleared = board_.clearFullRows();
  lines_ += static_cast<std::uint64_t>(cleared);
  // level_ still holds the pre-clear level here, which is the multiplier the
  // clear scores at (specification section 4.6).
  score_ = addScore(score_, clearPoints(cleared, level_));
  level_ = levelForLines(lines_);
  spawnNextPiece();
}

void Game::spawnPiece(PieceId id) {
  active_ = makeSpawnPiece(id);
  hasActive_ = true;
  gravityTimer_ = 0;
  groundedTicks_ = 0;
  lockResets_ = 0;
  if (!board_.canPlace(active_)) {
    // Block-out: the spawn cells are occupied. The piece stays in its spawn
    // state for the renderer but never enters the board.
    endRound();
    hasActive_ = false;
  }
}

void Game::spawnNextPiece() {
  spawnPiece(randomizer_.nextPiece());
}

bool Game::grounded() const {
  return board_.maxDrop(active_) == 0;
}

// ---------------------------------------------------------------------------
// Developer fixture hook (docs/AGENT_TESTING.md, T023).
// ---------------------------------------------------------------------------

bool Game::applyBoardFixture(const BoardFixture& fixture) {
  if (scene_ != Scene::Playing || !fixture.board.isConsistent()) {
    return false;
  }
  board_ = fixture.board;
  // The round keeps every counter the fixture did not name; only the piece
  // in play starts over, so a script's next taps meet a piece at spawn.
  shiftDir_ = 0;
  shiftTicks_ = 0;
  spawnPiece(fixture.hasActive ? fixture.active : active_.id);
  return true;
}

// ---------------------------------------------------------------------------
// Read-only state.
// ---------------------------------------------------------------------------

const Board& Game::board() const {
  return board_;
}

const Piece& Game::activePiece() const {
  return active_;
}

bool Game::hasActivePiece() const {
  return hasActive_;
}

Piece Game::ghostPiece() const {
  return Piece{active_.id, active_.rotation, active_.x,
               active_.y + board_.maxDrop(active_)};
}

const std::array<PieceId, kPreviewCount>& Game::queue() const {
  return randomizer_.queue();
}

bool Game::hasHoldPiece() const {
  return hasHold_;
}

PieceId Game::holdPieceId() const {
  return holdId_;
}

bool Game::holdLocked() const {
  return holdLocked_;
}

std::uint64_t Game::score() const {
  return score_;
}

std::uint64_t Game::lines() const {
  return lines_;
}

int Game::level() const {
  return level_;
}

Scene Game::scene() const {
  return scene_;
}

bool Game::quitRequested() const { return quitRequested_; }

int Game::titleSelection() const { return titleCursor_; }

int Game::pauseSelection() const { return pauseCursor_; }

int Game::overSelection() const { return overCursor_; }

int Game::optionsSelection() const { return optionsCursor_; }

std::uint64_t Game::highScore() const { return highScore_; }

void Game::setHighScore(std::uint64_t score) { highScore_ = score; }

bool Game::newRecord() const { return newRecord_; }

std::uint64_t Game::tickCount() const {
  return tickCount_;
}

std::uint64_t Game::seed() const {
  return seed_;
}

}  // namespace stackfall
