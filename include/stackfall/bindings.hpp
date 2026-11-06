#pragma once

// The rebindable control bindings of the controls customization screen
// (task T066, specification section 5).
//
// Before this module the section 5 tables were two constexpr arrays inside
// src/input_sdl.cpp. They still are the *defaults*: every value below is the
// table the specification pins, byte for byte, so a fresh installation and a
// recovered binding file play exactly the rounds the pinned replay hashes
// describe. What changes is that the tables became data the player edits.
//
// One ControlBindings holds four tables - keyboard and controller, each in
// landscape and portrait - because the two orientations are not one table
// turned: the portrait cluster reverses the rotate pair, which no rotation of
// a landscape table can express (notes/design-review.md). A capture on the
// customization screen edits the table of the orientation in force, so a
// player who turns the session keeps the bindings they set for the other
// grip.
//
// Each table lists, for each of the eight gameplay actions of section 5, the
// one or two device controls that perform it. A control id is the index into
// the device's control list below, so the whole module is plain integers and
// the rules core stays SDL-free: only src/input_sdl.cpp turns an id into an
// SDL_Scancode or an SDL_GameControllerButton, and it owns that one table.
//
// The menu roles never move. MenuUp, MenuDown, MenuSelect, and MenuBack sit
// on fixed controls of each orientation, beside whatever gameplay action the
// same control carries, so the player can always walk back out of the
// customization screen and every other menu - the reliable egress of task
// T066 - whatever they bind, whichever controller is attached, and whatever a
// damaged binding file leaves behind.
//
// One rule protects the tables from themselves: within one table a control
// serves at most one gameplay action. The defaults already obey it, the
// customization screen rejects a capture that would break it instead of
// silently mapping one input to two actions, and the file parser treats a
// violation as a damaged file, which recovers to the defaults under the
// storage recovery rule of specification section 9.

#include <array>
#include <cstdint>

#include "stackfall/input.hpp"
#include "stackfall/types.hpp"

