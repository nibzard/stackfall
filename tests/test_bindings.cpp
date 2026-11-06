// Headless suite for the rebindable control bindings (task T066,
// specification section 5).
//
// This suite links stackfall_core only, so it runs beside the rules on every
// machine that builds the tests. It pins the four halves of the module:
//
//   * the section 5 defaults - every table of every orientation names the
//     control the specification and notes/design-review.md pin, so a session
//     that never customized anything plays the rounds the pinned replay
//     hashes describe;
//   * the name vocabularies - every canonical name resolves and every display
//     name spells itself, because the controls file, the script language, the
//     screens, and the reference replay share those words;
//   * the one-control-one-action rule - the defaults obey it, a table that
//     breaks it is invalid, and a file that carries it is damaged;
//   * the controls file - the canonical rendering round-trips, every legacy
//     and damaged form recovers to the defaults, and the store saves and
//     reloads a custom table through the same atomic write as the record.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "stackfall/bindings.hpp"
#include "stackfall/storage.hpp"
#include "test_util.hpp"

namespace {

using stackfall::ControlBindings;
using stackfall::ControlTable;
using stackfall::ControlsStore;
using stackfall::DeviceKind;
using stackfall::GameAction;
using stackfall::Orientation;
using stackfall::actionUsingControl;
using stackfall::controlBindingsAreDefault;
using stackfall::controlBindingsValid;
using stackfall::controlsOfAction;
using stackfall::controllerControlDisplayName;
using stackfall::controllerControlFromName;
using stackfall::controllerControlName;
using stackfall::defaultControlBindings;
using stackfall::formatControlsFile;
using stackfall::keyboardControlDisplayName;
using stackfall::keyboardControlFromName;
using stackfall::keyboardControlName;
using stackfall::kNoControl;
using stackfall::menuRoleOfControl;
using stackfall::parseControlsFile;
using stackfall::rebindableActionFromKeyName;
using stackfall::rebindableActionKeyName;
using stackfall::rebindableActionName;
using stackfall::kRebindableActions;

// The save directory of one store test, under the build tree.
std::string scratchDir(const char* name) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() /
      ("stackfall-bindings-" + std::string(name));
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir.string();
}

// One table lookup, spelled the way the tests read.
int controlOf(const ControlTable& table, GameAction action, int slot) {
  const int index = static_cast<int>(action);
  return table[index][slot];
}

// True when `action` lists exactly the controls named.
bool actionBoundTo(const ControlTable& table, GameAction action,
                   int first, int second) {
  return controlOf(table, action, 0) == first &&
         controlOf(table, action, 1) == second;
}

// ---------------------------------------------------------------------------
// The section 5 defaults.
// ---------------------------------------------------------------------------

void test_default_landscape_controller_table() {
  const ControlTable table = defaultControlBindings().table(
      DeviceKind::Controller, Orientation::Landscape);
  SC_CHECK(actionBoundTo(table, GameAction::Left, stackfall::kControllerButtonLeft,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::Right,
                         stackfall::kControllerButtonRight, stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::Down, stackfall::kControllerButtonDown,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::RotateCW, stackfall::kControllerButtonA,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::RotateCCW,
                         stackfall::kControllerButtonB, stackfall::kNoControl));
  // Task T067: D-pad up hard drops and X carries nothing.
  SC_CHECK(actionBoundTo(table, GameAction::HardDrop, stackfall::kControllerButtonUp,
                         stackfall::kNoControl));
  SC_CHECK(actionUsingControl(table, stackfall::kControllerButtonX) < 0);
  SC_CHECK(actionBoundTo(table, GameAction::Hold, stackfall::kControllerButtonY,
                         stackfall::kControllerButtonL1));
  SC_CHECK(actionBoundTo(table, GameAction::Pause,
                         stackfall::kControllerButtonStart, stackfall::kNoControl));
}

