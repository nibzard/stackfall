// SDL input translation for STACKFALL (specification section 5, task T009).
//
// This file holds the two mapping tables of section 5 and nothing else knows
// a scancode or a controller button:
//
//   * kControllerBindings maps SDL game controller buttons. If the physical
//     RG503 markings disagree with the SDL button letters across layouts,
//     change this table only (specification section 5).
//   * kKeyboardBindings maps keyboard scancodes for desktop play and tests.
//
// Everything the tables produce is a GameAction for the SDL-free InputAdapter
// in stackfall/input.hpp, which owns the once-per-tick edge semantics. Events
// arrive here only after the main loop polled them, and always before the
// simulation ticks of the same frame (specification section 5).

#include "stackfall/input_sdl.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace stackfall {
namespace {

// One mapped device control and the up-to-two actions it performs. Several
// controls serve two roles: A rotates and confirms a menu item, B rotates the
// other way and leaves a menu, Enter pauses and confirms, Esc pauses and goes
// back, Up rotates and moves the menu cursor (specification section 5).
// kNoGameAction marks an unused secondary slot.
struct ControllerBinding {
  SDL_GameControllerButton button;
  GameAction primary;
  GameAction secondary;
};

struct KeyboardBinding {
  SDL_Scancode scancode;
  GameAction primary;
  GameAction secondary;
};

// The section 5 RG503 table: D-pad move and soft drop, A clockwise, B
// counter-clockwise, X hard drop, Y or left shoulder hold, Start pause, and
// the menu roles of A, B, and the D-pad: D-pad up and down move the menu
// cursor, so both carry their menu action next to any gameplay action.
constexpr ControllerBinding kControllerBindings[] = {
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, GameAction::Left, kNoGameAction},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GameAction::Right, kNoGameAction},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, GameAction::Down, GameAction::MenuDown},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, GameAction::MenuUp, kNoGameAction},
    {SDL_CONTROLLER_BUTTON_A, GameAction::RotateCW, GameAction::MenuSelect},
    {SDL_CONTROLLER_BUTTON_B, GameAction::RotateCCW, GameAction::MenuBack},
    {SDL_CONTROLLER_BUTTON_X, GameAction::HardDrop, kNoGameAction},
    {SDL_CONTROLLER_BUTTON_Y, GameAction::Hold, kNoGameAction},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, GameAction::Hold, kNoGameAction},
    {SDL_CONTROLLER_BUTTON_START, GameAction::Pause, kNoGameAction},
};

// The section 5 desktop table: arrows move and soft drop, X or Up rotate
// clockwise, Z rotates counter-clockwise, Space hard drops, C or left Shift
// holds, Enter or Esc pauses, plus the menu roles of Enter, Esc, Up, and
// Down. Scancodes, not keycodes, keep the layout physical: Z stays the
// bottom-left key whatever the operating system prints on it.
constexpr KeyboardBinding kKeyboardBindings[] = {
    {SDL_SCANCODE_LEFT, GameAction::Left, kNoGameAction},
    {SDL_SCANCODE_RIGHT, GameAction::Right, kNoGameAction},
    {SDL_SCANCODE_DOWN, GameAction::Down, GameAction::MenuDown},
    {SDL_SCANCODE_UP, GameAction::RotateCW, GameAction::MenuUp},
    {SDL_SCANCODE_X, GameAction::RotateCW, kNoGameAction},
    {SDL_SCANCODE_Z, GameAction::RotateCCW, kNoGameAction},
    {SDL_SCANCODE_SPACE, GameAction::HardDrop, kNoGameAction},
    {SDL_SCANCODE_C, GameAction::Hold, kNoGameAction},
    {SDL_SCANCODE_LSHIFT, GameAction::Hold, kNoGameAction},
    {SDL_SCANCODE_RETURN, GameAction::Pause, GameAction::MenuSelect},
    {SDL_SCANCODE_ESCAPE, GameAction::Pause, GameAction::MenuBack},
};

