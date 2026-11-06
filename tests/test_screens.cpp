// Headless suite for the section 4.7 screens (task T011).
//
// The state machine is rules core, so this suite drives it the same way the
// platform layer does: openTitle() opens a session and tick(TickInput) feeds
// each tick both halves of the input, the menu edges and the gameplay frame.
// Every screen, cursor, and transition of section 4.7 is checked without SDL:
// TITLE (Start Game / Controls / Options / Quit over the high score),
// CONTROLS, OPTIONS (SOUND / GHOST / GRID / HOLD / NEXT PIECES / GRAYSCALE /
// ORIENTATION / MUSIC / BACKGROUNDS; cursor only; the values are platform
// state) with its MUSIC_OPTIONS page behind the MUSIC row and its
// BACKGROUNDS page behind the BACKGROUNDS row (task T084), PLAYING, the
// PAUSED overlay (Resume / Restart / Quit to Title with
// every counter frozen exactly), and GAME_OVER (final values, record note,
// Play Again / Title). The session seed stream and the screen determinism of
// specification section 7 are checked here too, because the pinned replay
// hashes of the regression suite cover tick(InputFrame) alone.

#include <cstdint>

#include "stackfall/board.hpp"
#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/pieces.hpp"
#include "stackfall/types.hpp"

#include "game_test_peer.hpp"
#include "test_util.hpp"