void test_default_landscape_keyboard_table() {
  const ControlTable table =
      defaultControlBindings().table(DeviceKind::Keyboard, Orientation::Landscape);
  SC_CHECK(actionBoundTo(table, GameAction::Left, stackfall::kKeyboardKeyLeft,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::Right, stackfall::kKeyboardKeyRight,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::Down, stackfall::kKeyboardKeyDown,
                         stackfall::kNoControl));
  // Desktop defaults unchanged since the first release: X or Up rotates,
  // Space hard drops.
  SC_CHECK(actionBoundTo(table, GameAction::RotateCW, stackfall::kKeyboardKeyX,
                         stackfall::kKeyboardKeyUp));
  SC_CHECK(actionBoundTo(table, GameAction::RotateCCW, stackfall::kKeyboardKeyZ,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::HardDrop, stackfall::kKeyboardKeySpace,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(table, GameAction::Hold, stackfall::kKeyboardKeyC,
                         stackfall::kKeyboardKeyLeftShift));
  SC_CHECK(actionBoundTo(table, GameAction::Pause, stackfall::kKeyboardKeyEnter,
                         stackfall::kKeyboardKeyEscape));
}

void test_default_portrait_tables() {
  // The rotated frame of tasks T055 and T067: every direction keeps its device
  // identity and takes the action whose on-screen direction matches where the
  // turned device points it, so the D-pad's left arm and the left arrow - the
  // logical up of the grip - hard drop.
  const ControlBindings defaults = defaultControlBindings();
  const ControlTable& pad =
      defaults.table(DeviceKind::Controller, Orientation::Portrait);
  SC_CHECK(actionBoundTo(pad, GameAction::Left, stackfall::kControllerButtonDown,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(pad, GameAction::Right, stackfall::kControllerButtonUp,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(pad, GameAction::Down, stackfall::kControllerButtonRight,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(pad, GameAction::RotateCW, stackfall::kControllerButtonB,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(pad, GameAction::RotateCCW, stackfall::kControllerButtonA,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(pad, GameAction::HardDrop,
                         stackfall::kControllerButtonLeft, stackfall::kNoControl));
  SC_CHECK(actionUsingControl(pad, stackfall::kControllerButtonX) < 0);
  SC_CHECK(actionBoundTo(pad, GameAction::Hold, stackfall::kControllerButtonY,
                         stackfall::kControllerButtonL1));

  const ControlTable& key =
      defaults.table(DeviceKind::Keyboard, Orientation::Portrait);
  SC_CHECK(actionBoundTo(key, GameAction::HardDrop, stackfall::kKeyboardKeyLeft,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(key, GameAction::RotateCW, stackfall::kKeyboardKeyZ,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(key, GameAction::RotateCCW, stackfall::kKeyboardKeyX,
                         stackfall::kNoControl));
  SC_CHECK(actionBoundTo(key, GameAction::Down, stackfall::kKeyboardKeyRight,
                         stackfall::kNoControl));
  // Space carries nothing in portrait.
  SC_CHECK(actionUsingControl(key, stackfall::kKeyboardKeySpace) < 0);
}

void test_defaults_are_valid_and_self_identified() {
  const ControlBindings defaults = defaultControlBindings();
  SC_CHECK(controlBindingsValid(defaults));
  SC_CHECK(controlBindingsAreDefault(defaults));
  ControlBindings edited = defaults;
  edited.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::HardDrop)][0] = stackfall::kKeyboardKeyD;
  SC_CHECK(!controlBindingsAreDefault(edited));
  SC_CHECK(controlBindingsValid(edited));
}

// ---------------------------------------------------------------------------
// The name vocabularies.
// ---------------------------------------------------------------------------

void test_control_names_round_trip() {
  for (int control = 0; control < stackfall::kKeyboardControlCount; ++control) {
    const char* name = keyboardControlName(control);
    SC_CHECK(name != nullptr);
    SC_CHECK(keyboardControlFromName(name) == control);
    SC_CHECK(std::string(keyboardControlDisplayName(
                control, Orientation::Landscape)) != "?");
    SC_CHECK(std::string(keyboardControlDisplayName(
                control, Orientation::Portrait)) != "?");
  }
  for (int control = 0; control < stackfall::kControllerControlCount; ++control) {
    const char* name = controllerControlName(control);
    SC_CHECK(name != nullptr);
    SC_CHECK(controllerControlFromName(name) == control);
    SC_CHECK(std::string(controllerControlDisplayName(
                control, Orientation::Landscape)) != "?");
  }
  // Unknown words and out-of-range ids name nothing.
  SC_CHECK(keyboardControlFromName("nope") == kNoControl);
  SC_CHECK(controllerControlFromName("nope") == kNoControl);
  SC_CHECK(keyboardControlFromName(nullptr) == kNoControl);
  SC_CHECK(keyboardControlName(-1) == nullptr);
  SC_CHECK(keyboardControlName(stackfall::kKeyboardControlCount) == nullptr);
  SC_CHECK(controllerControlName(99) == nullptr);
  SC_CHECK(std::string(keyboardControlDisplayName(-4, Orientation::Landscape)) == "?");

  // M and Q stay off the keyboard list: the platform layer owns them as the
  // sound toggle and the developer quit, and a control the screen cannot give
  // away cannot end up double-booked with a fixed shortcut.
  SC_CHECK(keyboardControlFromName("m") == kNoControl);
  SC_CHECK(keyboardControlFromName("q") == kNoControl);

  // The portrait spellings of the four directions name where the turned
  // device points them (task T067).
  SC_CHECK(std::string(controllerControlDisplayName(
              stackfall::kControllerButtonLeft, Orientation::Portrait)) ==
           "D-PAD LEFT (UP)");
  SC_CHECK(std::string(keyboardControlDisplayName(
              stackfall::kKeyboardKeyLeft, Orientation::Portrait)) ==
           "LEFT ARROW (UP)");
  SC_CHECK(std::string(controllerControlDisplayName(
              stackfall::kControllerButtonLeft, Orientation::Landscape)) ==
           "D-PAD LEFT");
}

void test_action_names() {
  for (const GameAction action : kRebindableActions) {
    const char* key = rebindableActionKeyName(action);
    SC_CHECK(key != nullptr);
    SC_CHECK(rebindableActionFromKeyName(key) == action);
    SC_CHECK(std::string(rebindableActionName(action)) != "?");
  }
  SC_CHECK(rebindableActionFromKeyName("menuup") == stackfall::kNoGameAction);
  SC_CHECK(rebindableActionFromKeyName(nullptr) == stackfall::kNoGameAction);
  SC_CHECK(rebindableActionKeyName(GameAction::MenuUp) == nullptr);
}

// ---------------------------------------------------------------------------
// The fixed menu roles and the one-control-one-action rule.
// ---------------------------------------------------------------------------

void test_menu_roles_are_fixed() {
  // Landscape reads the cursor on the D-pad's up and down arms and the
  // keyboard's Up and Down, confirms on A and Enter, and goes back on B and
  // Esc; portrait moves the cursor on the arms that point up and down in the
  // rotated frame and reverses the cluster.
  SC_CHECK(menuRoleOfControl(DeviceKind::Keyboard, stackfall::kKeyboardKeyUp,
                             Orientation::Landscape) == GameAction::MenuUp);
  SC_CHECK(menuRoleOfControl(DeviceKind::Keyboard, stackfall::kKeyboardKeyDown,
                             Orientation::Landscape) == GameAction::MenuDown);
  SC_CHECK(menuRoleOfControl(DeviceKind::Keyboard, stackfall::kKeyboardKeyEnter,
                             Orientation::Landscape) == GameAction::MenuSelect);
  SC_CHECK(menuRoleOfControl(DeviceKind::Keyboard, stackfall::kKeyboardKeyEscape,
                             Orientation::Landscape) == GameAction::MenuBack);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonUp,
                             Orientation::Landscape) == GameAction::MenuUp);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonA,
                             Orientation::Landscape) == GameAction::MenuSelect);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonB,
                             Orientation::Landscape) == GameAction::MenuBack);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonX,
                             Orientation::Landscape) == stackfall::kNoGameAction);

  SC_CHECK(menuRoleOfControl(DeviceKind::Keyboard, stackfall::kKeyboardKeyLeft,
                             Orientation::Portrait) == GameAction::MenuUp);
  SC_CHECK(menuRoleOfControl(DeviceKind::Keyboard, stackfall::kKeyboardKeyRight,
                             Orientation::Portrait) == GameAction::MenuDown);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonLeft,
                             Orientation::Portrait) == GameAction::MenuUp);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonB,
                             Orientation::Portrait) == GameAction::MenuSelect);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonA,
                             Orientation::Portrait) == GameAction::MenuBack);
  // A control that carries no role in one orientation may still carry one in
  // the other; neither may leak.
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonUp,
                             Orientation::Portrait) == stackfall::kNoGameAction);
  SC_CHECK(menuRoleOfControl(DeviceKind::Controller, stackfall::kControllerButtonLeft,
                             Orientation::Landscape) == stackfall::kNoGameAction);
}