// Writes the actions of `binding` into `actions` and returns how many: zero
// for an unmapped control, one or two otherwise.
template <typename Binding>
int bindingActions(const Binding& binding, GameAction* actions) {
  int count = 0;
  if (binding.primary != kNoGameAction) {
    actions[count++] = binding.primary;
  }
  if (binding.secondary != kNoGameAction) {
    actions[count++] = binding.secondary;
  }
  return count;
}

// The name of the virtual pad, stable storage for the SDL descriptor.
constexpr char kVirtualPadName[] = "STACKFALL Virtual Pad";

// Fictional USB IDs of the virtual pad, chosen so no real device shares
// them. When these are zero, SDL packs the pad name into the device GUID,
// and SDL 2.26 then refuses the exact-GUID mapping this file adds right
// after attaching (SDL_GameControllerOpen fails with "Couldn't find
// mapping for device"). Explicit IDs give every SDL version the same
// stable GUID, free of name bytes. The PortMaster-aligned Ubuntu 22.04
// environment of SPEC section 10.2 builds against SDL 2.26.2; the desktop
// SDL 2.30 accepts either form.
constexpr Uint16 kVirtualPadVendorId = 0xFAC7;
constexpr Uint16 kVirtualPadProductId = 0xFA11;

}  // namespace

// ---------------------------------------------------------------------------
// Mapping lookups. Both live here so no other translation exists anywhere.
// ---------------------------------------------------------------------------

int mapControllerButton(SDL_GameControllerButton button, GameAction* actions) {
  for (const ControllerBinding& binding : kControllerBindings) {
    if (binding.button == button) {
      return bindingActions(binding, actions);
    }
  }
  return 0;
}

