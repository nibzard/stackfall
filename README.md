# STACKFALL

STACKFALL is an original, offline, deterministic 2D block-stacking game for
the Anbernic RG503 handheld (ROCKNIX, PortMaster). It is written in C++17
with an SDL2 platform layer and a pure rules core that never touches SDL2.
All graphics are procedural, drawn only from rectangles and lines and a
5×7 glyph table. No sprites, textures, image assets, or font files exist
anywhere in the project.

**Status:** M3 in progress. Milestone M0 delivered the SDL2 platform layer:
the 960×544 window, the renderer fallback chain, controller detection with a
keyboard fallback, and the fixed-step 60 Hz loop. Milestone M1 delivered the
rules core: the seven shape tables, SRS rotation with wall kicks, the
bitmask board engine, the PCG32 seven-bag randomizer, and the game tick
with gravity, hold, lock delay, scoring, and levels. In milestone M2, T009
wired the input adapter and the tick into the loop, and T010 added the
procedural renderer and the 5×7 glyph font, so the desktop build now plays
and draws the full scene. T011 added the title, controls, pause, and
game-over screens, so a full round now runs from the title screen to game
over through both input paths. T012 added atomic high-score persistence and
T013 added the synthesized sound cues with a persistent mute. The agent
verification of M2 and M3, the platform hardening, packaging, and device
validation remain. Read `docs/SPEC.md` for the complete specification.

## Repository layout

```text
.
├── CMakeLists.txt        Build definition: core, game, and test targets
├── LICENSE               MIT license
├── README.md             This file
├── docs/                 Specification and the agent test contracts
├── include/stackfall/    Public headers of the rules core
├── notes/                Agent verification reports
├── portmaster/           PortMaster packaging (arrives with M4 and M5)
├── src/                  Core sources and the SDL2 platform layer
└── tests/                Headless test suite and its driver
```

## Build and test

