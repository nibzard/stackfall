#pragma once

// The deterministic game state machine of the STACKFALL rules core
// (specification sections 4.5, 4.7, and 5). A Game owns the board, the active
// piece, the hold slot, the piece source, and every gameplay counter, and
// advances all of them inside tick() on the calling thread. Nothing here
// includes SDL or reads a clock, so one seed plus one tick-indexed
// InputFrame stream fixes the entire round (specification section 7).
//
// One tick applies the input processing order of section 5: the pause command
// first; then hold, which ends every other gameplay action of that tick; then
// rotation; then a horizontal move or a DAS/ARR repeat; then hard drop, which
// ends the piece actions of the tick; otherwise soft drop or natural gravity;
// and finally the ground and lock bookkeeping.
//
// The same class owns the section 4.7 screens: openTitle() starts a session,
// tick(TickInput) routes each tick to the screen in charge, and the title,
// pause, and game-over menus move their cursors through the menu edges. The
// screens keep the round itself untouched, so the pinned replay hashes of the
// regression suite (which drive tick(InputFrame) from start()) hold.
//
// The piece source is the only randomness in the round and it runs on the
// gameplay PCG32 stream (kGameplayStream), so cosmetic or audio randomness in
// the platform layer can never shift a piece sequence (specification section 7).

#include <array>
#include <cstdint>

#include "stackfall/board.hpp"
#include "stackfall/fixture.hpp"
#include "stackfall/input.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/randomizer.hpp"
#include "stackfall/scoring.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// Tests reach the private counters of a Game through this peer, which the test
// suite defines. Production code has no route to it, so the public interface
// stays read-only for the renderer (specification sections 5 and 7).
struct GameTestPeer;

// The screens of the state machine of section 4.7. A session opens at Title,
// runs rounds through Playing and Paused, ends them at GameOver, and shows the
// controller mappings at Controls and PauseControls and the player settings at
// Options. The numeric values are pinned by the regression hash, which folds
// the scene, so Controls, Options, and PauseControls join at the end instead
// of their flow order.
enum class Scene : std::uint8_t {
  Boot = 0,        // constructed, not started yet
  Title,           // Start Game / Controls / Options / Quit over the saved high score
  Playing,         // simulation runs
  Paused,          // simulation frozen, no counter moves
  GameOver,        // round ended by block-out or lock-out
  Controls,        // the mappings screen, reached from and returning to Title
  Options,         // SOUND, GHOST, GRID, HOLD, NEXT PIECES, GRAYSCALE,
                   // ORIENTATION, MUSIC; the values are platform state
                   // (tasks T044, T055, and T062)
  PauseControls,   // the mappings screen over a frozen round (task T037)
  MusicOptions,    // the music settings page behind the MUSIC row: MUSIC,
                   // MUSIC VOLUME, EFFECTS VOLUME, NEXT TRACK (task T062,
                   // MUSIC.md player controls)
  Rebind,          // the controls customization screen behind the mappings
                   // screen: one row per gameplay action plus RESTORE
                   // DEFAULTS (task T066)
  BackgroundOptions,  // the backgrounds page behind the BACKGROUNDS row:
                      // MODE and PREFERRED, the wallpaper settings of task
                      // T084; the values are platform state like every
                      // options row
};

// Tick-flow constants of section 4.5, all counted in 60 Hz simulation ticks.
inline constexpr int kDasTicks = 10;        // charge before horizontal repeats
inline constexpr int kArrTicks = 2;         // ticks between repeated steps
inline constexpr int kLockDelayTicks = 30;  // grounded ticks before a lock
inline constexpr int kLockDelayResets = 15; // lock timer resets per piece

