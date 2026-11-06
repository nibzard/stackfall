// SDL-path integration suite for the input adapter (task T009).
//
// This suite links SDL2 and runs separately from the portable stackfall_tests
// binary, because the rules suites must stay SDL-free (specification section
// 8). It proves the acceptance criteria of specification section 5 through
// the real SDL event path:
//
//   * keyboard scancodes translate through the section 5 table;
//   * a SDL virtual controller, mapped by an SDL_GAMECONTROLLERCONFIG line
//     exactly as the PortMaster launcher provides one, drives the same
//     table's controller half;
//   * both devices drive the real Game, and a press edge is consumed exactly
//     once even when a frame catches up through several ticks (section 11,
//     item 19);
//   * pending input behaves across pause, focus loss, and controller removal;
//   * the orientation turn of task T068 clears every held level and pending
//     edge the old tables raised, so a mid-session turn leaks no control into
//     the new tables and the releases that follow it change nothing.
//
// Keyboard events are pushed into the SDL event queue; the recorded desktop
// session in notes/agent-testing.md additionally covers the operating-system
// keyboard layer. No video subsystem is needed, so the suite runs headless.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <SDL.h>

#include "stackfall/game.hpp"
#include "stackfall/input.hpp"
#include "stackfall/input_sdl.hpp"
#include "test_util.hpp"

namespace {

using stackfall::ControlBindings;
using stackfall::ControlTable;
using stackfall::DeviceKind;
using stackfall::Game;
using stackfall::GameAction;
using stackfall::InputCapture;
using stackfall::Orientation;
using stackfall::Scene;
using stackfall::SdlInput;
using stackfall::TickInput;
using stackfall::defaultControlBindings;
using stackfall::kControllerButtonL1;
using stackfall::kControllerButtonStart;
using stackfall::kControllerButtonX;
using stackfall::kKeyboardKeyC;
using stackfall::kKeyboardKeyD;
using stackfall::kKeyboardKeyE;
using stackfall::kKeyboardKeyLeftShift;
using stackfall::kKeyboardKeyX;
using stackfall::kNoControl;
using stackfall::kNoGameAction;
using stackfall::controllerButtonOfControl;
using stackfall::controllerControlOfButton;
using stackfall::keyboardControlOfScancode;
using stackfall::scancodeOfKeyboardControl;

// Drains the SDL event queue through the adapter, exactly like the game loop.
void pump(SdlInput& input) {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    input.handleEvent(event);
  }
}

// Pushes one synthetic keyboard event into the SDL queue.
void pushKey(SDL_Scancode scancode, bool pressed, Uint8 repeat = 0) {
  SDL_Event event;
  SDL_zero(event);
  event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
  event.key.repeat = repeat;
  event.key.keysym.scancode = scancode;
  event.key.keysym.sym = SDL_GetKeyFromScancode(scancode);
  SDL_PushEvent(&event);
}

void pushFocusLost() {
  SDL_Event event;
  SDL_zero(event);
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
  SDL_PushEvent(&event);
}

// Builds a PortMaster-style mapping line for a device guid, using the button
// layout of the virtual pad.
std::string mappingLine(const char* guid, const char* name) {
  std::string line = guid;
  line += ",";
  line += name;
  line += ",a:b0,b:b1,x:b2,y:b3,leftshoulder:b4,start:b5,dpup:b6,dpdown:b7,"
          "dpleft:b8,dpright:b9,platform:";
  line += SDL_GetPlatform();
  return line;
}

std::string deviceGuidString(int deviceIndex) {
  char guid[33];
  SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(deviceIndex), guid,
                            sizeof(guid));
  return guid;
}

// Presses or releases one virtual pad button through the real SDL update
// path; SDL turns it into a controller button event for the open pad.
void padButton(SDL_Joystick* joystick, int button, bool pressed) {
  SDL_JoystickSetVirtualButton(joystick, button, pressed ? 1 : 0);
}

// One event poll, then `ticks` simulation ticks: the frame shape of a
// catch-up batch (specification section 11, item 19).
void runBatch(SdlInput& input, Game& game, int ticks) {
  pump(input);
  for (int i = 0; i < ticks; ++i) {
    game.tick(input.takeTickInput().game);
  }
}

// Rotation steps from `before` to `after`, counted clockwise in 0..3.
int rotationSteps(stackfall::Rotation before, stackfall::Rotation after) {
  return (static_cast<int>(after) + 4 - static_cast<int>(before)) % 4;
}

// True while the frame carries no gameplay edge at all.
bool noGameplayEdge(const TickInput& tick) {
  return !tick.game.rotateCWPressed && !tick.game.rotateCCWPressed &&
         !tick.game.hardDropPressed && !tick.game.holdPressed &&
         !tick.game.pausePressed;
}

// ---------------------------------------------------------------------------
// Keyboard half of the section 5 table, through pushed SDL key events.
// ---------------------------------------------------------------------------

void check_keyboard_table() {
  SdlInput input;

  struct KeyCase {
    SDL_Scancode scancode;
    GameAction held;      // held level while down, kNoGameAction for edges
    GameAction edge;      // gameplay edge on the press tick
    GameAction menuEdge;  // menu edge on the press tick
  };
  const KeyCase cases[] = {
      {SDL_SCANCODE_LEFT, GameAction::Left, kNoGameAction, kNoGameAction},
      {SDL_SCANCODE_RIGHT, GameAction::Right, kNoGameAction, kNoGameAction},
      {SDL_SCANCODE_DOWN, GameAction::Down, kNoGameAction,
       GameAction::MenuDown},
      {SDL_SCANCODE_UP, kNoGameAction, GameAction::RotateCW,
       GameAction::MenuUp},
      {SDL_SCANCODE_X, kNoGameAction, GameAction::RotateCW, kNoGameAction},
      {SDL_SCANCODE_Z, kNoGameAction, GameAction::RotateCCW, kNoGameAction},
      {SDL_SCANCODE_SPACE, kNoGameAction, GameAction::HardDrop,
       kNoGameAction},
      {SDL_SCANCODE_C, kNoGameAction, GameAction::Hold, kNoGameAction},
      {SDL_SCANCODE_LSHIFT, kNoGameAction, GameAction::Hold, kNoGameAction},
      {SDL_SCANCODE_RETURN, kNoGameAction, GameAction::Pause,
       GameAction::MenuSelect},
      {SDL_SCANCODE_ESCAPE, kNoGameAction, GameAction::Pause,
       GameAction::MenuBack},
  };

  for (const KeyCase& entry : cases) {
    pushKey(entry.scancode, true);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(input.adapter().isHeld(entry.held));
    }
    if (entry.edge != kNoGameAction) {
      SC_CHECK(input.adapter().edgePending(entry.edge));
    }

    const TickInput tick = input.takeTickInput();
    SC_CHECK(tick.game.leftHeld == (entry.held == GameAction::Left));
    SC_CHECK(tick.game.rightHeld == (entry.held == GameAction::Right));
    SC_CHECK(tick.game.downHeld == (entry.held == GameAction::Down));
    SC_CHECK(tick.game.rotateCWPressed ==
             (entry.edge == GameAction::RotateCW));
    SC_CHECK(tick.game.rotateCCWPressed ==
             (entry.edge == GameAction::RotateCCW));
    SC_CHECK(tick.game.hardDropPressed ==
             (entry.edge == GameAction::HardDrop));
    SC_CHECK(tick.game.holdPressed == (entry.edge == GameAction::Hold));
    SC_CHECK(tick.game.pausePressed == (entry.edge == GameAction::Pause));
    SC_CHECK(tick.menu.upPressed == (entry.menuEdge == GameAction::MenuUp));
    SC_CHECK(tick.menu.downPressed ==
             (entry.menuEdge == GameAction::MenuDown));
    SC_CHECK(tick.menu.selectPressed ==
             (entry.menuEdge == GameAction::MenuSelect));
    SC_CHECK(tick.menu.backPressed ==
             (entry.menuEdge == GameAction::MenuBack));

    // The next tick carries no edge again; a held level stays a level.
    const TickInput next = input.takeTickInput();
    SC_CHECK(noGameplayEdge(next));
    SC_CHECK(next.game.leftHeld == tick.game.leftHeld);
    SC_CHECK(next.game.downHeld == tick.game.downHeld);

    pushKey(entry.scancode, false);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(!input.adapter().isHeld(entry.held));
    }
  }

  // Auto-repeat carries no fresh edge (section 5).
  pushKey(SDL_SCANCODE_X, true);
  pump(input);
  SC_CHECK(input.takeTickInput().game.rotateCWPressed);
  pushKey(SDL_SCANCODE_X, true, 1);  // an SDL auto-repeat event
  pump(input);
  SC_CHECK(!input.takeTickInput().game.rotateCWPressed);
  pushKey(SDL_SCANCODE_X, false);
  pump(input);

  // Two keys mapping to the same action collapse into one edge per tick.
  pushKey(SDL_SCANCODE_C, true);
  pushKey(SDL_SCANCODE_C, false);
  pushKey(SDL_SCANCODE_LSHIFT, true);
  pump(input);
  SC_CHECK(input.takeTickInput().game.holdPressed);
  SC_CHECK(!input.takeTickInput().game.holdPressed);
  pushKey(SDL_SCANCODE_LSHIFT, false);
  pump(input);
}

