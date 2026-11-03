# STACKFALL — Game Design & Technical Specification

**Version:** 1.0 (implementation baseline)  
**Target:** Anbernic RG503 / ROCKNIX / PortMaster  
**Category:** Original single-player tetromino block-stacking game  
**Status:** M3 in progress; the platform layer, the rules core, the desktop build,  
high-score persistence, and synthesized sound are integrated and tested headless

Development runs unattended with AI coding agents. Agents run all available
software checks and inspect the desktop game throughout development. Defer
human playtests and physical device tests until the complete local package is
ready. See `docs/AGENT_TESTING.md` for the agent verification contract.
This policy takes precedence over testing schedules in the original briefs.

## 1. Product goal

Deliver a complete offline block-stacking game that launches from the RG503's ROCKNIX **Ports** menu, supports the built-in gamepad, maintains smooth 60 Hz gameplay, saves high scores, and can also be developed and tested on desktop Linux, Windows, or macOS. Gameplay must be deterministic, rules and tests must not depend on graphics, and every graphic must be drawn programmatically. **No sprites, textures, external fonts, game engine, or asset pipeline.**

**Working title:** STACKFALL. Use an original product name, color palette, layout, sounds, and presentation. Seven familiar tetromino shapes are the starting pieces; do not use the Tetris name or trade dress for distribution.

## 2. Release scope

### Mandatory for v1.0

- Seven tetrominoes: **I, O, T, S, Z, J, L**.
- 10-column × 20-visible-row playing field; two hidden rows above the field.
- 7-bag random piece generation, five upcoming-piece previews, one-piece hold with once-per-lock restriction.
- Deterministic fall gravity, left/right movement with DAS/ARR, soft drop, hard drop, 90° clockwise/counter-clockwise rotation, SRS wall kicks, collision detection, grounded lock delay, 1–4 line clears, score and increasing levels.
- Ghost landing preview; pause/resume; title screen; controls screen; game-over screen; restart; local high score.
- RG503 controller via SDL2 GameController; keyboard fallback for desktop development.
- Fullscreen 960×544 composition, fixed 60 Hz simulation, 60 FPS render target, drawn using rectangles/lines and procedurally generated glyphs; optional nonessential synthesized SFX with mute toggle.
- PortMaster-compatible AArch64 package, device-side log and clear recovery from errors.
- Automated headless rules tests and a smoke-test checklist for actual hardware.

### Explicitly excluded from v1.0

Online play, multiplayer, leaderboards, achievements, network dependency, custom block shapes, 180° rotation, T-spin detection/bonuses, combo and back-to-back scoring, replays exposed to users, themes, music files, sprite/font assets, elaborate particle effects, touch controls, and an in-game controller rebinding UI. Can be considered for v1.1+.

## 3. Platform and technical constraints

| Concern | Specification |
| --- | --- |
| Device | Anbernic RG503; Rockchip RK3566 quad-core Cortex-A55, Mali-G52, 1 GB RAM |
| OS | ROCKNIX, 64-bit ARM Linux; PortMaster launcher |
| Display | Native 960×544; logical coordinate system 960×544 |
| Stack | C++17, SDL2, CMake, Ninja, GCC/Clang |
| Device binary | Linux ELF, **AArch64**; e.g. `stackfall.aarch64` |
| Baseline build | PortMaster-aligned Ubuntu 22.04 ARM64 build container/chroot |
| Renderer | SDL2 2D renderer, accelerated/vsync preferred, gracefully fall back |
| Game tick | Fixed **60 Hz**; no floating point in board, timing, or collision logic |
| Offline | Zero online services or runtime downloads required after installation |
| RAM | Design for under 64 MiB process-private use; verify on device |
| Saves | Small versioned high-score file in the port's `conf/` directory |

The 60 FPS requirement is a **target** to validate on the RG503, not a guarantee inferred from hardware specifications.

## 4. Game rules

### 4.1 Board coordinates and representation

