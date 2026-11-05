// The rebindable control bindings of task T066 (specification section 5).
//
// This file holds the control vocabularies, the section 5 defaults, and the
// validity rule. It is plain data and integer walks: no SDL, no clock, and no
// game state, so it links into stackfall_core beside the rules and every
// suite - headless, SDL, and the reference replay - shares one word list.

#include "stackfall/bindings.hpp"

#include <cstring>

namespace stackfall {
namespace {

// One control's names: the canonical word of the file format, the script
// language, and the reference replay, and the screen spelling of each
// orientation. The four direction controls carry their own portrait word
// because the turned device points them at a different on-screen direction -
// the D-pad's left arm is the logical up of the portrait grip, which is where
// hard drop sits (task T067) - while every other control spells itself the
// same way in both frames.
struct ControlNames {
  const char* name;
  const char* landscape;
  const char* portrait;
};

const ControlNames kKeyboardNames[kKeyboardControlCount] = {
    {"left", "LEFT ARROW", "LEFT ARROW (UP)"},
    {"right", "RIGHT ARROW", "RIGHT ARROW (DOWN)"},
    {"up", "UP ARROW", "UP ARROW (RIGHT)"},
    {"down", "DOWN ARROW", "DOWN ARROW (LEFT)"},
    {"space", "SPACE", "SPACE"},
    {"enter", "ENTER", "ENTER"},
    {"esc", "ESC", "ESC"},
    {"lshift", "LEFT SHIFT", "LEFT SHIFT"},
    {"rshift", "RIGHT SHIFT", "RIGHT SHIFT"},
    {"lctrl", "LEFT CTRL", "LEFT CTRL"},
    {"tab", "TAB", "TAB"},
    {"backspace", "BACKSPACE", "BACKSPACE"},
    {"a", "A", "A"},
    {"b", "B", "B"},
    {"c", "C", "C"},
    {"d", "D", "D"},
    {"e", "E", "E"},
    {"f", "F", "F"},
    {"g", "G", "G"},
    {"h", "H", "H"},
    {"i", "I", "I"},
    {"j", "J", "J"},
    {"k", "K", "K"},
    {"l", "L", "L"},
    {"n", "N", "N"},
    {"o", "O", "O"},
    {"p", "P", "P"},
    {"r", "R", "R"},
    {"s", "S", "S"},
    {"t", "T", "T"},
    {"u", "U", "U"},
    {"v", "V", "V"},
    {"w", "W", "W"},
    {"x", "X", "X"},
    {"y", "Y", "Y"},
    {"z", "Z", "Z"},
};

const ControlNames kControllerNames[kControllerControlCount] = {
    {"a", "A", "A"},
    {"b", "B", "B"},
    {"x", "X", "X"},
    {"y", "Y", "Y"},
    {"l1", "L1", "L1"},
    {"start", "START", "START"},
    {"up", "D-PAD UP", "D-PAD UP (RIGHT)"},
    {"down", "D-PAD DOWN", "D-PAD DOWN (LEFT)"},
    {"left", "D-PAD LEFT", "D-PAD LEFT (UP)"},
    {"right", "D-PAD RIGHT", "D-PAD RIGHT (DOWN)"},
};

// The screen names of the eight rebindable actions, in action order. The
// customization screen and the controls screen draw these beside the control
// names, and the conflict message of a rejected capture repeats the one the
// player tried to double-book.
const char* const kActionNames[kRebindableActionCount] = {
    "MOVE LEFT", "MOVE RIGHT",  "SOFT DROP", "ROTATE CW",
    "ROTATE CCW", "HARD DROP",  "HOLD",      "PAUSE OR RESUME",
};

// The canonical action names of the controls file, in the same order.
const char* const kActionKeyNames[kRebindableActionCount] = {
    "left", "right", "down", "rotatecw", "rotateccw", "harddrop", "hold",
    "pause",
};

// The lookups of one device, shared by the four public name functions so
// keyboard and controller cannot grow different rules.
const ControlNames* namesOf(DeviceKind device, int* count) {
  *count = device == DeviceKind::Keyboard ? kKeyboardControlCount
                                          : kControllerControlCount;
  return device == DeviceKind::Keyboard ? kKeyboardNames : kControllerNames;
}

const char* displayNameOf(DeviceKind device, int control,
                          Orientation orientation) {
  int count = 0;
  const ControlNames* names = namesOf(device, &count);
  if (control < 0 || control >= count) {
    return "?";
  }
  return orientation == Orientation::Portrait ? names[control].portrait
                                              : names[control].landscape;
}

// The four default tables, one constant each so the review of a mapping reads
// the same shape the specification's tables have.
constexpr ControlTable kDefaultLandscapeController = {{
    {{kControllerButtonLeft, kNoControl}},     // move left
    {{kControllerButtonRight, kNoControl}},    // move right
    {{kControllerButtonDown, kNoControl}},     // soft drop
    {{kControllerButtonA, kNoControl}},        // rotate clockwise
    {{kControllerButtonB, kNoControl}},        // rotate counter-clockwise
    {{kControllerButtonUp, kNoControl}},       // hard drop (task T067)
    {{kControllerButtonY, kControllerButtonL1}},  // hold
    {{kControllerButtonStart, kNoControl}},    // pause
}};

constexpr ControlTable kDefaultLandscapeKeyboard = {{
    {{kKeyboardKeyLeft, kNoControl}},
    {{kKeyboardKeyRight, kNoControl}},
    {{kKeyboardKeyDown, kNoControl}},
    {{kKeyboardKeyX, kKeyboardKeyUp}},   // X or Up rotates
    {{kKeyboardKeyZ, kNoControl}},
    {{kKeyboardKeySpace, kNoControl}},   // Space hard drops
    {{kKeyboardKeyC, kKeyboardKeyLeftShift}},
    {{kKeyboardKeyEnter, kKeyboardKeyEscape}},
}};

// The portrait defaults of tasks T055 and T067 as notes/design-review.md pins
// them: every direction keeps its device identity and takes the action whose
// on-screen direction in the rotated frame matches where the turned device
// points it, so hard drop sits on the D-pad's left arm and the left arrow,
// the logical up of the grip. The cluster reverses the rotate pair - B
// clockwise and confirm, A the other way and back - and hold stays on Y, L1,
// C, and left Shift. X and Space carry nothing.
constexpr ControlTable kDefaultPortraitController = {{
    {{kControllerButtonDown, kNoControl}},
    {{kControllerButtonUp, kNoControl}},
    {{kControllerButtonRight, kNoControl}},
    {{kControllerButtonB, kNoControl}},
    {{kControllerButtonA, kNoControl}},
    {{kControllerButtonLeft, kNoControl}},     // hard drop, the grip's up
    {{kControllerButtonY, kControllerButtonL1}},
    {{kControllerButtonStart, kNoControl}},
}};

constexpr ControlTable kDefaultPortraitKeyboard = {{
    {{kKeyboardKeyDown, kNoControl}},
    {{kKeyboardKeyUp, kNoControl}},
    {{kKeyboardKeyRight, kNoControl}},
    {{kKeyboardKeyZ, kNoControl}},
    {{kKeyboardKeyX, kNoControl}},
    {{kKeyboardKeyLeft, kNoControl}},
    {{kKeyboardKeyC, kKeyboardKeyLeftShift}},
    {{kKeyboardKeyEnter, kKeyboardKeyEscape}},
}};

// True when every slot of `table` holds a control of `device` or kNoControl,
// the first slot of every action is a control, no action lists one control
// twice, and no control serves two actions - the one-table half of the
// validity rule.
bool tableValid(DeviceKind device, const ControlTable& table) {
  for (int action = 0; action < kRebindableActionCount; ++action) {
    if (table[action][0] == kNoControl) {
      return false;  // an action with no control cannot be played
    }
    if (table[action][1] == table[action][0]) {
      return false;  // "one or two controls" means two distinct controls
    }
    for (int slot = 0; slot < kMaxControlsPerAction; ++slot) {
      const int control = table[action][slot];
      if (control == kNoControl) {
        continue;
      }
      if (control < 0 ||
          control >= (device == DeviceKind::Keyboard ? kKeyboardControlCount
                                                     : kControllerControlCount)) {
        return false;
      }
      if (actionUsingControl(table, control) != action) {
        return false;  // the control serves two actions
      }
    }
  }
  return true;
}

}  // namespace

ControlBindings defaultControlBindings() {
  ControlBindings bindings;
  bindings.keyboard[orientationIndex(Orientation::Landscape)] =
      kDefaultLandscapeKeyboard;
  bindings.keyboard[orientationIndex(Orientation::Portrait)] =
      kDefaultPortraitKeyboard;
  bindings.controller[orientationIndex(Orientation::Landscape)] =
      kDefaultLandscapeController;
  bindings.controller[orientationIndex(Orientation::Portrait)] =
      kDefaultPortraitController;
  return bindings;
}

bool controlBindingsAreDefault(const ControlBindings& bindings) {
  const ControlBindings defaults = defaultControlBindings();
  for (int orientation = 0; orientation < 2; ++orientation) {
    for (int device = 0; device < kDeviceKindCount; ++device) {
      const auto kind = static_cast<DeviceKind>(device);
      const ControlTable& have =
          bindings.table(kind, static_cast<Orientation>(orientation));
      const ControlTable& want =
          defaults.table(kind, static_cast<Orientation>(orientation));
      for (int action = 0; action < kRebindableActionCount; ++action) {
        for (int slot = 0; slot < kMaxControlsPerAction; ++slot) {
          if (have[action][slot] != want[action][slot]) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

int actionUsingControl(const ControlTable& table, int control) {
  for (int action = 0; action < kRebindableActionCount; ++action) {
    for (int slot = 0; slot < kMaxControlsPerAction; ++slot) {
      if (table[action][slot] == control) {
        return action;
      }
    }
  }
  return -1;
}

bool controlBindingsValid(const ControlBindings& bindings) {
  for (int orientation = 0; orientation < 2; ++orientation) {
    const auto frame = static_cast<Orientation>(orientation);
    if (!tableValid(DeviceKind::Keyboard,
                    bindings.table(DeviceKind::Keyboard, frame)) ||
        !tableValid(DeviceKind::Controller,
                    bindings.table(DeviceKind::Controller, frame))) {
      return false;
    }
  }
  return true;
}

int controlsOfAction(const ControlTable& table, GameAction action, int* out) {
  const int index = rebindableActionIndex(action);
  if (index < 0) {
    return 0;
  }
  int count = 0;
  for (int slot = 0; slot < kMaxControlsPerAction; ++slot) {
    if (table[index][slot] != kNoControl) {
      out[count++] = table[index][slot];
    }
  }
  return count;
}

const char* keyboardControlName(int control) {
  if (control < 0 || control >= kKeyboardControlCount) {
    return nullptr;
  }
  return kKeyboardNames[control].name;
}

const char* controllerControlName(int control) {
  if (control < 0 || control >= kControllerControlCount) {
    return nullptr;
  }
  return kControllerNames[control].name;
}

int keyboardControlFromName(const char* name) {
  if (name == nullptr) {
    return kNoControl;
  }
  for (int control = 0; control < kKeyboardControlCount; ++control) {
    if (std::strcmp(kKeyboardNames[control].name, name) == 0) {
      return control;
    }
  }
  return kNoControl;
}

int controllerControlFromName(const char* name) {
  if (name == nullptr) {
    return kNoControl;
  }
  for (int control = 0; control < kControllerControlCount; ++control) {
    if (std::strcmp(kControllerNames[control].name, name) == 0) {
      return control;
    }
  }
  return kNoControl;
}

const char* keyboardControlDisplayName(int control, Orientation orientation) {
  return displayNameOf(DeviceKind::Keyboard, control, orientation);
}

const char* controllerControlDisplayName(int control, Orientation orientation) {
  return displayNameOf(DeviceKind::Controller, control, orientation);
}

const char* rebindableActionName(GameAction action) {
  const int index = rebindableActionIndex(action);
  return index < 0 ? "?" : kActionNames[index];
}

const char* rebindableActionKeyName(GameAction action) {
  const int index = rebindableActionIndex(action);
  return index < 0 ? nullptr : kActionKeyNames[index];
}

GameAction rebindableActionFromKeyName(const char* name) {
  if (name == nullptr) {
    return kNoGameAction;
  }
  for (int index = 0; index < kRebindableActionCount; ++index) {
    if (std::strcmp(kActionKeyNames[index], name) == 0) {
      return kRebindableActions[index];
    }
  }
  return kNoGameAction;
}

// The fixed menu roles, one row per orientation and device. These never enter
// ControlBindings, so no capture and no file can move them: the D-pad that
// hard drops also moves the cursor, the A that rotates also confirms, and the
// turned frame swaps which arms and buttons those are.
const struct {
  int control;
  GameAction role;
} kMenuRoles[2][2][4] = {
    // Landscape keyboard: cursor on Up and Down, select on Enter, back on Esc.
    {{{kKeyboardKeyUp, GameAction::MenuUp},
      {kKeyboardKeyDown, GameAction::MenuDown},
      {kKeyboardKeyEnter, GameAction::MenuSelect},
      {kKeyboardKeyEscape, GameAction::MenuBack}},
     // Landscape controller: the D-pad arms, A, and B.
     {{kControllerButtonUp, GameAction::MenuUp},
      {kControllerButtonDown, GameAction::MenuDown},
      {kControllerButtonA, GameAction::MenuSelect},
      {kControllerButtonB, GameAction::MenuBack}}},
    // Portrait keyboard: the frame's up and down are the Left and Right keys.
    {{{kKeyboardKeyLeft, GameAction::MenuUp},
      {kKeyboardKeyRight, GameAction::MenuDown},
      {kKeyboardKeyEnter, GameAction::MenuSelect},
      {kKeyboardKeyEscape, GameAction::MenuBack}},
     // Portrait controller: the grip's up and down are the left and right
     // arms, and the cluster reverses - B confirms, A goes back.
     {{kControllerButtonLeft, GameAction::MenuUp},
      {kControllerButtonRight, GameAction::MenuDown},
      {kControllerButtonB, GameAction::MenuSelect},
      {kControllerButtonA, GameAction::MenuBack}}},
};

GameAction menuRoleOfControl(DeviceKind device, int control,
                             Orientation orientation) {
  const int frame = orientationIndex(orientation);
  const int kind = deviceIndex(device);
  for (const auto& entry : kMenuRoles[frame][kind]) {
    if (entry.control == control) {
      return entry.role;
    }
  }
  return kNoGameAction;
}

}  // namespace stackfall