// The portrait keyboard table of task T055 as task T067 revises it, pinned
// by notes/design-review.md: every arrow takes the action whose on-screen
// direction in the rotated frame matches where the turned device points it,
// Z and X carry the reversed cluster pair, the left arrow is the logical up
// direction so it hard drops and moves the menu cursor up, C or left Shift
// holds, Space carries nothing, and Enter and Esc keep their own select and
// back roles.
void check_portrait_keyboard_table() {
  SdlInput input;
  SC_CHECK(input.orientation() == stackfall::Orientation::Landscape);
  input.setOrientation(stackfall::Orientation::Portrait);
  SC_CHECK(input.orientation() == stackfall::Orientation::Portrait);

  struct KeyCase {
    SDL_Scancode scancode;
    GameAction held;
    GameAction edge;
    GameAction menuEdge;
  };
  const KeyCase cases[] = {
      {SDL_SCANCODE_LEFT, kNoGameAction, GameAction::HardDrop,
       GameAction::MenuUp},
      {SDL_SCANCODE_RIGHT, GameAction::Down, kNoGameAction,
       GameAction::MenuDown},
      {SDL_SCANCODE_DOWN, GameAction::Left, kNoGameAction, kNoGameAction},
      {SDL_SCANCODE_UP, GameAction::Right, kNoGameAction, kNoGameAction},
      {SDL_SCANCODE_Z, kNoGameAction, GameAction::RotateCW, kNoGameAction},
      {SDL_SCANCODE_X, kNoGameAction, GameAction::RotateCCW, kNoGameAction},
      {SDL_SCANCODE_C, kNoGameAction, GameAction::Hold, kNoGameAction},
      {SDL_SCANCODE_LSHIFT, kNoGameAction, GameAction::Hold, kNoGameAction},
      // Space carries nothing in portrait since task T067: the left arrow is
      // the logical up direction that hard drops, and the hold pair moved to
      // C and left shift.
      {SDL_SCANCODE_SPACE, kNoGameAction, kNoGameAction, kNoGameAction},
      {SDL_SCANCODE_RETURN, kNoGameAction, GameAction::Pause,
       GameAction::MenuSelect},
      {SDL_SCANCODE_ESCAPE, kNoGameAction, GameAction::Pause,
       GameAction::MenuBack},
  };

  for (const KeyCase& entry : cases) {
    pushKey(entry.scancode, true);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(input.adapter().isHeld(entry.held));
    }
    if (entry.edge != kNoGameAction) {
      SC_CHECK(input.adapter().edgePending(entry.edge));
    }

    const TickInput tick = input.takeTickInput();
    SC_CHECK(tick.game.leftHeld == (entry.held == GameAction::Left));
    SC_CHECK(tick.game.rightHeld == (entry.held == GameAction::Right));
    SC_CHECK(tick.game.downHeld == (entry.held == GameAction::Down));
    SC_CHECK(tick.game.rotateCWPressed ==
             (entry.edge == GameAction::RotateCW));
    SC_CHECK(tick.game.rotateCCWPressed ==
             (entry.edge == GameAction::RotateCCW));
    SC_CHECK(tick.game.hardDropPressed ==
             (entry.edge == GameAction::HardDrop));
    SC_CHECK(tick.game.holdPressed == (entry.edge == GameAction::Hold));
    SC_CHECK(tick.game.pausePressed == (entry.edge == GameAction::Pause));
    SC_CHECK(tick.menu.upPressed == (entry.menuEdge == GameAction::MenuUp));
    SC_CHECK(tick.menu.downPressed ==
             (entry.menuEdge == GameAction::MenuDown));
    SC_CHECK(tick.menu.selectPressed ==
             (entry.menuEdge == GameAction::MenuSelect));
    SC_CHECK(tick.menu.backPressed ==
             (entry.menuEdge == GameAction::MenuBack));

    // The next tick carries no edge again; a held level stays a level.
    const TickInput next = input.takeTickInput();
    SC_CHECK(noGameplayEdge(next));
    SC_CHECK(next.game.leftHeld == tick.game.leftHeld);
    SC_CHECK(next.game.downHeld == tick.game.downHeld);

    pushKey(entry.scancode, false);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(!input.adapter().isHeld(entry.held));
    }
  }

  // Switching back to landscape restores the section 5 table.
  input.setOrientation(stackfall::Orientation::Landscape);
  pushKey(SDL_SCANCODE_UP, true);
  pump(input);
  const TickInput landscapeTick = input.takeTickInput();
  SC_CHECK(!landscapeTick.game.rightHeld);
  SC_CHECK(landscapeTick.game.rotateCWPressed);
  pushKey(SDL_SCANCODE_UP, false);
  pump(input);
}

// ---------------------------------------------------------------------------
// SDL_GAMECONTROLLERCONFIG loading and the controller half of the table.
// ---------------------------------------------------------------------------

// A virtual joystick without a mapping is no controller; one
// SDL_GAMECONTROLLERCONFIG line, loaded by the adapter, turns it into one.
void check_environment_mapping_load() {
  const int device = stackfall::attachVirtualJoystick();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SC_CHECK(SDL_IsGameController(device) == SDL_FALSE);

  const std::string line = mappingLine(deviceGuidString(device).c_str(),
                                       "Env Config Pad");
  SDL_setenv("SDL_GAMECONTROLLERCONFIG", line.c_str(), 1);
  SdlInput input;
  SC_CHECK(input.loadEnvironmentMappings() == 1);
  SC_CHECK(SDL_IsGameController(device) == SDL_TRUE);

  // Absent and malformed values load nothing and cost nothing.
  SDL_setenv("SDL_GAMECONTROLLERCONFIG", "", 1);
  SdlInput empty;
  SC_CHECK(empty.loadEnvironmentMappings() == 0);
  SDL_setenv("SDL_GAMECONTROLLERCONFIG", "this line is not a mapping", 1);
  SdlInput broken;
  SC_CHECK(broken.loadEnvironmentMappings() == 0);
  SDL_setenv("SDL_GAMECONTROLLERCONFIG", "", 1);

  SDL_JoystickDetachVirtual(device);
}

// The virtual pad, opened as a game controller, feeds the section 5
// controller table through real SDL controller events.
void check_controller_table() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  struct PadCase {
    int button;
    GameAction held;      // held level while down, kNoGameAction for edges
    GameAction edge;     // gameplay edge on the press tick
    GameAction menuEdge;  // menu edge on the press tick
  };
  const PadCase cases[] = {
      {stackfall::kVirtualButtonDpadLeft, GameAction::Left, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualButtonDpadRight, GameAction::Right, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualButtonDpadDown, GameAction::Down, kNoGameAction,
       GameAction::MenuDown},
      {stackfall::kVirtualButtonDpadUp, kNoGameAction, GameAction::HardDrop,
       GameAction::MenuUp},
      {stackfall::kVirtualButtonA, kNoGameAction, GameAction::RotateCW,
       GameAction::MenuSelect},
      {stackfall::kVirtualButtonB, kNoGameAction, GameAction::RotateCCW,
       GameAction::MenuBack},
      // X carries nothing since task T067 moved the default hard drop to
      // D-pad up: a press of it must change no level, no edge, and no menu
      // edge, which the all-empty row asserts.
      {stackfall::kVirtualButtonX, kNoGameAction, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualButtonY, kNoGameAction, GameAction::Hold,
       kNoGameAction},
      {stackfall::kVirtualButtonLeftShoulder, kNoGameAction, GameAction::Hold,
       kNoGameAction},
      {stackfall::kVirtualButtonStart, kNoGameAction, GameAction::Pause,
       kNoGameAction},
  };

  for (const PadCase& entry : cases) {
    padButton(joystick, entry.button, true);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(input.adapter().isHeld(entry.held));
    }

    const TickInput tick = input.takeTickInput();
    SC_CHECK(tick.game.leftHeld == (entry.held == GameAction::Left));
    SC_CHECK(tick.game.rightHeld == (entry.held == GameAction::Right));
    SC_CHECK(tick.game.downHeld == (entry.held == GameAction::Down));
    SC_CHECK(tick.game.rotateCWPressed ==
             (entry.edge == GameAction::RotateCW));
    SC_CHECK(tick.game.rotateCCWPressed ==
             (entry.edge == GameAction::RotateCCW));
    SC_CHECK(tick.game.hardDropPressed ==
             (entry.edge == GameAction::HardDrop));
    SC_CHECK(tick.game.holdPressed == (entry.edge == GameAction::Hold));
    SC_CHECK(tick.game.pausePressed == (entry.edge == GameAction::Pause));
    SC_CHECK(tick.menu.upPressed == (entry.menuEdge == GameAction::MenuUp));
    SC_CHECK(tick.menu.downPressed ==
             (entry.menuEdge == GameAction::MenuDown));
    SC_CHECK(tick.menu.selectPressed ==
             (entry.menuEdge == GameAction::MenuSelect));
    SC_CHECK(tick.menu.backPressed ==
             (entry.menuEdge == GameAction::MenuBack));

    // The next tick carries no edge again.
    const TickInput next = input.takeTickInput();
    SC_CHECK(noGameplayEdge(next));
    SC_CHECK(!next.menu.upPressed && !next.menu.downPressed);
    SC_CHECK(!next.menu.selectPressed && !next.menu.backPressed);

    padButton(joystick, entry.button, false);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(!input.adapter().isHeld(entry.held));
    }
  }

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The portrait controller table of task T055 as task T067 revises it, pinned
// by notes/design-review.md: the D-pad's up, down, left, and right become
// move right, move left, hard drop plus menu up, and soft drop plus menu
// down, because the turned device points them there; the cluster keeps the
// reversed rotate pair so B rotates clockwise and confirms while A rotates
// the other way and goes back, hold takes Y with the left shoulder beside
// it, X carries nothing, and Start keeps its role.
void check_portrait_controller_table() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  input.setOrientation(stackfall::Orientation::Portrait);

  struct PadCase {
    int button;
    GameAction held;
    GameAction edge;
    GameAction menuEdge;
  };
  const PadCase cases[] = {
      {stackfall::kVirtualButtonDpadLeft, kNoGameAction, GameAction::HardDrop,
       GameAction::MenuUp},
      {stackfall::kVirtualButtonDpadRight, GameAction::Down, kNoGameAction,
       GameAction::MenuDown},
      {stackfall::kVirtualButtonDpadDown, GameAction::Left, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualButtonDpadUp, GameAction::Right, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualButtonB, kNoGameAction, GameAction::RotateCW,
       GameAction::MenuSelect},
      {stackfall::kVirtualButtonA, kNoGameAction, GameAction::RotateCCW,
       GameAction::MenuBack},
      {stackfall::kVirtualButtonY, kNoGameAction, GameAction::Hold,
       kNoGameAction},
      // X carries nothing since task T067: the logical up direction of the
      // rotated grip hard drops, and hold moved to Y and the left shoulder.
      {stackfall::kVirtualButtonX, kNoGameAction, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualButtonLeftShoulder, kNoGameAction, GameAction::Hold,
       kNoGameAction},
      {stackfall::kVirtualButtonStart, kNoGameAction, GameAction::Pause,
       kNoGameAction},
  };

  for (const PadCase& entry : cases) {
    padButton(joystick, entry.button, true);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(input.adapter().isHeld(entry.held));
    }

    const TickInput tick = input.takeTickInput();
    SC_CHECK(tick.game.leftHeld == (entry.held == GameAction::Left));
    SC_CHECK(tick.game.rightHeld == (entry.held == GameAction::Right));
    SC_CHECK(tick.game.downHeld == (entry.held == GameAction::Down));
    SC_CHECK(tick.game.rotateCWPressed ==
             (entry.edge == GameAction::RotateCW));
    SC_CHECK(tick.game.rotateCCWPressed ==
             (entry.edge == GameAction::RotateCCW));
    SC_CHECK(tick.game.hardDropPressed ==
             (entry.edge == GameAction::HardDrop));
    SC_CHECK(tick.game.holdPressed == (entry.edge == GameAction::Hold));
    SC_CHECK(tick.game.pausePressed == (entry.edge == GameAction::Pause));
    SC_CHECK(tick.menu.upPressed == (entry.menuEdge == GameAction::MenuUp));
    SC_CHECK(tick.menu.downPressed ==
             (entry.menuEdge == GameAction::MenuDown));
    SC_CHECK(tick.menu.selectPressed ==
             (entry.menuEdge == GameAction::MenuSelect));
    SC_CHECK(tick.menu.backPressed ==
             (entry.menuEdge == GameAction::MenuBack));

    const TickInput next = input.takeTickInput();
    SC_CHECK(noGameplayEdge(next));
    SC_CHECK(!next.menu.upPressed && !next.menu.downPressed);
    SC_CHECK(!next.menu.selectPressed && !next.menu.backPressed);

    padButton(joystick, entry.button, false);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(!input.adapter().isHeld(entry.held));
    }
  }

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The portrait table drives the real game in the rotated frame: a physical
// D-pad up moves the piece toward the player's right, the on-screen direction
// that press points to in the turned grip, and B rotates clockwise where the
// landscape table would have used A.
void check_portrait_controller_drives_game() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  input.setOrientation(stackfall::Orientation::Portrait);
  Game game;
  game.start(35);
  const int startX = game.activePiece().x;
  const auto startRotation = game.activePiece().rotation;

  // D-pad up is move right in the rotated frame: one press-tick move.
  padButton(joystick, stackfall::kVirtualButtonDpadUp, true);
  runBatch(input, game, 3);
  SC_CHECK(game.activePiece().x == startX + 1);
  padButton(joystick, stackfall::kVirtualButtonDpadUp, false);
  pump(input);

  // B rotates clockwise: one step, not the counter-clockwise of landscape.
  padButton(joystick, stackfall::kVirtualButtonB, true);
  runBatch(input, game, 1);
  SC_CHECK(rotationSteps(startRotation, game.activePiece().rotation) == 1);
  padButton(joystick, stackfall::kVirtualButtonB, false);
  pump(input);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// ---------------------------------------------------------------------------