namespace {

using stackfall::Board;
using stackfall::Cell;
using stackfall::Game;
using stackfall::GameTestPeer;
using stackfall::InputFrame;
using stackfall::MenuFrame;
using stackfall::kBackgroundOptionsItemCount;
using stackfall::kMusicOptionsItemCount;
using stackfall::kOptionsBackgroundsRow;
using stackfall::kOptionsItemCount;
using stackfall::kOptionsMusicRow;
using stackfall::kOptionsPieceDesignsRow;
using stackfall::PieceId;
using stackfall::Scene;
using stackfall::TickInput;

// ---------------------------------------------------------------------------
// Input builders, one intent per function, mirroring tests/test_game.cpp.
// ---------------------------------------------------------------------------

MenuFrame menuUp() {
  MenuFrame m;
  m.upPressed = true;
  return m;
}

MenuFrame menuDown() {
  MenuFrame m;
  m.downPressed = true;
  return m;
}

MenuFrame menuSelect() {
  MenuFrame m;
  m.selectPressed = true;
  return m;
}

MenuFrame menuBack() {
  MenuFrame m;
  m.backPressed = true;
  return m;
}

// A session tick that carries menu edges and an idle gameplay frame.
TickInput session(const MenuFrame& menu) {
  TickInput tick;
  tick.menu = menu;
  return tick;
}

// A session tick that carries a gameplay frame and no menu edge.
TickInput gameplay(const InputFrame& frame) {
  TickInput tick;
  tick.game = frame;
  return tick;
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

InputFrame holdDown() {
  InputFrame f;
  f.downHeld = true;
  return f;
}

InputFrame holdLeft() {
  InputFrame f;
  f.leftHeld = true;
  return f;
}

// Ends the round deterministically from any piece position: the spawn area is
// filled except the active piece's own cells and column 9, which keeps every
// row incomplete so no clear rescues the board, and then the piece hard-drops.
// Under the immediate-spawn rule of task T029 the spawn cells sit in rows 0
// and 1, so the band runs one row lower than the hidden rows. The piece
// either locks in the hidden rows (lock-out) or lands somewhere below and the
// next spawn is blocked (block-out); both end the round on that tick.
void forceGameOver(Game& g) {
  Board& board = GameTestPeer::board(g);
  const std::array<Cell, stackfall::kCellsPerPiece> cells =
      stackfall::boardCells(g.activePiece());
  for (int y = -stackfall::kHiddenRows; y <= 1; ++y) {
    for (int x = 0; x < stackfall::kBoardWidth - 1; ++x) {
      bool pieceCell = false;
      for (const Cell& cell : cells) {
        pieceCell = pieceCell || (cell.x == x && cell.y == y);
      }
      if (!pieceCell) {
        board.settleCell(x, y, PieceId::J);
      }
    }
  }
  g.tick(pressHardDrop());
  SC_CHECK(g.scene() == Scene::GameOver);
}

// ---------------------------------------------------------------------------
// TITLE.
// ---------------------------------------------------------------------------

void test_open_title_and_start_game() {
  Game g;
  g.openTitle(20261103);
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(!g.hasActivePiece());
  SC_CHECK(!g.quitRequested());
  SC_CHECK(g.titleSelection() == 0);
  SC_CHECK(g.highScore() == 0);
  SC_CHECK(g.score() == 0);
  SC_CHECK(g.lines() == 0);
  SC_CHECK(g.level() == 1);
  SC_CHECK(g.board().isEmpty());
  SC_CHECK(g.seed() == 20261103);

  // Gameplay input does nothing on the title; only the menu half arrives.
  InputFrame wild;
  wild.leftHeld = true;
  wild.rightHeld = true;
  wild.downHeld = true;
  wild.rotateCWPressed = true;
  wild.rotateCCWPressed = true;
  wild.hardDropPressed = true;
  wild.holdPressed = true;
  wild.pausePressed = true;
  g.tick(gameplay(wild));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.tickCount() == 0);
  SC_CHECK(!g.hasActivePiece());

  // Start Game opens the round with the session seed; the selecting tick runs
  // no gameplay action.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.seed() == 20261103);
  SC_CHECK(g.tickCount() == 0);
}

void test_title_navigation_wraps() {
  Game g;
  g.openTitle(1);
  SC_CHECK(g.titleSelection() == 0);
  g.tick(session(menuDown()));
  SC_CHECK(g.titleSelection() == 1);
  g.tick(session(menuDown()));
  SC_CHECK(g.titleSelection() == 2);
  g.tick(session(menuDown()));
  SC_CHECK(g.titleSelection() == 3);
  g.tick(session(menuDown()));
  SC_CHECK(g.titleSelection() == 0);  // wraps past Quit
  g.tick(session(menuUp()));
  SC_CHECK(g.titleSelection() == 3);  // wraps back up
  g.tick(session(menuUp()));
  SC_CHECK(g.titleSelection() == 2);
  g.tick(session(menuUp()));
  SC_CHECK(g.titleSelection() == 1);
  g.tick(session(menuUp()));
  SC_CHECK(g.titleSelection() == 0);
}

void test_title_controls_round_trip() {
  Game g;
  g.openTitle(1);
  g.tick(session(menuDown()));  // cursor on Controls
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Controls);

  // The controls screen takes no gameplay input and no cursor input; back
  // returns to the title and select opens the customization screen of task
  // T066, which remembers this screen as its way back.
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Controls);
  g.tick(session(menuDown()));
  SC_CHECK(g.scene() == Scene::Controls);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.titleSelection() == 1);  // the cursor stayed on Controls

  // Reopen with A: select now opens the customization screen, and back from
  // there returns to the mappings screen rather than skipping it, so the way
  // back out is one screen at a time (task T066).
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Controls);
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Rebind);
  SC_CHECK(g.rebindSelection() == 0);   // the first action row opens
  SC_CHECK(g.rebindOrigin() == Scene::Controls);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Controls);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Title);
}

