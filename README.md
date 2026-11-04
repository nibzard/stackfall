# STACKFALL

An offline block-stacking game for the Anbernic RG503, with procedural graphics and synthesized sound.

![STACKFALL gameplay: a yellow square piece in flight above a settled piece, with Hold and the statistics on the left and five Next previews on the right.](docs/images/gameplay.png)

Clear lines, keep room for the next piece, and chase your high score as the game gets faster.
Hold a piece for later, plan around five previews, and use the ghost outline to place each drop.

The desktop build is playable with a keyboard or controller.
The handheld target is the RG503 running ROCKNIX through PortMaster.
The PortMaster package is assembled and checked locally; testing on the
device and publication remain unfinished.

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
   Open **Options** to change the sound, ghost outline, board grid, hold
   preview, Next preview count, and grayscale play scene.

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

The game saves your high score and all six options settings between sessions.
A round you quit, close, or interrupt keeps every point it scored, so an
abandoned run still counts toward the high score.
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

### Build the handheld binary

The ARM64 executable for the RG503 is built inside a PortMaster-aligned
Ubuntu 22.04 ARM64 Docker environment, not with the host SDL2:

```sh
tools/arm64/build-arm64.sh
```

The script builds the environment image, compiles the tree with CMake and
Ninja inside it, publishes `build-arm64/stackfall.aarch64`, documents its
runtime dependencies, and checks that the state hashes match the desktop
build. Read [tools/arm64/README.md](tools/arm64/README.md) for the
environment and the reproduction steps.

### Install on a handheld

The PortMaster launcher is
[`portmaster/Stackfall.sh`](portmaster/Stackfall.sh), written from the
current official template. One command stages the `/roms/ports` install
layout of the specification from a built binary:

```sh
tools/portmaster/install-tree.sh
```

The staged tree holds `Stackfall.sh` and `stackfall/stackfall.aarch64`
only; the launcher creates the port's `conf/` directory on every launch
and writes its own output to `stackfall/log.txt`.

### Package the release

One command assembles the distributable archive of the specification from
the ARM64 binary and a captured gameplay screenshot:

```sh
tools/portmaster/package.sh
```

The result is `build/portmaster-package/stackfall.zip`: the launcher, the
ARM64 executable, `port.json`, `gameinfo.xml`, the 4:3 `screenshot.png`,
the port readme, release notes, reproducible build instructions, and the
licenses. Unpacking it into a ports directory reproduces the install
layout above. The archive is written deterministically, and the build
instructions inside it name the SHA-256 of the binary and the commands
that reproduce it from a clean ARM64 environment.

Check the launcher with `tests/harness/t017.sh`, the package with
`tests/harness/t019.sh`, and the shipped version with
`tests/harness/t035.sh`. Read
[tools/portmaster/README.md](tools/portmaster/README.md) for what the
launcher does and how the two layouts relate.

### Project status

The project has passed the milestone M3 desktop gate.
The rules, menus, options screen, saves, sound, and the desktop
verification harness are implemented. The options screen changes the sound,
ghost outline, board grid, hold preview, Next preview count, and grayscale
play scene, and all six preferences persist between sessions.
Desktop stability checks, performance work, the 27-pixel board layout, and
the focused gameplay HUD (statistics left, Next right, no gameplay legend,
pause route to the controls screen) have landed.
The unattended harness (`tests/harness/run.sh`) drives fifteen sessions
through the real application, covering both inputs on every options row, the
settings file after each change and across relaunches and legacy files, the
hold-off rules, the five Next preview counts at one seed, the grayscale play
scene, and the record saved when a round is abandoned mid-game, each against
a headless reference replay and pixel checks.
The full desktop regression (`tests/harness/t015.sh`) covers the input
repeat tuning, lock delay, the seven-bag randomizer, restart paths, and
high-score persistence in Debug and Release, with deterministic hashes
pinned across builds.
The ARM64 binary builds in the PortMaster-aligned Ubuntu 22.04
environment and reproduces the desktop state hashes; a second build in a
clean container reproduces the binary bytes.
The PortMaster launcher and the `/roms/ports` install layout are written
and checked by `tests/harness/t017.sh`.
The release archive (`stackfall.zip`) is assembled locally with
`tools/portmaster/package.sh` and checked by `tests/harness/t019.sh`: it
carries the binary, the launcher, the packaging metadata, a 4:3 gameplay
screenshot captured from the desktop build, the licenses, release notes,
and reproducible build instructions.
The release version is 1.0.0, one value in the version constants, the CMake
project version, the sanity pin, the state-hash pins, the startup banner,
and the package release notes; `tests/harness/t035.sh` holds them together.
Testing on the RG503 and public publication follow.

The reports record what was checked and which limits remain:

| Read more | Contents |
| --- | --- |
| [Specification](docs/SPEC.md) | Rules, layout, platform requirements, and release criteria |
| [Testing guide](docs/AGENT_TESTING.md) | Scripted sessions, captures, and verification requirements |
| [Test report](notes/agent-testing.md) | Commands, results, and remaining checks |
| [Design review](notes/design-review.md) | Screenshot findings and verified changes |
| [Performance report](notes/performance.md) | Measurements, retained changes, and device limits |
| [PortMaster packaging](tools/portmaster/README.md) | Handheld launcher, install tree, and release archive |

Source code is in `src/`, public headers in `include/stackfall/`, and tests in `tests/`.
Build targets are defined in [`CMakeLists.txt`](CMakeLists.txt).

## License

[MIT](LICENSE).