// The analog thumbstick (task T86). Every check drives the real SDL axis
// path: the virtual pad reports a reading, SDL turns it into a
// SDL_CONTROLLERAXISMOTION event, and SdlInput resolves it to the D-pad arm
// the reading points at. What each arm then does is the table's business, so
// these checks pin the axis half - the deadzone, the press and release
// transitions, the edge-once rule - and the mapping both orientations give
// the same physical reading.
// ---------------------------------------------------------------------------

// Reports one virtual pad axis reading through the real SDL update path.
void padAxis(SDL_Joystick* joystick, int axis, int value) {
  SDL_JoystickSetVirtualAxis(joystick, axis, static_cast<Sint16>(value));
}

// Pushes one synthetic controller axis event for an axis outside the left
// stick, the way a pad with triggers would deliver it.
void pushForeignAxis(SDL_GameControllerAxis axis, int value) {
  SDL_Event event;
  SDL_zero(event);
  event.type = SDL_CONTROLLERAXISMOTION;
  event.caxis.which = 0;
  event.caxis.axis = axis;
  event.caxis.value = static_cast<Sint16>(value);
  SDL_PushEvent(&event);
}

// The landscape stick table: each deflection of an axis takes the action of
// the D-pad arm it points at, a reading inside the deadzone takes none, and
// the transitions are one press edge and one release, exactly as the arm
// itself produces.
void check_stick_landscape_table() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  struct StickCase {
    int axis;
    int value;
    GameAction held;      // held level while deflected, kNoGameAction for edges
    GameAction edge;      // gameplay edge on the deflection tick
    GameAction menuEdge;  // menu edge on the deflection tick
  };
  const StickCase cases[] = {
      {stackfall::kVirtualAxisLeftX, 32767, GameAction::Right, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualAxisLeftX, -32767, GameAction::Left, kNoGameAction,
       kNoGameAction},
      {stackfall::kVirtualAxisLeftY, 32767, GameAction::Down, kNoGameAction,
       GameAction::MenuDown},
      {stackfall::kVirtualAxisLeftY, -32767, kNoGameAction, GameAction::HardDrop,
       GameAction::MenuUp},
  };

  for (const StickCase& entry : cases) {
    // The deadzone below the boundary holds nothing, so the deflection that
    // follows is a real press, not a level the wobble already raised.
    padAxis(joystick, entry.axis, stackfall::kStickDeadzone - 1);
    pump(input);
    SC_CHECK(input.takeTickInput().game.leftHeld == false);
    padAxis(joystick, entry.axis, 0);
    pump(input);

    padAxis(joystick, entry.axis, entry.value);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(input.adapter().isHeld(entry.held));
    }
    if (entry.edge != kNoGameAction) {
      SC_CHECK(input.adapter().edgePending(entry.edge));
    }

    const TickInput tick = input.takeTickInput();
    SC_CHECK(tick.game.leftHeld == (entry.held == GameAction::Left));
    SC_CHECK(tick.game.rightHeld == (entry.held == GameAction::Right));
    SC_CHECK(tick.game.downHeld == (entry.held == GameAction::Down));
    SC_CHECK(tick.game.hardDropPressed ==
             (entry.edge == GameAction::HardDrop));
    SC_CHECK(tick.menu.upPressed == (entry.menuEdge == GameAction::MenuUp));
    SC_CHECK(tick.menu.downPressed ==
             (entry.menuEdge == GameAction::MenuDown));

    // The next tick carries no edge again; a deflected arm stays a level.
    const TickInput next = input.takeTickInput();
    SC_CHECK(noGameplayEdge(next));
    SC_CHECK(next.game.leftHeld == tick.game.leftHeld);
    SC_CHECK(next.game.downHeld == tick.game.downHeld);

    // Resting the stick releases the arm.
    padAxis(joystick, entry.axis, 0);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(!input.adapter().isHeld(entry.held));
    }
    input.takeTickInput();
  }

  // The deadzone boundary itself counts as deflected, so the exact value of
  // the constant is pinned on both axes and both directions.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, stackfall::kStickDeadzone);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Right));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -stackfall::kStickDeadzone);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, stackfall::kStickDeadzone);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Down));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  SC_CHECK(!input.adapter().isHeld(GameAction::Down));
  input.takeTickInput();

  // A swept stick releases the arm it leaves before pressing the next, so
  // left and right are never held together and the arrival tick carries the
  // new direction alone.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 32767);
  pump(input);
  SC_CHECK(!input.adapter().isHeld(GameAction::Left));
  SC_CHECK(input.adapter().isHeld(GameAction::Right));
  const TickInput swept = input.takeTickInput();
  SC_CHECK(!swept.game.leftHeld);
  SC_CHECK(swept.game.rightHeld);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  input.takeTickInput();

  // The two axes are independent: a deflection of one changes nothing about
  // the level the other holds, so a diagonal reading holds both directions.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 32767);
  pump(input);
  const TickInput diagonal = input.takeTickInput();
  SC_CHECK(diagonal.game.leftHeld && diagonal.game.downHeld);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Down));
  SC_CHECK(!input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  input.takeTickInput();

  // An axis outside the left stick is not a control of the layout: a trigger
  // reading changes no level and raises no edge.
  pushForeignAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767);
  pump(input);
  const TickInput trigger = input.takeTickInput();
  SC_CHECK(noGameplayEdge(trigger));
  SC_CHECK(!trigger.game.leftHeld && !trigger.game.rightHeld &&
           !trigger.game.downHeld);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The portrait stick table (tasks T055 and T068): the same physical readings