// Menu sizes of the section 4.7 screens, so the screens and their tests name
// one count instead of a bare literal. The title items are Start Game,
// Controls, Options, Quit.
inline constexpr int kTitleItemCount = 4;
inline constexpr int kPauseItemCount = 4;   // Resume, Restart, Controls, Quit
inline constexpr int kOverItemCount = 2;    // Play Again, Title
// SOUND, GHOST, GRID, HOLD, NEXT PIECES, GRAYSCALE (task T044), ORIENTATION
// (task T055), and MUSIC (task T062), appended so every earlier row keeps its
// index. The MUSIC row opens the music settings page below and the
// BACKGROUNDS row the backgrounds page (task T084). The PIECE DESIGNS row of
// task T095 follows the same append rule.
inline constexpr int kOptionsItemCount = 10;
inline constexpr int kOptionsMusicRow = 7;
inline constexpr int kOptionsBackgroundsRow = 8;
inline constexpr int kOptionsPieceDesignsRow = 9;
// The music settings page of MUSIC.md's player controls: the mode row, both
// volume rows, and the Next track action.
inline constexpr int kMusicOptionsItemCount = 4;
// The backgrounds page behind the BACKGROUNDS row (task T084): the mode row
// Auto / Off / Fixed and the preferred design row 1..20. Select changes
// nothing here for the same reason as every options row: the mode and the
// preferred design are platform state the core never holds.
inline constexpr int kBackgroundOptionsItemCount = 2;
inline constexpr int kBackgroundOptionsModeRow = 0;
inline constexpr int kBackgroundOptionsPreferredRow = 1;
// The controls customization screen of task T066: eight rows, one per
// rebindable gameplay action of stackfall/bindings.hpp, then RESTORE DEFAULTS.
// A select changes nothing in the core - the binding tables are platform
// state, exactly as the options row values are - so the platform layer reads
// the highlighted row and runs the capture or the restore itself.
inline constexpr int kRebindItemCount = 9;
inline constexpr int kRebindRestoreRow = 8;

class Game {
 public:
  // Builds an un-started game: Scene::Boot, an empty board, and no piece.
  Game();

  // Opens the title screen of section 4.7 with `seed` as the seed of the
  // first round: no round exists, the title menu starts at Start Game, and
  // the quit request clears. The platform layer calls this once per session
  // instead of start(), so a player picks Start Game (or Quit) themselves.
  void openTitle(std::uint64_t seed);

  // Resets every field and starts a fresh round from `seed`: the board
  // empties, the piece source restarts, the score, line, and level counters
  // return to their opening values, and the first piece spawns. Starting over
  // with the same seed reproduces the same round, so restart is the same call
  // (specification sections 4.3 and 4.7).
  void start(std::uint64_t seed);

  // Advances the round by one 60 Hz tick. A tick outside Playing and Paused,
  // and every tick while paused, changes nothing: pause freezes all counters
  // exactly (specification section 4.5).
  void tick(const InputFrame& input);

  // Advances the session by one 60 Hz tick through both halves of the input:
  // the screen in charge picks its half (stackfall/input.hpp). Title,
  // Controls, and GameOver read the menu edges alone; Paused reads the menu
  // edges plus the pause key; Playing hands its frame to tick(InputFrame).
  // The same TickInput stream from the same openTitle seed walks the same
  // screens and rounds on every build (specification section 7).
  void tick(const TickInput& input);

  // True after the player confirmed Quit on the title menu. The platform
  // layer polls this and leaves through its ordinary EXIT path
  // (specification section 4.7).
  bool quitRequested() const;

  // Developer fixture hook of docs/AGENT_TESTING.md, T023. Replaces the
  // settled board of the running round with `fixture.board`, resets the
  // per-piece counters, and respawns the active piece at its spawn state -
  // `fixture.active` when the fixture names one, otherwise the piece already
  // in play. A spawn the new board blocks ends the round through the ordinary
  // block-out path, which is exactly what a top-out fixture asks for. Score,
  // lines, level, hold, queue, and seed stay as they were, so the observation
  // stream carries the fixture's provenance next to the round it interrupted.
  //
  // Returns false and changes nothing outside Scene::Playing or for an
  // inconsistent board. No normal startup path calls this: only the
  // `--input-script` fixture command of the unattended harness does, and
  // every session that uses it labels the fixture in its evidence.
  bool applyBoardFixture(const BoardFixture& fixture);

  // -------------------------------------------------------------------------
  // Read-only state for the renderer and the tests (specification section 5).
  // -------------------------------------------------------------------------

  const Board& board() const;
  const Piece& activePiece() const;
  bool hasActivePiece() const;

  // The active piece moved to its landing row, for the ghost preview. Meaningful
  // only while a piece is in play; the ghost equals the piece itself when it is
  // grounded.
  Piece ghostPiece() const;

  const std::array<PieceId, kPreviewCount>& queue() const;

  bool hasHoldPiece() const;
  PieceId holdPieceId() const;

  // True while the one hold of the current piece is spent; a lock re-enables
  // hold (specification section 4.3).
  bool holdLocked() const;

  std::uint64_t score() const;
  std::uint64_t lines() const;  // total cleared lines of the round
  int level() const;
  Scene scene() const;

