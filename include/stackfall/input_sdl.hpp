#pragma once

// The SDL half of input translation for STACKFALL (specification section 5,
// task T009). SdlInput owns the one controller mapping table and the one
// keyboard mapping table of section 5, turns polled SDL events into
// InputAdapter button transitions, and loads extra controller mappings from
// the SDL_GAMECONTROLLERCONFIG string that the PortMaster launcher exports.
//
// Since task T066 those two tables are the rebindable ControlBindings of
// stackfall/bindings.hpp: the section 5 defaults arrive from
// defaultControlBindings(), the controls file of specification section 9
// replaces them at startup, and the controls customization screen rebinds a
// gameplay action mid-session through setControlBindings(). The fixed menu
// roles of bindings.hpp ride along beside the gameplay actions, so whatever
// the tables hold, every menu stays navigable.
//
// The same class owns the capture half of the customization screen: while a
// capture runs, the next real press of a rebindable control stops at the raw
// control id instead of becoming adapter state, so the press that names a new
// binding can never also rotate a piece or move a cursor.
//
// Keep every SDL type inside this header and src/input_sdl.cpp: the rules
// core, including InputAdapter in stackfall/input.hpp, stays SDL-free, so the
// headless suites never link SDL (specification section 7).
//
// If the physical RG503 buttons disagree with the SDL button letters across
// layouts, only the controller table in src/input_sdl.cpp changes; nothing
// else in the source names a controller button.

#include <SDL.h>

#include "stackfall/bindings.hpp"
#include "stackfall/input.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// The result of one capture: which control of which device the player pressed
// while SdlInput was capturing, or `happened == false` when nothing did.
struct InputCapture {
  bool happened = false;
  DeviceKind device = DeviceKind::Keyboard;
  int control = kNoControl;
};

class SdlInput {
 public:
  SdlInput() = default;

  // The orientation the tables translate into (task T055). Portrait remaps
  // every direction into the rotated frame and reverses the two cluster
  // pairs, exactly as notes/design-review.md pins it; landscape keeps the
  // section 5 table. Both tables put hard drop on the logical up direction
  // of their own frame (task T067). The platform layer sets this at startup
  // from the persisted preference and again on every ORIENTATION select,
  // because the selection turns the running session at once (task T068); the
  // default is landscape.
  //
  // The change also forgets every held level and pending press edge: a
  // control held across the turn keeps the action the old table gave it, and
  // its release then arrives through the new table, where it maps to a
  // different action and cannot clear the stuck level (task T068). The press
  // that selected the row has already spent its edge on the tick that carried
  // it, so this cannot cost an input the player made.
  void setOrientation(Orientation orientation) noexcept {
    orientation_ = orientation;
    adapter_.clearHeldAndPending();
  }
  Orientation orientation() const noexcept { return orientation_; }

  // The binding tables in force (task T066). The session starts on the
  // section 5 defaults, the platform layer replaces them once at startup from
  // the controls file, and the customization screen replaces them again on
  // every applied capture and every RESTORE DEFAULTS. The change forgets
  // every held level and pending edge for the same reason the orientation
  // turn does: a control held across the change keeps the action the old
  // table gave it, and its release cannot clear a level the new table never
  // raised. A capture never changes the tables here; the platform layer
  // validates the conflict rule of bindings.hpp first.
  void setControlBindings(const ControlBindings& bindings) {
    bindings_ = bindings;
    adapter_.clearHeldAndPending();
  }
  const ControlBindings& controlBindings() const noexcept { return bindings_; }

  // The capture half of the customization screen (task T066). beginCapture()
  // arms one capture; the next non-repeated press of a rebindable control of
  // either device stops at takeCapture() instead of reaching the adapter, so
  // a capture press never becomes gameplay or menu input. An auto-repeated
  // key event is not a press (specification section 5): the capture waits for
  // a fresh one, exactly as the adapter does. A control outside the
  // rebindable lists changes nothing and the capture stays armed. Cancelling
  // leaves the binding tables untouched.
  void beginCapture() noexcept { capture_.happened = false; capturing_ = true; }
  void cancelCapture() noexcept {
    capturing_ = false;
    capture_ = InputCapture{};
  }
  bool capturing() const noexcept { return capturing_; }

  // Returns and clears the capture that arrived since the last call. A second
  // call before another capture reports `happened == false`.
  InputCapture takeCapture() noexcept {
    const InputCapture result = capture_;
    capture_ = InputCapture{};
    return result;
  }

  // Adds every mapping line of the SDL_GAMECONTROLLERCONFIG environment
  // variable through SDL_GameControllerAddMapping and returns how many lines
  // SDL accepted (added or updated). Empty or absent variables load nothing;
  // malformed lines are skipped and logged, because one bad PortMaster line
  // must not cost the working ones. SDL already reads this variable during
  // SDL_Init(SDL_INIT_GAMECONTROLLER); this call covers values that appear
  // after initialization and logs what the environment provided.
  //
  // The game controller subsystem must be initialized.
  int loadEnvironmentMappings();