// The controls customization screen of task T066: eight action rows plus
// RESTORE DEFAULTS, the cursor wrapping over all nine, back returning to the
// screen that opened it, and a select changing nothing in the core because the
// binding tables are platform state.
void test_rebind_screen_navigation() {
  Game g;
  g.openTitle(1);
  g.tick(session(menuDown()));     // cursor on Controls
  g.tick(session(menuSelect()));   // open the mappings screen
  g.tick(session(menuSelect()));   // open the customization screen
  SC_CHECK(g.scene() == Scene::Rebind);
  SC_CHECK(g.rebindSelection() == 0);

  // Gameplay input never reaches a menu screen.
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Rebind);

  // The cursor walks the eight action rows and lands on RESTORE DEFAULTS,
  // then wraps back to the first row from both ends.
  for (int row = 1; row < stackfall::kRebindItemCount; ++row) {
    g.tick(session(menuDown()));
    SC_CHECK(g.rebindSelection() == row);
  }
  g.tick(session(menuDown()));
  SC_CHECK(g.rebindSelection() == 0);
  g.tick(session(menuUp()));
  SC_CHECK(g.rebindSelection() == stackfall::kRebindRestoreRow);

  // A select moves nothing and leaves nothing: the platform layer owns the
  // capture and the restore, exactly as it owns the options row values.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Rebind);
  SC_CHECK(g.rebindSelection() == stackfall::kRebindRestoreRow);

  // Back returns to the mappings screen, and back again to the title.
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Controls);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.titleSelection() == 1);
}

// The pause overlay's mappings screen opens the same customization screen,
// and back walks back out through the overlay, so a frozen round keeps its
// place while the player rebinds (task T066).
void test_pause_controls_rebind_round_trip() {
  Game g;
  g.openTitle(9);
  g.tick(session(menuSelect()));  // Start Game
  for (int i = 0; i < 10; ++i) {
    g.tick(gameplay(InputFrame::idle()));
  }
  const std::uint64_t ticksBefore = g.tickCount();
  g.tick(gameplay(pressPause()));
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));    // cursor on Controls
  g.tick(session(menuSelect()));  // the mappings screen over the frozen round
  SC_CHECK(g.scene() == Scene::PauseControls);
  g.tick(session(menuSelect()));  // the customization screen
  SC_CHECK(g.scene() == Scene::Rebind);
  SC_CHECK(g.rebindOrigin() == Scene::PauseControls);
  SC_CHECK(g.tickCount() == ticksBefore);  // the round stayed frozen

  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::PauseControls);
  SC_CHECK(g.tickCount() == ticksBefore);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Paused);
}

void test_title_quit() {
  Game g;
  g.openTitle(1);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  SC_CHECK(g.titleSelection() == 3);  // Quit sits below Options
  g.tick(session(menuSelect()));
  SC_CHECK(g.quitRequested());
  SC_CHECK(g.scene() == Scene::Title);  // the exit itself is the platform's

  // A fresh session clears the request.
  g.openTitle(2);
  SC_CHECK(!g.quitRequested());
}

void test_title_options_round_trip() {
  Game g;
  g.openTitle(1);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  SC_CHECK(g.titleSelection() == 2);  // OPTIONS sits between Controls and Quit
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Options);
  SC_CHECK(g.optionsSelection() == 0);  // SOUND is the opening row

  // The screen takes no gameplay input; only the menu half arrives.
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Options);

  // The cursor walks every one of the ten rows and wraps at both ends: down
  // reaches GHOST, GRID, HOLD, NEXT PIECES, GRAYSCALE, ORIENTATION, MUSIC,
  // BACKGROUNDS, and PIECE DESIGNS, down again wraps back to SOUND, and up
  // wraps from SOUND straight to PIECE DESIGNS (tasks T044, T055, T062,
  // T084, and T095).
  for (int row = 1; row < kOptionsItemCount; ++row) {
    g.tick(session(menuDown()));
    SC_CHECK(g.optionsSelection() == row);
  }
  SC_CHECK(kOptionsItemCount == 10);
  SC_CHECK(kOptionsMusicRow == 7);
  SC_CHECK(kOptionsBackgroundsRow == 8);
  SC_CHECK(kOptionsPieceDesignsRow == 9);
  g.tick(session(menuDown()));
  SC_CHECK(g.optionsSelection() == 0);  // wraps past PIECE DESIGNS
  g.tick(session(menuUp()));
  SC_CHECK(g.optionsSelection() == kOptionsPieceDesignsRow);  // up past SOUND

  // Select on a setting row changes nothing in the core: the row values are
  // platform state (specification section 7), so the screen stays put and the
  // cursor holds. The walk back up crosses BACKGROUNDS before MUSIC.
  g.tick(session(menuUp()));
  SC_CHECK(g.optionsSelection() == kOptionsBackgroundsRow);  // BACKGROUNDS
  g.tick(session(menuUp()));
  SC_CHECK(g.optionsSelection() == 7);  // MUSIC
  g.tick(session(menuUp()));
  SC_CHECK(g.optionsSelection() == 6);  // ORIENTATION
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Options);
  SC_CHECK(g.optionsSelection() == 6);

  // Back returns to the title with the cursor still on the OPTIONS item, so
  // A reopens the screen.
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.titleSelection() == 2);
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Options);
  SC_CHECK(g.optionsSelection() == 6);  // the row survived the round trip

  // A fresh session opens the screen on the SOUND row again.
  g.openTitle(2);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Options);
  SC_CHECK(g.optionsSelection() == 0);
}