// take the actions of the same D-pad arms, which the turned frame points
// somewhere else, so X- hard drops where it moved left in landscape, Y+ moves
// the piece left, and Y- moves it right. Nothing is dead and nothing is
// inverted, because the stick resolves to arms and the arms resolve through
// the portrait table.
void check_stick_portrait_table() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  input.setOrientation(stackfall::Orientation::Portrait);

  struct StickCase {
    int axis;
    int value;
    GameAction held;
    GameAction edge;
    GameAction menuEdge;
  };
  const StickCase cases[] = {
      // X negative is the D-pad's left arm, the logical up of the turned
      // grip: hard drop and menu up (task T067).
      {stackfall::kVirtualAxisLeftX, -32767, kNoGameAction,
       GameAction::HardDrop, GameAction::MenuUp},
      // X positive is the right arm: soft drop and menu down.
      {stackfall::kVirtualAxisLeftX, 32767, GameAction::Down, kNoGameAction,
       GameAction::MenuDown},
      // Y positive is the down arm: move left.
      {stackfall::kVirtualAxisLeftY, 32767, GameAction::Left, kNoGameAction,
       kNoGameAction},
      // Y negative is the up arm: move right.
      {stackfall::kVirtualAxisLeftY, -32767, GameAction::Right, kNoGameAction,
       kNoGameAction},
  };

  for (const StickCase& entry : cases) {
    padAxis(joystick, entry.axis, entry.value);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(input.adapter().isHeld(entry.held));
    }
    if (entry.edge != kNoGameAction) {
      SC_CHECK(input.adapter().edgePending(entry.edge));
    }
    const TickInput tick = input.takeTickInput();
    SC_CHECK(tick.game.leftHeld == (entry.held == GameAction::Left));
    SC_CHECK(tick.game.rightHeld == (entry.held == GameAction::Right));
    SC_CHECK(tick.game.downHeld == (entry.held == GameAction::Down));
    SC_CHECK(tick.game.hardDropPressed ==
             (entry.edge == GameAction::HardDrop));
    SC_CHECK(tick.menu.upPressed == (entry.menuEdge == GameAction::MenuUp));
    SC_CHECK(tick.menu.downPressed ==
             (entry.menuEdge == GameAction::MenuDown));

    const TickInput next = input.takeTickInput();
    SC_CHECK(noGameplayEdge(next));

    padAxis(joystick, entry.axis, 0);
    pump(input);
    if (entry.held != kNoGameAction) {
      SC_CHECK(!input.adapter().isHeld(entry.held));
    }
    input.takeTickInput();
  }

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The stick drives the real game with the D-pad's own movement rules: the
// immediate move on the deflection tick, the ten-tick DAS charge, the ARR
// cadence, soft drop, and hard drop. A keyboard and a D-pad press beside the
// stick change nothing about it.
void check_stick_drives_game() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  Game game;
  game.start(36);
  const int startX = game.activePiece().x;

  // Stick left held across three ticks: the press-tick move and no repeat
  // before the ten-tick DAS charge, exactly as the D-pad behaves.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  runBatch(input, game, 3);
  SC_CHECK(game.activePiece().x == startX - 1);
  SC_CHECK(game.tickCount() == 3);

  // Nine more ticks of the same deflection reach the first DAS repeat at
  // tick eleven (DAS 10, section 4.5) - the repeat system reads the held
  // level the stick raised, not a button of its own.
  runBatch(input, game, 9);
  SC_CHECK(game.activePiece().x == startX - 2);
  SC_CHECK(game.tickCount() == 12);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);

  // Stick right moves the other way one column per press tick.
  const int afterLeft = game.activePiece().x;
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 32767);
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().x == afterLeft + 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);

  // A deadzone reading moves nothing: the piece holds its column and its
  // row through the same number of ticks.
  const int beforeDeadzone = game.activePiece().x;
  const int beforeDeadzoneY = game.activePiece().y;
  padAxis(joystick, stackfall::kVirtualAxisLeftX, stackfall::kStickDeadzone - 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, stackfall::kStickDeadzone - 1);
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().x == beforeDeadzone);
  SC_CHECK(game.activePiece().y == beforeDeadzoneY);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);

  // Stick down soft drops: one cell per held tick with the manual-drop
  // point, taking priority over natural gravity (section 4.5).
  const int beforeDrop = game.activePiece().y;
  const auto scoreBeforeDrop = game.score();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 32767);
  runBatch(input, game, 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  SC_CHECK(game.activePiece().y == beforeDrop + 1);
  SC_CHECK(game.score() == scoreBeforeDrop + 1);

  // Stick up hard drops: the piece locks and a new piece spawns in the same
  // tick, exactly as a D-pad up press does (task T067).
  const int cellsBefore = game.board().settledCellCount();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, -32767);
  runBatch(input, game, 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);

  // The D-pad and the keyboard are unchanged beside the stick: both still
  // move the piece, and the stick's rest state holds nothing back.
  const int secondX = game.activePiece().x;
  padButton(joystick, stackfall::kVirtualButtonDpadRight, true);
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().x == secondX + 1);
  padButton(joystick, stackfall::kVirtualButtonDpadRight, false);
  pump(input);
  const int thirdX = game.activePiece().x;
  pushKey(SDL_SCANCODE_LEFT, true);
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().x == thirdX - 1);
  pushKey(SDL_SCANCODE_LEFT, false);
  pump(input);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The stick drives the turned round on the portrait table, and the face
// buttons stay playable beside it: the operator's device report of
// 2026-11-05 is exactly this combination.
void check_stick_portrait_drives_game() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  input.setOrientation(stackfall::Orientation::Portrait);
  Game game;
  game.start(37);
  const int startX = game.activePiece().x;
  const auto startRotation = game.activePiece().rotation;

  // Y positive is the D-pad's down arm, which the turned frame points at the
  // player's left: the piece moves left, one column on the deflection tick.
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 32767);
  runBatch(input, game, 3);
  SC_CHECK(game.activePiece().x == startX - 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);

  // Y negative is the up arm, which points right: the piece moves right.
  padAxis(joystick, stackfall::kVirtualAxisLeftY, -32767);
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().x == startX);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);

  // X positive is the right arm, the logical down of the turned grip: a soft
  // drop of one cell with its point.
  const int beforeDrop = game.activePiece().y;
  const auto scoreBeforeDrop = game.score();
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 32767);
  runBatch(input, game, 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  SC_CHECK(game.activePiece().y == beforeDrop + 1);
  SC_CHECK(game.score() == scoreBeforeDrop + 1);

  // The face buttons beside the stick, on the reversed cluster: B rotates
  // clockwise, A the other way, and Y holds.
  padButton(joystick, stackfall::kVirtualButtonB, true);
  runBatch(input, game, 1);
  padButton(joystick, stackfall::kVirtualButtonB, false);
  pump(input);
  SC_CHECK(rotationSteps(startRotation, game.activePiece().rotation) == 1);
  padButton(joystick, stackfall::kVirtualButtonA, true);
  runBatch(input, game, 1);
  padButton(joystick, stackfall::kVirtualButtonA, false);
  pump(input);
  SC_CHECK(rotationSteps(startRotation, game.activePiece().rotation) == 0);
  const auto pieceBeforeHold = game.activePiece().id;
  padButton(joystick, stackfall::kVirtualButtonY, true);
  runBatch(input, game, 1);
  padButton(joystick, stackfall::kVirtualButtonY, false);
  pump(input);
  SC_CHECK(game.activePiece().id != pieceBeforeHold || game.hasHoldPiece());

  // X negative is the left arm, the logical up of the turned grip: the stick
  // hard drops, the same arm the D-pad hard drops on (task T067).
  const int cellsBefore = game.board().settledCellCount();
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  runBatch(input, game, 1);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// A deflection held across the orientation turn is forgotten with every
// other level, and the stick's next motion starts from a clean reading: the
// release that follows the turn cannot clear a level under the new table.
void check_stick_orientation_turn_clears() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  // Stick left is move left in landscape; the turn re-reads the same
  // deflection as the portrait table's hard-drop arm, so the level the old
  // table raised must not survive it.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();
  input.setOrientation(stackfall::Orientation::Portrait);
  SC_CHECK(!input.adapter().isHeld(GameAction::Left));
  SC_CHECK(!input.adapter().edgePending(GameAction::HardDrop));
  input.takeTickInput();

  // The stick is still deflected, but no further axis event arrives; when
  // the player rests it, the release lands on an arm nobody believes in
  // anymore and clears nothing that was not held.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  SC_CHECK(!input.adapter().edgePending(GameAction::HardDrop));
  const TickInput rested = input.takeTickInput();
  SC_CHECK(!rested.game.leftHeld && !rested.game.rightHeld &&
           !rested.game.downHeld);
  SC_CHECK(noGameplayEdge(rested));

  // A fresh deflection under the portrait table takes the turned meaning.
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 32767);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  input.takeTickInput();

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// A capture never takes a stick deflection and the deflection never reaches
// the adapter while the capture is armed; the arm deflected before it is
// released cleanly.
void check_stick_capture_ignored() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  input.beginCapture();
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  const InputCapture capture = input.takeCapture();
  SC_CHECK(capture.happened == false);
  SC_CHECK(input.adapter().sourceCount() == 0);
  const TickInput during = input.takeTickInput();
  SC_CHECK(!during.game.leftHeld && noGameplayEdge(during));
  input.cancelCapture();

  // After the capture ends, the still-deflected stick has no pending press
  // to fire, but the arm it holds releases cleanly on the way back.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  const TickInput rested = input.takeTickInput();
  SC_CHECK(!rested.game.leftHeld && noGameplayEdge(rested));

  // A deflection after the capture behaves normally again.
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  input.takeTickInput();

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The disconnected pad takes its stick with it: no arm the stick held stays
// held, and the replug starts from a resting reading.
void check_stick_removal() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  input.takeTickInput();

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
  pump(input);
  SC_CHECK(!input.adapter().isHeld(GameAction::Left));
  SC_CHECK(input.adapter().sourceCount() == 0);
  const TickInput after = input.takeTickInput();
  SC_CHECK(!after.game.leftHeld && noGameplayEdge(after));
}

// ---------------------------------------------------------------------------
// Both devices drive the game; edges survive catch-up exactly once.
// ---------------------------------------------------------------------------

