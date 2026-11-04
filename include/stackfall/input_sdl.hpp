#pragma once

// The SDL half of input translation for STACKFALL (specification section 5,
// task T009). SdlInput owns the one controller mapping table and the one
// keyboard mapping table of section 5, turns polled SDL events into
// InputAdapter button transitions, and loads extra controller mappings from
// the SDL_GAMECONTROLLERCONFIG string that the PortMaster launcher exports.
//
// Keep every SDL type inside this header and src/input_sdl.cpp: the rules
// core, including InputAdapter in stackfall/input.hpp, stays SDL-free, so the
// headless suites never link SDL (specification section 7).
//
// If the physical RG503 buttons disagree with the SDL button letters across
// layouts, only the controller table in src/input_sdl.cpp changes; nothing
// else in the source names a controller button.

#include <SDL.h>

#include "stackfall/input.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

class SdlInput {
 public:
  // The orientation the tables translate into (task T055). Portrait remaps
  // every direction into the rotated frame and reverses the two cluster
  // pairs, exactly as notes/design-review.md pins it; landscape keeps the
  // section 5 table. The platform layer sets this once at startup from the
  // persisted preference, and the default is landscape.
  void setOrientation(Orientation orientation) noexcept {
    orientation_ = orientation;
  }
  Orientation orientation() const noexcept { return orientation_; }

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
  Orientation orientation_ = Orientation::Landscape;
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

// Resolves one mapped control to its actions, straight from the tables this
// header's class uses. Writes at most two actions and returns how many; zero
// means the control is unmapped. Tests and the controls screen read the same
// translation the event path applies. The orientation argument selects the
// table: landscape is the section 5 mapping, portrait the rotated one of
// notes/design-review.md (task T055); the two-argument forms are the landscape
// table, which every pre-portrait caller used.
int mapControllerButton(SDL_GameControllerButton button, GameAction* actions);
int mapKeyboardScancode(SDL_Scancode scancode, GameAction* actions);
int mapControllerButton(SDL_GameControllerButton button, Orientation orientation,
                        GameAction* actions);
int mapKeyboardScancode(SDL_Scancode scancode, Orientation orientation,
                        GameAction* actions);

}  // namespace stackfall