void test_validity_rejects_double_booked_controls() {
  // A capture that would map one input to two gameplay actions is the one
  // thing a binding screen may never do, and the validity rule is what the
  // parser and the capture path both lean on.
  ControlBindings bindings = defaultControlBindings();
  ControlTable& key =
      bindings.table(DeviceKind::Keyboard, Orientation::Landscape);
  key[static_cast<int>(GameAction::HardDrop)][0] = stackfall::kKeyboardKeyZ;
  SC_CHECK(!controlBindingsValid(bindings));

  // An action with no control at all is just as unplayable.
  ControlBindings empty = defaultControlBindings();
  empty.table(DeviceKind::Controller, Orientation::Landscape)
      [static_cast<int>(GameAction::Pause)][0] = kNoControl;
  SC_CHECK(!controlBindingsValid(empty));

  // A control id outside the device's list cannot have come from the screen.
  ControlBindings stray = defaultControlBindings();
  stray.table(DeviceKind::Controller, Orientation::Landscape)
      [static_cast<int>(GameAction::Pause)][0] = stackfall::kKeyboardKeySpace;
  SC_CHECK(!controlBindingsValid(stray));

  // The same control twice inside one action is not a canonical table either.
  ControlBindings twice = defaultControlBindings();
  twice.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::Hold)][1] = stackfall::kKeyboardKeyC;
  SC_CHECK(!controlBindingsValid(twice));
}