- Width `W=10`; visible height `H=20`; hidden rows `HIDDEN=2`.
- Logical `x=0..9`; `y=-2..19`. Visible cells have `y>=0`; hidden cells are `y=-2,-1`.
- Row storage: `std::array<uint16_t,22> occupancy`. Only bits `0..9` are used; a filled row has mask `0x03FF`.
- Color/type storage: `std::array<std::array<uint8_t,10>,22> cellType` using `0=empty`, `1..7=piece IDs`. Row index is `y+2`.
- `occupancy` is authoritative for collision; both occupancy and color/type rows move together on a line clear. Assert they remain consistent in tests/debug builds.
- Never read or write a row outside `[-2,19]`, including on rotations near the top.

### 4.2 Starting pieces

Each piece consists of four unit squares. Spawn orientation is `0`; clockwise `R`; half-turn `2`; counter-clockwise `L`. Canonical spawn drawings are below (`.` = empty). Each row has four entries, and each tetromino has four rows. The internal rotation center and kick data must follow SRS: JLSTZ occupy an embedded 3×3 rotation system; I uses the standard 4×4 system; O does not kick.

```text
I      O      T      S      Z      J      L
....   .OO.   .T..   .SS.   ZZ..   J...   ..L.
IIII   .OO.   TTT.   SS..   .ZZ.   JJJ.   LLL.
....   ....   ....   ....   ....   ....   ....
....   ....   ....   ....   ....   ....   ....
```

- Spawn horizontally centered at matrix `originX=3`. This places the I at columns 3..6 and O at 4..5.
- Spawn such that the topmost occupied cell has `y=-1`; for the I above, matrix origin is `y=-2` because its first occupied row is local row 1; all other pieces begin at `originY=-1`.
- A piece is *active*, not written to `occupancy`, until it locks.
- A move or rotation is valid only if every occupied candidate cell is inside the 10 columns and `y=-2..19` and does not intersect a settled occupancy bit. Reject otherwise, leaving the piece unchanged.
- Preview and ghost are render-only representations of active/queued pieces.

### 4.3 Randomizer and queue

- Use PCG32 (fixed, explicitly implemented algorithm) seeded once per game. No platform-dependent C++ distribution implementation in the deterministic core.
- Use Fisher–Yates to shuffle all seven IDs into a bag. Consume every piece before generating the next bag. Fill a queue sufficiently far ahead to always display the **next five pieces**.
- Fresh game creates fresh seed. Developer test CLI accepts `--seed <integer>` for reproducibility.
- Holding an active piece is allowed once per newly locked piece. If hold slot is empty, store active and draw from queue; otherwise swap active and held piece. Returned piece always resets to spawn rotation/position. Hold cannot be repeated until the current active piece locks.
- Any spawn or hold swap that cannot occupy its spawn cells triggers **GAME OVER** (block-out).

### 4.4 Movement and SRS rotation

- Left/right: one column per successful movement; no movement through settled cells or beyond walls.
- 90° clockwise and counter-clockwise rotations; no 180° button in v1.
- Implement the standard **Super Rotation System (SRS)** using immutable precomputed orientation masks and canonical ordered kick tables for `0↔R`, `R↔2`, `2↔L`, `L↔0`. JLSTZ share one kick table, I has its own, and O remains stationary. Try up to five positions and choose the first valid candidate, otherwise reject the rotation.
- The SRS reference lists `+y` upward; the game's pixel/grid `+y` points downward. **Negate reference y kick values when encoding them.** Use one consistent convention in code and tests. Reference: https://www.tetris.wiki/SRS

### 4.5 Gravity, repeats, and locking

All values are in **60 Hz simulation ticks** and are not dependent on render FPS.

| Rule | v1.0 constant |
| --- | --- |
| Simulation rate | 60 ticks/s |
| Initial move on D-pad/key press | Immediately on next tick |
| Delayed auto-shift (DAS) | 10 ticks before horizontal repeating |
| Auto-repeat rate (ARR) | 2 ticks between repeated horizontal steps |
| Soft drop | 1 attempted cell per tick while held (takes priority over natural gravity) |
| Hard drop | Move to lowest valid y and lock instantly |
| Lock delay | 30 consecutive grounded ticks before lock |
| Lock-delay reset allowance | At most 15 valid grounded shifts/rotations per active piece |
| Pause | Freeze all simulation counters exactly; no game updates |