// The music settings page behind the MUSIC row (task T062, MUSIC.md player
// controls): select on the MUSIC row opens it, its four rows take the same
// cursor moves, select changes nothing in the core for the same reason as the
// options screen, and back returns to the options screen with the cursor
// still on the MUSIC row, so B followed by A reopens the page.
void test_music_options_page() {
  Game g;
  g.openTitle(7);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuSelect()));  // OPTIONS
  SC_CHECK(kMusicOptionsItemCount == 4);
  for (int row = 0; row < kOptionsMusicRow; ++row) {
    g.tick(session(menuDown()));
  }
  SC_CHECK(g.optionsSelection() == kOptionsMusicRow);

  // The opening select belongs to the options screen, exactly as the options
  // screen's own opening press belongs to the title.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::MusicOptions);
  SC_CHECK(g.musicSelection() == 0);  // MUSIC is the opening row

  // The screen takes no gameplay input, exactly like the options screen.
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::MusicOptions);

  // The cursor walks the four rows and wraps at both ends.
  for (int row = 1; row < kMusicOptionsItemCount; ++row) {
    g.tick(session(menuDown()));
    SC_CHECK(g.musicSelection() == row);
  }
  g.tick(session(menuDown()));
  SC_CHECK(g.musicSelection() == 0);  // wraps past NEXT TRACK
  g.tick(session(menuUp()));
  SC_CHECK(g.musicSelection() == 3);  // wraps back up past MUSIC

  // Select changes nothing in the core: the mode and both volumes are
  // platform state and Next track is a platform request.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::MusicOptions);
  SC_CHECK(g.musicSelection() == 3);

  // Back returns to the options screen on the MUSIC row, so A reopens the
  // page on its opening row.
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Options);
  SC_CHECK(g.optionsSelection() == kOptionsMusicRow);
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::MusicOptions);
  SC_CHECK(g.musicSelection() == 0);

  // Back twice leaves the whole settings stack: the title keeps its cursor on
  // the OPTIONS item, and a fresh session starts the page over.
  g.tick(session(menuBack()));
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.titleSelection() == 2);
  g.openTitle(8);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuSelect()));
  for (int row = 0; row < kOptionsMusicRow; ++row) {
    g.tick(session(menuDown()));
  }
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::MusicOptions);
  SC_CHECK(g.musicSelection() == 0);
}