  // The highlighted item of each menu, 0-based: title Start Game / Controls /
  // Options / Quit, pause Resume / Restart / Controls / Quit to Title, game
  // over Play Again / Title, options SOUND / GHOST / GRID / HOLD / NEXT
  // PIECES / GRAYSCALE / ORIENTATION / MUSIC / BACKGROUNDS, the music page
  // MUSIC / MUSIC VOLUME / EFFECTS VOLUME / NEXT TRACK, and the backgrounds
  // page MODE / PREFERRED (specification section 4.7).
  // Menu state only; a round never reads it and the state hash never folds
  // it.
  int titleSelection() const;
  int pauseSelection() const;
  int overSelection() const;
  int optionsSelection() const;
  int musicSelection() const;
  int rebindSelection() const;
  int backgroundsSelection() const;

  // The screen the customization screen returns to on back: Controls when the
  // title opened it, PauseControls when the pause overlay did (task T066).
  Scene rebindOrigin() const;

  // The session high score shown on the title screen. The platform layer
  // loads it at startup, saves the record the moment a running score passes
  // the stored best (task T052), and feeds the persisted best back through
  // setHighScore between rounds, so a running round always compares against
  // the record it began with.
  std::uint64_t highScore() const;
  void setHighScore(std::uint64_t score);

  // True while the ended round beat the session high score; set once, when
  // the round enters GameOver (specification section 4.7).
  bool newRecord() const;

  // Simulation ticks the round ran while playing; pause adds none.
  std::uint64_t tickCount() const;

  // The seed this round started from.
  std::uint64_t seed() const;

 private:
  friend struct GameTestPeer;

  // -------------------------------------------------------------------------
  // One stage of the tick order of section 5.
  // -------------------------------------------------------------------------

  // Swaps the active piece with the hold slot and spawns the returned piece in
  // its spawn state; a blocked swap ends the round (block-out).
  void handleHold();

  // Rotates clockwise or counter-clockwise through the SRS candidates of
  // pieces.hpp; a rejected rotation leaves the piece unchanged.
  bool tryRotate(bool clockwise);

  // Moves the active piece one column when every occupied cell stays valid.
  bool tryMove(int dx);

  // Reads the held direction, moves immediately on a fresh press, and repeats
  // the move every kArrTicks once kDasTicks have passed (section 4.5).
  void handleHorizontalShift(const InputFrame& input);

  // Attempts one downward cell: by soft drop while down is held, otherwise by
  // natural gravity when the level timer runs out.
  bool tryFall();

  // Moves the active piece to its landing row, scores the travel, and locks it
  // in the same tick (specification section 4.5).
  void handleHardDrop();

  // Accrues grounded ticks and locks the piece when the lock delay expires.
  void handleGroundAndLock();

  // Commits the active piece, clears full rows, scores and re-levels, and
  // spawns the next piece; hidden-row cells end the round first (lock-out).
  void lockActivePiece();

  // Puts `id` on the board in its spawn state with fresh per-piece counters;
  // occupied spawn cells end the round (block-out).
  void spawnPiece(PieceId id);

  // Draws the next piece from the queue and spawns it.
  void spawnNextPiece();

  // Applies the lock-delay rule of section 4.5 after a successful move or
  // rotation: an action that leaves the piece airborne clears the grounded
  // streak, an action made while grounded resets the streak and spends one of
  // the resets, and a failed action never reaches this code.
  void applyLockDelayReset(bool wasGroundedBefore);

  // True when the active piece cannot move one row down.
  bool grounded() const;

  // -------------------------------------------------------------------------
  // The screens of section 4.7. Each helper consumes the menu edges of one
  // screen and changes nothing but that screen's cursor and the scene.
  // -------------------------------------------------------------------------

  // TITLE: Start Game opens a round, Controls shows the mappings, Options
  // shows the player settings, Quit asks the platform layer to exit.
  void tickTitle(const MenuFrame& menu);

  // CONTROLS: back returns to the title and select opens the controls
  // customization screen of task T066, which remembers this screen as its
  // way back. Before T066 confirm and back both returned to the title; the
  // mappings table gained a doorway instead of losing one.
  void tickControls(const MenuFrame& menu);

  // PAUSE_CONTROLS: the same two edges over a frozen round - back returns to
  // the pause overlay on its Controls item, select opens the customization
  // screen with this screen as its way back (task T066).
  void tickPauseControls(const MenuFrame& menu);

  // REBIND: back returns to the screen that opened this one and the D-pad
  // moves the cursor over the nine rows. A select changes nothing here, for
  // the same reason as the options screen: the binding tables are platform
  // state the core never holds, so the platform layer applies the capture or
  // the restore of the highlighted row itself (task T066).
  void tickRebind(const MenuFrame& menu);