void test_controls_of_action_skips_empty_slots() {
  ControlBindings bindings = defaultControlBindings();
  const ControlTable& table =
      bindings.table(DeviceKind::Controller, Orientation::Landscape);
  int controls[2];
  SC_CHECK(controlsOfAction(table, GameAction::Left, controls) == 1);
  SC_CHECK(controls[0] == stackfall::kControllerButtonLeft);
  SC_CHECK(controlsOfAction(table, GameAction::Hold, controls) == 2);
  SC_CHECK(controls[0] == stackfall::kControllerButtonY);
  SC_CHECK(controls[1] == stackfall::kControllerButtonL1);
  // A menu role is not a rebindable action.
  SC_CHECK(controlsOfAction(table, GameAction::MenuUp, controls) == 0);
}

// ---------------------------------------------------------------------------
// The controls file.
// ---------------------------------------------------------------------------

void test_controls_file_round_trip() {
  const ControlBindings defaults = defaultControlBindings();
  const std::string bytes = formatControlsFile(defaults);
  SC_CHECK(bytes ==
           "version=1\n"
           "landscape.pad.left=left\n"
           "landscape.pad.right=right\n"
           "landscape.pad.down=down\n"
           "landscape.pad.rotatecw=a\n"
           "landscape.pad.rotateccw=b\n"
           "landscape.pad.harddrop=up\n"
           "landscape.pad.hold=y+l1\n"
           "landscape.pad.pause=start\n"
           "landscape.key.left=left\n"
           "landscape.key.right=right\n"
           "landscape.key.down=down\n"
           "landscape.key.rotatecw=x+up\n"
           "landscape.key.rotateccw=z\n"
           "landscape.key.harddrop=space\n"
           "landscape.key.hold=c+lshift\n"
           "landscape.key.pause=enter+esc\n"
           "portrait.pad.left=down\n"
           "portrait.pad.right=up\n"
           "portrait.pad.down=right\n"
           "portrait.pad.rotatecw=b\n"
           "portrait.pad.rotateccw=a\n"
           "portrait.pad.harddrop=left\n"
           "portrait.pad.hold=y+l1\n"
           "portrait.pad.pause=start\n"
           "portrait.key.left=down\n"
           "portrait.key.right=up\n"
           "portrait.key.down=right\n"
           "portrait.key.rotatecw=z\n"
           "portrait.key.rotateccw=x\n"
           "portrait.key.harddrop=left\n"
           "portrait.key.hold=c+lshift\n"
           "portrait.key.pause=enter+esc\n");

  ControlBindings read;
  SC_CHECK(parseControlsFile(bytes, &read));
  SC_CHECK(controlBindingsAreDefault(read));

  // A custom table survives the round trip and reports as custom.
  ControlBindings custom = defaults;
  custom.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::HardDrop)][0] = stackfall::kKeyboardKeyD;
  custom.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  custom.table(DeviceKind::Controller, Orientation::Landscape)
      [static_cast<int>(GameAction::HardDrop)][0] = stackfall::kControllerButtonX;
  custom.table(DeviceKind::Controller, Orientation::Landscape)
      [static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  const std::string customBytes = formatControlsFile(custom);
  SC_CHECK(customBytes.find("landscape.key.harddrop=d") != std::string::npos);
  SC_CHECK(customBytes.find("landscape.pad.harddrop=x") != std::string::npos);
  ControlBindings readCustom;
  SC_CHECK(parseControlsFile(customBytes, &readCustom));
  SC_CHECK(!controlBindingsAreDefault(readCustom));
  SC_CHECK(readCustom.table(DeviceKind::Keyboard, Orientation::Landscape)
               [static_cast<int>(GameAction::HardDrop)][0] ==
           stackfall::kKeyboardKeyD);
  SC_CHECK(readCustom.table(DeviceKind::Controller, Orientation::Landscape)
               [static_cast<int>(GameAction::HardDrop)][0] ==
           stackfall::kControllerButtonX);
  // Every other table of the custom file is untouched.
  SC_CHECK(readCustom.table(DeviceKind::Keyboard, Orientation::Portrait) ==
           defaults.table(DeviceKind::Keyboard, Orientation::Portrait));
}

