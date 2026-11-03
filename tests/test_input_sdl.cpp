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
//   * pending input behaves across pause, focus loss, and controller removal.
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

using stackfall::Game;
using stackfall::GameAction;
using stackfall::Scene;
using stackfall::SdlInput;
using stackfall::TickInput;
using stackfall::kNoGameAction;

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
      {stackfall::kVirtualButtonDpadUp, kNoGameAction, kNoGameAction,
       GameAction::MenuUp},
      {stackfall::kVirtualButtonA, kNoGameAction, GameAction::RotateCW,
       GameAction::MenuSelect},
      {stackfall::kVirtualButtonB, kNoGameAction, GameAction::RotateCCW,
       GameAction::MenuBack},
      {stackfall::kVirtualButtonX, kNoGameAction, GameAction::HardDrop,
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

// ---------------------------------------------------------------------------
// Both devices drive the game; edges survive catch-up exactly once.
// ---------------------------------------------------------------------------

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

  // X hard drops: the piece locks and a new piece spawns in the same tick.
  const int cellsBefore = game.board().settledCellCount();
  padButton(joystick, stackfall::kVirtualButtonX, true);
  runBatch(input, game, 1);
  padButton(joystick, stackfall::kVirtualButtonX, false);
  pump(input);
  SC_CHECK(game.board().settledCellCount() == cellsBefore + 4);

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

}  // namespace

int main() {
  if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
    std::fprintf(stderr, "SDL_Init(SDL_INIT_GAMECONTROLLER) failed: %s\n",
                 SDL_GetError());
    return EXIT_FAILURE;
  }

  check_environment_mapping_load();
  check_keyboard_table();
  check_controller_table();
  check_keyboard_drives_game();
  check_controller_drives_game();
  check_pause_consumes_pending_input();
  check_focus_loss_clears_input();
  check_controller_removal();

  SDL_Quit();
  stackfall_test::print_summary();
  std::printf("stackfall_input_tests: %s\n",
              stackfall_test::g_failures == 0 ? "PASS" : "FAIL");
  return stackfall_test::g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