// The backgrounds page behind the BACKGROUNDS row (task T084,
// docs/BACKGROUND_IMPLEMENTATION.md player options): select on the
// BACKGROUNDS row opens it, its two rows take the same cursor moves, select
// changes nothing in the core for the same reason as the options screen, and
// back returns to the options screen with the cursor still on the
// BACKGROUNDS row, so B followed by A reopens the page. The opening select
// of a fresh session lands on the MODE row again.
void test_background_options_page() {
  Game g;
  g.openTitle(21);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuSelect()));  // OPTIONS
  SC_CHECK(kBackgroundOptionsItemCount == 2);
  for (int row = 0; row < kOptionsBackgroundsRow; ++row) {
    g.tick(session(menuDown()));
  }
  SC_CHECK(g.optionsSelection() == kOptionsBackgroundsRow);

  // The opening select belongs to the options screen, exactly as the music
  // page's does.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::BackgroundOptions);
  SC_CHECK(g.backgroundsSelection() == 0);  // MODE is the opening row

  // The screen takes no gameplay input, exactly like the options screen.
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::BackgroundOptions);

  // The cursor walks the two rows and wraps at both ends.
  g.tick(session(menuDown()));
  SC_CHECK(g.backgroundsSelection() == 1);  // DESIGN
  g.tick(session(menuDown()));
  SC_CHECK(g.backgroundsSelection() == 0);  // wraps past DESIGN
  g.tick(session(menuUp()));
  SC_CHECK(g.backgroundsSelection() == 1);  // wraps back up past MODE

  // Select changes nothing in the core: the background mode and the
  // preferred design are platform state (specification section 7).
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::BackgroundOptions);
  SC_CHECK(g.backgroundsSelection() == 1);

  // Back returns to the options screen on the BACKGROUNDS row, so A reopens
  // the page on its opening row.
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Options);
  SC_CHECK(g.optionsSelection() == kOptionsBackgroundsRow);
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::BackgroundOptions);
  SC_CHECK(g.backgroundsSelection() == 0);

  // Back twice leaves the whole settings stack: the title keeps its cursor
  // on the OPTIONS item, and a fresh session starts the page over.
  g.tick(session(menuBack()));
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.titleSelection() == 2);
  g.openTitle(22);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuSelect()));
  for (int row = 0; row < kOptionsBackgroundsRow; ++row) {
    g.tick(session(menuDown()));
  }
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::BackgroundOptions);
  SC_CHECK(g.backgroundsSelection() == 0);
}

// ---------------------------------------------------------------------------
// PAUSED.
// ---------------------------------------------------------------------------

void test_pause_overlay_resume_paths() {
  Game g;
  g.openTitle(9);
  g.tick(session(menuSelect()));  // Start Game
  g.tick(gameplay(holdDown()));
  g.tick(gameplay(holdDown()));
  g.tick(gameplay(holdDown()));
  const std::uint64_t ticksBefore = g.tickCount();
  const std::uint64_t scoreBefore = g.score();
  const std::array<std::uint16_t, stackfall::kBoardRows> boardBefore =
      g.board().occupancy();
  SC_CHECK(ticksBefore == 3);
  SC_CHECK(scoreBefore == 3);

  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Paused);
  SC_CHECK(g.pauseSelection() == 0);  // Resume is the default item

  // Menu navigation and gameplay input freeze every counter exactly
  // (specification section 4.5).
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuUp()));
  SC_CHECK(g.pauseSelection() == 1);
  InputFrame wild = holdDown();
  wild.hardDropPressed = true;
  wild.rotateCWPressed = true;
  wild.holdPressed = true;
  g.tick(gameplay(wild));
  SC_CHECK(g.scene() == Scene::Paused);
  SC_CHECK(g.tickCount() == ticksBefore);
  SC_CHECK(g.score() == scoreBefore);
  SC_CHECK(g.board().occupancy() == boardBefore);
  SC_CHECK(GameTestPeer::gravityTimer(g) == 0);
  SC_CHECK(GameTestPeer::groundedTicks(g) == 0);

  // Back to Resume, then B (back) resumes without ticking the round.
  g.tick(session(menuUp()));
  SC_CHECK(g.pauseSelection() == 0);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.tickCount() == ticksBefore);
  SC_CHECK(g.score() == scoreBefore);

  // The pause key alone also resumes (Start / Enter / Esc).
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Paused);
  SC_CHECK(g.pauseSelection() == 0);
  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Playing);

  // A (select) on Resume resumes as well.
  g.tick(gameplay(pressPause()));
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Playing);

  // The round then continues from the frozen counters, not from a reset.
  g.tick(gameplay(InputFrame::idle()));
  SC_CHECK(g.tickCount() == ticksBefore + 1);
}