Gravity, in ticks per cell (v1 levels 1–20):

```text
Level:  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20
Ticks: 48 43 38 33 28 23 18 13  8  6  5  5  4  4  3  3  2  2  1  1
```

- Gravity timer increments on unpaused ticks without soft drop. When its threshold is reached, attempt one downward cell and reset the gravity timer. When soft-drop is held, attempt one downward cell per tick and reset natural gravity timer; award manual-drop points only on successful downward movement.
- Grounded means a one-cell downward move is invalid. Grounded ticks accrue while grounded. A successful lateral move/rotation made while grounded may reset the lock timer, up to 15 times per active piece; a valid move/rotation that becomes airborne sets grounded ticks to zero but does not replenish the reset budget. Failed inputs never reset it.
- Gravity blocked at the bottom does not instantly lock: only hard drop or expiry of lock delay does. Lock commits the four active cells to the board, clears rows, updates score and level, then spawns the next piece and resets the per-piece hold restriction.
- If the just-locked piece has any occupied cell in a hidden row (`y<0`), game ends due to **lock-out**; no new piece spawns. This is a deliberate, simple v1 rule.

### 4.6 Clearing and scoring

- After lock, find every full row (`occupancy[y] == 0x03FF`) across all stored rows; remove all full rows simultaneously, shift the remaining rows down, and clear rows created at the top. The active piece is no longer present while clearing.
- Total cleared lines are persistent within the current game; initial level 1. `level = min(20, 1 + floor(totalLines/10))`.
- A single lock clearing four lines is a valid four-line clear; a piece can never clear more than four.
- Add the following points, multiplied by the **level before the clear**:

| Lines cleared in one lock | Base points |
| --- | ---: |
| 0 | 0 |
| 1 | 100 |
| 2 | 300 |
| 3 | 500 |
| 4 | 800 |

- Soft drop: `+1` point per successfully manually dropped cell; hard drop: `+2` per cell traveled on hard drop. All scores are unsigned 64-bit with safe formatting/overflow checks.
- No T-spins, combo multipliers, back-to-back bonuses, or perfect-clear bonuses in v1.0. These are potential later additions and should not be accidentally implied by UI.
- Marathon is endless until top-out; speed stops increasing after level 20 but scoring continues.

### 4.7 Game state machine

```text
BOOT -> TITLE -> PLAYING -> GAME_OVER -> TITLE
                  |  ^           | 
                  v  |           +-> NEW GAME -> PLAYING
                PAUSED
```

- **TITLE:** Start Game / Controls / Quit; display saved high score.
- **CONTROLS:** display mappings, confirm/back to title.
- **PLAYING:** game logic updates, piece input is active.
- **PAUSED:** overlay with Resume / Restart / Quit to Title; entire game state and clocks remain frozen. Restart begins a fresh seeded game; Quit to Title abandons the current round.
- **GAME_OVER:** final score, total lines, level, new-record indication, Play Again / Title. Save the high score when it improves.
- **EXIT:** cleanly stop audio, controller, renderer, SDL subsystems; return to ROCKNIX cleanly.

## 5. Controls

SDL2 GameController mapping must be loaded from the PortMaster environment (`SDL_GAMECONTROLLERCONFIG`) when available. The physical Anbernic markings may not match SDL button letters across layouts, so test on actual hardware and update a mapping table in one location, not scattered throughout the source.

| Function | RG503 SDL mapping (initial) | Desktop keyboard |
| --- | --- | --- |
| Move left/right | D-pad left/right | Left/Right |
| Soft drop | D-pad down | Down |
| Rotate clockwise | A | X / Up |
| Rotate counter-clockwise | B | Z |
| Hard drop | X | Space |
| Hold | Y or left shoulder | C / Left Shift |
| Pause/resume | Start | Enter / Esc |
| Select menu item | A | Enter |
| Back from controls | B | Esc |
| Menu navigation | D-pad up/down | Up/Down |

