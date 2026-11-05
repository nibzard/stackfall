#pragma once

// The SDL-free input side of the STACKFALL rules core (specification sections
// 5 and 7). One InputFrame describes one simulation tick: the directions held
// at that tick and the button edges that fire on it. One InputAdapter turns
// device button transitions from every attached source into those frames, so
// the simulation never sees SDL types (specification section 7).
//
// Held directions are levels that the repeat system of section 4.5 reads every
// tick. The five button fields are edges: the adapter sets one on the single
// tick that carries the press, so a held button fires once and a press edge is
// consumed exactly once even when the main loop catches up through several
// simulation ticks in one frame (specification section 11, item 19).
//
// InputAdapter is pure bookkeeping over integers: no clocks, no platform
// calls, and no SDL. The headless suite drives it with synthetic button
// transitions; the SDL layer of src/input_sdl.cpp feeds it real events
// through one mapping table per device kind.

#include <cstdint>

namespace stackfall {

struct InputFrame {
  // Held directions, read every tick.
  bool leftHeld = false;
  bool rightHeld = false;
  bool downHeld = false;

  // Edge-triggered presses; each fires at most once (section 5).
  bool rotateCWPressed = false;
  bool rotateCCWPressed = false;
  bool hardDropPressed = false;
  bool holdPressed = false;
  bool pausePressed = false;

  // A frame with nothing held and nothing pressed.
  static InputFrame idle() { return InputFrame{}; }

  // The auto-shift direction this frame asks for: -1 for left only, +1 for
  // right only, and 0 when neither or both directions are held, because left
  // and right held together cause no horizontal movement (section 5).
  int horizontalDirection() const {
    if (leftHeld == rightHeld) {
      return 0;
    }
    return leftHeld ? -1 : 1;
  }
};

// The menu edges of one simulation tick. The screens of section 4.7 read these
// next to the gameplay frame: the same press feeds both halves when one
// control serves two roles, and the screen in charge picks its half.
struct MenuFrame {
  bool upPressed = false;      // D-pad up / Up
  bool downPressed = false;    // D-pad down / Down
  bool selectPressed = false;  // A / Enter
  bool backPressed = false;    // B / Esc
};

// Everything one simulation tick consumes from the adapter. Both halves are
// filled and every pending edge is consumed by the same takeTickInput() call,
// so a scene change made during a tick can never meet an edge that the same
// tick already spent.
struct TickInput {
  InputFrame game;
  MenuFrame menu;
};

// The abstract actions of specification section 5. A device mapping table
// translates its buttons onto these once; the adapter then knows nothing
// about controllers or keyboards. kNoGameAction marks an unused table slot.
enum class GameAction : std::uint8_t {
  Left = 0,     // held: move left
  Right,        // held: move right
  Down,         // held: soft drop
  RotateCW,     // edge: rotate clockwise
  RotateCCW,    // edge: rotate counter-clockwise
  HardDrop,     // edge: hard drop
  Hold,         // edge: hold the piece
  Pause,        // edge: pause or resume
  MenuUp,       // edge: menu cursor up
  MenuDown,     // edge: menu cursor down
  MenuSelect,   // edge: confirm a menu item
  MenuBack,     // edge: leave a menu
  kCount        // sentinel: one past the last real action
};

inline constexpr GameAction kNoGameAction = GameAction::kCount;
inline constexpr int kGameActionCount = static_cast<int>(GameAction::kCount);

// The keyboard's source id inside InputAdapter. SDL joystick instance ids are
// non-negative, so a negative id cannot collide with a controller. The value
// itself is plain data: the core never learns where it came from.
inline constexpr int kKeyboardInputSource = -1;

// Merges button transitions from every input source into one tick input.
//
// A source is one keyboard or one game controller, named by a caller-chosen
// int id. Sources appear on their first button event and disappear through
// dropSource(). Per source the adapter keeps a held bitmask and a pending-edge
// bitmask:
//
//   * Held bits merge across sources with OR, so the keyboard and a
//     controller can hold the same direction, and releasing one leaves the
//     other in charge.
//   * A press sets a pending edge bit. Edges of the same action from any
//     number of presses or sources collapse into one, and takeTickInput()
//     clears every pending bit on the tick that carries them. A press that
//     arrives between ticks waits in the pending set; a press whose source
//     disappears before a tick consumes it is dropped with the source.
//
// This is the machinery behind the section 5 rules that one press fires once
// no matter how many ticks the frame runs, and behind the section 11 item 19
// acceptance: a catch-up batch of N ticks sees an edge on its first tick only.
class InputAdapter {
 public:
  // Records one button transition of one source. A press queues the edge for
  // the next tick; a release only clears the held level.
  void setButton(int source, GameAction action, bool pressed) {
    SourceState* state = findSource(source);
    if (state == nullptr) {
      return;  // the source table is full; the extra device is ignored
    }
    const std::uint16_t bit = actionBit(action);
    if (pressed) {
      state->held |= bit;
      state->pending |= bit;
    } else {
      state->held &= static_cast<std::uint16_t>(~bit);
    }
  }