// One canonical file with the value of `key` replaced by `value`, so every
// damaged form below edits exactly one line and nothing else.
std::string withValue(const std::string& canonical, const char* key,
                      const char* value) {
  const std::string line = std::string(key) + "=";
  const std::size_t at = canonical.find(line);
  SC_CHECK(at != std::string::npos);
  const std::size_t valueAt = at + line.size();
  const std::size_t valueEnd = canonical.find('\n', valueAt);
  std::string edited = canonical;
  edited.replace(valueAt, valueEnd - valueAt, value);
  return edited;
}

void test_controls_file_rejects_damaged_forms() {
  const std::string canonical = formatControlsFile(defaultControlBindings());
  ControlBindings ignored;

  // One key bound to two gameplay actions: damaged, not a setting.
  const std::string conflict =
      withValue(canonical, "landscape.key.harddrop", "z");
  SC_CHECK(!parseControlsFile(conflict, &ignored));

  // A partial file never shipped, so it is damaged rather than a legacy form:
  // the file without its final newline, without the portrait blocks, and
  // without everything past the first block.
  const std::size_t lastLine = canonical.rfind('\n');
  SC_CHECK(!parseControlsFile(canonical.substr(0, lastLine), &ignored));
  SC_CHECK(!parseControlsFile(
      canonical.substr(0, canonical.find("portrait.")), &ignored));
  SC_CHECK(!parseControlsFile(
      canonical.substr(0, canonical.find("landscape.key.")), &ignored));

  // An unknown control word, a second cross-action conflict on the pad block,
  // an unknown action word, a padded value, a version other than one, and the
  // same control twice in one value.
  SC_CHECK(!parseControlsFile(
      withValue(canonical, "landscape.key.harddrop", "f13"), &ignored));
  SC_CHECK(!parseControlsFile(
      withValue(canonical, "landscape.pad.hold", "start"), &ignored));
  std::string unknownAction = canonical;
  const std::size_t actionAt = unknownAction.find("landscape.pad.left=");
  unknownAction.replace(actionAt, 4, "jump");
  SC_CHECK(!parseControlsFile(unknownAction, &ignored));
  SC_CHECK(!parseControlsFile(
      withValue(canonical, "landscape.pad.pause", " start"), &ignored));
  std::string version = canonical;
  version.replace(version.find("version=1"), 9, "version=2");
  SC_CHECK(!parseControlsFile(version, &ignored));
  SC_CHECK(!parseControlsFile(
      withValue(canonical, "landscape.pad.pause", "y+y"), &ignored));

  // A damaged parse leaves the caller's tables untouched.
  ControlBindings keep = defaultControlBindings();
  keep.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::Pause)][0] = stackfall::kKeyboardKeyTab;
  const ControlBindings before = keep;
  SC_CHECK(!parseControlsFile(conflict, &keep));
  SC_CHECK(keep.table(DeviceKind::Keyboard, Orientation::Landscape) ==
           before.table(DeviceKind::Keyboard, Orientation::Landscape));
}