  // OPTIONS: back returns to the title and the D-pad moves the cursor over
  // the eight rows. Select on the MUSIC row opens the music settings page;
  // on any other row select changes nothing here, because the row values are
  // platform state (the player preferences), which the core never holds, so
  // the platform layer applies the change of the highlighted row itself
  // (specification section 7).
  void tickOptions(const MenuFrame& menu);

  // MUSIC_OPTIONS: back returns to the options screen with the cursor still
  // on the MUSIC row, and the D-pad moves the cursor over the four rows
  // (MUSIC.md player controls). Select changes nothing here for the same
  // reason as the options screen: the mode and both volumes are platform
  // state, and Next track is a request the platform layer sends the music
  // director.
  void tickMusicOptions(const MenuFrame& menu);

  // BACKGROUND_OPTIONS: back returns to the options screen with the cursor
  // still on the BACKGROUNDS row, and the D-pad moves the cursor over the two
  // rows (task T084). Select changes nothing here for the same reason as the
  // options screen: the background mode and the preferred design are platform
  // state, so the platform layer reads the highlighted row and applies the
  // change itself.
  void tickBackgroundOptions(const MenuFrame& menu);

  // PAUSED: back resumes, the D-pad moves the cursor, A runs the highlighted
  // item, and the pause key alone still resumes. Every gameplay counter stays
  // frozen (section 4.5).
  void tickPaused(const TickInput& input);

  // GAME_OVER: Play Again opens a fresh round, Title returns to the title.
  void tickGameOver(const MenuFrame& menu);

  // Moves `cursor` by the menu edges, wrapping at both ends of `itemCount`
  // items.
  static void moveCursor(int& cursor, int itemCount, const MenuFrame& menu);

  // Opens a fresh round from the session seed and advances that seed, so
  // Start Game, Restart, and Play Again each deal a different deterministic
  // round (specification sections 4.3 and 4.7).
  void beginRound();

  // Drops the current round and returns to the title without touching the
  // session seed, so the round a player abandoned never replays
  // (specification section 4.7).
  void abandonToTitle();

  // Ends the round at GameOver: the game-over menu starts at Play Again, and
  // a better score raises the session high score and lights the record note.
  void endRound();

  // Returns every round field to its opening value around `seed`.
  void resetRound(std::uint64_t seed);

  Board board_;
  Randomizer randomizer_;
  Piece active_;
  bool hasActive_ = false;

  PieceId holdId_ = PieceId::I;
  bool hasHold_ = false;
  bool holdLocked_ = false;

  std::uint64_t score_ = 0;
  std::uint64_t lines_ = 0;
  int level_ = kMinLevel;
  Scene scene_ = Scene::Boot;

  std::uint64_t tickCount_ = 0;
  std::uint64_t seed_ = 0;

  // Session state of the section 4.7 screens. The cursors and the record flag
  // are screen state, not round state: the regression hash folds the round
  // alone, and a round started through start() leaves them untouched.
  std::uint64_t nextSeed_ = 0;    // the seed the next fresh round begins from
  std::uint64_t highScore_ = 0;   // session best, until milestone M3 persists it
  int titleCursor_ = 0;           // highlighted title item, 0..3
  int pauseCursor_ = 0;           // highlighted pause item, 0..3
  int overCursor_ = 0;            // highlighted game-over item, 0..1
  int optionsCursor_ = 0;         // highlighted options row, 0..8
  int musicCursor_ = 0;           // highlighted music page row, 0..3
  int rebindCursor_ = 0;          // highlighted rebind row, 0..8 (T066)
  int backgroundsCursor_ = 0;     // highlighted backgrounds page row, 0..1
                                   // (T084)
  Scene rebindFrom_ = Scene::Controls;  // where back returns (T066)
  bool newRecord_ = false;        // the ended round beat highScore_
  bool quitRequested_ = false;    // Quit confirmed on the title menu

  // Gameplay counters of section 4.5.
  int gravityTimer_ = 0;   // ticks since the last natural drop attempt
  int groundedTicks_ = 0;  // consecutive grounded ticks of the active piece
  int lockResets_ = 0;     // lock timer resets spent by the active piece
  int shiftDir_ = 0;       // auto-shift direction in charge: -1, 0, or +1
  int shiftTicks_ = 0;     // ticks charged since the press or the last repeat
};

}  // namespace stackfall