  // Forgets every held level and pending edge of `source`. Call this when a
  // controller disappears, so its buttons cannot stick and its unconsumed
  // presses cannot fire later.
  void dropSource(int source) {
    for (int i = 0; i < count_; ++i) {
      if (sources_[i].source == source) {
        sources_[i] = sources_[count_ - 1];
        --count_;
        return;
      }
    }
  }

  // Clears every held level of every source but keeps the sources and their
  // pending press edges. The SDL layer calls this on window focus loss:
  // releases may never arrive for buttons that were down when the window lost
  // focus, so nothing may stay held, but a press the window received while
  // focused still fires on its tick (specification section 11, item 19).
  void releaseAllHeld() {
    for (int i = 0; i < count_; ++i) {
      sources_[i].held = 0;
    }
  }

  // Forgets every held level and every pending press edge of every source,
  // keeping the sources themselves. Call this when the mapping tables change
  // under hands that may already hold a control - the orientation turn of
  // task T068: a level the old table raised survives into the new one, and
  // the release that follows arrives through the new table, where the same
  // control maps to a different action and cannot clear it, so a direction
  // would stay held forever. A pending edge waits for the same reason: it
  // holds the action the old table gave its press.
  void clearHeldAndPending() {
    for (int i = 0; i < count_; ++i) {
      sources_[i].held = 0;
      sources_[i].pending = 0;
    }
  }

  // True while at least one source holds the action.
  bool isHeld(GameAction action) const {
    const std::uint16_t bit = actionBit(action);
    for (int i = 0; i < count_; ++i) {
      if ((sources_[i].held & bit) != 0) {
        return true;
      }
    }
    return false;
  }

  // True while a press of the action waits for its consuming tick. Only edge
  // actions reach an InputFrame or MenuFrame; the queries exist for tests and
  // for the input log of recorded sessions.
  bool edgePending(GameAction action) const {
    const std::uint16_t bit = actionBit(action);
    for (int i = 0; i < count_; ++i) {
      if ((sources_[i].pending & bit) != 0) {
        return true;
      }
    }
    return false;
  }

  int sourceCount() const { return count_; }

  // Builds the input of one simulation tick: held levels merged over every
  // source, every pending edge placed on exactly this tick, and every pending
  // bit cleared. Call this once per tick, after the event poll, so a catch-up
  // batch of several ticks consumes each press exactly once.
  TickInput takeTickInput() {
    std::uint16_t held = 0;
    std::uint16_t pending = 0;
    for (int i = 0; i < count_; ++i) {
      held |= sources_[i].held;
      pending |= sources_[i].pending;
      sources_[i].pending = 0;
    }
    TickInput tick;
    tick.game.leftHeld = test(held, GameAction::Left);
    tick.game.rightHeld = test(held, GameAction::Right);
    tick.game.downHeld = test(held, GameAction::Down);
    tick.game.rotateCWPressed = test(pending, GameAction::RotateCW);
    tick.game.rotateCCWPressed = test(pending, GameAction::RotateCCW);
    tick.game.hardDropPressed = test(pending, GameAction::HardDrop);
    tick.game.holdPressed = test(pending, GameAction::Hold);
    tick.game.pausePressed = test(pending, GameAction::Pause);
    tick.menu.upPressed = test(pending, GameAction::MenuUp);
    tick.menu.downPressed = test(pending, GameAction::MenuDown);
    tick.menu.selectPressed = test(pending, GameAction::MenuSelect);
    tick.menu.backPressed = test(pending, GameAction::MenuBack);
    return tick;
  }

 private:
  // One keyboard or controller. Twelve actions fit one uint16_t.
  struct SourceState {
    int source;
    std::uint16_t held;     // actions currently down on this source
    std::uint16_t pending;  // press edges waiting for their tick
  };

  static constexpr int kMaxSources = 8;  // keyboard plus spare controllers

  static std::uint16_t actionBit(GameAction action) {
    return static_cast<std::uint16_t>(1u << static_cast<int>(action));
  }

  static bool test(std::uint16_t mask, GameAction action) {
    return (mask & actionBit(action)) != 0;
  }

  SourceState* findSource(int source) {
    for (int i = 0; i < count_; ++i) {
      if (sources_[i].source == source) {
        return &sources_[i];
      }
    }
    if (count_ >= kMaxSources) {
      return nullptr;
    }
    sources_[count_] = SourceState{source, 0, 0};
    return &sources_[count_++];
  }

  SourceState sources_[kMaxSources];
  int count_ = 0;
};

}  // namespace stackfall