- D-pad left+right held simultaneously causes no horizontal movement. Pressing a new opposite direction resets DAS/ARR state and moves immediately if the opposite key is no longer down.
- Rotation, hold, hard drop, pause and menu actions are **edge-triggered**. Holding them does not fire repeatedly; horizontal movement uses the tick-based repeat system.
- Input processing order within a gameplay tick: pause command; hold (if used, ignore other gameplay actions this tick); rotation; horizontal move/repeat; hard drop (immediately end piece actions for tick); otherwise soft drop or natural gravity; then ground/lock bookkeeping. Process SDL events before simulation, and consume press edges at most once even when the main loop catches up multiple simulation ticks.
- Let PortMaster handle its platform-wide exit chord; do not capture or override it. Keyboard and gamepad disconnection must not crash the game.

## 6. Visual specification

### 6.1 Canvas and placement

- Logical canvas: 960 × 544. Use `SDL_RenderSetLogicalSize` or equivalent letterboxing to handle a non-native desktop window.
- Board cell: **24 × 24 logical pixels**. Board is `240 × 480`, from **x=360..599**, **y=32..511**.
- Board frame: subtle rectangular outline, no texture, no image.
- Left panel: Hold preview and control hints in approx. x=52..324.
- Right panel: Next five pieces plus score, lines and level in approx. x=630..910.
- Full visible grid always fits without scrolling; preview shapes are drawn from the same shape cell coordinates at smaller cell size (e.g. 15 px).

### 6.2 Draw order

1. Solid background color, then faint grid lines (optional user setting).
2. Board border and settled cells, using their stored piece colors.
3. Ghost (same piece outline or dim fill), only when below the live piece.
4. Active piece.
5. Hold and Next previews.
6. Score/line/level fields, title, overlays, menus.

All geometry is generated at runtime from integer coordinates, color values and piece masks. No calls to texture/image loading are allowed. UI glyphs use a source-defined **5×7 integer bitmask font**, rendered by filled rectangles (so there are no font files). Draw from a seven-color custom palette with high-contrast outlines, and include a monochrome fallback mode in a later release if needed.

### 6.3 Sound

Sound is secondary to device compatibility. If enabled, synthesize short PCM tones from mathematical waveforms at initialization for rotation, move/land, clear, four-line clear, and game-over. Do not use `.wav` or `.ogg` assets. A missing audio device must not prevent the game from starting. Add an on/off toggle; initialize mute state from a settings file if that feature is implemented. **Sound may be disabled for an early device bring-up, but a complete v1 release should include basic synthesized cues.**

## 7. Deterministic simulation contract

- `Game::tick(InputFrame)` changes all gameplay state on a single thread. `InputFrame` is a small immutable struct of action presses and current held buttons. It has no SDL types.
- `Renderer::draw(const Game&)` reads state only; never mutates the game or calls random functions.
- `Game::start(seed)` resets all fields; equal seed plus identical tick-indexed inputs must produce identical game state and score across builds using the same rules version.
- Maintain a fixed-step accumulator based on `SDL_GetPerformanceCounter/Frequency`, with `dt=1/60s`. Poll events as often as possible, run the required number of simulation ticks, and render once. Avoid variable-`dt` gameplay. Guard against a large resume/suspend elapsed time (e.g. clamp elapsed frame delta to 250 ms) and log extraordinary stalls rather than allowing uncontrolled catch-up.
- Keep gameplay RNG separate from optional cosmetic/audio randomness.
- During development, compute a stable game-state hash after a supplied replay sequence of tick-indexed input frames; the command-line test runner compares this hash against pinned regression values.

## 8. Source architecture

