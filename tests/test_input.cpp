// Headless suite for the SDL-free input adapter (task T009).
//
// The acceptance criteria are specification section 5 and section 11 item 19:
// a press edge is consumed exactly once even when a frame catches up through
// several simulation ticks, held directions merge across input sources, left
// and right held together cause no movement, and losing a source or the
// window focus clears exactly that input. The SDL event path above the
// adapter is covered by the separate stackfall_input_tests target; everything
// here runs without SDL, so it belongs to the portable suite.

#include <cstdint>

#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "test_util.hpp"

namespace {

using stackfall::Game;
using stackfall::GameAction;
using stackfall::InputAdapter;
using stackfall::InputFrame;
using stackfall::TickInput;

constexpr int kKeyboard = stackfall::kKeyboardInputSource;
constexpr int kPadA = 3;  // a controller instance id; any non-negative int
constexpr int kPadB = 4;

// True when the frame carries no held level and no edge at all.
bool isEmpty(const TickInput& tick) {
  const InputFrame& f = tick.game;
  return !f.leftHeld && !f.rightHeld && !f.downHeld && !f.rotateCWPressed &&
         !f.rotateCCWPressed && !f.hardDropPressed && !f.holdPressed &&
         !f.pausePressed && !tick.menu.upPressed && !tick.menu.downPressed &&
         !tick.menu.selectPressed && !tick.menu.backPressed;
}

void check_idle_adapter() {
  InputAdapter adapter;
  SC_CHECK(isEmpty(adapter.takeTickInput()));
  SC_CHECK(isEmpty(adapter.takeTickInput()));
  SC_CHECK(adapter.sourceCount() == 0);
}

// Section 11 item 19: one press edge fires on exactly one tick of a catch-up
// batch, and never again while the button stays down.
void check_edge_consumed_once_across_catch_up() {
  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::RotateCW, true);

  // The catch-up batch: five ticks run after one event poll.
  const TickInput first = adapter.takeTickInput();
  SC_CHECK(first.game.rotateCWPressed);
  for (int i = 0; i < 4; ++i) {
    const TickInput next = adapter.takeTickInput();
    SC_CHECK(!next.game.rotateCWPressed);
  }

  // Releasing and staying idle adds nothing.
  adapter.setButton(kKeyboard, GameAction::RotateCW, false);
  for (int i = 0; i < 3; ++i) {
    SC_CHECK(isEmpty(adapter.takeTickInput()));
  }
}

// The same rule at Game level: one rotation per press, not one per catch-up
// tick.
void check_game_rotates_once_per_press() {
  Game game;
  game.start(7);
  const auto before = game.activePiece().rotation;

  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::RotateCW, true);
  adapter.setButton(kKeyboard, GameAction::RotateCW, false);
  for (int i = 0; i < 6; ++i) {
    game.tick(adapter.takeTickInput().game);
  }
  const auto after = game.activePiece().rotation;
  const int steps = (static_cast<int>(after) + 4 - static_cast<int>(before)) %
                    4;
  SC_CHECK(steps == 1);
}

// A press that arrives between ticks waits for the next tick even across
// frames where no tick runs at all, then fires once.
void check_pending_edge_survives_zero_tick_frames() {
  InputAdapter adapter;
  adapter.setButton(kPadA, GameAction::Pause, true);
  adapter.setButton(kPadA, GameAction::Pause, false);
  // No tick consumes the edge yet; more presses pile onto the same edge.
  adapter.setButton(kKeyboard, GameAction::Pause, true);

  const TickInput tick = adapter.takeTickInput();
  SC_CHECK(tick.game.pausePressed);  // collapsed, still one edge
  SC_CHECK(isEmpty(adapter.takeTickInput()));
}

// Held directions are levels: they stay on every tick until the release.
void check_held_levels_persist() {
  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::Left, true);
  adapter.setButton(kKeyboard, GameAction::Down, true);
  for (int i = 0; i < 3; ++i) {
    const TickInput tick = adapter.takeTickInput();
    SC_CHECK(tick.game.leftHeld);
    SC_CHECK(tick.game.downHeld);
  }
  adapter.setButton(kKeyboard, GameAction::Left, false);
  const TickInput after = adapter.takeTickInput();
  SC_CHECK(!after.game.leftHeld);
  SC_CHECK(after.game.downHeld);
}