// Task T87: the player moves the hard-drop trigger off Up and de-sensitizes
// the Up paths. The proof has three halves, all on the virtual controller:
//
//   * on the defaults, a full stick-up deflection is one edge behind the
//     deadzone - it fires exactly once however long the thumb holds the arm,
//     and a light reading inside the deadzone fires nothing at all;
//   * with hard drop rebound to the right shoulder, D-pad up and a full
//     stick-up deflection stop hard-dropping entirely - no gameplay edge, no
//     held level, only the fixed MenuUp role the arm always carries - so no
//     second trigger survives the rebind;
//   * the turned frame obeys the same rule reading by reading: the portrait
//     rebind frees the grip's up arm (D-pad left, stick X negative) while the
//     shoulder drops in portrait too.
void check_harddrop_rebind_off_up() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  // The defaults first: the stick's up arm is an edge behind the deadzone.
  padAxis(joystick, stackfall::kVirtualAxisLeftY, -(stackfall::kStickDeadzone - 1));
  pump(input);
  const TickInput light = input.takeTickInput();
  SC_CHECK(noGameplayEdge(light));
  SC_CHECK(!light.game.downHeld && !light.game.leftHeld &&
           !light.game.rightHeld);
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  input.takeTickInput();

  padAxis(joystick, stackfall::kVirtualAxisLeftY, -32767);
  pump(input);
  const TickInput deflected = input.takeTickInput();
  SC_CHECK(deflected.game.hardDropPressed);
  SC_CHECK(deflected.menu.upPressed);  // the arm's fixed role rides along
  for (int held = 0; held < 8; ++held) {
    // A held deflection is a level, not a repeated edge: the drop fired on
    // the crossing tick alone, so a light touch that never crosses the
    // deadzone cannot drop and a full one cannot drop twice.
    const TickInput stillHeld = input.takeTickInput();
    SC_CHECK(!stillHeld.game.hardDropPressed);
    SC_CHECK(noGameplayEdge(stillHeld));
  }
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  input.takeTickInput();

  // The rebind: hard drop leaves Up for the free right shoulder in the
  // landscape table, exactly the capture the customization screen applies.
  ControlBindings rebound = defaultControlBindings();
  ControlTable& landscape =
      rebound.table(DeviceKind::Controller, Orientation::Landscape);
  landscape[static_cast<int>(GameAction::HardDrop)][0] =
      stackfall::kControllerButtonR1;
  landscape[static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  ControlTable& portrait =
      rebound.table(DeviceKind::Controller, Orientation::Portrait);
  portrait[static_cast<int>(GameAction::HardDrop)][0] =
      stackfall::kControllerButtonR1;
  portrait[static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  input.setControlBindings(rebound);

  // The shoulder hard drops on both presses of the pad and deflections of
  // the stick path's frame - one edge per press, nothing while it is held.
  padButton(joystick, stackfall::kVirtualButtonRightShoulder, true);
  pump(input);
  const TickInput shoulderTick = input.takeTickInput();
  SC_CHECK(shoulderTick.game.hardDropPressed);
  SC_CHECK(noGameplayEdge(input.takeTickInput()));
  padButton(joystick, stackfall::kVirtualButtonRightShoulder, false);
  pump(input);
  input.takeTickInput();

  // The freed D-pad arm performs no gameplay action; its menu role is the
  // egress rule of task T066 and never rebinds.
  padButton(joystick, stackfall::kVirtualButtonDpadUp, true);
  pump(input);
  const TickInput freedArm = input.takeTickInput();
  SC_CHECK(noGameplayEdge(freedArm));
  SC_CHECK(!freedArm.game.downHeld && !freedArm.game.leftHeld &&
           !freedArm.game.rightHeld);
  SC_CHECK(freedArm.menu.upPressed);
  padButton(joystick, stackfall::kVirtualButtonDpadUp, false);
  pump(input);
  input.takeTickInput();

  // No second trigger on the axis path: a full stick-up deflection crosses
  // the deadzone, resolves to the freed arm, and carries its menu role alone.
  padAxis(joystick, stackfall::kVirtualAxisLeftY, -32767);
  pump(input);
  const TickInput freedStick = input.takeTickInput();
  SC_CHECK(noGameplayEdge(freedStick));
  SC_CHECK(!freedStick.game.downHeld && !freedStick.game.leftHeld &&
           !freedStick.game.rightHeld);
  SC_CHECK(freedStick.menu.upPressed);
  for (int held = 0; held < 8; ++held) {
    const TickInput stillHeld = input.takeTickInput();
    SC_CHECK(noGameplayEdge(stillHeld));
  }
  padAxis(joystick, stackfall::kVirtualAxisLeftY, 0);
  pump(input);
  input.takeTickInput();

  // The turned frame: the grip's up arm is D-pad left, which the stick's
  // X-negative reading reaches, and the portrait rebind freed it too, so the
  // shoulder drops in portrait while nothing else does.
  input.setOrientation(Orientation::Portrait);
  padAxis(joystick, stackfall::kVirtualAxisLeftX, -32767);
  pump(input);
  const TickInput turnedStick = input.takeTickInput();
  SC_CHECK(noGameplayEdge(turnedStick));
  SC_CHECK(turnedStick.menu.upPressed);  // the grip's up moves the cursor
  padAxis(joystick, stackfall::kVirtualAxisLeftX, 0);
  pump(input);
  input.takeTickInput();
  padButton(joystick, stackfall::kVirtualButtonDpadLeft, true);
  pump(input);
  const TickInput turnedArm = input.takeTickInput();
  SC_CHECK(noGameplayEdge(turnedArm));
  SC_CHECK(turnedArm.menu.upPressed);
  padButton(joystick, stackfall::kVirtualButtonDpadLeft, false);
  pump(input);
  input.takeTickInput();
  padButton(joystick, stackfall::kVirtualButtonRightShoulder, true);
  pump(input);
  SC_CHECK(input.takeTickInput().game.hardDropPressed);
  padButton(joystick, stackfall::kVirtualButtonRightShoulder, false);
  pump(input);
  input.takeTickInput();
  input.setOrientation(Orientation::Landscape);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

void check_keyboard_drives_game() {
  SdlInput input;
  Game game;
  game.start(31);
  const int startX = game.activePiece().x;
  const auto startRotation = game.activePiece().rotation;

  // One X tap, then a four-tick catch-up frame: exactly one rotation.
  pushKey(SDL_SCANCODE_X, true);
  pushKey(SDL_SCANCODE_X, false);
  runBatch(input, game, 4);
  SC_CHECK(rotationSteps(startRotation, game.activePiece().rotation) == 1);

  // Left held across a three-tick frame: the press-tick move and no repeat
  // before the ten-tick DAS charge (section 4.5).
  pushKey(SDL_SCANCODE_LEFT, true);
  runBatch(input, game, 3);
  SC_CHECK(game.activePiece().x == startX - 1);
  pushKey(SDL_SCANCODE_LEFT, false);
  pump(input);
}

void check_controller_drives_game() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  Game game;
  game.start(32);
  const int startX = game.activePiece().x;
  const auto startRotation = game.activePiece().rotation;

  // A press, then a five-tick catch-up frame: one rotation, one edge
  // (section 11, item 19).
  padButton(joystick, stackfall::kVirtualButtonA, true);
  runBatch(input, game, 5);
  SC_CHECK(rotationSteps(startRotation, game.activePiece().rotation) == 1);
  SC_CHECK(game.tickCount() == 5);
  padButton(joystick, stackfall::kVirtualButtonA, false);
  pump(input);

  // D-pad held across twelve ticks: the immediate move plus one DAS repeat
  // at tick eleven (DAS 10, ARR 2, section 4.5).
  padButton(joystick, stackfall::kVirtualButtonDpadLeft, true);
  runBatch(input, game, 12);
  SC_CHECK(game.activePiece().x == startX - 2);
  SC_CHECK(game.tickCount() == 17);
  padButton(joystick, stackfall::kVirtualButtonDpadLeft, false);
  pump(input);

  // X no longer hard drops (task T067): its press is consumed and changes
  // nothing, so the same piece stays active and nothing settles.
  const int cellsBefore = game.board().settledCellCount();
  const auto pieceBefore = game.activePiece().id;
  padButton(joystick, stackfall::kVirtualButtonX, true);
  runBatch(input, game, 1);
  padButton(joystick, stackfall::kVirtualButtonX, false);
  pump(input);
  SC_CHECK(game.board().settledCellCount() == cellsBefore);
  SC_CHECK(game.activePiece().id == pieceBefore);

  // D-pad up hard drops: the piece locks and a new piece spawns in the same
  // tick (task T067).
  padButton(joystick, stackfall::kVirtualButtonDpadUp, true);
  runBatch(input, game, 1);
  padButton(joystick, stackfall::kVirtualButtonDpadUp, false);
  pump(input);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The menu press that returns a round to play drops no piece (task T067):
// D-pad up moves the menu cursor and hard drops in one press, so a press the
// menu consumed may not act on the round that opens next. The rules core
// spends the gameplay half of every menu tick itself, whatever route the
// session takes back into play; this check pins the two the player uses.
void check_menu_up_never_drops_on_return_to_play() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  Game game;
  game.openTitle(34);

  // The title menu: one D-pad up press moves the cursor and queues a hard
  // drop with it, and the select that starts the round rides a later tick.
  padButton(joystick, stackfall::kVirtualButtonDpadUp, true);
  pump(input);
  game.tick(input.takeTickInput());
  SC_CHECK(game.scene() == Scene::Title);
  SC_CHECK(game.titleSelection() == 3);  // Start Game wrapped to Quit
  padButton(joystick, stackfall::kVirtualButtonDpadUp, false);
  pump(input);
  padButton(joystick, stackfall::kVirtualButtonDpadDown, true);
  pump(input);
  game.tick(input.takeTickInput());
  padButton(joystick, stackfall::kVirtualButtonDpadDown, false);
  pump(input);
  SC_CHECK(game.titleSelection() == 0);  // back on Start Game
  padButton(joystick, stackfall::kVirtualButtonA, true);
  pump(input);
  game.tick(input.takeTickInput());  // starts the round
  padButton(joystick, stackfall::kVirtualButtonA, false);
  pump(input);
  SC_CHECK(game.scene() == Scene::Playing);
  SC_CHECK(game.board().settledCellCount() == 0);
  const auto firstPiece = game.activePiece().id;
  for (int i = 0; i < 20; ++i) {
    game.tick(input.takeTickInput().game);  // gravity alone
  }
  SC_CHECK(game.board().settledCellCount() == 0);
  SC_CHECK(game.activePiece().id == firstPiece);

  // The pause overlay: the same up press moves its cursor, and the resume
  // that follows drops no piece either.
  padButton(joystick, stackfall::kVirtualButtonStart, true);
  pump(input);
  game.tick(input.takeTickInput());
  padButton(joystick, stackfall::kVirtualButtonStart, false);
  pump(input);
  SC_CHECK(game.scene() == Scene::Paused);
  padButton(joystick, stackfall::kVirtualButtonDpadUp, true);
  pump(input);
  game.tick(input.takeTickInput());  // consumed by the overlay
  padButton(joystick, stackfall::kVirtualButtonDpadUp, false);
  pump(input);
  padButton(joystick, stackfall::kVirtualButtonStart, true);
  pump(input);
  game.tick(input.takeTickInput());  // resumes
  padButton(joystick, stackfall::kVirtualButtonStart, false);
  pump(input);
  SC_CHECK(game.scene() == Scene::Playing);
  for (int i = 0; i < 20; ++i) {
    game.tick(input.takeTickInput().game);
  }
  SC_CHECK(game.board().settledCellCount() == 0);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// Presses made while paused are consumed by paused ticks and never fire
// after the resume (specification sections 4.5 and 5).
void check_pause_consumes_pending_input() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  Game game;
  game.start(33);
  const auto startRotation = game.activePiece().rotation;

  padButton(joystick, stackfall::kVirtualButtonStart, true);
  runBatch(input, game, 1);
  SC_CHECK(game.scene() == Scene::Paused);
  const auto ticksAtPause = game.tickCount();
  padButton(joystick, stackfall::kVirtualButtonStart, false);
  pump(input);

  // A rotation pressed while paused disappears on a paused tick.
  padButton(joystick, stackfall::kVirtualButtonA, true);
  padButton(joystick, stackfall::kVirtualButtonA, false);
  runBatch(input, game, 2);
  SC_CHECK(game.tickCount() == ticksAtPause);
  SC_CHECK(game.activePiece().rotation == startRotation);

  // Resume; no stored edge fires.
  padButton(joystick, stackfall::kVirtualButtonStart, true);
  runBatch(input, game, 1);
  SC_CHECK(game.scene() == Scene::Playing);
  padButton(joystick, stackfall::kVirtualButtonStart, false);
  pump(input);
  for (int i = 0; i < 5; ++i) {
    game.tick(input.takeTickInput().game);
  }
  SC_CHECK(game.activePiece().rotation == startRotation);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// Focus loss clears held keyboard and controller input, so a key stuck down
// across an alt-tab cannot keep moving the piece.
void check_focus_loss_clears_input() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  pushKey(SDL_SCANCODE_DOWN, true);  // no key-up will arrive
  pushKey(SDL_SCANCODE_RETURN, true);  // a press edge waits for its tick
  padButton(joystick, stackfall::kVirtualButtonDpadRight, true);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Down));
  SC_CHECK(input.adapter().isHeld(GameAction::Right));

  pushFocusLost();
  pump(input);
  SC_CHECK(!input.adapter().isHeld(GameAction::Down));
  SC_CHECK(!input.adapter().isHeld(GameAction::Right));
  const TickInput tick = input.takeTickInput();
  SC_CHECK(!tick.game.downHeld && !tick.game.rightHeld);
  // The press the window received while focused still fires after the loss.
  SC_CHECK(tick.game.pausePressed);
  SC_CHECK(tick.menu.selectPressed);

  // The controller still works after refocus; its earlier hold is gone.
  padButton(joystick, stackfall::kVirtualButtonDpadLeft, true);
  pump(input);
  SC_CHECK(input.takeTickInput().game.leftHeld);
  padButton(joystick, stackfall::kVirtualButtonDpadLeft, false);
  pump(input);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// The orientation turn of task T068 clears what the old tables raised. A