```text
stackfall/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── docs/
│   └── SPEC.md
├── include/stackfall/
│   ├── types.hpp           # constants, PieceId, Rotation, cells
│   ├── pieces.hpp          # SRS masks, kick offsets
│   ├── board.hpp           # occupancy, collision, line clears
│   ├── randomizer.hpp      # PCG32 and 7-bag
│   ├── game.hpp            # deterministic state & tick
│   ├── input.hpp           # SDL-free InputFrame
│   └── scoring.hpp         # level, gravity, points
├── src/
│   ├── main.cpp            # SDL setup and fixed-step loop
│   ├── pieces.cpp
│   ├── board.cpp
│   ├── randomizer.cpp
│   ├── game.cpp
│   ├── scoring.cpp
│   ├── input_sdl.cpp       # controller/keyboard translation
│   ├── renderer_sdl.cpp    # procedural geometry and glyphs
│   ├── audio_sdl.cpp       # generated simple SFX
│   └── storage.cpp         # settings/high score, atomic save
├── tests/
│   ├── test_pieces.cpp
│   ├── test_board.cpp
│   ├── test_randomizer.cpp
│   ├── test_game.cpp
│   └── test_regression.cpp
└── portmaster/
    ├── Stackfall.sh
    ├── port.json
    ├── gameinfo.xml
    └── README.md
```

CMake must build `stackfall` and `stackfall_tests`. `stackfall_core` is a portable static library with no SDL linkage. `stackfall` links `stackfall_core` and SDL2. `stackfall_tests` links `stackfall_core` only, so rule tests run headlessly on any development machine.

Key core interfaces (conceptual, refine as implementation begins):

```cpp
struct InputFrame {
  bool leftHeld, rightHeld, downHeld;
  bool rotateCWPressed, rotateCCWPressed;
  bool hardDropPressed, holdPressed, pausePressed;
};

struct Piece { PieceId id; Rotation rotation; int x, y; };

class Game {
public:
  void start(uint64_t seed);
  void tick(const InputFrame& input);
  const Board& board() const;
  const Piece& activePiece() const;
  // Pure queries: queue, hold, score, level, scene/state
};
```

## 9. Storage and reliability

- In PortMaster, launcher creates `$GAMEDIR/conf`, exports `XDG_DATA_HOME="$GAMEDIR/conf"`, and optionally passes an explicit save-directory environment variable to avoid ambiguity.
- High score file uses UTF-8 ASCII, a version field and a decimal score. Example: `version=1\nhigh_score=12345\n`. No JSON library required.
- On successful new record, write a temporary file in the same directory, flush/close, then rename into place. Detect invalid/partial files and safely recover to high score 0; do not crash.
- Log startup version, architecture, SDL init failures, selected renderer and controller, any save failure and orderly shutdown to `log.txt` through launcher redirection.
- If controller is absent, keep keyboard playable. If accelerated renderer creation fails, try a basic SDL renderer; if all renderer creation fails, print an actionable error and terminate cleanly.
- No network permission required, and no telemetry.

## 10. Building and deploying

### 10.1 Desktop iteration

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
./build/stackfall
```

Build a `Release` configuration for performance testing; avoid platform-specific C++ APIs in the core.

### 10.2 AArch64 build

Use an Ubuntu 22.04 ARM64 PortMaster-aligned container or chroot with compatible SDL2 development libraries. Build using CMake/Ninja inside the ARM64 environment, confirm `file stackfall.aarch64` reports `ELF 64-bit ... ARM aarch64`, and use `readelf`/`ldd` checks to identify runtime dependencies. Do not ship an x86 executable, or assume the host desktop SDL2 binary is compatible.

Official environment guide: https://portmaster.games/build-environments.html

### 10.3 Local ROCKNIX install

```text
/roms/ports/
├── Stackfall.sh
└── stackfall/
    ├── stackfall.aarch64
    ├── conf/
    │   └── highscore.dat      # created at runtime
    ├── libs.aarch64/          # only if additional libs are needed
    └── log.txt                # created at runtime