// Section 5: left and right held together cause no movement, whatever the
// sources are.
void check_opposite_directions_cancel() {
  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::Left, true);
  SC_CHECK(adapter.takeTickInput().game.horizontalDirection() == -1);

  adapter.setButton(kPadA, GameAction::Right, true);
  TickInput both = adapter.takeTickInput();
  SC_CHECK(both.game.leftHeld && both.game.rightHeld);
  SC_CHECK(both.game.horizontalDirection() == 0);

  // The keyboard releases; the pad direction takes over alone. The core reads
  // the change and moves immediately after its DAS/ARR reset (section 5).
  adapter.setButton(kKeyboard, GameAction::Left, false);
  TickInput padOnly = adapter.takeTickInput();
  SC_CHECK(padOnly.game.horizontalDirection() == 1);
}

// Two sources may hold the same direction; releasing one leaves the other.
void check_sources_merge() {
  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::Left, true);
  adapter.setButton(kPadA, GameAction::Left, true);
  SC_CHECK(adapter.takeTickInput().game.leftHeld);

  adapter.setButton(kKeyboard, GameAction::Left, false);
  SC_CHECK(adapter.takeTickInput().game.leftHeld);
  adapter.setButton(kPadA, GameAction::Left, false);
  SC_CHECK(!adapter.takeTickInput().game.leftHeld);
}

// A disappearing controller loses its held levels and its unconsumed press
// edges, and nothing else.
void check_drop_source_on_removal() {
  InputAdapter adapter;
  adapter.setButton(kPadA, GameAction::Right, true);
  adapter.setButton(kPadA, GameAction::HardDrop, true);  // pending edge
  adapter.setButton(kKeyboard, GameAction::Down, true);
  adapter.setButton(kKeyboard, GameAction::Hold, true);  // pending edge

  adapter.dropSource(kPadA);
  SC_CHECK(adapter.sourceCount() == 1);

  const TickInput tick = adapter.takeTickInput();
  SC_CHECK(!tick.game.rightHeld);      // held level gone with the pad
  SC_CHECK(!tick.game.hardDropPressed);  // its ghost press never fires
  SC_CHECK(tick.game.downHeld);        // keyboard untouched
  SC_CHECK(tick.game.holdPressed);
}

// Two sources pressing the same action keep one shared edge: dropping one
// source still leaves the other's press pending.
void check_drop_source_keeps_other_edge() {
  InputAdapter adapter;
  adapter.setButton(kPadA, GameAction::Pause, true);
  adapter.setButton(kKeyboard, GameAction::Pause, true);

  adapter.dropSource(kPadA);
  SC_CHECK(adapter.takeTickInput().game.pausePressed);
}

// Focus loss clears every held level: releases may never arrive for buttons
// that were down when the window lost focus. The sources stay and a press
// edge the window already received still fires, because the player really
// pressed it (specification section 11, item 19).
void check_release_all_held_on_focus_loss() {
  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::Down, true);
  adapter.setButton(kKeyboard, GameAction::Pause, true);
  adapter.setButton(kPadA, GameAction::Left, true);
  SC_CHECK(adapter.sourceCount() == 2);

  adapter.releaseAllHeld();
  SC_CHECK(adapter.sourceCount() == 2);
  SC_CHECK(!adapter.isHeld(GameAction::Down));
  SC_CHECK(!adapter.isHeld(GameAction::Left));
  const TickInput tick = adapter.takeTickInput();
  SC_CHECK(!tick.game.downHeld);
  SC_CHECK(!tick.game.leftHeld);
  SC_CHECK(tick.game.pausePressed);  // the queued press still fires

  // The pad works again without re-registering: a fresh press both holds and
  // fires, and a later release clears the level.
  adapter.setButton(kPadA, GameAction::Left, true);
  const TickInput held = adapter.takeTickInput();
  SC_CHECK(held.game.leftHeld);
  adapter.setButton(kPadA, GameAction::Left, false);
  SC_CHECK(!adapter.takeTickInput().game.leftHeld);
}