// control held across the turn keeps the action the old table gave it, and
// its release then arrives through the new table, where the same control maps
// to a different action and cannot clear the stuck level - so a direction
// would stay held forever and a pending edge would fire under a table the
// player never pressed it in. setOrientation() drops both halves at once,
// which is what lets the platform layer turn a running session.
void check_orientation_turn_clears_input() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;

  // The state of a player mid-turn: two held directions (keyboard down, pad
  // right) and one press whose tick has not come yet - the select that will
  // carry the turn itself.
  pushKey(SDL_SCANCODE_DOWN, true);
  padButton(joystick, stackfall::kVirtualButtonDpadRight, true);
  pushKey(SDL_SCANCODE_RETURN, true);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Down));
  SC_CHECK(input.adapter().isHeld(GameAction::Right));
  SC_CHECK(input.adapter().edgePending(GameAction::MenuSelect));

  // The turn: nothing the old tables raised survives it.
  input.setOrientation(stackfall::Orientation::Portrait);
  const TickInput after = input.takeTickInput();
  SC_CHECK(!after.game.downHeld && !after.game.rightHeld);
  SC_CHECK(!after.menu.selectPressed && !after.game.pausePressed);
  SC_CHECK(noGameplayEdge(after));
  SC_CHECK(!input.adapter().isHeld(GameAction::Down));
  SC_CHECK(!input.adapter().isHeld(GameAction::Right));

  // The releases still to come map through the portrait table - key down
  // releases the logical left, the pad's right arm the logical down - and
  // neither may raise or clear anything: the levels are already gone.
  pushKey(SDL_SCANCODE_DOWN, false);
  padButton(joystick, stackfall::kVirtualButtonDpadRight, false);
  pump(input);
  const TickInput released = input.takeTickInput();
  SC_CHECK(!released.game.leftHeld && !released.game.downHeld);
  SC_CHECK(noGameplayEdge(released));

  // The new table works from the first press after the turn: the keyboard's
  // up arrow holds the logical right of the rotated frame.
  pushKey(SDL_SCANCODE_UP, true);
  pump(input);
  SC_CHECK(input.takeTickInput().game.rightHeld);
  pushKey(SDL_SCANCODE_UP, false);
  pump(input);

  // Repeated turns in one session keep the rule: landscape again clears the
  // portrait levels the same way, and the landscape table answers the same
  // control with its own action.
  pushKey(SDL_SCANCODE_UP, true);  // portrait: hold right
  pump(input);
  input.setOrientation(stackfall::Orientation::Landscape);
  const TickInput turnedBack = input.takeTickInput();
  SC_CHECK(!turnedBack.game.rightHeld);
  SC_CHECK(noGameplayEdge(turnedBack));
  pushKey(SDL_SCANCODE_UP, false);
  pump(input);
  pushKey(SDL_SCANCODE_UP, true);  // landscape: rotate clockwise
  pump(input);
  const TickInput landscapeTick = input.takeTickInput();
  SC_CHECK(!landscapeTick.game.rightHeld);
  SC_CHECK(landscapeTick.game.rotateCWPressed);
  pushKey(SDL_SCANCODE_UP, false);
  pump(input);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// Detaches the virtual joystick that carries `instance`, by instance id,
// because device indices shift when another virtual pad detaches first.
void detachByInstance(SDL_JoystickID instance) {
  const int joysticks = SDL_NumJoysticks();
  for (int i = 0; i < joysticks; ++i) {
    if (SDL_JoystickGetDeviceInstanceID(i) == instance) {
      SDL_JoystickDetachVirtual(i);
      return;
    }
  }
}

// Removing a controller clears its held levels and its unconsumed edges; the
// keyboard and a second pad keep working (specification section 5).
void check_controller_removal() {
  const int deviceA = stackfall::attachVirtualController();
  const int deviceB = stackfall::attachVirtualController();
  SC_CHECK(deviceA >= 0 && deviceB >= 0);
  if (deviceA < 0 || deviceB < 0) {
    return;
  }
  SDL_GameController* padA = SDL_GameControllerOpen(deviceA);
  SDL_GameController* padB = SDL_GameControllerOpen(deviceB);
  SC_CHECK(padA != nullptr && padB != nullptr);
  if (padA == nullptr || padB == nullptr) {
    return;
  }
  SDL_Joystick* stickA = SDL_GameControllerGetJoystick(padA);
  SDL_Joystick* stickB = SDL_GameControllerGetJoystick(padB);
  const SDL_JoystickID instanceB = SDL_JoystickInstanceID(stickB);
  SdlInput input;

  // Both pads hold opposite directions: no movement (section 5).
  padButton(stickA, stackfall::kVirtualButtonDpadRight, true);
  padButton(stickB, stackfall::kVirtualButtonDpadLeft, true);
  pump(input);
  const TickInput both = input.takeTickInput();
  SC_CHECK(both.game.leftHeld && both.game.rightHeld);
  SC_CHECK(both.game.horizontalDirection() == 0);

  // A press whose tick never came: Start on the pad about to disappear.
  padButton(stickA, stackfall::kVirtualButtonStart, true);
  pump(input);

  // Detach pad A: SDL queues its removal, the adapter drops its source.
  SDL_JoystickDetachVirtual(deviceA);
  pump(input);
  const TickInput after = input.takeTickInput();
  SC_CHECK(!after.game.rightHeld);     // the pad's held level is gone
  SC_CHECK(!after.game.pausePressed);  // its ghost press never fires
  SC_CHECK(after.game.leftHeld);       // pad B still drives the game
  SC_CHECK(after.game.horizontalDirection() == -1);
  SDL_GameControllerClose(padA);

  // The remaining sources keep playing: pad B releases its direction and the
  // keyboard rotates the piece.
  padButton(stickB, stackfall::kVirtualButtonDpadLeft, false);
  pump(input);
  Game game;
  game.start(34);
  pushKey(SDL_SCANCODE_Z, true);
  pushKey(SDL_SCANCODE_Z, false);
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().rotation ==
           stackfall::Rotation::CounterClockwise);

  SDL_GameControllerClose(padB);
  detachByInstance(instanceB);
}

// ---------------------------------------------------------------------------
// The rebindable bindings and the capture of the customization screen
// (task T066).
// ---------------------------------------------------------------------------