```

- Build a launcher from the **current** official PortMaster template; discover `controlfolder`, `source control.txt`, source CFW modifier if present, call `get_controls`, use `/$directory/ports/stackfall` for the game folder, export `SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"`, set save and library paths, call `pm_platform_helper` on the executable, run it, and call `pm_finish` afterward.
- The PortMaster default exit chord may rely on a background `gptokeyb` process even when SDL input is native. Launch it in exit-hotkey-only mode as appropriate to the installed version and test that it exits/cleans up. Avoid keyboard mapping injection when direct SDL controller input works.
- Launch from the ROCKNIX Ports menu, test controls, leave/reenter the game, and review `stackfall/log.txt`.

Official packaging guide: https://portmaster.games/packaging.html

### 10.4 Distributable PortMaster archive

For publishing, use the current PortMaster directory structure and add `port.json`, `README.md`, `gameinfo.xml`, required 4:3 gameplay `screenshot.png`, any licenses and the actual native executable. The packaging metadata is **not required for early local binary smoke tests** but is required for a PortMaster-ready public release.

## 11. Automated verification

Implement assertions and data-driven tests, including at least:

1. Each of the seven masks has exactly four occupied cells in each of its four orientations.
2. O never changes cell coordinates through rotation and never kicks.
3. I horizontal/vertical orientations and all JLSTZ states match known SRS reference cases.
4. Left/right movement correctly stops at walls for every orientation.
5. Rotation against left and right walls applies valid kicks or fails without moving.
6. Rotation above the hidden-row boundary never reads or writes outside the board.
7. Collision with settled cells blocks movement; active piece is not prematurely committed.
8. One, two, three and four full lines are cleared correctly and colors shift with occupancy.
9. Gravity moves exactly when scheduled; pause freezes timers; restart resets them.
10. Grounded piece locks after 30 ticks, not before; maximum 15 lock-delay resets.
11. Hard drop travels to the lowest legal position and locks in the same tick.
12. Soft-drop and hard-drop point accounting matches the actual number of cells moved.
13. Each seven-draw bag is a permutation of I/O/T/S/Z/J/L; seeded sequences match fixtures.
14. Hold works once per piece, respawns in orientation 0, and is re-enabled only after lock.
15. Failed spawn and lock with hidden-row cells produce game over deterministically.
16. Score/level boundaries: 9→10 lines, 19→20, level cap 20, score multiplied by pre-clear level.
17. Full-row mask is `0x03FF`, and colors/occupancy remain consistent after hundreds of random tests.
18. Same seed and tick input stream yield identical state hashes across desktop and ARM64.
19. Input edge presses are consumed once even if catch-up executes multiple ticks.
20. Corrupt/missing high-score file is safely handled, and good records round-trip after relaunch.

## 12. Device acceptance checklist

Run this checklist at the end, after the local package and agent checks are
complete. Device access and human feedback do not block earlier software work.

The release is complete only when all of these pass on a real RG503 running ROCKNIX:

- [ ] Port appears in **Ports**, opens to title, displays no missing-library error.
- [ ] Game starts without keyboard or network, using only the RG503 buttons.
- [ ] All seven shapes spawn, rotate, fall, hard-drop and lock properly.
- [ ] Hold and five previews work; hold cannot be abused more than once per piece.
- [ ] Ghost is correct next to walls and over tall stacks.
- [ ] 1/2/3/4-line clears and scoring/level increases match test fixtures.
- [ ] Game pauses/resumes and survives repeated menu transitions without a stuck button.
- [ ] Game-over, restart, quit-to-title and PortMaster exit chord work.
- [ ] Score persists after quitting and rebooting; corrupted save does not crash.
- [ ] Native display is properly centered with nothing clipped or unreadable.
- [ ] Continuous 10-minute play session remains responsive at approximately 60 FPS; record actual frame times, identify spikes, and verify no sustained slowdowns.
- [ ] No controller-disconnect, audio-init, renderer-fallback or missing-save crash.
- [ ] Launch log shows a clean process exit; launch and quit repeatedly.

## 13. Implementation plan and gating

### M0 — repository and toolchain