// Menu edges ride on the same tick as gameplay edges and are consumed
// together, so a menu action and a gameplay action pressed together both land
// once, on the same tick.
void check_menu_edges_share_the_tick() {
  InputAdapter adapter;
  adapter.setButton(kKeyboard, GameAction::MenuSelect, true);
  adapter.setButton(kKeyboard, GameAction::Pause, true);
  adapter.setButton(kPadA, GameAction::MenuUp, true);

  const TickInput tick = adapter.takeTickInput();
  SC_CHECK(tick.menu.selectPressed);
  SC_CHECK(tick.game.pausePressed);
  SC_CHECK(tick.menu.upPressed);

  const TickInput next = adapter.takeTickInput();
  SC_CHECK(!next.menu.selectPressed);
  SC_CHECK(!next.menu.upPressed);
}

// Presses made while the game is paused are consumed by paused ticks, so they
// cannot fire after the resume (specification sections 4.5 and 5).
void check_presses_during_pause_are_consumed() {
  Game game;
  game.start(11);
  InputAdapter adapter;

  adapter.setButton(kKeyboard, GameAction::Pause, true);
  adapter.setButton(kKeyboard, GameAction::Pause, false);
  game.tick(adapter.takeTickInput().game);
  SC_CHECK(game.scene() == stackfall::Scene::Paused);
  const auto ticksAtPause = game.tickCount();

  // A rotation pressed while paused disappears on a paused tick.
  adapter.setButton(kKeyboard, GameAction::RotateCW, true);
  adapter.setButton(kKeyboard, GameAction::RotateCW, false);
  game.tick(adapter.takeTickInput().game);
  SC_CHECK(game.tickCount() == ticksAtPause);  // counters frozen
  SC_CHECK(game.activePiece().rotation == stackfall::Rotation::Spawn);

  // Resume, then idle ticks: no stored edge fires.
  adapter.setButton(kKeyboard, GameAction::Pause, true);
  adapter.setButton(kKeyboard, GameAction::Pause, false);
  game.tick(adapter.takeTickInput().game);
  SC_CHECK(game.scene() == stackfall::Scene::Playing);
  for (int i = 0; i < 5; ++i) {
    game.tick(adapter.takeTickInput().game);
  }
  SC_CHECK(game.activePiece().rotation == stackfall::Rotation::Spawn);
}

// A restart made during a tick cannot inherit an edge: every pending press
// was consumed by that same tick (specification section 4.7).
void check_restart_inherits_no_pending_edge() {
  Game game;
  game.start(21);
  InputAdapter adapter;

  // Menu select restarts the round; the same tick consumed the select edge.
  adapter.setButton(kPadA, GameAction::MenuSelect, true);
  game.tick(adapter.takeTickInput().game);
  game.start(22);  // what the platform layer does on the restart command

  // The new round's first ticks carry no stale select or gameplay edge.
  for (int i = 0; i < 3; ++i) {
    SC_CHECK(isEmpty(adapter.takeTickInput()));
  }
}

// Past the source limit the adapter ignores further devices instead of
// failing; the limit covers the keyboard plus spare controllers.
void check_source_limit_is_safe() {
  InputAdapter adapter;
  for (int i = 0; i < 12; ++i) {
    adapter.setButton(i, GameAction::Left, true);
  }
  // Eight sources registered (the first ids); the rest were ignored.
  SC_CHECK(adapter.sourceCount() == 8);
  SC_CHECK(adapter.takeTickInput().game.leftHeld);
}

}  // namespace

void run_input_tests() {
  check_idle_adapter();
  check_edge_consumed_once_across_catch_up();
  check_game_rotates_once_per_press();
  check_pending_edge_survives_zero_tick_frames();
  check_held_levels_persist();
  check_opposite_directions_cancel();
  check_sources_merge();
  check_drop_source_on_removal();
  check_drop_source_keeps_other_edge();
  check_release_all_held_on_focus_loss();
  check_menu_edges_share_the_tick();
  check_presses_during_pause_are_consumed();
  check_restart_inherits_no_pending_edge();
  check_source_limit_is_safe();
}