int mapKeyboardScancode(SDL_Scancode scancode, GameAction* actions) {
  for (const KeyboardBinding& binding : kKeyboardBindings) {
    if (binding.scancode == scancode) {
      return bindingActions(binding, actions);
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// SdlInput.
// ---------------------------------------------------------------------------

int SdlInput::loadEnvironmentMappings() {
  const char* config = std::getenv("SDL_GAMECONTROLLERCONFIG");
  if (config == nullptr || config[0] == '\0') {
    std::printf("SDL_GAMECONTROLLERCONFIG not set; built-in mappings only\n");
    return 0;
  }
  int accepted = 0;
  int rejected = 0;
  // One mapping per line, the PortMaster format. Feed SDL each line on its
  // own so one malformed entry costs only itself.
  const char* line = config;
  while (line != nullptr && *line != '\0') {
    const char* end = std::strchr(line, '\n');
    const std::string entry(line, end != nullptr
                                      ? static_cast<std::size_t>(end - line)
                                      : std::strlen(line));
    line = end != nullptr ? end + 1 : nullptr;
    if (entry.empty()) {
      continue;
    }
    const int result = SDL_GameControllerAddMapping(entry.c_str());
    if (result >= 0) {
      ++accepted;
    } else {
      ++rejected;
      std::printf("SDL_GAMECONTROLLERCONFIG line rejected: %s (%s)\n",
                  entry.c_str(), SDL_GetError());
    }
  }
  std::printf("SDL_GAMECONTROLLERCONFIG: %d mapping(s) loaded, %d rejected\n",
              accepted, rejected);
  return accepted;
}

void SdlInput::handleEvent(const SDL_Event& event) {
  switch (event.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      // An auto-repeated key down carries no fresh edge, and the level is
      // already held, so repeats change nothing.
      if (event.type == SDL_KEYDOWN && event.key.repeat != 0) {
        break;
      }
      GameAction actions[2];
      const int count =
          mapKeyboardScancode(event.key.keysym.scancode, actions);
      const bool pressed = event.type == SDL_KEYDOWN;
      for (int i = 0; i < count; ++i) {
        onButton(kKeyboardInputSource, actions[i], pressed);
      }
      break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
      GameAction actions[2];
      const int count = mapControllerButton(
          static_cast<SDL_GameControllerButton>(event.cbutton.button),
          actions);
      const bool pressed = event.type == SDL_CONTROLLERBUTTONDOWN;
      const int source = static_cast<int>(event.cbutton.which);
      for (int i = 0; i < count; ++i) {
        onButton(source, actions[i], pressed);
      }
      break;
    }
    case SDL_CONTROLLERDEVICEREMOVED:
      // The disconnected pad stops contributing: its held buttons cannot
      // stick and its unconsumed press edges cannot fire later
      // (specification section 5).
      adapter_.dropSource(static_cast<int>(event.cdevice.which));
      break;
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        // Key and button releases may never arrive for controls that were
        // down when the window lost focus, so every held level clears. A
        // button still held through refocus must be pressed again. Press
        // edges the window already received stay queued: the player really
        // pressed them (specification section 11, item 19).
        std::printf("Window focus lost; every held button cleared\n");
        adapter_.releaseAllHeld();
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Developer virtual controller.
// ---------------------------------------------------------------------------

int attachVirtualJoystick() {
  SDL_VirtualJoystickDesc desc;
  SDL_zero(desc);
  desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  // Type UNKNOWN on purpose: SDL hands GAMECONTROLLER-typed joysticks an
  // auto-generated default mapping, and an unmapped pad is what a mapping
  // test needs as its starting point. The layout itself is a gamepad: the
  // ten buttons of the section 5 table.
  desc.type = SDL_JOYSTICK_TYPE_UNKNOWN;
  desc.vendor_id = kVirtualPadVendorId;
  desc.product_id = kVirtualPadProductId;
  desc.naxes = 0;
  desc.nbuttons = kVirtualButtonCount;
  desc.nhats = 0;
  desc.name = kVirtualPadName;
  const int deviceIndex = SDL_JoystickAttachVirtualEx(&desc);
  if (deviceIndex < 0) {
    std::printf("Virtual controller attach failed: %s\n", SDL_GetError());
    return -1;
  }
  std::printf("Virtual controller attached as device %d\n", deviceIndex);
  return deviceIndex;
}

bool addVirtualControllerMapping(int deviceIndex) {
  const SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(deviceIndex);
  char guidString[33];
  SDL_JoystickGetGUIDString(guid, guidString, sizeof(guidString));
  // The section 5 subset: every button the game reads, laid out in
  // VirtualPadButton order.
  std::string mapping = guidString;
  mapping += ",STACKFALL Virtual Pad,a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,"
             "start:b5,dpup:b6,dpdown:b7,dpleft:b8,dpright:b9,platform:";
  mapping += SDL_GetPlatform();
  const int result = SDL_GameControllerAddMapping(mapping.c_str());
  if (result < 0) {
    std::printf("Virtual controller mapping rejected: %s\n", SDL_GetError());
    return false;
  }
  std::printf("Virtual controller %s (guid %s)\n",
              result == 1 ? "mapping added" : "mapping updated", guidString);
  return true;
}

int attachVirtualController() {
  const int deviceIndex = attachVirtualJoystick();
  if (deviceIndex < 0) {
    return -1;
  }
  if (!addVirtualControllerMapping(deviceIndex)) {
    SDL_JoystickDetachVirtual(deviceIndex);
    return -1;
  }
  return deviceIndex;
}

bool detachVirtualController(int deviceIndex) {
  if (SDL_JoystickDetachVirtual(deviceIndex) != 0) {
    std::printf("Virtual controller detach failed: %s\n", SDL_GetError());
    return false;
  }
  std::printf("Virtual controller detached\n");
  return true;
}

int virtualPadButtonFromName(const char* name) {
  struct NamedButton {
    const char* name;
    int button;
  };
  static const NamedButton names[] = {
      {"a", kVirtualButtonA},
      {"b", kVirtualButtonB},
      {"x", kVirtualButtonX},
      {"y", kVirtualButtonY},
      {"l1", kVirtualButtonLeftShoulder},
      {"start", kVirtualButtonStart},
      {"up", kVirtualButtonDpadUp},
      {"down", kVirtualButtonDpadDown},
      {"left", kVirtualButtonDpadLeft},
      {"right", kVirtualButtonDpadRight},
  };
  for (const NamedButton& entry : names) {
    if (std::strcmp(entry.name, name) == 0) {
      return entry.button;
    }
  }
  return -1;
}

}  // namespace stackfall