void test_controls_store_round_trip() {
  const std::string dir = scratchDir("store");
  const ControlsStore store(dir);
  SC_CHECK(store.filePath() == dir + "/controls.dat");

  // A fresh directory reports the defaults with NotFound.
  const auto fresh = store.load();
  SC_CHECK(fresh.status == stackfall::SaveStatus::NotFound);
  SC_CHECK(controlBindingsAreDefault(fresh));

  // A custom save reloads as itself.
  ControlBindings custom = defaultControlBindings();
  ControlTable& table =
      custom.table(DeviceKind::Keyboard, Orientation::Landscape);
  table[static_cast<int>(GameAction::HardDrop)][0] = stackfall::kKeyboardKeyD;
  table[static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  table[static_cast<int>(GameAction::RotateCW)][0] = stackfall::kKeyboardKeyUp;
  table[static_cast<int>(GameAction::RotateCW)][1] = kNoControl;
  SC_CHECK(store.save(custom) == stackfall::SaveStatus::Ok);
  const auto reloaded = store.load();
  SC_CHECK(reloaded.status == stackfall::SaveStatus::Ok);
  SC_CHECK(reloaded.table(DeviceKind::Keyboard, Orientation::Landscape) ==
           custom.table(DeviceKind::Keyboard, Orientation::Landscape));
  SC_CHECK(reloaded.table(DeviceKind::Controller, Orientation::Portrait) ==
           custom.table(DeviceKind::Controller, Orientation::Portrait));

  // A damaged file recovers every table to the defaults.
  const std::string path = store.filePath();
  std::FILE* handle = std::fopen(path.c_str(), "w");
  SC_CHECK(handle != nullptr);
  if (handle != nullptr) {
    std::fputs("version=1\nlandscape.pad.left=left\n", handle);
    std::fclose(handle);
  }
  const auto damaged = store.load();
  SC_CHECK(damaged.status == stackfall::SaveStatus::Invalid);
  SC_CHECK(controlBindingsAreDefault(damaged));

  // A store with no directory reports NoSaveDirectory and saves nothing.
  const ControlsStore nowhere("");
  SC_CHECK(nowhere.load().status == stackfall::SaveStatus::NoSaveDirectory);
  SC_CHECK(nowhere.save(custom) == stackfall::SaveStatus::NoSaveDirectory);
}

// The analog thumbstick translation (task T86): every reading resolves to the
// D-pad arm that points the same physical way, and every reading inside the
// deadzone resolves to none. The translation knows the device's physical
// directions only - the orientation applies afterwards, through the same
// tables the D-pad arms travel, which is what keeps the stick and the D-pad
// from ever disagreeing about a direction.
void test_stick_axis_translation() {
  using stackfall::StickAxis;
  using stackfall::controllerControlOfStickAxis;
  using stackfall::kControllerButtonDown;
  using stackfall::kControllerButtonLeft;
  using stackfall::kControllerButtonRight;
  using stackfall::kControllerButtonUp;
  using stackfall::kStickDeadzone;

  // The full deflections of both axes.
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, 32767) ==
           kControllerButtonRight);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, -32767) ==
           kControllerButtonLeft);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, -32768) ==
           kControllerButtonLeft);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, 32767) ==
           kControllerButtonDown);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, -32767) ==
           kControllerButtonUp);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, -32768) ==
           kControllerButtonUp);

  // A resting stick holds no direction: zero, the wobble of a real stick
  // near its center, and both sides of the deadzone boundary.
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, 0) == kNoControl);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, 0) == kNoControl);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, kStickDeadzone - 1) ==
           kNoControl);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, -(kStickDeadzone - 1)) ==
           kNoControl);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, kStickDeadzone - 1) ==
           kNoControl);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, -(kStickDeadzone - 1)) ==
           kNoControl);

  // The deadzone boundary itself counts as deflected, so the exact value is
  // testable from a session script on both axes and both directions.
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, kStickDeadzone) ==
           kControllerButtonRight);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::X, -kStickDeadzone) ==
           kControllerButtonLeft);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, kStickDeadzone) ==
           kControllerButtonDown);
  SC_CHECK(controllerControlOfStickAxis(StickAxis::Y, -kStickDeadzone) ==
           kControllerButtonUp);

  // Each arm the stick can reach carries the action the orientation's table
  // gives it, so the logical mapping below is a property of the tables, not
  // of the stick. Landscape: left, right, soft drop, hard drop on up.
  // Portrait: the turned frame swaps every direction, so the same physical
  // readings move the piece left, right, soft drop on right, and hard drop
  // on left (tasks T055 and T067).
  const ControlBindings defaults = defaultControlBindings();
  struct ArmCase {
    int control;
    GameAction landscape;
    GameAction portrait;
  };
  const ArmCase arms[] = {
      {kControllerButtonLeft, GameAction::Left, GameAction::HardDrop},
      {kControllerButtonRight, GameAction::Right, GameAction::Down},
      {kControllerButtonDown, GameAction::Down, GameAction::Left},
      {kControllerButtonUp, GameAction::HardDrop, GameAction::Right},
  };
  for (const ArmCase& arm : arms) {
    const ControlTable& landscape =
        defaults.table(DeviceKind::Controller, Orientation::Landscape);
    const ControlTable& portrait =
        defaults.table(DeviceKind::Controller, Orientation::Portrait);
    SC_CHECK(kRebindableActions[actionUsingControl(landscape, arm.control)] ==
             arm.landscape);
    SC_CHECK(kRebindableActions[actionUsingControl(portrait, arm.control)] ==
             arm.portrait);
  }
}