  // Translates one polled SDL event into adapter state: keyboard key events,
  // game controller button events, and the events that end input (focus loss
  // and controller removal). Device discovery stays with the caller's
  // controller set; this method only reads input-carrying events.
  void handleEvent(const SDL_Event& event);

  // The input of one simulation tick, straight from the adapter. Consume it
  // exactly once per tick after polling events (specification section 5).
  TickInput takeTickInput() { return adapter_.takeTickInput(); }

  // Read-only view for tests and the input log.
  const InputAdapter& adapter() const { return adapter_; }

 private:
  // One mapped button press or release of one source.
  void onButton(int source, GameAction action, bool pressed) {
    adapter_.setButton(source, action, pressed);
  }

  InputAdapter adapter_;
  ControlBindings bindings_ = defaultControlBindings();
  Orientation orientation_ = Orientation::Landscape;
  bool capturing_ = false;
  InputCapture capture_;
};

// ---------------------------------------------------------------------------
// Developer virtual controller (specification section 5 testing policy and
// docs/AGENT_TESTING.md, T023). The functions attach an SDL virtual joystick
// laid out like an RG503 gamepad so automated runs can exercise the real SDL
// game controller path without a physical device.
// ---------------------------------------------------------------------------

// Button indices of the virtual pad, referenced by the mapping string and by
// test and script drivers.
enum VirtualPadButton : int {
  kVirtualButtonA = 0,
  kVirtualButtonB,
  kVirtualButtonX,
  kVirtualButtonY,
  kVirtualButtonLeftShoulder,
  kVirtualButtonStart,
  kVirtualButtonDpadUp,
  kVirtualButtonDpadDown,
  kVirtualButtonDpadLeft,
  kVirtualButtonDpadRight,
  kVirtualButtonCount
};

// Attaches the virtual joystick and returns its device index, or -1 when SDL
// rejected the descriptor. The joystick carries no controller mapping until
// addVirtualControllerMapping() or an SDL_GAMECONTROLLERCONFIG line gives it
// one, so a test can prove either mapping path on its own.
int attachVirtualJoystick();

// Writes the developer mapping for the attached virtual joystick and returns
// true when SDL accepted it. The mapping follows the section 5 table: the
// virtual buttons land on a, b, x, y, left shoulder, start, and the D-pad.
bool addVirtualControllerMapping(int deviceIndex);

// Attaches the virtual joystick and maps it in one step: the form the game
// binary uses for its --virtual-controller developer option.
int attachVirtualController();

// Detaches the virtual joystick. SDL queues a removal event for every open
// handle; closing those handles stays with their owners. Returns true on
// success.
bool detachVirtualController(int deviceIndex);

// Resolves one script or test name to a virtual pad button index; -1 when the
// name is unknown. Accepted names: a, b, x, y, l1, start, up, down, left,
// right.
int virtualPadButtonFromName(const char* name);

// ---------------------------------------------------------------------------
// The device translations (task T066). One table per device turns a control
// id of stackfall/bindings.hpp into the SDL enum the event carries, and back.
// These are the only places in the source that name an SDL scancode or an SDL
// controller button, so the rebindable lists and the SDL events cannot drift
// apart.
// ---------------------------------------------------------------------------

// The scancode of `control`, or SDL_SCANCODE_UNKNOWN when the id is out of
// range.
SDL_Scancode scancodeOfKeyboardControl(int control);

// The control of `scancode`, or kNoControl when no rebindable control sits on
// that key. Every other key - including the M and Q the platform layer keeps
// as fixed shortcuts - is no control at all.
int keyboardControlOfScancode(SDL_Scancode scancode);

// The controller button of `control`, or SDL_CONTROLLER_BUTTON_INVALID.
SDL_GameControllerButton controllerButtonOfControl(int control);

// The control of `button`, or kNoControl when the button sits outside the
// ten-button RG503 layout.
int controllerControlOfButton(SDL_GameControllerButton button);

// Resolves one mapped control to its actions, straight from the tables this
// header's class uses. Writes at most two actions - the gameplay action the
// control performs, then the menu role it carries - and returns how many;
// zero means the control is unmapped. Tests and the controls screen read the
// same translation the event path applies. The orientation argument selects
// the table: landscape is the section 5 mapping with D-pad up hard drop (task
// T067), portrait the rotated one of notes/design-review.md (tasks T055 and
// T067).
int mapControllerButton(SDL_GameControllerButton button, Orientation orientation,
                        const ControlBindings& bindings, GameAction* actions);
int mapKeyboardScancode(SDL_Scancode scancode, Orientation orientation,
                        const ControlBindings& bindings, GameAction* actions);

}  // namespace stackfall