1. Configure with Ninja:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   ```

2. Build all targets:

   ```bash
   cmake --build build
   ```

3. Run the headless test suite. No window opens:

   ```bash
   ctest --test-dir build --output-on-failure
   ```

4. Start the game executable:

   ```bash
   ./build/stackfall
   ```

Build a `Release` configuration for performance testing. For a headless
machine, set `SDL_VIDEODRIVER=dummy` when you run `./build/stackfall`. Add
`--frames N` to exit after N rendered frames.

## CMake targets

- `stackfall_core` — portable static library with the game rules. It has no
  SDL2 linkage, so rule tests run headless on any development machine.
- `stackfall_input` — static library with the SDL2 input adapter: the
  controller bindings, the tick-by-tick input queue, and the virtual
  controller of the developer options.
- `stackfall_render` — static library with the procedural renderer and the
  glyph font. It draws from rectangles and lines only.
- `stackfall_audio` — static library with the synthesized sound cues: the
  waveform synthesis, the cue detector that reads a round, and the SDL audio
  device with the mute toggle. It reads no audio file.
- `stackfall` — the game executable. It links the four libraries and SDL2.
- `stackfall_tests` — headless test suite. It links `stackfall_core` only,
  so CTest needs no display or audio device.
- `stackfall_input_tests`, `stackfall_render_tests`, `stackfall_audio_tests`
  — SDL-path suites. They render off-screen or run against the SDL dummy
  audio driver, so CTest still needs no display and no sound card.

## Current state (M3 in progress)

Milestone M0 is complete:

- Repository layout matches the specification.
- All CMake targets build with Ninja.
- The test suite runs under CTest without a window.
- `src/main.cpp` opens the 960×544 window, tries a logged renderer
  fallback chain, discovers game controllers, and falls back to the
  keyboard when none is open (T002).
- The main loop runs an integer fixed-step 60 Hz accumulator. It clamps a
  frame delta to 250 ms and logs a stall, so a suspend causes no catch-up
  (T002).
- `./build/stackfall --frames N` renders N frames and exits. A run with
  `SDL_VIDEODRIVER=dummy` exits 0 after a clean shutdown.

Milestone M1, the rules core, is complete. Six components are done:

- `include/stackfall/types.hpp` — board constants, piece ids, and rotation
  states (T003).
- `include/stackfall/pieces.hpp` with `src/pieces.cpp` — the seven shape
  tables, four orientations per piece, spawn positions, SRS wall-kick
  tables, and the rotation attempt rule (T003, T004).
- `include/stackfall/board.hpp` with `src/board.cpp` — the 22-row bitmask
  board and its color array, collision checks, lock, simultaneous line
  clears, and the drop-distance query used by ghost and hard drop (T005).
- `include/stackfall/randomizer.hpp` with `src/randomizer.cpp` — PCG32, the
  Fisher-Yates seven-bag shuffle, and the five-piece preview queue (T006).
- `include/stackfall/game.hpp` with `src/game.cpp` and `src/scoring.cpp` —
  the scene machine, the single-threaded tick, gravity, DAS/ARR, soft and
  hard drop, hold, lock delay, clears, scoring, and levels (T007).
- `tests/test_regression.cpp` — the replay-script parser, the state hash,
  and five pinned rounds. `./build/stackfall_tests --seed N` replays every
  pinned scenario at seed N, prints the refresh values, and requires the
  same hash and score twice (T008).

Milestone M2 is in progress. All three components are done:

- T009 wired the platform layer to the rules. The loop polls `SdlInput`,
  runs every due simulation tick through `Game::tick`, and feeds each tick
  one queued input, so a press edge fires exactly once. The developer
  options `--seed`, `--frames`, `--input-script`, `--input-log`, and
  `--virtual-controller` drive deterministic sessions.
- T010 added `include/stackfall/font.hpp`, the source-defined 5×7 glyph
  table, and `src/renderer_sdl.cpp` with
  `include/stackfall/renderer_sdl.hpp`. `Renderer::draw(const Game&)`
  draws the whole scene in the order of specification section 6.2: the
  background, the faint grid, the board frame, the settled cells, the
  ghost, the active piece, the hold and next previews at 15 px cells, and
  the score fields and hints as glyphs. It reads the round through const
  queries only and loads no textures, so the build needs no asset files.
- T011 completed the section 4.7 scene machine: `Game::openTitle` starts a
  session seed stream, `Game::tick(const TickInput&)` dispatches the title,
  controls, pause, and game-over screens, and the main loop leaves through
  the title menu. The title screen offers Start Game, Controls, and Quit
  next to the high score. The controls screen lists both mapping tables.
  The pause overlay freezes every counter and offers Resume, Restart, and
  Quit to Title. The game-over overlay shows the score, the lines, the
  level, and a new record note. Restart and Play Again draw a fresh seed
  from the session stream, and Quit to Title abandons the round. All four
  screens draw through the same procedural renderer, with the overlays as a
  blend veil and an opaque panel.

The M2 gate still needs the unattended agent checks. The session harness of
`docs/AGENT_TESTING.md` drives the real SDL event path (T023), the play
inspection covers keyboard and virtual-controller input with screenshots
(T024), and the review cycle of `docs/DESIGN_REVIEW.md` inspects the
screenshots and verifies the accepted fixes (T027, T028).

Milestone M3 is in progress. Persistence and sound are done:

- T012 added `include/stackfall/storage.hpp` with `src/storage.cpp`, the
  atomic high-score save. The record is one versioned ASCII file,
  `highscore.dat`, in the save directory: `version=1` on one line and
  `high_score=12345` on the next. A save writes a temporary file in the same
  directory, flushes and closes it, and renames it into place, so an
  interrupted save leaves the previous record intact. A missing, invalid, or
  partial record loads as high score 0 and never stops the game. The session
  loads the record at startup and saves it when a round sets a new record at
  game over.
- The save directory comes from the environment: `STACKFALL_SAVE_DIR` first,
  then an absolute `XDG_DATA_HOME` (the PortMaster launcher sets it to the
  port's `conf/` directory), then `$HOME/.local/share/stackfall`. Without any
  of them the session runs with the high score in memory only. The developer
  option `--save-dir DIR` names the directory directly and carries the record,
  the settings, and `log.txt` together.
- T013 added `include/stackfall/audio_sdl.hpp` with `src/audio_sdl.cpp`, the
  sound of the game. Five cues come out of one source-defined table:
  move/land, rotate, clear, four-line clear, and game over. Each cue is a
  short list of segments, and each segment sweeps one frequency, mixes
  square, triangle, and sine waves from one integer phase accumulator, and
  decays on its own envelope, so a cue is the same bytes on every machine.
  There is no `.wav`, `.ogg`, or other audio asset anywhere. A `CueDetector`
  reads the round through const queries after every simulation tick and
  raises one cue per event, so the rules core carries no audio notion and
  the gameplay randomizer stays untouched. The `AudioDriver` opens the SDL
  audio device, synthesizes the cues at the rate and channel count the
  device reports, and mixes them in its callback; a missing subsystem or
  device logs one line and the game runs silently. The M key or the
  controller Select button switches the mute, and the setting persists in
  `settings.dat` beside the high-score record.
- T014 gave the session log a file sink. Every platform-layer line goes to
  stdout and to `log.txt`: `STACKFALL_LOG_FILE` names the file exactly, and
  without it the file lands beside `highscore.dat` in the save directory, a
  location a launcher's own stdout redirection never touches. The sink opens
  before anything can fail, so the startup block (version, architecture, SDL
  versions), an `SDL_Init` failure, the selected renderer and controller, a
  failed save, and the clean-shutdown line all reach the file. A location
  that cannot be opened costs the file, never the game. Video failing exits
  cleanly with one actionable error; the gamepad subsystem failing leaves the
  keyboard playable; a controller disconnect logs and falls back to the
  keyboard without crashing. When presentation does not block - software
  rendering, or a vsync flag without a real vblank - the fallback frame
  limiter holds the 60 Hz frame rate.

The M3 remainder is hardening and verification: the stability and failure
checks (T025), the bounded performance loop (T026), and the full desktop
regression gate (T015).

The suites pass 338,405 core checks, 382 input checks, 860 renderer checks,
and 4,005 audio checks in Debug and Release.

## License

MIT. See `LICENSE`.