void test_pause_overlay_restart_and_quit_to_title() {
  Game g;
  g.openTitle(9);
  g.tick(session(menuSelect()));  // round one runs on the session seed
  SC_CHECK(g.seed() == 9);
  g.tick(gameplay(holdDown()));
  SC_CHECK(g.tickCount() == 1);

  // Pause and Restart: a fresh seeded round, not a replay.
  g.tick(gameplay(pressPause()));
  g.tick(session(menuDown()));  // cursor on Restart
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.seed() != 9);
  SC_CHECK(g.tickCount() == 0);
  SC_CHECK(g.board().isEmpty());
  SC_CHECK(g.hasActivePiece());
  const std::uint64_t secondSeed = g.seed();

  // Quit to Title abandons the round; the next Start does not replay it.
  g.tick(gameplay(pressPause()));
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));  // cursor on Quit to Title, past Controls
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(!g.hasActivePiece());
  SC_CHECK(g.board().isEmpty());
  SC_CHECK(g.titleSelection() == 0);
  g.tick(session(menuSelect()));  // Start Game
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.seed() != 9);
  SC_CHECK(g.seed() != secondSeed);
}

// The pause overlay's Controls item opens the mappings screen over the frozen
// round and back returns to the same overlay without advancing a counter or
// losing state (task T037).
void test_pause_controls_round_trip() {
  Game g;
  g.openTitle(9);
  g.tick(session(menuSelect()));  // Start Game
  g.tick(gameplay(holdDown()));
  g.tick(gameplay(holdDown()));
  const std::uint64_t ticksBefore = g.tickCount();
  const std::uint64_t scoreBefore = g.score();
  const auto boardBefore = g.board().occupancy();

  g.tick(gameplay(pressPause()));
  SC_CHECK(g.scene() == Scene::Paused);
  g.tick(session(menuDown()));
  g.tick(session(menuDown()));  // cursor on Controls
  SC_CHECK(g.pauseSelection() == 2);
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::PauseControls);

  // The mappings screen ignores gameplay input and cursor moves; the round
  // below every one of its ticks stays frozen exactly.
  InputFrame wild = holdDown();
  wild.hardDropPressed = true;
  wild.holdPressed = true;
  g.tick(gameplay(wild));
  g.tick(session(menuDown()));
  SC_CHECK(g.scene() == Scene::PauseControls);
  SC_CHECK(g.pauseSelection() == 2);  // the pause cursor waits on Controls
  SC_CHECK(g.tickCount() == ticksBefore);
  SC_CHECK(g.score() == scoreBefore);
  SC_CHECK(g.board().occupancy() == boardBefore);

  // Back returns to the overlay on the same item, still frozen, and a second
  // select reopens the mappings screen.
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Paused);
  SC_CHECK(g.pauseSelection() == 2);
  SC_CHECK(g.tickCount() == ticksBefore);
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::PauseControls);
  // Select now opens the customization screen of task T066 over the same
  // frozen round, and back from it returns here; confirm no longer returns to
  // the overlay.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Rebind);
  SC_CHECK(g.tickCount() == ticksBefore);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::PauseControls);
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Paused);

  // One more down lands on Quit to Title, so the four-item order holds.
  g.tick(session(menuDown()));
  SC_CHECK(g.pauseSelection() == 3);

  // Back on the overlay resumes the frozen round, which then continues.
  g.tick(session(menuUp()));
  g.tick(session(menuUp()));
  g.tick(session(menuUp()));  // back to Resume
  g.tick(session(menuBack()));
  SC_CHECK(g.scene() == Scene::Playing);
  g.tick(gameplay(InputFrame::idle()));
  SC_CHECK(g.tickCount() == ticksBefore + 1);
  SC_CHECK(g.score() == scoreBefore);
}