namespace stackfall {

// The device kinds that each carry their own table.
enum class DeviceKind : std::uint8_t {
  Keyboard = 0,
  Controller = 1,
};

inline constexpr int kDeviceKindCount = 2;

// The eight gameplay actions the customization screen can rebind. The values
// are the first eight of GameAction, in GameAction order, so the table index
// of an action is its enum value and rebindableActionIndex() is a bounds
// check rather than a search. MenuUp and everything after it stay outside:
// they are the fixed menu roles the egress rule above depends on.
inline constexpr int kRebindableActionCount = 8;

inline constexpr std::array<GameAction, kRebindableActionCount>
    kRebindableActions = {{
        GameAction::Left,      GameAction::Right,  GameAction::Down,
        GameAction::RotateCW,  GameAction::RotateCCW, GameAction::HardDrop,
        GameAction::Hold,      GameAction::Pause,
    }};

// The table index of `action`, or -1 when the action is not rebindable.
inline constexpr int rebindableActionIndex(GameAction action) {
  const int index = static_cast<int>(action);
  return index >= 0 && index < kRebindableActionCount ? index : -1;
}

// No control in a slot. The parser and the capture path both treat it as
// "unused", never as a binding.
inline constexpr int kNoControl = -1;

// How many controls one action may list. The defaults use the second slot for
// the pairs section 5 names - keyboard X or Up rotates, C or left Shift holds,
// Enter or Esc pauses, gamepad Y or L1 holds - and a capture writes exactly
// one control, so a rebound action drops its spare rather than inheriting a
// control the player did not just press.
inline constexpr int kMaxControlsPerAction = 2;

// ---------------------------------------------------------------------------
// Keyboard controls. The ids are indices into the name table of
// src/bindings.cpp. M and Q stay out of the list on purpose: the platform
// layer reads them as the sound toggle and the developer quit of section 9,
// and a control the screen cannot give away cannot end up double-booked with
// a fixed shortcut.
// ---------------------------------------------------------------------------
enum KeyboardControl : int {
  kKeyboardKeyLeft = 0,   // the left arrow
  kKeyboardKeyRight,      // the right arrow
  kKeyboardKeyUp,         // the up arrow
  kKeyboardKeyDown,       // the down arrow
  kKeyboardKeySpace,
  kKeyboardKeyEnter,      // the return key
  kKeyboardKeyEscape,
  kKeyboardKeyLeftShift,
  kKeyboardKeyRightShift,
  kKeyboardKeyLeftCtrl,
  kKeyboardKeyTab,
  kKeyboardKeyBackspace,
  // The letters in alphabetical order, without M and Q.
  kKeyboardKeyA,
  kKeyboardKeyB,
  kKeyboardKeyC,
  kKeyboardKeyD,
  kKeyboardKeyE,
  kKeyboardKeyF,
  kKeyboardKeyG,
  kKeyboardKeyH,
  kKeyboardKeyI,
  kKeyboardKeyJ,
  kKeyboardKeyK,
  kKeyboardKeyL,
  kKeyboardKeyN,
  kKeyboardKeyO,
  kKeyboardKeyP,
  kKeyboardKeyR,
  kKeyboardKeyS,
  kKeyboardKeyT,
  kKeyboardKeyU,
  kKeyboardKeyV,
  kKeyboardKeyW,
  kKeyboardKeyX,
  kKeyboardKeyY,
  kKeyboardKeyZ
};

// How many keyboard controls the list holds, for the array bounds below.
inline constexpr int kKeyboardControlCount = kKeyboardKeyZ + 1;

// ---------------------------------------------------------------------------
// Controller controls, the eleven buttons of the RG503 layout of section 3
// and of the developer virtual pad: the cluster, both shoulders, Start, and
// the four D-pad arms. The order matches the pad-button table of
// tests/harness/reference_replay.cpp so a script name and a binding name are
// the same word. The right shoulder joined with task T87, which let the
// player move the hard-drop trigger off Up onto any button of the device:
// the left shoulder holds by default (it is Hold's spare), so without the
// right one no shoulder was free to take a rebound action. R1 carries
// nothing in either default table - the defaults stay per T067 - and it
// carries no menu role, so it is the one control a capture can always take.
// ---------------------------------------------------------------------------
enum ControllerControl : int {
  kControllerButtonA = 0,
  kControllerButtonB,
  kControllerButtonX,
  kControllerButtonY,
  kControllerButtonL1,
  kControllerButtonR1,
  kControllerButtonStart,
  kControllerButtonUp,     // the D-pad up arm
  kControllerButtonDown,   // the D-pad down arm
  kControllerButtonLeft,   // the D-pad left arm
  kControllerButtonRight   // the D-pad right arm
};

// How many controller controls the list holds.
inline constexpr int kControllerControlCount = kControllerButtonRight + 1;

// ---------------------------------------------------------------------------
// The analog thumbstick (task T86). The RG503's left stick reaches the game
// as two SDL game controller axes, and each axis reading resolves to the
// D-pad arm that points the same physical way. The stick therefore plays
// through the very tables the D-pad plays through: the binding table of the
// orientation in force gives the reading its gameplay action, the fixed menu
// role of that arm rides along, and a rebound table moves the stick with the
// arm. No second mapping exists anywhere, so the stick can never disagree
// with the D-pad about what a direction means.
//
// This translation is plain integer logic over control ids, so it lives in
// the SDL-free core beside the vocabulary it resolves into: the SDL layer
// feeds it the axis values of real events, and the headless reference replay
// feeds it the values a script names, which is what keeps the two streams
// comparable.
// ---------------------------------------------------------------------------

// The two axes the game reads: the left stick's horizontal and vertical
// reading. The right stick and the triggers are not controls of the layout.
enum class StickAxis : std::uint8_t {
  X = 0,
  Y = 1,
};

inline constexpr int kStickAxisCount = 2;

// The deadzone, in SDL axis units. SDL reports each axis in -32768..32767,
// and a resting stick wanders near zero, so a reading whose magnitude stays
// below this value holds no direction at all. The unit is the SDL axis unit,
// not a percentage, so the SDL layer and the reference replay compare the
// same integer the event carried.
inline constexpr int kStickDeadzone = 8000;

// The D-pad control `value` deflects `axis` toward, or kNoControl while the
// reading sits inside the deadzone. Negative X is the left arm, positive X
// the right arm, negative Y the up arm, and positive Y the down arm - the
// physical directions of the device, before any orientation applies them.
// A reading of exactly the deadzone counts as deflected.
int controllerControlOfStickAxis(StickAxis axis, int value);

// One table: the controls of each of the eight actions, in action order.
// Every slot holds a control id or kNoControl; the first slot is never empty
// in a valid table.
using ControlTable =
    std::array<std::array<int, kMaxControlsPerAction>, kRebindableActionCount>;

// The orientation and device index helpers every caller of ControlBindings
// uses, so no table access names a bare 0 or 1.
inline constexpr int orientationIndex(Orientation orientation) {
  return orientation == Orientation::Portrait ? 1 : 0;
}

inline constexpr int deviceIndex(DeviceKind device) {
  return static_cast<int>(device);
}

// The four tables together: the whole binding state of one session, the value
// the controls file persists and the customization screen edits.
struct ControlBindings {
  ControlTable keyboard[2] = {{}, {}};      // indexed by orientationIndex()
  ControlTable controller[2] = {{}, {}};    // indexed by orientationIndex()