**Deliverable:** CMake targets, test runner, Git repository, desktop SDL window, SDL controller detection, 60 Hz fixed-step loop.  
**Gate:** Compiles cleanly on desktop; test executable runs without window; blank window exits cleanly.

### M1 — pure tetromino engine (first coding milestone)

**Deliverable:** seven shapes/rotations, SRS kicks, 10×22 board, occupancy+colors, collision, bag randomizer, spawning, movement, gravity, lock, row clears, scoring, hold and ghost query functions.  
**Gate:** Headless unit tests pass for shapes, rotations, collision, randomizer, clears, scoring and top-out. No SDL required for tests.

### M2 — playable desktop alpha

**Deliverable:** SDL controller/keyboard adapter, board renderer, procedural font, title/play/pause/game-over screens, hold/next previews and scores.  
**Gate:** Full game playable by keyboard and controller on desktop; no external graphics.

An agent verifies this gate through the real SDL event path. Use a virtual
display and virtual controller when physical input devices are unavailable.
Capture and inspect screenshots. No human playtest is required at this gate.
Then run the independent screenshot review in `docs/DESIGN_REVIEW.md`.
Route accepted findings into Looper tasks and verify required fixes before
the stability baseline. Preserve the approved gameplay scope.

### M3 — polished rules and persistence

**Deliverable:** DAS/ARR tuning, complete lock-delay handling, seven-bag edge cases, high-score save/load, synthesized feedback sounds, regression tests and error handling.  
**Gate:** No known rule or state bugs in repeated full-game desktop sessions.

Agents run the desktop sessions, visual review, and stability checks defined
in `docs/AGENT_TESTING.md`. Save commands, seeds, screenshots, and results.
After the stability baseline, run the bounded T026 optimization loop in
`docs/PERFORMANCE.md`. Keep only changes supported by repeated measurements
and passing behavior checks. Complete this loop before the desktop gate.

### M4 — RG503 native bring-up

**Deliverable:** compatible ARM64 release binary, PortMaster launcher, direct gamepad mapping, logs, and software validation.  
**Gate:** Binary architecture, dependencies, launcher, and cross-platform state hashes pass software checks. Physical device checks remain deferred to final acceptance.

### M5 — packaging and v1.0 release

**Deliverable:** ready-to-run PortMaster archive, required metadata/README/screenshot/licenses, release notes and reproducible build instructions.  
**Gate:** Fresh ROCKNIX installation launches and plays entirely offline; score persists across reboot.

Complete the archive and all agent checks before requesting human or device
testing. The final operator pass includes the section 12 checklist, a fresh
installation, and a human assessment of controls, clarity, feedback, and pace.
Agent evidence cannot establish handheld performance or human enjoyment.

## 14. Definition of done

STACKFALL v1.0 is a **finished game**, not merely a board demonstration, when: it launches from ROCKNIX Ports; a player can begin, play, score, progress in speed, hold, pause, lose, restart and quit; all seven shapes behave consistently; graphics require no asset files; high scores persist; deterministic tests pass; and full sessions run acceptably on the physical RG503. Only after those requirements are met should we consider extra shapes, modes, spin scoring or graphics effects.

## 15. Implementation kickoff

Implement **M0 → M1** first, in this precise sequence:

1. `types.hpp` and canonical piece masks/orientations.
2. SRS kick data with wall-rotation unit tests.
3. `Board` with bitmask collision, lock and simultaneous line clearing.
4. PCG32 + seeded seven-bag queue.
5. `Game::tick(InputFrame)` with fixed tick rules.
6. Headless regression tests and deterministic state hashes.
7. SDL rendering and gamepad input only once the mathematical rules pass.

This ordering protects the project from visual bugs masking rule bugs and makes device porting substantially easier.

---

**Technical references** (reviewed November 2026):
- RG503 ROCKNIX hardware/software: https://rocknix.org/devices/anbernic/rg503/
- PortMaster AArch64 build environments: https://portmaster.games/build-environments.html
- PortMaster packaging/launcher: https://portmaster.games/packaging.html
- SRS rotations and kick tables: https://www.tetris.wiki/SRS