void test_restart_with_a_held_direction() {
  Game g;
  g.openTitle(5);
  g.tick(session(menuSelect()));
  const int spawnX = g.activePiece().x;  // the column the round opened on
  g.tick(gameplay(holdLeft()));          // the press tick moves immediately
  SC_CHECK(g.activePiece().x == spawnX - 1);
  g.tick(gameplay(pressPause()));

  // Keep left held while selecting Restart; the menu half drives the tick.
  TickInput selectHeld = gameplay(holdLeft());
  selectHeld.menu = menuDown();  // Restart
  g.tick(selectHeld);
  selectHeld.menu = menuSelect();
  g.tick(selectHeld);
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.hasActivePiece());
  const int freshX = g.activePiece().x;

  // The still-held direction moves the fresh piece on its first playing tick
  // and nothing is stuck: one column, exactly like a fresh press.
  g.tick(gameplay(holdLeft()));
  SC_CHECK(g.activePiece().x == freshX - 1);
  SC_CHECK(freshX == spawnX);  // the new piece began at the spawn column
}

// ---------------------------------------------------------------------------
// GAME_OVER.
// ---------------------------------------------------------------------------

void test_game_over_menu_and_record() {
  Game g;
  g.openTitle(77);
  g.tick(session(menuSelect()));  // round one
  g.tick(gameplay(holdDown()));
  g.tick(gameplay(holdDown()));
  g.tick(gameplay(holdDown()));
  SC_CHECK(g.score() == 3);

  // The record is decided at game over only, not while the score climbs.
  SC_CHECK(!g.newRecord());
  SC_CHECK(g.highScore() == 0);

  forceGameOver(g);
  SC_CHECK(g.scene() == Scene::GameOver);
  SC_CHECK(!g.hasActivePiece());
  SC_CHECK(g.newRecord());
  const std::uint64_t firstScore = g.score();  // includes the hard-drop points
  SC_CHECK(g.highScore() == firstScore);
  SC_CHECK(g.overSelection() == 0);  // Play Again is the default item

  // Navigation toggles the two items and wraps.
  g.tick(session(menuDown()));
  SC_CHECK(g.overSelection() == 1);
  g.tick(session(menuDown()));
  SC_CHECK(g.overSelection() == 0);
  g.tick(session(menuUp()));
  SC_CHECK(g.overSelection() == 1);

  // Title keeps the raised high score for the title screen to show; the
  // abandoned round's own score resets.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Title);
  SC_CHECK(g.highScore() == firstScore);
  SC_CHECK(g.score() == 0);

  // Play Again opens a fresh seeded round with a clear record flag.
  g.tick(session(menuSelect()));
  SC_CHECK(g.scene() == Scene::Playing);
  SC_CHECK(g.hasActivePiece());
  SC_CHECK(g.board().isEmpty());
  SC_CHECK(g.tickCount() == 0);
  SC_CHECK(!g.newRecord());
  SC_CHECK(g.highScore() == firstScore);  // the session best survives rounds
  SC_CHECK(g.seed() != 77);

  // The record note follows the larger score, whichever round made it; the
  // bar never lowers.
  g.tick(gameplay(holdDown()));
  forceGameOver(g);
  const std::uint64_t secondScore = g.score();
  SC_CHECK(g.highScore() == (secondScore > firstScore ? secondScore : firstScore));
  SC_CHECK(g.newRecord() == (secondScore > firstScore));
}

void test_high_score_loaded_from_outside() {
  // The platform layer loads a persisted best at startup (milestone M3 wires
  // the file); a round that does not reach it changes nothing.
  Game g;
  g.openTitle(123);
  g.setHighScore(999999);
  g.tick(session(menuSelect()));
  g.tick(gameplay(holdDown()));
  forceGameOver(g);
  SC_CHECK(!g.newRecord());
  SC_CHECK(g.highScore() == 999999);
}