// The custom tables the harness relaunch session pins: keyboard D and pad X
// hard drop on the landscape tables, everything else the section 5 defaults.
ControlBindings customDropBindings() {
  ControlBindings bindings = defaultControlBindings();
  ControlTable& key =
      bindings.table(DeviceKind::Keyboard, Orientation::Landscape);
  key[static_cast<int>(GameAction::HardDrop)][0] = kKeyboardKeyD;
  key[static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  ControlTable& pad =
      bindings.table(DeviceKind::Controller, Orientation::Landscape);
  pad[static_cast<int>(GameAction::HardDrop)][0] = kControllerButtonX;
  pad[static_cast<int>(GameAction::HardDrop)][1] = kNoControl;
  return bindings;
}

// The control-id translations are the only place a control meets an SDL
// enum, so a wrong id here would send every custom binding to the wrong
// button. Every id must resolve and round trip, and M stays outside the list
// as the platform layer's fixed sound shortcut.
void check_device_translation_tables() {
  SC_CHECK(scancodeOfKeyboardControl(kKeyboardKeyD) == SDL_SCANCODE_D);
  SC_CHECK(scancodeOfKeyboardControl(kKeyboardKeyX) == SDL_SCANCODE_X);
  SC_CHECK(scancodeOfKeyboardControl(kKeyboardKeyLeftShift) ==
           SDL_SCANCODE_LSHIFT);
  SC_CHECK(keyboardControlOfScancode(SDL_SCANCODE_D) == kKeyboardKeyD);
  SC_CHECK(keyboardControlOfScancode(SDL_SCANCODE_LSHIFT) ==
           kKeyboardKeyLeftShift);
  SC_CHECK(keyboardControlOfScancode(SDL_SCANCODE_M) == kNoControl);
  SC_CHECK(keyboardControlOfScancode(SDL_SCANCODE_Q) == kNoControl);
  SC_CHECK(keyboardControlOfScancode(SDL_SCANCODE_F13) == kNoControl);
  SC_CHECK(controllerButtonOfControl(kControllerButtonX) ==
           SDL_CONTROLLER_BUTTON_X);
  SC_CHECK(controllerButtonOfControl(kControllerButtonStart) ==
           SDL_CONTROLLER_BUTTON_START);
  SC_CHECK(controllerControlOfButton(SDL_CONTROLLER_BUTTON_X) ==
           kControllerButtonX);
  SC_CHECK(controllerControlOfButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ==
           kControllerButtonL1);
  // The right shoulder of task T87, the free shoulder a rebind can take.
  SC_CHECK(controllerButtonOfControl(stackfall::kControllerButtonR1) ==
           SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
  SC_CHECK(controllerControlOfButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ==
           stackfall::kControllerButtonR1);
  SC_CHECK(stackfall::virtualPadButtonFromName("r1") ==
           stackfall::kVirtualButtonRightShoulder);
  SC_CHECK(stackfall::virtualPadButtonFromName("r2") == -1);
  SC_CHECK(controllerControlOfButton(SDL_CONTROLLER_BUTTON_GUIDE) ==
           kNoControl);
}

// Custom tables replace the defaults for gameplay while the fixed menu roles
// ride along: D and pad X hard drop, the freed Space and D-pad up perform no
// gameplay action, and Enter, Esc, A, and B keep their menu roles whatever
// the tables hold - the egress rule of task T066.
void check_custom_bindings_drive_both_devices() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  input.setControlBindings(customDropBindings());

  // The keyboard's new hard drop fires once and holds nothing.
  pushKey(SDL_SCANCODE_D, true);
  pump(input);
  const TickInput keyDrop = input.takeTickInput();
  SC_CHECK(keyDrop.game.hardDropPressed);
  SC_CHECK(!keyDrop.game.downHeld);
  pushKey(SDL_SCANCODE_D, false);
  pump(input);

  // The freed control carries no gameplay action; the menu roles it never
  // had still arrive, because roles never move.
  pushKey(SDL_SCANCODE_SPACE, true);
  pump(input);
  const TickInput freedKey = input.takeTickInput();
  SC_CHECK(!freedKey.game.hardDropPressed && !freedKey.game.rotateCWPressed);
  SC_CHECK(noGameplayEdge(freedKey));
  pushKey(SDL_SCANCODE_SPACE, false);
  pump(input);

  // The pad's new hard drop fires once; the freed D-pad up keeps its menu
  // role and loses its gameplay action.
  padButton(joystick, stackfall::kVirtualButtonX, true);
  pump(input);
  const TickInput padDrop = input.takeTickInput();
  SC_CHECK(padDrop.game.hardDropPressed);
  padButton(joystick, stackfall::kVirtualButtonX, false);
  pump(input);
  padButton(joystick, stackfall::kVirtualButtonDpadUp, true);
  pump(input);
  const TickInput freedPad = input.takeTickInput();
  SC_CHECK(!freedPad.game.hardDropPressed);
  SC_CHECK(noGameplayEdge(freedPad));
  SC_CHECK(freedPad.menu.upPressed);  // the role survived the rebind
  padButton(joystick, stackfall::kVirtualButtonDpadUp, false);
  pump(input);

  // The fixed menu roles answer under the custom tables on both devices.
  pushKey(SDL_SCANCODE_RETURN, true);
  pump(input);
  SC_CHECK(input.takeTickInput().menu.selectPressed);
  pushKey(SDL_SCANCODE_RETURN, false);
  pump(input);
  pushKey(SDL_SCANCODE_ESCAPE, true);
  pump(input);
  SC_CHECK(input.takeTickInput().menu.backPressed);
  pushKey(SDL_SCANCODE_ESCAPE, false);
  pump(input);
  padButton(joystick, stackfall::kVirtualButtonA, true);
  pump(input);
  SC_CHECK(input.takeTickInput().menu.selectPressed);
  padButton(joystick, stackfall::kVirtualButtonA, false);
  pump(input);
  padButton(joystick, stackfall::kVirtualButtonB, true);
  pump(input);
  SC_CHECK(input.takeTickInput().menu.backPressed);
  padButton(joystick, stackfall::kVirtualButtonB, false);
  pump(input);

  // The portrait tables kept the section 5 defaults: a rebind touches only
  // the orientation in force, so the same key in portrait is no control of
  // the hard drop.
  input.setOrientation(Orientation::Portrait);
  pushKey(SDL_SCANCODE_D, true);
  pump(input);
  const TickInput portraitTick = input.takeTickInput();
  SC_CHECK(!portraitTick.game.hardDropPressed);
  SC_CHECK(noGameplayEdge(portraitTick));
  pushKey(SDL_SCANCODE_D, false);
  pump(input);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// A capture eats the press that names the new binding: the next real press
// stops at takeCapture() and never reaches the adapter. Auto-repeat is not a
// press, a release never captures, a control outside the rebindable lists
// changes nothing, and cancel leaves the tables untouched.
void check_capture_semantics() {
  SdlInput input;
  input.setControlBindings(customDropBindings());

  // A keyboard press captures and nothing leaks into the tick.
  input.beginCapture();
  SC_CHECK(input.capturing());
  pushKey(SDL_SCANCODE_D, true);
  pump(input);
  const TickInput during = input.takeTickInput();
  SC_CHECK(noGameplayEdge(during));
  SC_CHECK(!during.game.downHeld);
  SC_CHECK(!during.menu.selectPressed);
  const InputCapture keyCapture = input.takeCapture();
  SC_CHECK(keyCapture.happened);
  SC_CHECK(keyCapture.device == DeviceKind::Keyboard);
  SC_CHECK(keyCapture.control == kKeyboardKeyD);
  SC_CHECK(!input.capturing());  // the capture ended with the press
  SC_CHECK(!input.takeCapture().happened);  // a second call reads nothing
  pushKey(SDL_SCANCODE_D, false);  // the release passes through, harmlessly
  pump(input);
  SC_CHECK(noGameplayEdge(input.takeTickInput()));

  // Auto-repeat during a capture waits for a fresh press.
  input.beginCapture();
  pushKey(SDL_SCANCODE_X, true, 1);
  pump(input);
  SC_CHECK(input.capturing());
  SC_CHECK(!input.takeCapture().happened);
  pushKey(SDL_SCANCODE_X, true);
  pump(input);
  const InputCapture repeatCapture = input.takeCapture();
  SC_CHECK(repeatCapture.happened);
  SC_CHECK(repeatCapture.control == kKeyboardKeyX);
  pushKey(SDL_SCANCODE_X, false);
  pump(input);
  input.takeTickInput();

  // A release alone captures nothing.
  input.beginCapture();
  pushKey(SDL_SCANCODE_C, false);
  pump(input);
  SC_CHECK(input.capturing());
  SC_CHECK(!input.takeCapture().happened);
  pushKey(SDL_SCANCODE_C, true);
  pump(input);
  SC_CHECK(input.takeCapture().control == kKeyboardKeyC);
  pushKey(SDL_SCANCODE_C, false);
  pump(input);
  input.takeTickInput();

  // A control outside the rebindable lists changes nothing and the capture
  // stays armed.
  input.beginCapture();
  pushKey(SDL_SCANCODE_M, true);
  pump(input);
  SC_CHECK(input.capturing());
  SC_CHECK(!input.takeCapture().happened);
  pushKey(SDL_SCANCODE_M, false);
  pump(input);
  input.cancelCapture();
  SC_CHECK(!input.capturing());
  SC_CHECK(!input.takeCapture().happened);
  input.takeTickInput();

  // A captured press never drives the round: the game under a capture plays
  // the tick as though no key moved.
  Game game;
  game.start(41);
  const int cellsBefore = game.board().settledCellCount();
  input.beginCapture();
  pushKey(SDL_SCANCODE_D, true);  // the custom hard drop
  runBatch(input, game, 2);
  SC_CHECK(game.board().settledCellCount() == cellsBefore);
  SC_CHECK(input.takeCapture().control == kKeyboardKeyD);
  pushKey(SDL_SCANCODE_D, false);
  pump(input);

  // A pad press captures the same way.
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  input.beginCapture();
  padButton(joystick, stackfall::kVirtualButtonX, true);
  pump(input);
  const InputCapture padCapture = input.takeCapture();
  SC_CHECK(padCapture.happened);
  SC_CHECK(padCapture.device == DeviceKind::Controller);
  SC_CHECK(padCapture.control == kControllerButtonX);
  SC_CHECK(noGameplayEdge(input.takeTickInput()));
  padButton(joystick, stackfall::kVirtualButtonX, false);
  pump(input);

  // Cancel after a captured press still leaves the tables as they were.
  const ControlBindings before = input.controlBindings();
  input.beginCapture();
  padButton(joystick, stackfall::kVirtualButtonStart, true);
  pump(input);
  input.cancelCapture();
  SC_CHECK(!input.takeCapture().happened);
  SC_CHECK(input.controlBindings().table(DeviceKind::Controller,
                                          Orientation::Landscape) ==
           before.table(DeviceKind::Controller, Orientation::Landscape));
  padButton(joystick, stackfall::kVirtualButtonStart, false);
  pump(input);
  input.takeTickInput();

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// Changing the tables mid-session clears what the old tables raised, for the
// same reason the orientation turn does: a control held across the change
// keeps its old action, and its release cannot clear a level the new table
// never raised.
void check_bindings_change_clears_input() {
  SdlInput input;

  // A held direction and a pending press under the defaults.
  pushKey(SDL_SCANCODE_LEFT, true);
  pushKey(SDL_SCANCODE_SPACE, true);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Left));
  SC_CHECK(input.adapter().edgePending(GameAction::HardDrop));

  input.setControlBindings(customDropBindings());
  const TickInput after = input.takeTickInput();
  SC_CHECK(!after.game.leftHeld);
  SC_CHECK(noGameplayEdge(after));
  SC_CHECK(!input.adapter().isHeld(GameAction::Left));

  // The release still to come raises and clears nothing.
  pushKey(SDL_SCANCODE_LEFT, false);
  pump(input);
  const TickInput released = input.takeTickInput();
  SC_CHECK(!released.game.leftHeld);
  SC_CHECK(noGameplayEdge(released));
}

// Custom bindings in a round: one captured-and-applied press hard drops
// exactly once across a catch-up frame, on both devices, and a held custom
// direction keeps the section 4.5 charge rules.
void check_custom_bindings_drive_round() {
  SdlInput input;
  input.setControlBindings(customDropBindings());
  input.beginCapture();
  pushKey(SDL_SCANCODE_D, true);  // capture the custom drop key
  pump(input);
  input.cancelCapture();
  pushKey(SDL_SCANCODE_D, false);
  pump(input);
  input.takeTickInput();

  // A left rebind to E, so a held custom control exercises the DAS path.
  ControlBindings bindings = customDropBindings();
  bindings.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::Left)][0] = kKeyboardKeyE;
  bindings.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::Left)][1] = kNoControl;
  input.setControlBindings(bindings);

  Game game;
  game.start(42);
  const int startX = game.activePiece().x;
  pushKey(SDL_SCANCODE_E, true);
  runBatch(input, game, 3);
  SC_CHECK(game.activePiece().x == startX - 1);
  // Focus loss clears the custom held level like any other.
  pushFocusLost();
  pump(input);
  SC_CHECK(!input.adapter().isHeld(GameAction::Left));
  runBatch(input, game, 2);
  SC_CHECK(game.activePiece().x == startX - 1);
  pushKey(SDL_SCANCODE_E, false);  // the release that never arrived
  pump(input);

  // The custom hard drop across a four-tick catch-up frame: one drop, one
  // spawn, and no second edge on the ticks that follow (section 11, item 19).
  const int cellsBefore = game.board().settledCellCount();
  pushKey(SDL_SCANCODE_D, true);
  runBatch(input, game, 4);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);
  pushKey(SDL_SCANCODE_D, false);
  runBatch(input, game, 3);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);

  // The same edge rule on the pad: X hard drops once across five ticks.
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  const int cellsAtPad = game.board().settledCellCount();
  padButton(joystick, stackfall::kVirtualButtonX, true);
  runBatch(input, game, 5);
  SC_CHECK(game.board().settledCellCount() == cellsAtPad + 4);
  padButton(joystick, stackfall::kVirtualButtonX, false);
  runBatch(input, game, 3);
  SC_CHECK(game.board().settledCellCount() == cellsAtPad + 4);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// Two devices at once under custom tables: opposite directions from the two