  ControlTable& table(DeviceKind device, Orientation orientation) {
    return device == DeviceKind::Keyboard
               ? keyboard[orientationIndex(orientation)]
               : controller[orientationIndex(orientation)];
  }

  const ControlTable& table(DeviceKind device,
                            Orientation orientation) const {
    return device == DeviceKind::Keyboard
               ? keyboard[orientationIndex(orientation)]
               : controller[orientationIndex(orientation)];
  }
};

// The section 5 defaults, both orientations, both devices: D-pad up hard drops
// in landscape and the D-pad's left arm - the logical up of the turned grip -
// hard drops in portrait (task T067), keyboard Space hard drops and X or Up
// rotates, Y or L1 holds with C or left Shift beside it, Start, Enter, and Esc
// pause. Everything the specification and notes/design-review.md pin.
ControlBindings defaultControlBindings();

// True when `bindings` is exactly defaultControlBindings(), so a session that
// never customized anything can be told apart from one that did and the log
// can name it.
bool controlBindingsAreDefault(const ControlBindings& bindings);

// The action index whose table lists `control`, or -1 when the control is
// free. This is the one query the conflict rule needs: a capture that would
// make it return an action other than the one being rebound is a conflict.
int actionUsingControl(const ControlTable& table, int control);

// True when every action of every table names one or two distinct controls of
// its own device, no control serves two actions, and no slot holds an id
// outside the device's list. The file parser and the capture path both gate
// on it, so a binding set that could not have come from the screen never
// reaches the input tables.
bool controlBindingsValid(const ControlBindings& bindings);

// Writes the controls of `action` into `out` and returns how many: zero, one,
// or two. `kNoControl` slots are skipped, so the caller never sees a hole.
int controlsOfAction(const ControlTable& table, GameAction action, int* out);

// ---------------------------------------------------------------------------
// Names. One canonical name per control is the word the controls file, the
// script language, and the reference replay all use, so the three cannot drift
// apart; the display names are what the screens draw, and the four direction
// controls carry their own portrait spelling because a turned device points
// them somewhere else (task T067).
// ---------------------------------------------------------------------------

// The canonical name of `control`, or nullptr when the id is out of range.
const char* keyboardControlName(int control);
const char* controllerControlName(int control);

// The control of that canonical name, or kNoControl when the word names
// nothing. Case sensitive, exactly as the file writes it.
int keyboardControlFromName(const char* name);
int controllerControlFromName(const char* name);

// The screen name of `control` in `orientation`: "D-PAD LEFT (UP)" for the
// portrait hard-drop arm, "LEFT SHIFT" everywhere, and so on. Returns "?" for
// an out-of-range id, so a stray id can never reach the glyph table.
const char* keyboardControlDisplayName(int control, Orientation orientation);
const char* controllerControlDisplayName(int control, Orientation orientation);

// The screen name of `action`: "MOVE LEFT", "SOFT DROP", "HARD DROP", and the
// rest of the eight rows of the customization screen.
const char* rebindableActionName(GameAction action);

// The canonical name of `action`, the word the controls file writes on the
// left of its '='.
const char* rebindableActionKeyName(GameAction action);

// The action of a canonical action name, or kNoGameAction when the word names
// no rebindable action.
GameAction rebindableActionFromKeyName(const char* name);

// The fixed menu role of `control` in `orientation`, or kNoGameAction when the
// control carries none. Four controls of each device hold one role each:
// landscape reads the cursor on the keyboard's Up and Down and confirms on
// Enter and Esc, and on the pad's D-pad up and down, A, and B; portrait moves
// the cursor on the two directions that point up and down in the rotated
// frame - the keyboard's Left and Right, the pad's left and right arms - and
// reverses the cluster, so B confirms and A goes back. Nothing a player binds
// changes these, which is the egress rule of task T066: every menu stays
// reachable whatever the binding tables hold.
GameAction menuRoleOfControl(DeviceKind device, int control,
                             Orientation orientation);

}  // namespace stackfall