// ---------------------------------------------------------------------------
// Session determinism (specification section 7).
// ---------------------------------------------------------------------------

// One scripted session through every screen: controls round trip, a round, a
// restart, a resume, a game over, and a second round.
void driveSession(Game& g) {
  g.openTitle(424242);
  g.tick(session(menuDown()));    // cursor on Controls
  g.tick(session(menuSelect()));  // open the controls screen
  g.tick(session(menuBack()));    // back to the title, cursor on Controls
  g.tick(session(menuUp()));      // cursor back on Start Game
  g.tick(session(menuSelect()));  // Start Game
  for (int i = 0; i < 40; ++i) {
    g.tick(gameplay(InputFrame::idle()));
  }
  g.tick(gameplay(pressPause()));
  g.tick(session(menuDown()));    // Restart
  g.tick(session(menuSelect()));
  for (int i = 0; i < 25; ++i) {
    g.tick(gameplay(holdDown()));
  }
  g.tick(gameplay(pressPause()));
  g.tick(session(menuBack()));  // resume
  forceGameOver(g);
  g.tick(session(menuDown()));    // Title from game over
  g.tick(session(menuSelect()));
  g.tick(session(menuSelect()));  // Start Game again
}

void test_session_determinism() {
  Game a;
  Game b;
  driveSession(a);
  driveSession(b);
  SC_CHECK(a.scene() == b.scene());
  SC_CHECK(a.scene() == Scene::Playing);
  SC_CHECK(a.seed() == b.seed());
  SC_CHECK(a.score() == b.score());
  SC_CHECK(a.lines() == b.lines());
  SC_CHECK(a.level() == b.level());
  SC_CHECK(a.tickCount() == b.tickCount());
  SC_CHECK(a.titleSelection() == b.titleSelection());
  SC_CHECK(a.pauseSelection() == b.pauseSelection());
  SC_CHECK(a.overSelection() == b.overSelection());
  SC_CHECK(a.highScore() == b.highScore());
  SC_CHECK(a.newRecord() == b.newRecord());
  SC_CHECK(a.quitRequested() == b.quitRequested());
  SC_CHECK(a.board().occupancy() == b.board().occupancy());
  SC_CHECK(a.board().cellTypes() == b.board().cellTypes());
  SC_CHECK(a.queue() == b.queue());
  SC_CHECK(a.hasActivePiece() == b.hasActivePiece());
  SC_CHECK(a.hasActivePiece());
  SC_CHECK(a.activePiece().id == b.activePiece().id);
  SC_CHECK(a.activePiece().rotation == b.activePiece().rotation);
  SC_CHECK(a.activePiece().x == b.activePiece().x);
  SC_CHECK(a.activePiece().y == b.activePiece().y);
}

void test_seed_stream_differs_per_session_seed() {
  // The same stream walked from a different session seed reaches a different
  // round; the stream itself stays reproducible.
  Game a;
  driveSession(a);
  Game other;
  other.openTitle(424243);
  other.tick(session(menuSelect()));
  SC_CHECK(other.seed() == 424243);
  SC_CHECK(other.scene() == Scene::Playing);

  // Repeating the first session reproduces its rounds exactly.
  Game a2;
  driveSession(a2);
  SC_CHECK(a2.seed() == a.seed());
  SC_CHECK(a2.score() == a.score());
}

}  // namespace

void run_screens_tests() {
  test_open_title_and_start_game();
  test_title_navigation_wraps();
  test_title_controls_round_trip();
  test_title_quit();
  test_title_options_round_trip();
  test_music_options_page();
  test_background_options_page();
  test_pause_overlay_resume_paths();
  test_pause_overlay_restart_and_quit_to_title();
  test_pause_controls_round_trip();
  test_rebind_screen_navigation();
  test_pause_controls_rebind_round_trip();
  test_restart_with_a_held_direction();
  test_game_over_menu_and_record();
  test_high_score_loaded_from_outside();
  test_session_determinism();
  test_seed_stream_differs_per_session_seed();
}