// The right shoulder of task T87: the one shoulder a capture can always
// take, because the left one is Hold's spare in both default tables and the
// conflict rule forbids taking a control another action already serves. R1
// carries nothing by default - the defaults stay per T067 - and it carries no
// menu role, so binding an action to it changes exactly one cell of one
// table. The controls file knows its canonical word, so a rebound table
// survives the same round trip every custom table does.
void test_right_shoulder_control() {
  using stackfall::kControllerButtonR1;
  SC_CHECK(controllerControlFromName("r1") == kControllerButtonR1);
  SC_CHECK(std::string(controllerControlName(kControllerButtonR1)) == "r1");
  SC_CHECK(std::string(controllerControlDisplayName(kControllerButtonR1,
                                                    Orientation::Landscape)) ==
           "R1");
  SC_CHECK(std::string(controllerControlDisplayName(kControllerButtonR1,
                                                    Orientation::Portrait)) ==
           "R1");

  // R1 is free in both default tables and holds no menu role in either
  // orientation, while the left shoulder stays Hold's spare - which is why
  // the rebind path needed the second shoulder at all.
  const ControlBindings defaults = defaultControlBindings();
  for (int frame = 0; frame < 2; ++frame) {
    const auto orientation = static_cast<Orientation>(frame);
    const ControlTable& pad = defaults.table(DeviceKind::Controller,
                                             orientation);
    SC_CHECK(actionUsingControl(pad, kControllerButtonR1) < 0);
    SC_CHECK(actionUsingControl(pad, stackfall::kControllerButtonL1) >= 0);
    SC_CHECK(menuRoleOfControl(DeviceKind::Controller, kControllerButtonR1,
                               orientation) == stackfall::kNoGameAction);
  }

  // Hard drop may move to R1 in one orientation alone; the tables stay valid
  // and the file carries the word, so a relaunch reads the shoulder back.
  ControlBindings rebound = defaults;
  ControlTable& landscape =
      rebound.table(DeviceKind::Controller, Orientation::Landscape);
  landscape[static_cast<int>(GameAction::HardDrop)][0] = kControllerButtonR1;
  landscape[static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  SC_CHECK(controlBindingsValid(rebound));
  SC_CHECK(!controlBindingsAreDefault(rebound));
  const std::string bytes = formatControlsFile(rebound);
  SC_CHECK(bytes.find("landscape.pad.harddrop=r1") != std::string::npos);
  SC_CHECK(bytes.find("portrait.pad.harddrop=left") != std::string::npos);
  ControlBindings read;
  SC_CHECK(parseControlsFile(bytes, &read));
  SC_CHECK(read.table(DeviceKind::Controller, Orientation::Landscape) ==
           rebound.table(DeviceKind::Controller, Orientation::Landscape));
  SC_CHECK(read.table(DeviceKind::Controller, Orientation::Portrait) ==
           defaults.table(DeviceKind::Controller, Orientation::Portrait));

  // The defaults themselves render without the word: a legacy file from
  // before T87 is byte-identical to what this build writes for the defaults,
  // and R1 spelled into a value the defaults never hold still parses, because
  // the word names a control of the device.
  SC_CHECK(formatControlsFile(defaults).find("r1") == std::string::npos);
  SC_CHECK(parseControlsFile(formatControlsFile(defaults), &read));
  SC_CHECK(controlBindingsAreDefault(read));

  // A rebound Up stops hard-dropping entirely: no second trigger hides in the
  // tables, because one control serves at most one action and the freed arm
  // names no action at all. The stick resolves to the same arm, so its
  // deflection carries the arm's menu role alone.
  const ControlTable& table =
      rebound.table(DeviceKind::Controller, Orientation::Landscape);
  SC_CHECK(actionUsingControl(table, stackfall::kControllerButtonUp) < 0);
  SC_CHECK(kRebindableActions[actionUsingControl(table, kControllerButtonR1)] ==
           GameAction::HardDrop);
  SC_CHECK(stackfall::controllerControlOfStickAxis(stackfall::StickAxis::Y,
                                                   -32767) ==
           stackfall::kControllerButtonUp);
}

}  // namespace

void run_bindings_tests() {
  test_default_landscape_controller_table();
  test_default_landscape_keyboard_table();
  test_default_portrait_tables();
  test_defaults_are_valid_and_self_identified();
  test_control_names_round_trip();
  test_action_names();
  test_menu_roles_are_fixed();
  test_validity_rejects_double_booked_controls();
  test_controls_of_action_skips_empty_slots();
  test_stick_axis_translation();
  test_right_shoulder_control();
  test_controls_file_round_trip();
  test_controls_file_rejects_damaged_forms();
  test_controls_store_round_trip();
  stackfall_test::print_summary();
}
