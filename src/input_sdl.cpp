// SDL input translation for STACKFALL (specification section 5, task T009).
//
// This file holds the two SDL device tables and nothing else knows a scancode
// or a controller button:
//
//   * kKeyboardScancodes maps the rebindable keyboard controls of
//     stackfall/bindings.hpp onto SDL scancodes for desktop play and tests.
//   * kControllerButtons maps the ten rebindable controller controls onto SDL
//     game controller buttons. If the physical RG503 markings disagree with
//     the SDL button letters across layouts, change this table only
//     (specification section 5).
//
// The gameplay actions those controls perform live in the ControlBindings the
// session owns - the section 5 defaults, the controls file, or whatever the
// customization screen bound - and the fixed menu roles sit beside them in
// stackfall/bindings.cpp. Everything the two halves produce is a GameAction
// for the SDL-free InputAdapter in stackfall/input.hpp, which owns the
// once-per-tick edge semantics. Events arrive here only after the main loop
// polled them, and always before the simulation ticks of the same frame
// (specification section 5).

#include "stackfall/input_sdl.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace stackfall {
namespace {

// The keyboard half of the device tables, in KeyboardControl order. Scancodes,
// not keycodes, keep the layout physical: Z stays the bottom-left key whatever
// the operating system prints on it.
constexpr SDL_Scancode kKeyboardScancodes[kKeyboardControlCount] = {
    SDL_SCANCODE_LEFT,      // kKeyboardKeyLeft
    SDL_SCANCODE_RIGHT,     // kKeyboardKeyRight
    SDL_SCANCODE_UP,        // kKeyboardKeyUp
    SDL_SCANCODE_DOWN,      // kKeyboardKeyDown
    SDL_SCANCODE_SPACE,     // kKeyboardKeySpace
    SDL_SCANCODE_RETURN,    // kKeyboardKeyEnter
    SDL_SCANCODE_ESCAPE,    // kKeyboardKeyEscape
    SDL_SCANCODE_LSHIFT,    // kKeyboardKeyLeftShift
    SDL_SCANCODE_RSHIFT,    // kKeyboardKeyRightShift
    SDL_SCANCODE_LCTRL,     // kKeyboardKeyLeftCtrl
    SDL_SCANCODE_TAB,       // kKeyboardKeyTab
    SDL_SCANCODE_BACKSPACE, // kKeyboardKeyBackspace
    SDL_SCANCODE_A,         // kKeyboardKeyA
    SDL_SCANCODE_B,         // kKeyboardKeyB
    SDL_SCANCODE_C,         // kKeyboardKeyC
    SDL_SCANCODE_D,         // kKeyboardKeyD
    SDL_SCANCODE_E,         // kKeyboardKeyE
    SDL_SCANCODE_F,         // kKeyboardKeyF
    SDL_SCANCODE_G,         // kKeyboardKeyG
    SDL_SCANCODE_H,         // kKeyboardKeyH
    SDL_SCANCODE_I,         // kKeyboardKeyI
    SDL_SCANCODE_J,         // kKeyboardKeyJ
    SDL_SCANCODE_K,         // kKeyboardKeyK
    SDL_SCANCODE_L,         // kKeyboardKeyL
    SDL_SCANCODE_N,         // kKeyboardKeyN
    SDL_SCANCODE_O,         // kKeyboardKeyO
    SDL_SCANCODE_P,         // kKeyboardKeyP
    SDL_SCANCODE_R,         // kKeyboardKeyR
    SDL_SCANCODE_S,         // kKeyboardKeyS
    SDL_SCANCODE_T,         // kKeyboardKeyT
    SDL_SCANCODE_U,         // kKeyboardKeyU
    SDL_SCANCODE_V,         // kKeyboardKeyV
    SDL_SCANCODE_W,         // kKeyboardKeyW
    SDL_SCANCODE_X,         // kKeyboardKeyX
    SDL_SCANCODE_Y,         // kKeyboardKeyY
    SDL_SCANCODE_Z,         // kKeyboardKeyZ
};

// The controller half, in ControllerControl order: the ABXY cluster, the left
// shoulder, Start, and the four D-pad arms of the RG503 layout.
constexpr SDL_GameControllerButton kControllerButtons[kControllerControlCount] = {
    SDL_CONTROLLER_BUTTON_A,             // kControllerButtonA
    SDL_CONTROLLER_BUTTON_B,             // kControllerButtonB
    SDL_CONTROLLER_BUTTON_X,             // kControllerButtonX
    SDL_CONTROLLER_BUTTON_Y,             // kControllerButtonY
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  // kControllerButtonL1
    SDL_CONTROLLER_BUTTON_START,         // kControllerButtonStart
    SDL_CONTROLLER_BUTTON_DPAD_UP,       // kControllerButtonUp
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,     // kControllerButtonDown
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,     // kControllerButtonLeft
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,    // kControllerButtonRight
};

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
// The device translations. Both live here so no other translation exists
// anywhere.
// ---------------------------------------------------------------------------

SDL_Scancode scancodeOfKeyboardControl(int control) {
  if (control < 0 || control >= kKeyboardControlCount) {
    return SDL_SCANCODE_UNKNOWN;
  }
  return kKeyboardScancodes[control];
}

int keyboardControlOfScancode(SDL_Scancode scancode) {
  for (int control = 0; control < kKeyboardControlCount; ++control) {
    if (kKeyboardScancodes[control] == scancode) {
      return control;
    }
  }
  return kNoControl;
}

SDL_GameControllerButton controllerButtonOfControl(int control) {
  if (control < 0 || control >= kControllerControlCount) {
    return SDL_CONTROLLER_BUTTON_INVALID;
  }
  return kControllerButtons[control];
}

int controllerControlOfButton(SDL_GameControllerButton button) {
  for (int control = 0; control < kControllerControlCount; ++control) {
    if (kControllerButtons[control] == button) {
      return control;
    }
  }
  return kNoControl;
}

// ---------------------------------------------------------------------------
// Mapping lookups. Both live here so no other translation exists anywhere.
// ---------------------------------------------------------------------------

int mapControllerButton(SDL_GameControllerButton button,
                        Orientation orientation, const ControlBindings& bindings,
                        GameAction* actions) {
  const int control = controllerControlOfButton(button);
  if (control == kNoControl) {
    return 0;
  }
  const ControlTable& table =
      bindings.table(DeviceKind::Controller, orientation);
  int count = 0;
  const int action = actionUsingControl(table, control);
  if (action >= 0) {
    actions[count++] = kRebindableActions[action];
  }
  const GameAction role = menuRoleOfControl(DeviceKind::Controller, control,
                                            orientation);
  if (role != kNoGameAction) {
    actions[count++] = role;
  }
  return count;
}

int mapKeyboardScancode(SDL_Scancode scancode, Orientation orientation,
                        const ControlBindings& bindings, GameAction* actions) {
  const int control = keyboardControlOfScancode(scancode);
  if (control == kNoControl) {
    return 0;
  }
  const ControlTable& table = bindings.table(DeviceKind::Keyboard, orientation);
  int count = 0;
  const int action = actionUsingControl(table, control);
  if (action >= 0) {
    actions[count++] = kRebindableActions[action];
  }
  const GameAction role =
      menuRoleOfControl(DeviceKind::Keyboard, control, orientation);
  if (role != kNoGameAction) {
    actions[count++] = role;
  }
  return count;
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
      // already held, so repeats change nothing. A capture waits for a real
      // press the same way (task T066).
      if (event.type == SDL_KEYDOWN && event.key.repeat != 0) {
        break;
      }
      const int control = keyboardControlOfScancode(event.key.keysym.scancode);
      const bool pressed = event.type == SDL_KEYDOWN;
      if (pressed && control != kNoControl && capturing_) {
        // The customization screen wants the control itself, not its action:
        // the press that names a new binding never reaches the adapter, so it
        // cannot rotate a piece or move a cursor on its way in. A release is
        // never a capture; it only clears the level the press raised, which a
        // captured press never did.
        capture_ = InputCapture{true, DeviceKind::Keyboard, control};
        capturing_ = false;
        break;
      }
      GameAction actions[2];
      const int count = mapKeyboardScancode(event.key.keysym.scancode,
                                            orientation_, bindings_, actions);
      for (int i = 0; i < count; ++i) {
        onButton(kKeyboardInputSource, actions[i], pressed);
      }
      break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
      const auto button =
          static_cast<SDL_GameControllerButton>(event.cbutton.button);
      const int control = controllerControlOfButton(button);
      if (control != kNoControl && capturing_ &&
          event.type == SDL_CONTROLLERBUTTONDOWN) {
        capture_ = InputCapture{true, DeviceKind::Controller, control};
        capturing_ = false;
        break;
      }
      GameAction actions[2];
      const int count =
          mapControllerButton(button, orientation_, bindings_, actions);
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
      // (specification section 5). A capture in flight survives the loss:
      // the next press of whichever pad is attached names the control.
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
