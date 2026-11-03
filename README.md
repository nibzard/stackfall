# STACKFALL

An offline block-stacking game for the Anbernic RG503, with procedural graphics and synthesized sound.

![STACKFALL gameplay: a falling yellow square above a colorful stack, with Hold on the left and Next on the right.](docs/images/gameplay.png)

Clear lines, keep room for the next piece, and chase your high score as the game gets faster.
Hold a piece for later, plan around five previews, and use the ghost outline to place each drop.

The desktop build is playable with a keyboard or controller.
The handheld target is the RG503 running ROCKNIX through PortMaster.
Handheld packaging and testing on the device remain unfinished.

## Play on desktop

You need a C++17 compiler, CMake 3.16 or later, and Ninja.
Install the development files for Simple DirectMedia Layer (SDL) 2.

1. Configure and build:

   ```sh
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

2. Start the game:

   ```sh
   ./build/stackfall
   ```

3. Select **Start Game**. Open **Controls** from the title screen to see the bindings.

### Controls

| Action | Keyboard | Controller |
| --- | --- | --- |
| Move | Left / Right | D-pad left / right |
| Soft drop | Down | D-pad down |
| Rotate clockwise | X / Up | A |
| Rotate counterclockwise | Z | B |
| Hard drop | Space | X |
| Hold | C / Left Shift | Y / Left shoulder |
| Pause or resume | Enter / Esc | Start |
| Mute or unmute | M | Select |

Use Up / Down to navigate menus, Enter to select, and Esc to go back.
On a controller, use the D-pad, A, and B.

### Your progress

The game saves your high score and mute setting between sessions.
On Linux, saves normally go in `~/.local/share/stackfall`.
Set `STACKFALL_SAVE_DIR` or pass `--save-dir DIR` to use another directory.

## How it plays

Each bag contains all seven pieces in a shuffled order.
You can hold once per piece, and the ghost shows where a hard drop lands.
Wall kicks let you rotate near an edge or another piece.

Clearing rows earns points. Soft and hard drops add points for the distance traveled.
The game advances one level every ten cleared lines, up to level 20.
A round ends when a new piece cannot enter or a piece locks above the visible board.

Every block, outline, and letter is drawn from rectangles and lines on a 960 × 544 canvas.
The game draws its own 5 × 7 font and generates its sound effects.
It needs no external graphics, fonts, or audio files to run.

## Development

STACKFALL uses C++17. The rules run at a fixed 60 ticks per second, independent of rendering.
The same seed and input sequence produce the same game state.
The rules library has no SDL dependency; separate libraries handle input, rendering, and audio.

### Run the tests

After building, run:

```sh
ctest --test-dir build --output-on-failure
```

The five test suites cover the rules, seeded replays, input, rendering, and audio.
They run without a display or sound device.
For a Debug build, use `-DCMAKE_BUILD_TYPE=Debug` with a separate build directory.

To run the game without a display or sound device:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build/stackfall --seed 20261103 --frames 360
```

Run `./build/stackfall --help` for replay, capture, and input scripting options.

### Project status

The rules, menus, saves, sound, and desktop verification harness are implemented.
Desktop stability checks, performance work, and the 27-pixel board layout have landed.
Next comes the approved panel layout and the final desktop regression checks.
Handheld packaging and testing on the RG503 follow.

The reports record what was checked and which limits remain:

| Read more | Contents |
| --- | --- |
| [Specification](docs/SPEC.md) | Rules, layout, platform requirements, and release criteria |
| [Testing guide](docs/AGENT_TESTING.md) | Scripted sessions, captures, and verification requirements |
| [Test report](notes/agent-testing.md) | Commands, results, and remaining checks |
| [Design review](notes/design-review.md) | Screenshot findings and verified changes |
| [Performance report](notes/performance.md) | Measurements, retained changes, and device limits |
| [PortMaster packaging](portmaster/README.md) | Planned handheld package |

Source code is in `src/`, public headers in `include/stackfall/`, and tests in `tests/`.
Build targets are defined in [`CMakeLists.txt`](CMakeLists.txt).

## License

[MIT](LICENSE).