// sources cancel, the release of one gives the other its effect back, and
// both sources keep their own edges.
void check_simultaneous_inputs_custom_tables() {
  const int device = stackfall::attachVirtualController();
  SC_CHECK(device >= 0);
  if (device < 0) {
    return;
  }
  SDL_GameController* controller = SDL_GameControllerOpen(device);
  SC_CHECK(controller != nullptr);
  if (controller == nullptr) {
    return;
  }
  SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
  SdlInput input;
  ControlBindings bindings = customDropBindings();
  bindings.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::Left)][0] = kKeyboardKeyE;
  bindings.table(DeviceKind::Keyboard, Orientation::Landscape)
      [static_cast<int>(GameAction::Left)][1] = kNoControl;
  input.setControlBindings(bindings);

  // The keyboard holds its custom left while the pad holds right: neither
  // direction wins (section 5).
  pushKey(SDL_SCANCODE_E, true);
  padButton(joystick, stackfall::kVirtualButtonDpadRight, true);
  pump(input);
  const TickInput both = input.takeTickInput();
  SC_CHECK(both.game.leftHeld && both.game.rightHeld);
  SC_CHECK(both.game.horizontalDirection() == 0);

  // The keyboard's release returns control to the pad.
  pushKey(SDL_SCANCODE_E, false);
  pump(input);
  const TickInput padAlone = input.takeTickInput();
  SC_CHECK(padAlone.game.rightHeld && !padAlone.game.leftHeld);
  SC_CHECK(padAlone.game.horizontalDirection() == 1);
  padButton(joystick, stackfall::kVirtualButtonDpadRight, false);
  pump(input);

  // Both custom drops pressed in one frame: one merged edge, one drop.
  Game game;
  game.start(43);
  const int cellsBefore = game.board().settledCellCount();
  pushKey(SDL_SCANCODE_D, true);
  padButton(joystick, stackfall::kVirtualButtonX, true);
  runBatch(input, game, 2);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);
  runBatch(input, game, 3);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);
  pushKey(SDL_SCANCODE_D, false);
  padButton(joystick, stackfall::kVirtualButtonX, false);
  pump(input);

  SDL_GameControllerClose(controller);
  SDL_JoystickDetachVirtual(device);
}

// Unplugging mid-hold and replugging: the custom table survives the round
// trip, the removed pad's held level dies with it, and the new pad answers
// the custom binding at once.
void check_unplug_replug_custom_bindings() {
  const int deviceA = stackfall::attachVirtualController();
  SC_CHECK(deviceA >= 0);
  if (deviceA < 0) {
    return;
  }
  SDL_GameController* padA = SDL_GameControllerOpen(deviceA);
  SC_CHECK(padA != nullptr);
  if (padA == nullptr) {
    return;
  }
  SDL_Joystick* stickA = SDL_GameControllerGetJoystick(padA);
  SdlInput input;
  input.setControlBindings(customDropBindings());

  // A held direction on the pad that is about to disappear.
  padButton(stickA, stackfall::kVirtualButtonDpadRight, true);
  pump(input);
  SC_CHECK(input.adapter().isHeld(GameAction::Right));

  // The unplug drops the source: no held level, no ghost edge.
  SDL_JoystickDetachVirtual(deviceA);
  pump(input);
  const TickInput after = input.takeTickInput();
  SC_CHECK(!after.game.rightHeld);
  SC_CHECK(noGameplayEdge(after));
  SDL_GameControllerClose(padA);

  // The replug opens a new source on the same custom table, and the custom
  // drop fires on it from the first press.
  const int deviceB = stackfall::attachVirtualController();
  SC_CHECK(deviceB >= 0);
  if (deviceB < 0) {
    return;
  }
  SDL_GameController* padB = SDL_GameControllerOpen(deviceB);
  SC_CHECK(padB != nullptr);
  if (padB == nullptr) {
    return;
  }
  SDL_Joystick* stickB = SDL_GameControllerGetJoystick(padB);
  Game game;
  game.start(44);
  const int cellsBefore = game.board().settledCellCount();
  padButton(stickB, stackfall::kVirtualButtonX, true);
  runBatch(input, game, 2);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);
  // The menu roles answer on the new pad too: the egress path never depended
  // on which instance is attached.
  padButton(stickB, stackfall::kVirtualButtonX, false);
  pump(input);
  padButton(stickB, stackfall::kVirtualButtonB, true);
  pump(input);
  SC_CHECK(input.takeTickInput().menu.backPressed);
  padButton(stickB, stackfall::kVirtualButtonB, false);
  pump(input);

  SDL_GameControllerClose(padB);
  SDL_JoystickDetachVirtual(deviceB);
}

}  // namespace

int main() {
  if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL_Init(SDL_INIT_GAMECONTROLLER) failed: %s\n",
                 SDL_GetError());
    return EXIT_FAILURE;
  }

  check_environment_mapping_load();
  check_keyboard_table();
  check_portrait_keyboard_table();
  check_controller_table();
  check_portrait_controller_table();
  check_stick_landscape_table();
  check_stick_portrait_table();
  check_stick_drives_game();
  check_stick_portrait_drives_game();
  check_stick_orientation_turn_clears();
  check_stick_capture_ignored();
  check_stick_removal();
  check_harddrop_rebind_off_up();
  check_portrait_controller_drives_game();
  check_keyboard_drives_game();
  check_controller_drives_game();
  check_menu_up_never_drops_on_return_to_play();
  check_pause_consumes_pending_input();
  check_focus_loss_clears_input();
  check_orientation_turn_clears_input();
  check_controller_removal();
  check_device_translation_tables();
  check_custom_bindings_drive_both_devices();
  check_capture_semantics();
  check_bindings_change_clears_input();
  check_custom_bindings_drive_round();
  check_simultaneous_inputs_custom_tables();
  check_unplug_replug_custom_bindings();

  SDL_Quit();
  stackfall_test::print_summary();
  std::printf("stackfall_input_tests: %s\n",
              stackfall_test::g_failures == 0 ? "PASS" : "FAIL");
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
