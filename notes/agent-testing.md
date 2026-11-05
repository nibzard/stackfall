# Agent verification report

Date: 2026-11-02.

Sources: [game specification](../docs/SPEC.md),
[agent testing contract](../docs/AGENT_TESTING.md), and [task list](../init).

## Initial review

This report covers the current M0 window and existing M1 components.
Full gameplay, visual gameplay review, and stability tasks remain pending.
No human or physical device test runs during this review.

The repository has no commit at inspection time. Tested source identities
are saved in `build/agent-checks/2026-11-02-review/source.sha256`.
The tested `src/main.cpp` SHA-256 digest is
`4dd411a521a351796c69309d0a7fd54afbcd4f52250cc8fa571df5b71ee5d74d`.

## Verified results

- Passed: Debug and Release builds and headless tests. The Release test
  runner reports 323,387 checks and zero failures.
- Passed: the dummy video driver starts the window and exits cleanly after
  a bounded frame run. Renderer fallback selects software rendering.
- Passed: the real SDL window opens under an X11 virtual display. The agent
  opens and inspects `build/agent-checks/2026-11-02-review/window.png`.
  It shows the expected blank background. No gameplay is implemented yet.
- Passed: injected keyboard Q closes the window with exit code zero.
  The log records 33 rendered frames and 29 simulation ticks.
  See `build/agent-checks/2026-11-02-review/window.log`.
- Finding: a dummy-driver run renders 100,000 frames in 5.42 seconds at
  98% CPU. It records 324 simulation ticks. This demonstrates missing
  render pacing when presentation does not block. T014 tracks the fix.
  These measurements do not establish handheld performance.

## Commands and environment

The host uses GCC 13.3.0 and SDL 2.30.0. The virtual X11 run selects the
OpenGL renderer. The dummy-driver run selects the software renderer.

```bash
cmake -S . -B /tmp/stackfall-review-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/stackfall-review-debug
ctest --test-dir /tmp/stackfall-review-debug --output-on-failure
cmake -S . -B /tmp/stackfall-review-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/stackfall-review-release
ctest --test-dir /tmp/stackfall-review-release --output-on-failure
/tmp/stackfall-review-release/stackfall_tests
SDL_VIDEODRIVER=dummy /tmp/stackfall-review-debug/stackfall --frames 120
/usr/bin/time -f 'Elapsed: %e seconds; CPU: %P' env SDL_VIDEODRIVER=dummy /tmp/stackfall-review-debug/stackfall --frames 100000
```

For the graphical run, start Xvfb through `xvfb-run -a` with screen
`0 960x544x24`. Launch the Debug executable. Find the visible `STACKFALL`
window with `xdotool search --onlyvisible --name '^STACKFALL$'`.
Focus that window with `xdotool windowfocus --sync` and its window identifier.
Capture the display with FFmpeg's `x11grab` input at 960x544.
Send `xdotool key q`, wait for exit, and inspect the image and log.
The review driver bounds discovery and process exit to ten seconds each.

## Pending checks and open questions

- T023 adds the reusable application driver and structured observations.
- T024 runs agent gameplay and visual review after the first playable build.
- T025 runs stability, audio, persistence, failure, and pacing checks.
- The current spawn rule hides the I piece until its first downward move.
  At level 1, the specified delay is 48 ticks, or 0.8 seconds.
  This is a prediction from the rule and shape data. Gameplay is not yet
  available to observe that delay. T024 must capture it before a rule change.
  (Resolved 2026-11-03 by task T029: T024 captured the delay, the design
  owner approved immediate visibility, and pieces now spawn with their
  topmost cells at visible row 0. See the T029 section below.)
- Physical button mappings, handheld performance, and human enjoyment remain
  unverified. Request the final operator pass after the local package exists.

## T008: replay hash regression runner (2026-11-02)

Sources: [game specification](../docs/SPEC.md) sections 4.3 and 7,
section 11 item 18 (desktop half). Tested source identities are saved in
`build/agent-checks/2026-11-02-t008-regression/source.sha256`.

The suite gained `tests/test_regression.cpp`: a replay-script parser, a
64-bit FNV-1a state hash that folds every field of a round, five pinned
scenarios, and the `--seed <integer>` option on the `stackfall_tests` CLI.
`tests/game_test_peer.hpp` now holds `GameTestPeer`, shared with
`tests/test_game.cpp`.

- Passed: Debug and Release builds with no compiler warnings, and
  `ctest --test-dir build` / `build-rel`. Both tests pass: the plain suite
  and `stackfall_tests --seed 987654321`.
- Passed: 335,820 checks with zero failures without options; 335,825 with
  `--seed 987654321` (the pass adds its own repeat checks).
- Passed: three separate `./build/stackfall_tests --seed 987654321`
  processes printed byte-identical output, and the Debug and Release
  binaries printed the same hashes and scores. Saved as
  `build/agent-checks/2026-11-02-t008-regression/seed-run-{1,2,3}.txt`
  and `seed-run-release.txt`.
- Passed: the same suite under AddressSanitizer and UndefinedBehaviorSanitizer
  reported zero failures and the same hashes.
- Verified: the hash separates rounds by seed, by input length, and by each
  mutated state field (board cell, line count, hold slot, hold lock, active
  piece, tick counters, previews). The suite checks all of this on every run.
- Pinned rounds: `gravity-lock` (seed 20261102, one lock by lock delay),
  `soft-drop-moves` (seed 1), `hold-and-pause` (seed 424242),
  `stack-and-clear` (seed 30, two single clears through a deliberate
  placement script), and `block-out` (seed 999, deterministic top-out).

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321   # prints pinned and custom-seed hashes
```

Findings and open questions:

- Finding (fixed during this task): the first parser draft read a repeat
  count from a default of one instead of zero, so `3*l` expanded to 13
  frames. The parser checks caught it before any value was pinned.
- The ARM64 half of section 11 item 18 stays open until T016 builds the
  rules suite for AArch64 and compares the printed hashes. The hash mixes
  bytes, not words, so endianness cannot differ between the two builds.
- Replay scripts express one InputFrame per tick. A longer DAS hold than
  ten ticks auto-shifts, so the pinned placements use single-tick presses.

## T009: SDL input adapter (2026-11-02)

Sources: [game specification](../docs/SPEC.md) sections 5 and 11 item 19,
[agent testing contract](../docs/AGENT_TESTING.md). Tested source identities
are saved in `build/agent-checks/2026-11-02-t009-input/source.sha256`.

The SDL-free `InputAdapter` in `include/stackfall/input.hpp` merges button
transitions from the keyboard and every controller into one tick input and
consumes each press edge exactly once. `src/input_sdl.cpp` holds the two
section 5 mapping tables (the only place that names a scancode or a controller
button), loads `SDL_GAMECONTROLLERCONFIG` line by line, and attaches a
developer virtual controller. The main loop polls events first and then gives
every simulation tick one `TickInput`. New developer options: `--seed`,
`--input-log`, `--input-script`, `--virtual-controller`.

- Passed: Debug and Release builds with no warnings; `ctest` in both
  configurations runs three suites: `stackfall_tests` (335,875 checks, now
  including the headless input adapter suite), `stackfall_tests --seed
  987654321`, and the new `stackfall_input_tests` (380 checks, SDL event
  path, no display needed).
- Passed: the same three suites under AddressSanitizer and
  UndefinedBehaviorSanitizer, plus a scripted application run with leak
  detection: exit 0, no findings.
- Passed: keyboard and controller tables of section 5 verified event by
  event, including the double roles (A select, B back, Up rotate plus menu
  cursor, Enter and Esc pause) and auto-repeat suppression.
- Passed: `SDL_GAMECONTROLLERCONFIG` loading proved end to end: an unmapped
  virtual joystick becomes a controller only after the environment line
  loads; malformed and empty values load nothing without failing.
- Passed: section 11 item 19 at three levels. Headless adapter batch, SDL
  catch-up batches driving the real Game (one rotation per press, DAS at
  tick 11 over a 12-tick batch), and a stalled application frame recorded in
  a session log (`tick=0 slot=0/8 cw=1`, the other seven slots idle).
- Passed: recorded desktop sessions, saved in
  `build/agent-checks/2026-11-02-t009-input/`. Session A drives the real
  application through the script and the virtual controller (keyboard taps,
  a 140 ms stall frame, pause with a consumed press, controller removal
  while a direction is held, restart with no inherited edge). Session B runs
  under Xvfb with openbox and injects operating-system keyboard events with
  xdotool, including a rotate delivered while the process is stopped with
  SIGSTOP and a minimize that orphans a held Left key. The independent
  checker `assert-sessions.py` validates both logs: `ALL PASS`
  (75 and 334 ticks).
- Passed: normal startup is unchanged: `--help`, and a bounded run without
  developer options, exit cleanly.

Commands:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure
timeout 60 env SDL_VIDEODRIVER=dummy ./build/stackfall --seed 4242 \
  --input-log build/agent-checks/2026-11-02-t009-input/session-a-input.log \
  --input-script build/agent-checks/2026-11-02-t009-input/session-a.script.txt
timeout 90 xvfb-run -a -s "-screen 0 960x544x24" \
  env STACKFALL_BIN="$PWD/build/stackfall" \
  OUT="$PWD/build/agent-checks/2026-11-02-t009-input" \
  sh build/agent-checks/2026-11-02-t009-input/session-b-inner.sh
python3 build/agent-checks/2026-11-02-t009-input/assert-sessions.py \
  build/agent-checks/2026-11-02-t009-input \
  build/agent-checks/2026-11-02-t009-input
```

Findings and open questions:

- Finding (fixed during this task): SDL 2.30.0 hands every
  `SDL_JOYSTICK_TYPE_GAMECONTROLLER` virtual joystick an auto-generated
  default mapping, which would hide whether a mapping test proves anything.
  The developer pad now attaches as `SDL_JOYSTICK_TYPE_UNKNOWN`, so only an
  explicit line makes it a controller.
- Finding (fixed during this task): `SDL_JoystickSetVirtualButton` sets
  state; a press and release inside one script pump produced no event at
  all. The script driver applies `SDL_JoystickUpdate` after every pad
  command, so a scripted tap behaves like a quick real press. The first
  session A run caught this (pad edges missing) and the rerun passed.
- Session logs vary in batch sizes between runs because wall-clock timing
  drives the loop; edge once-per-batch is asserted structurally instead.
- The scripted driver is the bounded precursor of the T023 harness
  (`--input-script`, `--input-log`, `--virtual-controller`); T023 generalizes
  it into screenshots and structured observations.
- Physical RG503 controller letters remain unverified until final device
  acceptance; changing them touches only `kControllerBindings`.

## T010: procedural renderer and glyph font (2026-11-02)

Task T010, specification sections 6.1 and 6.2. Source identity (sha256 at
verification time):

- `src/renderer_sdl.cpp` f660cddccf4a14f6595c1da82c0ec8b491bc94acb16dd49099c95a259e1317e3
- `include/stackfall/renderer_sdl.hpp` 4709a6fc8600cfb4a764939567f20aae6dcbee5d764149ea9f606639c28742e9
- `include/stackfall/font.hpp` 8052805930fea35ec6d18dae9755199690c5bf4b55de25890835d008cc693c99
- `tests/test_renderer_sdl.cpp` a5d091528b0df48cac190b37605fca1afd905c517b43d08a82dd2a70de55eb01
- `tests/test_font.cpp` 805a703427d4b9b39445f7911724ec46cc084e9568ff9859cdbe3b28f0ae68f4
- `src/main.cpp` ad1f6767643017362c42ba7924e0c3d0218ca137f1f05d482eef7389fdd73363
  (after the repair below; the pre-repair file hashed
  587333112683f2d7a467cb5071049d3d2877798b227f31aa0639f1f43834749d)

Verified results:

- `ctest` passes 4/4 in Debug (337,840 core checks, 380 input checks, 778
  renderer checks). A separate build with `-fsanitize=address,undefined`
  passes 4/4 the same way.
- The renderer suite draws into `SDL_CreateSoftwareRenderer` on an
  off-screen surface, so it asserts exact pixels: settled cells against an
  independently ticked reference game, ghost color and hollow outline,
  hidden spawn rows invisible, five next previews at exact slot centers,
  glyph shapes at scale 3, a byte-snapshot of the game equal across two
  draws (read-only draw), and letterbox behavior at 1.5x and at a pure
  offset.
- The font suite pins the 95-glyph table: coverage, five-column bounds,
  pairwise distinct alphanumerics, anchor shapes, and text metrics.
- Real-application run under `xvfb-run` (openbox, ffmpeg x11grab at 4 fps):
  29 frames captured, exit 0, log ends "344 frames rendered, 330 ticks
  simulated ... Clean shutdown". Pixel-classified inspection of the frames
  (script `build/agent-checks/2026-11-02-t010-render/inspect_frame.py`)
  confirms the acceptance: board well exactly at canvas (360, 32) 240x480,
  settled cells in the exact palette (fill, light 2/5 toward white, dark
  1/2 toward black, edge), a 1 px hollow ghost outline in the exact ghost
  color at the landing row in 21 frames, the active piece entering at
  visible row 0 (hidden spawn rows never drawn), the hold piece in its box,
  five next previews in their slots, hint text at x = 176, and score values
  centered at y = 384/434/484.
- No-asset acceptance: a grep over `src/`, `include/`, and `tests/` finds no
  `IMG_`, `TTF_`, `Mix_`, `SDL_LoadBMP`, `SDL_CreateTexture`, or
  `SDL_RenderCopy`, and the repository carries no image or font files. The
  binary run from an empty temporary directory with `SDL_VIDEODRIVER=dummy`
  renders 240 frames and exits 0 with a clean shutdown.

Commands (paths relative to the repository root):

```bash
cmake --build build && ctest --test-dir build --output-on-failure
./build/stackfall_render_tests
./build/stackfall_tests
timeout 120 xvfb-run -a -s "-screen 0 960x544x24" \
  env STACKFALL_BIN="$PWD/build/stackfall" \
  OUT="$PWD/build/agent-checks/2026-11-02-t010-render" \
  sh build/agent-checks/2026-11-02-t010-render/capture-session.sh \
  "$PWD/build/agent-checks/2026-11-02-t010-render" "$PWD/build/stackfall"
python3 build/agent-checks/2026-11-02-t010-render/inspect_frame.py \
  build/agent-checks/2026-11-02-t010-render/frame-013.png full
```

Findings and open questions:

- Finding (fixed during this task): the board frame fill ran after the grid
  lines and erased them. The well fill moved into the background step, and
  the frame draws only its outline. The pixel suite caught this.
- Finding (verification setup): the game window opens decorated
  (`SDL_WINDOW_RESIZABLE`, no fullscreen flag), so an xvfb capture shows a
  20 px openbox title bar above the canvas and the capture crops the bottom
  20 px. The specification sets no fullscreen requirement, and PortMaster
  packaging (M4) supplies the device launch context, so the window flags
  stay as they are. Frame analysis subtracts the 20 px offset.
- The vision-model pass over the contact sheet proved unreliable (it
  reported gradients the flat renderer never draws). The accepted evidence
  is the exact-color classification script above, which reads the frames
  programmatically.
- The keyboard column of the hints panel arrives with the controls screen
  of T011; the panel stays gamepad-first until then.

### Repair (2026-11-02, same session)

Combined verification failed after the first T010 attempt: builds and all
four ctest suites passed, but the scripted smoke run
(`--seed 20261102 --input-script` pressing Enter, 600 frames) produced no
`-> playing` marker, so the grep step failed. The binary itself exited 0
with a clean shutdown.

- Root cause: the main loop initialized its scene-change tracker from the
  already-started game, so the round's Boot -> Playing entry was never
  logged. Every later transition (pause, resume, game over) logged fine;
  only the round entry was silent. A scripted session could not prove the
  round reached playing.
- Fix: `src/main.cpp` `runMainLoop` now tracks the scene from
  `Scene::Boot`, so the first observed scene logs as
  `Scene boot -> playing at tick 0` through the same path as every other
  transition. No renderer, core, or input change.

Re-verification after the repair:

- The exact failed command chain passes end to end: Debug and Release
  builds, `ctest` 4/4 in both, the smoke run exits 0, the log holds
  `Scene boot -> playing at tick 0` and `Scene playing -> paused at tick 1`
  (Enter pauses, as mapped), and no tracked file matches an asset
  extension.
- No-asset acceptance repeated: the binary runs from an empty temporary
  directory under `SDL_VIDEODRIVER=dummy`, renders 240 frames, and exits 0.
- Fresh real-application capture under Xvfb without a window manager (so
  the 960x544 window maps undecorated at (0,0), removing the openbox
  title-bar offset of the first attempt) with
  `ffmpeg -f x11grab -draw_mouse 0`. Scripted session: one hold swap, nine
  hard drops with moves and rotations, then a long wait and quit; game over
  at tick 220 left a tall settled stack for the frozen-board frames.
  `classify.py` reads the raw frames and asserts exact palette and
  geometry constants: background, board well, grid line, frame outline,
  hold piece, five next previews, HOLD/NEXT headings, score text, and hint
  text in 88/88 game frames; settled cells in 88/88; the ghost outline in
  21/88 (live-play frames; the game-over frames show no active piece);
  every sampled pixel matches the source palette alone. Output saved in
  `build/agent-checks/2026-11-02-t010-repair/classify-output.txt`; the
  141 MB `frames.raw` was deleted after classification and regenerates
  with `capture.sh`.
- Capture notes for the next pass: frame 0 is the bare X root, the last
  frame can catch window destruction, and the default X cursor overlays
  the canvas center unless `-draw_mouse 0` is set; the classifier skips
  the boundary frames.

## T011: title, controls, pause, and game-over screens (2026-11-03)

Task T011, specification section 4.7. Source identity (sha256 at
verification time):

- `src/game.cpp` e0397a6d07876c3583f212fd305af54e6896b098f02b6d0ded1db97176192839
- `include/stackfall/game.hpp` 36efdec7d18f292b13c11961608970cf7cd5b84f45709643006eba0ec2a552b7
- `src/renderer_sdl.cpp` e5d281a56b769aa889d5467489165341e870cb63a2f20cda6779053a831b31c2
- `include/stackfall/renderer_sdl.hpp` 5494f8d7ba37bb64cf86483b4693fbe9210712d01027ec5b49d1830508881a11
- `src/main.cpp` aa9e597787a8cb951ff0f12897015f22f838c3c276234ea246f8a082793ea0d9
- `include/stackfall/input.hpp` bd7ebe96bf95d752fe3014b7adc88e6d4514c53ce2800fe09efbeb9e129559e7
- `src/input_sdl.cpp` 2b785c7d77981dba68063f9292c94656af2bd615aa977fa81e61d296d18b2896
- `tests/test_screens.cpp` 4378efde33561c9af4691193c466428243d4571e4280fd4544d12a4795fd08a6
- `tests/test_renderer_sdl.cpp` dae19022a183a3fc64c20c5b78e0233848ed0a91604ac662b448179f3fbd2bd5

Verified results:

- `ctest` passes 4/4 in Debug and in Release: 337,977 core checks (now
  including the headless screens suite), 382 input checks, 860 renderer
  checks. A separate build with `-fsanitize=address,undefined` passes 4/4
  the same way.
- The screens suite drives the section 4.7 machine headlessly through the
  real `TickInput` path: title navigation with wrap, the controls round
  trip, quit through the title menu, every pause path with the counters
  frozen bit-exactly, restart and quit-to-title seed behavior, held
  controls carried across a restart, the game-over menu with the record
  rule (`highScore == max` of the rounds, `newRecord` strict), an injected
  loaded high score, and session determinism (two full sessions from one
  seed compare equal through two game overs).
- The renderer suite asserts exact pixels for every new screen: the title
  logo, seven piece previews, menu marker square, and high score row; the
  three controls columns and footer; the pause overlay as a blended veil
  over a byte-identical frozen board; the game-over overlay with its three
  stat values and the accent record note present only on a record. The
  play scene stayed byte-identical to the T010 output.
- Two full desktop sessions under `xvfb-run` walk the real SDL event path
  through all 15 scene transitions of section 4.7 (title, controls, two
  resumes, restart, quit to title, play again, game over twice): session A
  on the keyboard, session B on the developer virtual controller, both
  from seed 20261103 with exit 0 and a clean shutdown.
  `assert-sessions.py` checks the recorded logs structurally: the scene
  sequence, the seed stream (every round seed differs, restart seeds
  differ from the first round), the game-over summaries (record yes on the
  first round, no on the second by construction), edge once-per-batch, and
  the menu-driven quit. Both logs: ALL PASS.
- Screen captures from the live application (lossless PNG sequence,
  `ffmpeg -f x11grab -c:v png`, because YUV 4:2:0 MP4 encoding shifts the
  exact palette colors) classified pixel by pixel against the source
  palette: title, controls, playing, paused, game-over, and play-again
  screens, ALL PASS (`classify-screens.py`).
- Both sessions repeated under AddressSanitizer and UndefinedBehaviorSanitizer
  with leak detection: exit 0, no findings, and the structural assertion
  passes on the sanitizer logs too.
- No-asset acceptance re-checked: no `IMG_`, `TTF_`, `Mix_`,
  `SDL_LoadBMP`, `SDL_CreateTexture`, or `SDL_RenderCopy` in `src/`,
  `include/`, or `tests/`, and no image, font, or audio file is tracked.

Findings and repairs made during verification:

- Two real input defects surfaced only through the full session scripts.
  First, the controller table mapped D-pad down to the soft-drop level but
  not to `MenuDown`, so the pad could not move the menu cursor down while
  the keyboard could (asymmetry in `kControllerBindings`; fixed by adding
  the secondary menu action). Second, window focus loss called a new
  `dropAllSources()` that erased every source, which also erased pending
  press edges; the SDL dummy video driver emits repeated FOCUS_LOST events
  while a script pumps, so menu presses disappeared. Spec section 11 item
  19 says a received press must fire, so the focus-loss path now calls
  `releaseAllHeld()`: held levels clear (releases may never arrive), the
  sources stay, and queued press edges still fire. The adapter suite and
  the SDL event suite pin both behaviors.
- Held-level one-pump taps (press and release between ticks) produce no
  event pair in a scripted session: the release clears the level before
  any tick samples it. This is the specified semantics, so the session
  scripts hold direction keys across pumps instead of tapping them.
- MP4 capture of the screen sequence destroyed the exact colors (YUV
  4:2:0 chroma subsampling moved (17,18,27) to (16,18,26)), so pixel
  classification needs the lossless PNG sequence.

Commands (paths relative to the repository root; evidence in
`build/agent-checks/2026-11-03-t011-screens/`):

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
timeout 180 xvfb-run -a -s "-screen 0 960x544x24" \
  env SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
  ./build/stackfall --seed 20261103 \
  --input-script build/agent-checks/2026-11-03-t011-screens/session-a-keyboard.script.txt \
  --input-log build/agent-checks/2026-11-03-t011-screens/session-a-input.log
python3 build/agent-checks/2026-11-03-t011-screens/assert-sessions.py \
  build/agent-checks/2026-11-03-t011-screens
# session B adds --virtual-controller and session-b-pad.script.txt; the
# sanitizer runs repeat both with the build-san binaries.
```

Open items:

- The M2 acceptance gate (a human-played full round on keyboard and
  controller) stays with T024, which owns unattended verification; this
  task proves the same flow through scripted sessions and exact-pixel
  captures.
- Physical RG503 controller letters remain unverified until device
  acceptance; changing them touches only `kControllerBindings`.

## T012: atomic high-score persistence (2026-11-03)

Task T012, specification sections 9 and 10.3, section 11 item 20. Source
identity (sha256 at verification time) is saved in
`build/agent-checks/2026-11-03-t012-storage/source.sha256`:

- `include/stackfall/storage.hpp`
  3753a72928a79b6c7c7c5fdff64de361985159e6d6a59d4d0c71327edd3c2433
- `src/storage.cpp`
  ba584ea0e0fd3a89c74ecab6ff228990c55dae6dc23cada055c9a407b5baa2b1
- `tests/test_storage.cpp`
  b65d9305d4a1d5b7648cc1056791f86a4bd51f8ff42d1c53d2a697babc4d38d0
- `src/main.cpp`
  8693374eebcbd858c96fee3ce9eec90b25d72a6d16f8f5c7688ca3674275a54f

`HighScoreStore` resolves the save directory from `STACKFALL_SAVE_DIR`,
then an absolute `XDG_DATA_HOME`, then `$HOME/.local/share/stackfall`, and
turns persistence off when nothing names a directory. The record is
`highscore.dat`: `version=1\nhigh_score=<decimal>\n`. A save writes a
hidden temporary file in the same directory, flushes it, fsyncs it on POSIX,
closes it, and renames it into place. A load reports Ok, NotFound, Invalid,
NoSaveDirectory, or WriteFailed; every status except Ok carries high score 0.

Verified results:

- Passed: `ctest` 4/4 in Debug and in Release, and the same under
  AddressSanitizer and UndefinedBehaviorSanitizer. The core suite grew to
  338,201 checks with the new `tests/test_storage.cpp` (224 checks); the
  input suite stays at 382 and the renderer suite at 860.
- Passed: the headless persistence suite pins the format (canonical render,
  parse of every valid form, rejection of a 29-entry damaged corpus, among
  its cases a lost final newline, CRLF, a future version, a padded score, a
  second record, a NUL byte, and a value one past the 64-bit limit, plus a
  record above the size cap), the directory resolution of every environment
  combination, the round trip through a second store and through
  `fromEnvironment()`, and the boundary values 0 and 18446744073709551615.
- Passed: corrupt-file recovery on disk. Ten damaged records (empty,
  partial, foreign version, text garbage, a doubled record, CRLF, a NUL
  byte, binary filler, and a 2 KiB file) plus an oversized record and a
  directory at the record's path all load as high score 0 with status
  invalid, and the next save repairs the file.
- Passed: write-failure behavior. A regular file in the place of the parent
  directory fails the save without a crash and removes nothing; a directory
  at the record's path fails the rename, keeps the previous entry, and
  leaves no temporary file; a default-constructed store disables
  persistence. A successful save leaves exactly one entry in the save
  directory. A read-only record is still replaced, because the rename
  replaces the directory entry; that case is skipped when the suite runs
  with privileges that ignore permission bits.
- Passed: application round trips across real process restarts
  (`build/agent-checks/2026-11-03-t012-storage/`, seed 20261103, a scripted
  title-to-game-over session of 60 hard drops, score 218 deterministically):
  run 1 saves the first record and leaves exactly `version=1` and
  `high_score=218`; run 2 reloads 218, ends with `new record no`, and leaves
  the file byte-identical; five more restarts stay stable 5/5. A stored
  record of 50 is replaced by the better 218 round. A partial file
  (`high_score=21` without a final newline) loads as 0 and is repaired at
  game over. An unwritable location logs `Saving the high score 218 failed:
  write failed` and exits 0. A session without `STACKFALL_SAVE_DIR`,
  `XDG_DATA_HOME`, or `HOME` logs the missing save directory and exits 0.
  `XDG_DATA_HOME` alone lands the record at `<dir>/highscore.dat`. Every
  run exits 0 with a clean shutdown.
- Passed: the same two-process round trip under AddressSanitizer with leak
  detection and UndefinedBehaviorSanitizer: exit 0, no findings.

Commands (paths relative to the repository root):

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
timeout 120 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  STACKFALL_SAVE_DIR="$PWD/build/agent-checks/2026-11-03-t012-storage/conf-run1" \
  ./build/stackfall --seed 20261103 \
  --input-script build/agent-checks/2026-11-03-t012-storage/session.script.txt \
  --input-log build/agent-checks/2026-11-03-t012-storage/run1-input.log
```

Findings and open questions:

- The load log of an unwritable location reports `not found`, because the
  directory cannot exist yet; the save then reports `write failed`. Both
  statuses are logged, so the failure stays visible.
- Persistence survives process restarts and power-loss-safe renames on the
  build host. The RG503 storage behavior (the port's `conf/` directory on
  device, and a real power cut) is checked at device acceptance, because
  the launcher that exports the save directory arrives with T017.
- T025 repeats these checks across 50 restart and menu cycles and adds an
  unwritable save directory to the stability pass.

## T013: synthesized sound effects with mute toggle (2026-11-03)

Task T013, specification section 6.3. Source identity (sha256 at verification
time) is saved in `build/agent-checks/2026-11-03-t013-audio/source.sha256`:
`include/stackfall/audio_sdl.hpp`, `src/audio_sdl.cpp`,
`tests/test_audio_sdl.cpp` (new), plus the settings half of
`include/stackfall/storage.hpp`, `src/storage.cpp`, `tests/test_storage.cpp`,
`src/main.cpp`, and `CMakeLists.txt`.

What landed: one `stackfall_audio` library with three parts. `synthesizeCue`
renders the five cues of section 6.3 (move/land, rotate, clear, four-line
clear, game over) from a source-defined segment table through integer phase
accumulators and integer envelopes; no `.wav` or `.ogg` asset exists and
nothing reads audio from disk. `CueDetector` observes the round after every
simulation tick through const queries only and raises one cue per event. The
`AudioDriver` opens the SDL audio device, synthesizes the cues at the rate
and channel count the device reports, and mixes them inside the SDL callback
with saturation. The M key and controller Select switch the mute, and
`settings.dat` (`version=1` / `sound=on|off`) beside `highscore.dat` carries
the choice across launches; a damaged file falls back to sound on.

Verified results:

- Passed: `ctest` 5/5 in Debug, in Release, and under AddressSanitizer with
  leak detection and UndefinedBehaviorSanitizer. The core suite grew to
  338,294 checks (the settings file added 93), the input suite stays at 382,
  the renderer suite at 860, and the new audio suite reports 4,005 checks.
  The pinned regression hashes still match, so the rules core and its
  gameplay randomizer are untouched.
- Passed: synthesis. Every cue at 22050, 32000, 44100, and 48000 Hz renders
  the exact documented length, starts and ends on exact silence (a linear
  attack and a closing fade, so a cue cannot click), opens at full level
  within 5 ms, keeps at least a fifth of its samples nonzero, peaks above
  5000, renders byte-identically on a second call, and differs pairwise from
  every other cue. Measured peaks at 44100 Hz: move/land 5662, rotate 6806,
  clear 9112, four 9986, game over 7865; durations 55, 45, 200, 380, and
  800 ms.
- Passed: cue detection through the real rules core. A lateral step, a
  rotation (including one that succeeds only through the `(2, -1)` I wall
  kick, which raises the rotate cue and never a move on top of it), a
  landing lock with zero to four cleared lines (MoveLand, Clear ×3,
  FourLineClear), and game over by lock-out and by top-out each raise
  exactly one cue on exactly their tick. Gravity, soft drop, hold swaps,
  menu ticks, paused ticks, and rejected inputs at a wall raise nothing. A
  scripted restart resets the detector, and the suite proves the false
  positive the reset prevents.
- Passed: the audio device. With a bogus `SDL_AUDIODRIVER` the subsystem
  fails, `open()` returns false, cue requests still count as dropped, and
  the game state is untouched. With the SDL dummy driver the device opens at
  44100 Hz and two channels, all five cues play, the mute stops new cues
  while the device stays alive, the counters keep every event, and a
  close/reopen cycle loses no cue. The mixer saturates inside int16 with
  every voice sounding at once and returns to exact silence afterwards.
- Passed: WAV evidence. `./build/stackfall_audio_tests --dump-dir DIR`
  writes the five cues as mono 16-bit PCM WAV files for the final human
  listening pass; the header and every sample byte are checked, and the
  files live only under the ignored build directory. Current copies are in
  `build/agent-checks/2026-11-03-t013-audio/wav/`.
- Passed: desktop sessions. Seed 20261103, a planned 19-piece placement
  searched offline against the rules core (rotations, single-column taps,
  hard drops) produces a four-line clear, and the same script drives the
  real binary end to end. One session (`session-allfive`) plays every cue:
  `requested move/land=70 rotate=24 clear=2 four=1 gameover=1, played ...`
  with the same numbers, 0 muted, 0 without a device, exit 0, clean
  shutdown, and per-cue `sfx` lines in the input log that line up with the
  tick lines (the `four` cue lands on the tick whose input line carries
  `drop=1`).
- Passed: mute and persistence. A session that presses M twice suppresses
  exactly the four cues raised while muted, writes `sound=off` then
  `sound=on` to `settings.dat`, and leaves the file at the last state. A
  second run over `sound=off` starts muted, logs `Sound setting off (ok)`,
  and unmutes on M. A corrupted file (`sound=maybe`) loads as invalid with
  sound on. Without `STACKFALL_SAVE_DIR`, `XDG_DATA_HOME`, or `HOME` the
  session logs that the setting stays in memory and still plays cues.
- Passed: missing audio. `SDL_AUDIODRIVER=stackfall-missing-driver` (the
  subsystem fails) and this host's default ALSA (the subsystem starts but
  the device cannot open) both run the same session to the same score 1798,
  lines 6, and the same 98 requested cues, reported as `98 without a
  device`, exit 0, clean shutdown. Gameplay is byte-identical with and
  without sound.
- Unavailable: real audio output. This host has no sound card (ALSA has no
  device, and the PipeWire and PulseAudio libraries are absent), so every
  playing run went through the SDL dummy audio driver: a real SDL audio
  device with a real callback thread, but nothing behind it to hear. Human
  listening, cue feel, and device latency stay with final acceptance, as
  the task policy states.

Commands (paths relative to the repository root; evidence in
`build/agent-checks/2026-11-03-t013-audio/`):

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
./build/stackfall_audio_tests \
  --dump-dir build/agent-checks/2026-11-03-t013-audio/wav
timeout 240 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  STACKFALL_SAVE_DIR="$PWD/build/agent-checks/2026-11-03-t013-audio/conf-c" \
  ./build/stackfall --seed 20261103 \
  --input-script \
  build/agent-checks/2026-11-03-t013-audio/session-allfive.script.txt \
  --input-log build/agent-checks/2026-11-03-t013-audio/session-allfive-input.log
# session-mute.script.txt covers the M toggle; session-noaudio-* replace
# SDL_AUDIODRIVER with a bogus name or drop it to lose the device. The
# four-line plan itself was searched offline by a throwaway program that
# drives the same rules core.
```

Findings and open questions:

- The four-line clear cue needed a planned placement: no blind script
  reaches one. The plan (19 pieces) is saved with the session evidence, and
  the same technique can seed future clear scenarios for T023.
- The mute toggle is a keyboard key and a controller button only. No screen
  shows the sound state, so a player learns it from the sound stopping. A
  controls-screen line or a HUD note is a UI decision for the design review
  round, not a defect of this task.
- T025 repeats the audio failure paths across its restart and stability
  cycles, and checks that repeated open/close cycles leak no audio
  resources.

## T013 repair: platform-layer failures of the combined check (2026-11-03)

The combined verification of the retained T013 integration stopped after
CTest: every suite passed, but three platform-layer checks of the combined
script failed, all in `src/main.cpp`, none in the audio code.

What failed and the repairs:

- Renderer failure with `SDL_RENDER_DRIVER=nosuch`. SDL 2.30 quietly ignores
  a render driver hint it does not know and hands out whatever backend still
  matches the flags, so the game fell back to software, exited 0, and printed
  nothing actionable. `createRenderer` now compares the hint against
  `SDL_GetNumRenderDrivers` first: a forced driver this SDL does not carry is
  a failed renderer setup, logged with the driver names that exist, and the
  program prints one actionable error naming `SDL_RENDER_DRIVER` and
  `SDL_VIDEODRIVER` and exits 1. A valid forced name (verified with
  `SDL_RENDER_DRIVER=software`) still opens that driver.
- Startup log carried no architecture. `main` now prints
  `Architecture: x86_64` (compiler macros: `aarch64` on the handheld target)
  beside the version lines.
- No fallback frame limiter. With the dummy video driver the loop rendered
  360 frames in under one second. `runMainLoop` now holds every frame to its
  1/60 s slot when the frame's work finishes early, and leaves a present
  that already blocked (vsync) alone, so the two pacing mechanisms never
  stack; a stall restarts the cadence instead of bursting. The limiter logs
  its first engagement.

Re-run evidence (this repair worktree, Debug and Release, all suites green,
combined script passes end to end; evidence refreshed under
`build/agent-checks/2026-11-03-t013-audio/`, scripts carried over from the
retained integration):

- `stackfall_audio_tests --dump-dir .../wav`: 4,005 checks, 0 failures, five
  WAV cue files rewritten.
- `session-allfive` (dummy audio, seed 20261103): exit 0, clean shutdown,
  `requested move/land=70 rotate=24 clear=2 four=1 gameover=1`, identical
  `played` counters, 0 muted, 0 without a device - the same numbers as the
  first run. With the limiter the session now renders 864 frames for 863
  ticks instead of unbounded spin.
- `session-mute`: 4 cues muted, `settings.dat` left at `sound=on`, clean
  shutdown.
- `session-noaudio` (`SDL_AUDIODRIVER=stackfall-missing-driver`): the same
  round ends at score 1798, lines 6, all 98 requested cues reported
  `without a device`, exit 0, clean shutdown.
- Pacing check of the combined script: 360 frames now take about 6 s with
  359 ticks simulated, and the log carries the fallback limiter line.

## T023: unattended SDL test harness (2026-11-03)

Task T023, `docs/AGENT_TESTING.md`. One command runs the scene cycle
unattended:

```bash
tests/harness/run.sh
```

Source identity of the recorded run is saved in
`build/agent-checks/harness-2026-11-03-080826/source.sha256` (23 lines: the
game binary `8e5d6ba0...`, the reference binary `f9f03033...`, and every
harness source, fixture, and script). Every session runs from seed
`20261103`. The run needs no human and no physical device, and takes
about 5 s under the dummy video driver.

What landed:

- Developer options on the game binary: `--save-dir`, `--observations`,
  `--screenshot-dir`, `--fixture-dir`, `--tick-per-frame`, beside the
  existing `--seed`, `--frames`, `--input-script`, and `--input-log`.
  A plain run without any of them is unchanged.
- `--tick-per-frame` runs exactly one simulation tick per frame with
  pacing off, so a checkpoint's tick number never depends on how the
  wall clock batched frames. This is what makes two runs comparable.
- The script language of `tests/harness/scripts/` (key, pad, wait,
  stall, detach, restart, fixture, check, shot, quit, echo). A `check`
  flushes after its frame's tick and render, so the record and the
  image describe the state that was drawn.
- One observation record per checkpoint: scene, tick, score, lines,
  level, high score, record flag, active piece, ghost, hold, five-piece
  queue, menu cursor, quit flag, settled cell count, the 22 row masks,
  and 220 color digits. `stackfall_core` owns the formatter
  (`src/observe.cpp`), so the game binary and the headless reference
  print the same bytes.
- `tests/harness/reference_replay.cpp` (`stackfall_reference`): the
  rules core alone replays a script through its own copy of the
  specification section 5 tables and writes the same records. No SDL,
  no display.
- Three labeled fixtures in `tests/fixtures/` with `name:`,
  `description:`, and `provenance:` fields: `four-line-ready`,
  `lockout-ready`, and `blockout-ready`. The strict parser
  (`src/fixture.cpp`, pinned by `tests/test_fixture.cpp`) lives in the
  core, and the fixture path runs only behind the `fixture` script
  command.
- `tests/harness/run.sh` builds, records the source identity, runs the
  headless `ctest` suites first, then drives six sessions: the full
  scene cycle on the keyboard, the same cycle on the virtual controller
  with a mid-game detach, a planned normal game from an empty board
  (rotations, taps, hold, soft drop, delayed auto-shift, one line
  clear, pause), and the three fixtures. Every game process runs under
  `timeout` with a frame bound, and a trap kills any child that
  outlives the script.
- `tests/harness/check_harness.py` decides pass or fail. It compares
  every game record against the reference record field by field, then
  asserts expectations pinned from `docs/SPEC.md` alone: the section
  4.6 clear table, hard-drop 2 per cell, soft-drop 1 per cell, the
  pause freeze, the hold restriction, scene sequences, scores, the
  saved record file, and 960x544 BMP screenshots. 157 checks, 0
  failures.

Verified results:

- Passed: `tests/harness/run.sh` with defaults, `HARNESS_RUN_ID=
  dev-release` (Release build), `HARNESS_RUN_ID=dev-san` (AddressSanitizer
  with leak detection and UndefinedBehaviorSanitizer), and
  `HARNESS_VIDEO=x11 HARNESS_RUN_ID=dev-x11` under
  `xvfb-run -a -s "-screen 0 960x544x24"`. All four: 157 checks,
  0 failures.
- Passed: `ctest` 5/5 in Debug, Release, and the sanitizer build.
  The core suite grew to 338,398 checks (the fixture parser and the
  record format added cases); the pinned regression hashes still match,
  so the rules are untouched.
- Passed: determinism. The planned normal game repeated within one run
  reproduces its records byte for byte. Probe runs at catch-up waits
  of 6, 12, 14, and 20 ticks produced identical records too, because
  the dummy driver's window-focus events all arrive during startup,
  before the first input matters.
- Passed: the failure paths. A broken left-key mapping was caught by
  the input suite (harness exit 1). A build that polled events after
  the simulation tick was caught by the stream comparison, which named
  the diverged field and the tick offset. `HARNESS_TIMEOUT_S=0.05`
  produced timeout exit 124, the harness reported it and exited 1, and
  no game process survived. Evidence of those runs was kept in
  `build/agent-checks/negtest-timeout/` and throwaway worktrees.
- Passed: screenshot content, by exact-pixel classification. The
  four-line fixture shot shows the four loaded rows in the S, O, L,
  and J colors with column 5 empty in every row; the after-clear shot
  shows an empty board plus one piece color; the title shot shows the
  title background and logo colors. The BMP checker covers format and
  size for all 102 shots of the recorded run.
- Passed: normal startup. A bounded plain run exits 0 with a clean
  shutdown, logs no harness mode, and leaves no files behind.

Commands:

```bash
tests/harness/run.sh                 # everything below in one command
HARNESS_BUILD_DIR="$PWD/build-rel" HARNESS_RUN_ID=dev-release \
  tests/harness/run.sh
cmake -S . -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
HARNESS_BUILD_DIR="$PWD/build-san" HARNESS_RUN_ID=dev-san \
  tests/harness/run.sh
HARNESS_VIDEO=x11 HARNESS_RUN_ID=dev-x11 timeout 500 \
  xvfb-run -a -s "-screen 0 960x544x24" tests/harness/run.sh
```

Findings and open questions:

- A `check` in the same pump as the next input records that input's
  tick effect. Every script puts a `wait` between a check and further
  input; the rule is documented with the script language.
- The reference stops on the title-menu quit, mirroring the game loop.
  Without that, it ran to its million-tick guard.
- `run.sh` is POSIX `sh`; process substitution is a bashism and failed
  under dash, so the repeat comparison strips the `shot=` field through
  temporary files instead.
- Real audio output stays unavailable on this host (no sound card), so
  sessions run against the SDL dummy audio driver. The audio content
  checks belong to T013 evidence.
- Visual review by opened image, the hidden I-piece spawn delay, and
  controller-letter judgment stay with T024, which now runs on this
  harness.

## T024: agent play and visual inspection (2026-11-03)

Round M2 gate. The agent plays the real SDL application and inspects
what it draws, on top of the T023 harness. One command,
`tests/harness/t024.sh`, builds, records the source identity, runs the
headless `ctest` suites, replays five sessions, and checks them. Four
sessions run under the dummy video driver with `--tick-per-frame`, so
the checker compares every game record against the headless reference
field by field. The fifth runs under a real X server at real time. It
checks focus loss and the letterboxed smaller window. Its records are
behavioral evidence only, because no reference stream exists at real
time.

Sessions and seeds:

- `t024-shapes-keyboard`, seed 31337. The first bag is S Z O L I J T,
  so one round touches every shape on the keyboard path. The script
  covers spawn frames, taps, both rotations, wall and floor kicks,
  ghost, soft drop, hard drop, hold swap and its lock, pause during
  movement, restart with a button held, mute, and the full
  title, controls, play, game over, play again, title, quit cycle.
- `t024-shapes-pad`, seed 20261104. The same coverage on the virtual
  controller, plus a detach while a pad direction is held.
- `t024-i-spawn`, seed 777. The bag gives J then I. The session pins
  the hidden I-piece spawn delay tick by tick.
- `t024-clears-23`, seed 31337. Two new labeled fixtures,
  `double-line-ready` and `triple-line-ready`, force the two-clear and
  three-clear rows of the section 4.6 table.
- `t024-focus-letterbox`, seed 31337, under `xvfb-run` with openbox
  and xdotool. The outer driver minimizes the window while Left is
  held, restores it, and resizes it to 640x480.

What landed:

- `tests/harness/t024.sh`: the driver described above. Environment
  options `T024_BUILD_DIR`, `T024_RUN_ID`, `T024_TIMEOUT_S`, and
  `T024_SKIP_X11`. The keyboard session repeats once inside the run;
  the repeat must match byte for byte after the `shot=` paths are
  stripped.
- `tests/harness/t024-x11.sh`: the X11 driver. It waits for the window
  by name, minimizes and restores it around the focus-loss checkpoint,
  resizes it, and asserts a clean exit.
- `tests/harness/check_t024.py`: 287 checks. It reuses `check_harness`
  helpers, compares both record streams, and pins the expectations
  below from `docs/SPEC.md` and the records alone.
- Five scripts under `tests/harness/scripts/t024-*.script.txt` and two
  fixtures under `tests/fixtures/`.
- `tests/harness/reference_replay.cpp`: the reference now accepts
  `key m`, the sound toggle, as a platform-only key with no rule. The
  game driver already accepted it; the reference aborted on it. The
  T023 harness still passes 157 of 157 after the change.

Verified results:

- Passed: `tests/harness/t024.sh` in Debug with the X11 session
  (`t024-final`, 287 checks), in Release (`t024-release`, 287), and
  under AddressSanitizer, UndefinedBehaviorSanitizer, and leak
  checking with the X11 session skipped (`t024-san`, 267; its 20 X11
  checks need the display server).
- Passed: `ctest` 5 of 5 in every build before the sessions ran.
- Passed: determinism. The keyboard session repeated byte-identical
  inside `t024-final`.
- Passed: movement, rotation, and kicks against the reference stream.
  One tap moves one column. Both rotation directions cycle the states.
  The grounded L rotates 0 to R with the SRS kick (-1,-1):
  `L,0,7,15` becomes `L,1,6,14`. The wall-pinned I rotates R to 2 with
  the I kick (-1,0): `I,1,7` becomes `I,2,6`, flush at the wall.
- Passed: drop scoring. Soft drop scores 1 per cell (+6 for six). Hard
  drop scores 2 per cell, for example 38 at the I lock in
  `t024-i-spawn`. The double clear scores 336 at 2 lines and the
  triple 872 at 5, matching section 4.6 plus the drop points. After
  each clear only the I cells above the cleared band remain, exactly
  where the fixtures put them.
- Passed: hold. The swap frame shows the stashed piece in the slot.
  The second press before a lock changes nothing; that frame is
  byte-identical to the swap frame. After the next lock the slot reads
  free, and the return swap brings the stashed piece back out: the I
  on the keyboard path and the Z on the pad path.
- Passed: pause and counters. The tick, score, and lines freeze while
  paused. Delayed auto-shift resumes after unpause, so the piece keeps
  moving right. Restart with Left held moves on the restart's first
  frames and stops on release, with no stuck direction.
- Passed: the scene cycle on both paths, ending in `quit=1` and a
  clean shutdown. Game-over scores: 214 then 228 on the keyboard, with
  high 228 saved; 262 then 222 on the pad, with high 262 saved.
- Passed: focus loss. The log line reads "Window focus lost; every
  held button cleared". The piece column freezes without a key
  release, the input log shows no move cue between the last Left
  press tick and the first Right press tick, and a new press moves the
  piece again. Pause and resume also hold inside the 640x480 window.
- Passed: controller removal. The pad direction held across `detach`
  stops moving the piece. Nothing sticks after the detach, and the
  keyboard finishes the round.
- Passed: sound cue accounting. Each event requests one cue. The
  clears session plays clear=2 and rotate=2, one per event. The
  keyboard session requests rotate=15, one per scripted rotation
  press. Muting drops exactly the 11 cues requested while muted, and
  the summary line balances requested against played, muted, and
  without a device. The X11 session reports 5 without a device,
  because this host has no sound card.

Hidden I-piece spawn delay, recorded as a design question:

- Seed 777 locks the J at tick 5. The I spawns on that same tick at
  `I,0,3,-2`, fully above row 0. It stays invisible through tick 52
  and first shows its single row at tick 53. The delay is exactly 48
  ticks, the level 1 gravity interval, 0.800 s at 60 Hz.
- Every piece spawns at y=-1, the I at y=-2, so each spawn frame
  shows only the lower rows. The renderer skips cells above row 0
  (`src/renderer_sdl.cpp:311`). The I is the only piece that starts
  fully hidden, and it waits for its first gravity step to appear.
- Evidence: `t024-i-spawn-frame.png`, `t024-i-hidden-plus47.png`, and
  `t024-i-first-visible-plus48.png` in the review directory below.
- Open question for the design owner: keep the 48-tick hidden
  interval, or spawn pieces at row 0 so every piece is visible at
  once? A change moves lockout timing, so it is a design decision.
  Nothing was changed here.
- Disposition, 2026-11-03: task T029 implemented the second option.
  Pieces now spawn with their topmost occupied cells at visible row 0
  (I matrix origin y=-1, all others y=0), so this 48-tick hidden
  interval no longer occurs. The measurement above is the preserved
  pre-change evidence. The T029 section below records the new proof.

Visual review, by opened image:

- 35 checkpoint BMPs were converted to PNG under
  `build/agent-checks/t024-final/review/`, listed in `manifest.txt`
  with dimensions. Every file was opened and inspected through this
  environment's image reader, then paired with pixel checks where the
  reader's judgment needed support.
- Title: the STACKFALL logo, the START, CONTROLS, QUIT items, the
  selection bar, and the high-score line all read clearly. Nothing
  clips.
- Controls: the two-column mapping rows align, the footer hint reads,
  and the contrast stays high.
- Spawn and hold frames: each spawn frame shows only the lower rows
  at the top edge, as pinned above. The I spawn frame shows no active
  piece at all, only its ghost outline near the floor. The first
  visible I frame shows exactly one horizontal cyan row.
- Ghost: an outline copy at the landing row, clearly distinct from
  the solid piece, and drawn even while the active piece is hidden.
- Walls and kicks: pieces sit flush at both walls with no overlap and
  no gap. The kicked L visibly hovers one row above the stack beside
  it, and its ghost shows it settling back down.
- Hold availability: a locked hold draws at reduced brightness. The
  pixel check confirms it, purple `#b070e0` free against `#60417d`
  locked, and cyan against `#1a6f79`. An unavailable hold reads
  clearly.
- Previews: five NEXT pieces on every gameplay frame, in the queue
  order of the record, for example Z O L I J on seed 31337.
- Clear feedback: the fixture shots show the labeled rows with the
  shared hole column, and the after-clear shots show the band gone
  with only the dropped I cells left. Scores 336 and 872 read on the
  panels.
- Pause overlay: a dimmed board behind a centered PAUSED box, RESUME
  highlighted, panels still readable.
- Game over: GAME OVER with the score summary, PLAY AGAIN
  highlighted, and the tall stack dimmed behind. Strong contrast.
- Letterbox at 640x480: black bars about one eighth of the width on
  each side, the whole scene scaled in proportion, and every label,
  overlay, and menu still readable. No clipping at the smaller size.

Findings and open questions:

- No game defect surfaced. Every failure during development traced to
  the harness: the reference's missing `m` key, two fixture row-count
  mistakes, missing `wait` guards between checks and inputs, and
  checker pins written against the wrong field. All fixes stay inside
  the approved rules, and the affected sessions were rerun.
- The vision reader under-reported the hold dimming as "not dimmed".
  The pixel comparison above is the controlling evidence. Treat
  reader answers as leads, and confirm color judgments with pixels.
- Real audio output stays unavailable on this host. The X11 session
  accounts its 5 cues as without a device. Device latency and human
  judgment stay with the operator pass in T018 and T020.
- The hidden I spawn delay question above needs a design decision
  before the physical rounds. (Decided and implemented 2026-11-03 by
  task T029; every piece is now visible on its spawn frame.)

Commands:

```bash
tests/harness/t024.sh
T024_BUILD_DIR="$PWD/build-rel" T024_RUN_ID=t024-release \
  tests/harness/t024.sh
cmake -S . -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
T024_BUILD_DIR="$PWD/build-san" T024_RUN_ID=t024-san T024_SKIP_X11=1 \
  tests/harness/t024.sh
xvfb-run -a -s "-screen 0 960x544x24" tests/harness/t024.sh
tests/harness/run.sh    # T023 regression after the reference change
```

Source identity of the recorded runs: commit `bce47f6` ("Integrate
T023") plus the T024 files, hashed in
`build/agent-checks/t024-final/source.sha256` (game binary
`15eefb81...`, reference binary `0449ccdf...`), and the matching files
in `t024-release` and `t024-san`. Inspected artifacts:
`build/agent-checks/t024-final/review/*.png` and `manifest.txt`, the
per-session `shots/` and `input.log` directories, the `*.run.obs`
record streams, and `t024-focus-letterbox.driver.log`.

## T014: error handling, renderer fallback, and logging (2026-11-03)

Round M3. The retained integration already carried three T014 behaviors
from the T013 repair (`src/main.cpp`: the renderer-failure exit with the
driver list, the Architecture line, the fallback frame limiter). This
session completed the remaining scope in the integrated candidate: the
log.txt sink, the save-failure and controller-disconnect logging review,
and the vsync on/off pacing pass.

What landed:

- `include/stackfall/storage.hpp` and `src/storage.cpp` resolve the session
  log's path: `STACKFALL_LOG_FILE` names the file exactly; without it the
  file lands beside `highscore.dat` in the save directory, so the
  specification's launcher-redirected log.txt in the game directory
  (section 10.3) and the game's own sink never write one file. No location
  at all logs to stdout alone.
- `src/main.cpp` opens that file before anything that can fail and routes
  every platform-layer line through `logf`/`logErrorf` (stdout/stderr plus
  log.txt, flushed per line), so the startup block, an SDL init failure,
  every renderer attempt, the selected renderer and controller state, a
  failed save, and the clean-shutdown line all reach the disk file. The
  structured streams (input log, observation records) stay stdout-only.
  Every exit path closes the sink.
- `SDL_Init` is split: video failing is fatal (clean exit 1, actionable
  error), the gamepad subsystem failing is not - the session logs it and
  stays keyboard-playable, exactly the no-controller state of section 9.
- `tests/test_storage.cpp` pins the path resolution with seven
  environment cases; the core suite grew to 338,405 checks.

Verification, all under `build/agent-checks/2026-11-03-t014-logging/`
(script `verify.sh`, sources and binaries in `source.sha256`, 52 checks,
0 failures, Debug build unless noted):

- Passed: `ctest` 5/5 in Debug and Release; core suite 338,405 checks.
- Passed: required log entries. A normal dummy-driver run writes
  `log.txt` with the sink line, `STACKFALL 0.1.0`, `Architecture: x86_64`,
  the SDL versions, the selected renderer, `No game controller detected;
  keyboard fallback active`, the loop summary, and `Clean shutdown`
  (`normal/log.txt`).
- Passed: forced video failure. `SDL_VIDEODRIVER=stackfall-no-such-video`
  exits 1 in 7 ms with `SDL_Init(SDL_INIT_VIDEO) failed` on stderr and in
  log.txt, after the startup block (`video-fail/`).
- Passed: forced renderer failure. `SDL_RENDER_DRIVER` naming a driver
  SDL does not carry exits 1 with the attempt line, the available drivers
  (`opengl, opengles2, software` here), and one actionable error naming
  `SDL_RENDER_DRIVER` and `SDL_VIDEODRIVER` - on stderr and in log.txt
  (`render-fail/log.txt`).
- Passed: missing audio. `SDL_AUDIODRIVER=stackfall-missing-driver` plays
  the full keyboard scene cycle, reports every cue `without a device`,
  exits 0 with a clean shutdown (`audio-missing/`).
- Passed: gamepad subsystem failure. An `LD_PRELOAD` shim
  (`fail_gamecontroller.c/.so` in the run directory) fails exactly
  `SDL_Init(SDL_INIT_GAMECONTROLLER)`: the session logs the failure with
  the forced reason and `No game controller subsystem; keyboard fallback
  active`, then plays the whole keyboard scene cycle to a clean exit 0
  (`gc-fail/`). This is the strongest of the keyboard-playable checks.
- Passed: controller disconnect. The pad scene cycle detaches the virtual
  controller mid-game while a direction is held: log.txt carries the
  opened line, the removed line, `No game controller left; keyboard
  fallback active`, and a clean shutdown, exit 0 (`pad-detach/`). The
  T024 rerun below re-proves the no-stuck-input part.
- Passed: save failure. A read-only save directory loads `High score 0
  (not found)`, then both game-overs of the scene cycle log `Saving the
  high score 218 failed: write failed` (and 220), exit 0, and no record
  appears in the directory (`save-readonly/log.txt`). With every save
  hint unset, the session logs `No save directory: set
  STACKFALL_SAVE_DIR or XDG_DATA_HOME` and still finishes cleanly
  (`save-nodir/`).
- Passed: fallback pacing, elapsed/render/tick measurements:

  | Run | Renderer | Elapsed | Frames | Ticks | Limiter |
  | --- | --- | ---: | ---: | ---: | --- |
  | dummy, Debug | software, vsync flag SDL only pretends to honor | 6,035 ms | 360 | 359 | engaged |
  | dummy, Release | software, same | 6,023 ms | 360 | 359 | engaged |
  | x11 under Xvfb | software, no vsync | 6,144 ms | 360 | 359 | engaged |
  | x11 under Xvfb | opengl, accelerated, vsync | 6,143 ms | 360 | 359 | engaged |

  With vertical synchronization requested (the x11 opengl run) and without
  it (software runs, and the dummy driver, whose renderer reports the
  vsync flag while presentation returns at once), the loop holds about
  60 frames per second and the simulation stays at 60 Hz (359 due ticks
  for 360 frames in every run). The paced loop now measures 2% CPU over
  360 dummy-driver frames (`cpu-paced/`), against the 98% CPU spin of
  100,000 frames in 5.42 s recorded before the limiter existed. Host
  numbers do not establish RG503 performance; T025 owns the extended
  percentile and stability measurements.
- Passed: harness regressions. `tests/harness/run.sh` 157/157
  (`build/agent-checks/t014-regress-harness/`) and `tests/harness/t024.sh`
  287/287 under `xvfb-run` (`build/agent-checks/t014-regress-t024/`),
  so the log sink changes nothing about the recorded sessions, the
  screenshots, or the plain-startup run.

Commands:

```bash
bash build/agent-checks/2026-11-03-t014-logging/verify.sh
HARNESS_RUN_ID=t014-regress-harness tests/harness/run.sh
xvfb-run -a -s "-screen 0 960x544x24" env T024_RUN_ID=t014-regress-t024 \
  tests/harness/t024.sh
```

Findings and open questions:

- The dummy driver accepts `SDL_RENDERER_PRESENTVSYNC` on its software
  renderer and then returns from present immediately, so "renderer
  reports vsync" cannot be read as "presentation blocks". The limiter
  keys on elapsed time, not the flag, which is why both x11 rows pace
  correctly.
- No location resolves on a machine without `HOME`, `XDG_DATA_HOME`,
  `STACKFALL_SAVE_DIR`, and `STACKFALL_LOG_FILE`; that state logs to
  stdout alone and is pinned by the storage suite. The PortMaster
  launcher always exports a save directory, so the device always keeps
  log.txt.
- The extended ten-minute stability run, restart cycles, sanitizer pass,
  and frame-time percentiles with vsync on and off belong to T025 and
  were not duplicated here.

## T030: readable control hints (2026-11-03)

Round M2 fix from the T027 screenshot review, key
`looper:finding:control-hint-glyph-scale`. The hint text drew at glyph
scale 1, so seven rows of 7 px carried every control hint on the play
panel and every footer hint on the title, pause, and game-over screens.

What landed:

- `src/renderer_sdl.cpp` raises `kHintScale` from 1 to 2, matching the
  14 px panel label height. The play hints reflow: the action column
  moves from x=176 to x=192, because at scale 2 the widest control ends
  at 182 and the widest action then ends at 310, inside the left panel
  x=52..324. Row pitch, colors, the 5x7 glyph table, and the rectangle
  drawing stay as they were.
- `tests/test_renderer_sdl.cpp` gains `textRowRuns`, the row measure
  `tools/design-review/check_fixes.py` takes on a packet, restated as a
  pixel check. It pins the play hints (eight lines, each exactly
  14 px, no text past the panel edge), the title footer line, and the
  pause footer line. The render suite reports 875 checks. Reverting the
  renderer constant alone fails nine checks, so the cover is live.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in `build/`.
- Passed: `tests/harness/run.sh` 157 of 157
  (`harness-2026-11-03-111423/`).
- Passed: `tests/harness/t024.sh` 287 of 287 under `xvfb-run`
  (`t030-regress-t024/`).
- Passed: fresh review packet `design-review/t030-fix/packet` (seed
  31337, twelve images, `DR_RUN_ID=t030-fix
  tools/design-review/capture.sh`). `check_fixes.py` reports
  `control-hint-glyph-scale` PASS at 14 logical px and saves
  `fix-checks.json`. Measured from the `gameplay-early` pixels, the hint
  text spans x=64..309 and y=148..307, clear of the panel edge and the
  board.
- Inspected: `gameplay-early.png` and `title.png` of that packet. The
  seven hint rows read, the columns stay aligned, nothing clips, and the
  title footer line sits whole above the canvas bottom.
- Still failing on the same packet, both outside this task:
  `ghost-outline-contrast` 2.44 (T031) and `letterbox-scene-centered`
  with bars 0 and 118 px (T032). T028 keeps both open.

Commands:

```bash
ctest --test-dir build --output-on-failure
HARNESS_RUN_ID=t030-regress-harness tests/harness/run.sh
T024_RUN_ID=t030-regress-t024 xvfb-run -a -s "-screen 0 960x544x24" \
  tests/harness/t024.sh
DR_RUN_ID=t030-fix tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t030-fix/packet
```

## T031: readable ghost outline (2026-11-03)

Round M2 fix from the T027 screenshot review, key
`looper:finding:ghost-outline-contrast`. The ghost outline blended three
fifths toward the well, so the landing preview measured 2.44:1 against
the well while the live piece reached about 9.8:1.

What landed:

- `include/stackfall/renderer_sdl.hpp` blends one quarter of the piece
  fill toward the well in `ghostColor`. Every one of the seven outlines
  now clears the 3:1 floor (the dimmest, J, reaches 3.48:1) and each
  stays at least 1.5 times dimmer than the live fill of its own piece.
  The outline stays one pixel wide and hollow.
- `tests/test_renderer_sdl.cpp` restates the WCAG relative-luminance
  contrast of `tools/design-review/check_fixes.py` and the quarter blend
  with its own arithmetic, then holds all seven pieces to the floor, the
  dimmer-than-live rule, and the one-pixel hollow outline. The render
  suite reports 900 checks. Reverting the header constant alone fails 14
  checks, so the cover is live.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in `build/`.
- Passed: `tests/harness/run.sh` 157 of 157
  (`t031-regress-harness/`).
- Passed: `tests/harness/t024.sh` 287 of 287 under `xvfb-run`
  (`t031-regress-t024/`).
- Passed: fresh review packet `design-review/t031-fix/packet` (seed
  31337, twelve images, `DR_RUN_ID=t031-fix
  tools/design-review/capture.sh`). `check_fixes.py` reports
  `ghost-outline-contrast` PASS: outline `(57, 159, 83)` against the
  well `(10, 12, 19)`, 5.82:1. Report saved as `fix-checks.json`.
- Measured: the T ghost of the `tall-stack` fixture image, which the
  checker does not read, holds 3.72:1 across 368 px with a hollow
  interior in all four cells.
- Unavailable: the remote image-analysis service rejected every upload
  this session (error 1210, PNG and JPEG, crop and full frame), so no
  vision-model pass ran. The visual check fell back to direct pixel
  reading plus a per-pixel map of both ghost regions; the S and the T
  outlines read as clean hollow shapes beside solid settled cells.
- Still failing on the same packet, outside this task:
  `letterbox-scene-centered` with bars 0 and 118 px (T032).

Commands:

```bash
ctest --test-dir build --output-on-failure
HARNESS_RUN_ID=t031-regress-harness tests/harness/run.sh
T024_RUN_ID=t031-regress-t024 xvfb-run -a -s "-screen 0 960x544x24" \
  tests/harness/t024.sh
DR_RUN_ID=t031-fix tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t031-fix/packet \
  --json build/agent-checks/design-review/t031-fix/fix-checks.json
```

## T032: centered letterboxed scene (2026-11-03)

Round M2 fix from the T027 screenshot review, key
`looper:finding:letterbox-scene-centered`. A 640x480 window held the scene
at its top: 0 px of bar above, 118 px below.

The defect was not the resize handling the packet first suggested. Probes
under the same X11 driver showed SDL recenters the letterbox viewport
itself, so the presented window was already centered. Two other defects
made the frame measure wrong: `SDL_RenderReadPixels` with a null rectangle
reads the logical viewport area, so the capture held only the scene at the
top of the image, and `SDL_RenderClear` clears the whole target, so the
scene's background clear painted the slack bars in the canvas color
instead of leaving them black.

What landed:

- `applyCanvas`, `beginFrame`, and `readOutputPixels` in the renderer
  module (`src/renderer_sdl.cpp`): state the canvas, paint the black
  letterbox base, and capture the whole output in device pixels.
- `src/main.cpp` calls `applyCanvas` at startup and on
  `SDL_WINDOWEVENT_SIZE_CHANGED`, calls `beginFrame` before every draw, and
  captures checkpoints through `readOutputPixels`.
- `Renderer::clearBackground` fills the canvas rectangle instead of
  clearing the whole rendering target, so the scene keeps its hands off
  the bars.
- `tests/test_renderer_sdl.cpp` measures the bars of a 640x480 and a
  720x480 frame from pixels, requires the even split, the black bars, the
  stale pixels gone, the device-space capture, and a byte-identical native
  frame. The render suite reports 924 checks. Reverting the renderer
  changes alone fails 7 checks, so the cover is live.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in `build/`.
- Passed: `tests/harness/run.sh` 157 of 157
  (`t032-regress-harness/`).
- Passed: `tests/harness/t024.sh` 287 of 287 under `xvfb-run`
  (`t032-regress-t024/`). Every letterboxed frame of its X11 session
  measures bars of 59 and 59 px; every native frame stays full bleed.
- Passed: fresh review packet `design-review/t032-fix/packet` (seed 31337,
  twelve images, `DR_RUN_ID=t032-fix tools/design-review/capture.sh`).
  `check_fixes.py` reports `letterbox-scene-centered` PASS: window 640x480,
  top bar 59 px, bottom bar 59 px. All three required criteria pass.
  Report saved as `fix-checks.json`.
- Passed: a 720x480 probe with the same driver measures two 36 px bars
  (`design-review/t032-probe720/`), the size that measured 0 and 72 px
  before.
- Passed: the native output is unchanged. The `scenes` session ran once
  from a build of the previous commit and once from this one, same seed,
  script, and save directory: all nine checkpoint BMPs are byte-identical,
  and the observation records differ only in the screenshot path.

Commands:

```bash
ctest --test-dir build --output-on-failure
HARNESS_RUN_ID=t032-regress-harness tests/harness/run.sh
T024_RUN_ID=t032-regress-t024 xvfb-run -a -s "-screen 0 960x544x24" \
  tests/harness/t024.sh
DR_RUN_ID=t032-fix tools/design-review/capture.sh
DR_LETTERBOX_WxH=720x480 DR_RUN_DIR=build/agent-checks/design-review/t032-probe720/letterbox \
  xvfb-run -a -s "-screen 0 960x544x24" tools/design-review/letterbox-x11.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t032-fix/packet \
  --json build/agent-checks/design-review/t032-fix/fix-checks.json
```

## T033: session log follows --save-dir (2026-11-03)

Round M3 fix, key `looper:finding:session-log-save-dir-placement`. A run
with `--save-dir DIR` still opened its log in
`$HOME/.local/share/stackfall/log.txt`: `main()` resolved the log path
from the environment alone, before the option had any say, so every
harness and review run overwrote one shared log in the home directory
while `highscore.dat` and `settings.dat` went to the option's directory.

Saved failure first:
`build/agent-checks/2026-11-03-t033/repro/` holds the run of
`SDL_VIDEODRIVER=dummy ./build/stackfall --seed 4242 --frames 90
--save-dir /tmp/sf-t033-repro/save`, the stdout that names the home log,
the home log as it was before, and the empty save directory.

What landed:

- `logFilePath(saveDirectory)` in `src/storage.cpp` resolves the log
  against one save directory: `STACKFALL_LOG_FILE` wins, then the log
  lands in that directory as `log.txt`, and an empty directory is "off",
  exactly as it is for the two stores.
  `logFilePathFromEnvironment()` is now that function fed
  `saveDirectoryFromEnvironment()`, so one rule serves both callers.
- `src/main.cpp` resolves `saveDirectory` once after `parseOptions` -
  the option's value when a harness passed one, the environment
  otherwise - and feeds it to the log sink and both stores, so the
  record, the settings, and the log can never split across directories.
- `tests/test_storage.cpp` pins the new precedence: the option's
  directory beats every environment hint, a trailing separator is
  normalized, `STACKFALL_LOG_FILE` still wins, and an empty directory
  logs to stdout alone.
- `tests/harness/check_harness.py` adds one rule per session,
  `<session>/save-dir-log`: `log.txt` must sit beside the record in the
  `--save-dir` the harness itself passes and must carry the banner and
  the clean shutdown. Removing that one file from a copy of the harness
  run fails the checker; `guard-proof.txt` holds that failing output.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in `build/`; the rules suite reports 338,409
  checks and zero failures.
- Passed: `tests/harness/run.sh` 163 of 163
  (`2026-11-03-t033-harness/`); the six new `save-dir-log` rules pass.
- Passed: `tests/harness/t024.sh` 287 of 287 under `xvfb-run`
  (`2026-11-03-t033-t024/`).
- Passed: the acceptance run of the finding. With
  `--save-dir /tmp/sf-t033/save1` the binary logs
  `Log: stdout and /tmp/sf-t033/save1/log.txt`, that file holds the
  `STACKFALL 0.1.0` banner and `Clean shutdown`, and the home log's
  SHA-256 is the same before and after the run.
- Passed: `STACKFALL_LOG_FILE` still wins over the option; a run
  without the option and with an isolated `HOME` still writes
  `$HOME/.local/share/stackfall/log.txt`; a relative
  `--save-dir save3/` yields `save3/log.txt`, separator normalized.

Device behavior is unchanged: the PortMaster launcher exports
`XDG_DATA_HOME`, which still names the save directory for a launch
without options.

Commands:

```bash
ctest --test-dir build --output-on-failure
HARNESS_RUN_ID=2026-11-03-t033-harness tests/harness/run.sh
T024_RUN_ID=2026-11-03-t033-t024 xvfb-run -a -s "-screen 0 960x544x24" \
  tests/harness/t024.sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/stackfall \
  --seed 4242 --frames 90 --save-dir /tmp/sf-t033/save1
```

## T034: reused harness run id starts clean (2026-11-03)

Harness fix, key `looper:finding:harness-reused-run-id-stale-save`
(papercut `pc_01M6GJCT9EYDM542WRMVSX2H6Q`). A second
`HARNESS_RUN_ID=<same id> tests/harness/run.sh` failed three checks:
`run.sh` recreated the run directory with `mkdir -p`, so the sessions kept
the earlier pass's `--save-dir`, and its `highscore.dat` (220) made the
first title frame report `high=220` where the headless reference - which
always starts from an empty save - reports 0, and made the first game
over `record=0` instead of 1. `t024.sh` had the same defect through
`T024_RUN_ID`. `tools/design-review/capture.sh` already removed each
session directory for exactly this reason.

Saved failures first, both from the pre-fix drivers of commit 2db13b7:

- `build/agent-checks/t034-repro-harness-before-fix/`: `run.sh` passed
  163 of 163, then the rerun with the same id failed 3 checks
  (`scene-cycle-keyboard/reference-stream`, `first-round-record`,
  `scene-cycle-pad/reference-stream`). `check-failures.txt`,
  `console-first-pass.log`, and `console-failed-rerun.log` hold both
  passes; `scene-cycle-keyboard/save/highscore.dat` holds the leaked
  `high_score=220`.
- `build/agent-checks/t034-repro-t024-before-fix/`: the same shape for
  `t024.sh`, 287 of 287 then 3 failures on the rerun (leaked
  `high_score=228`).

What landed:

- `tests/harness/run.sh` and `tests/harness/t024.sh` remove an existing
  run directory before `mkdir`, announce the removal, and then start the
  run from an empty directory, so no session can inherit a save, a log,
  or a screenshot from an earlier pass.
- Both drivers reject a run id that is not one directory name (`.`,
  `..`, anything with a slash) before anything is removed, so the cleanup
  can never leave `build/agent-checks/`. An empty variable never reaches
  it: `${VAR:-default}` already falls back to the dated name.
- `docs/AGENT_TESTING.md` states the reuse rule next to the environment
  variables of both drivers.

Verification, under `build/agent-checks/`:

- Passed: `tests/harness/run.sh` twice in a row with
  `HARNESS_RUN_ID=t034-repro` (`t034-repro/`): 163 of 163 both times,
  each rerun announcing the removal of the earlier pass.
- Passed: `tests/harness/t024.sh` twice in a row with
  `T024_RUN_ID=t034-fixed-t024` under `xvfb-run`
  (`t034-fixed-t024/`): 287 of 287 both times.
- Passed: the guard. `HARNESS_RUN_ID` and `T024_RUN_ID` values `../escape`,
  `.`, `..`, and `sub/dir` exit 1 with "must name one directory" and
  leave `build/agent-checks/` untouched; an empty value falls back to the
  dated default and still passes a full run.
- Passed: `ctest` 5 of 5 in `build/` (run by both drivers before their
  sessions).

Commands:

```bash
HARNESS_RUN_ID=t034-repro tests/harness/run.sh
HARNESS_RUN_ID=t034-repro tests/harness/run.sh
T024_RUN_ID=t034-fixed-t024 xvfb-run -a -s "-screen 0 960x544x24" \
  tests/harness/t024.sh
T024_RUN_ID=t034-fixed-t024 xvfb-run -a -s "-screen 0 960x544x24" \
  tests/harness/t024.sh
HARNESS_RUN_ID="../escape" tests/harness/run.sh
```

## T025: unattended stability and failure checks (2026-11-03)

Round M3, at commit 657ea6d plus this task's edits. One driver runs the
whole pass phase by phase into `build/agent-checks/2026-11-03-t025-stability/`
(sources and binaries in `source.sha256`):

```bash
tests/harness/t025.sh
```

Phases: `host`, `fuzz`, `hardkill`, `cycles`, `restarts`, `longplay`,
`pacing`, `failures`, `san`, `regress`. The operator extension - the
bounded parser fuzz and the hard-kill pass - ran before the acceptance
phases, as required. Helper scripts: `tests/harness/t025_gen.py` (the
session generator), `tests/harness/t025_fuzz.py` (the fuzz pass), and
`tests/harness/frame_stats.py` (frame-time statistics; the game grew the
developer option `--frame-times FILE`, one microsecond duration per
frame, to feed it).

Host: 12th Gen Intel Core i9-12900HK, 8 CPUs, 12 GB RAM, Ubuntu 24.04,
g++ 13.3.0, CMake 3.28.3, SDL 2.30.0, Xvfb with xdotool and openbox.
Every real-time session ran under `xvfb-run`; the dummy video driver
served the headless ones. All numbers are host measurements; none of
them certifies RG503 performance.

### The fuzz finding, fixed and pinned

The fuzz pass found one real defect before any acceptance phase ran.
`InputScript::parseLine` narrowed a 64-bit `wait`/`stall` count to `int`;
`stall 6442450944` (2^32 + 2^31) truncated to `INT_MIN`, which
`SDL_Delay` read as an unsigned ~24.8-day delay. The process hung inside
the delay, outlived `timeout 10` sending SIGTERM, and needed SIGKILL;
its block-buffered stdout log stayed empty. The mirrored parser of the
headless reference carried the same cast. Evidence:
`fuzz/finding-pre-fix/` (reproducer script, README, the pre-fix hang,
and the post-fix rejection).

Fix: `stackfall::parseScriptCount` (`include/stackfall/script.hpp`,
`src/script.cpp`) accepts only 0 through 3600000 and writes the output
only on success; both parsers route through it with one shared error
message. `tests/test_script.cpp` pins the valid range, the boundary, the
crashing inputs (`2147483648`, `4294967296`, `6442450941`,
`6442450944`, `18446744073709551615`, `3600001`, `03600001`, and more),
and the malformed shapes. The core suite grew to 338,471 checks, 0
failures.

### Phase results

- Passed, fuzz (150 s budget): the save-parser mutation loop ran 50.2 s
  under AddressSanitizer and UndefinedBehaviorSanitizer with every
  accepted parse equal to the canonical rendering; 236 binary cases
  through the sanitized game (135 clean exits, 101 rejected scripts, 0
  findings) in 98.5 s. Report: `fuzz/run/fuzz-report.json`.
- Passed, hardkill (operator extension): 24 SIGKILLs during save writes
  (18 triggered on the game-over log line plus a uniform delay through an
  `LD_PRELOAD` shim that holds fsync and rename 120 ms, 6 random). Every
  resulting file loaded or was rejected safely: 3 absent, 21 canonical,
  0 damaged (`hardkill/kills.tsv`); killed writes leave the previous
  record or nothing, because the save path writes a per-pid temporary
  file and renames over the target. The leftover
  `.highscore.dat.tmp.<pid>` files never become the record. A verify run
  then loaded and extended the final file.
- Passed, hardkill-two (operator extension): two simultaneous instances
  on one save directory seeded with `high_score=10`, each with its own
  `STACKFALL_LOG_FILE`. Both exited 0 with clean shutdowns, no save
  failure lines, and the record moved 10 to 226 - never shrank.
- Passed, cycles: 52 restart and menu cycles in one process at real-time
  pacing (pause-restart, quit-to-title restart, play-again, title
  restart), seed 20261501, with the virtual controller attached,
  movement probes before, between, and after, five sound toggles, and
  RSS sampling of the game process. No stuck input (every probe moved
  the piece), one controller open and zero removals, no crash, clean
  shutdown. Frame times over 179.3 s: 10,755 frames, 10,756 ticks
  (60.00 per second), median 16.76 ms, p95 17.28 ms, p99 17.63 ms, max
  68.69 ms - one unscheduled host hiccup, logged as "Unusual frame stall
  of 68 ms" and caught up inside the clamp budget. Game process resident
  memory 16,476 kB early, 16,516 kB
  late, 2 threads throughout.
- Passed, restarts: 50 process restarts (seeds 777001 through 777050),
  each loading the record the previous run left, saving only when the
  new game over beat it. Final file canonical, record 248, best game
  248, weakest 208.
- Passed, longplay (the ten-minute acceptance session, seed 20261601,
  Release build, under Xvfb with the opengl renderer): 600.8 s wall,
  35,971 frames rendered, 35,989 ticks simulated (59.90 per second - the
  one deficit is the clamped stall below, by design). Frame times:
  median 16.58 ms, p95 17.41 ms, p99 17.58 ms, max 1,216.89 ms (the
  injected stall itself). Twelve games across six distinct round seeds,
  four pause and resume cycles, five sound toggles, 17 game-over
  records, 52 checkpoint screenshots, clean shutdown. Both injected
  stalls landed in active rounds: 1,216 ms measured, clamped to 250 ms
  with catch-up skipped, and 137 ms logged as an unusual stall and
  caught up inside the budget. CPU 41% of one core, peak resident
  126,460 kB; the sampler shows the steady state flat (middle third
  123,664 kB, last third 125,544 kB, 19 threads - Mesa's llvmpipe pool
  under Xvfb).
- Passed, pacing: 7,200-frame runs (120 s each, seed 31) per renderer
  mode, with the logged renderer flags pinning which is which:

  | Run | Renderer (as logged) | Vsync | Elapsed | Ticks/s | Median | p95 | p99 | Max | CPU | Peak RSS |
  | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
  | x11 opengl | opengl (accelerated, vsync) | on | 120.0 s | 60.00 | 16.52 ms | 17.50 ms | 17.63 ms | 17.88 ms | 16% | 120,420 kB |
  | x11 opengl | opengl (accelerated) | off | 120.0 s | 60.00 | 16.55 ms | 17.50 ms | 17.64 ms | 17.79 ms | 16% | 120,748 kB |
  | x11 software | software (not accelerated, vsync flag SDL only pretends to honor) | on | 120.0 s | 60.00 | 16.83 ms | 17.19 ms | 17.50 ms | 17.78 ms | 23% | 125,128 kB |
  | dummy | software (same pretense) | on | 120.0 s | 60.00 | 16.51 ms | 17.52 ms | 17.63 ms | 17.77 ms | 1% | 12,160 kB |

  The vsync-off row is the same opengl renderer with `SDL_RENDER_VSYNC=0`
  stripping the flag, so the comparison isolates the flag. Under Xvfb
  presentation never blocks - the on and off rows are statistically the
  same - and in every mode the elapsed-time fallback limiter alone holds
  the 60 Hz simulation (7,199 due ticks for 7,200 frames each time).
  T014's finding carries over with percentiles now.
- Passed, failures (17 checks): missing audio (every cue reported
  without a device, clean exit), forced renderer failure (exit 1 with
  the driver list and an actionable error), the software renderer, focus
  loss through `t024-x11.sh` (minimize and restore; held buttons
  cleared), controller removal mid-play, the long stall (1,516 ms
  measured, clamped to 250 ms, the session continues), an unwritable
  save directory (chmod 500), missing, empty, partial, truncated,
  garbage, doubled, and CRLF save files (each rejected as invalid or not
  found), no save directory at all, and a plain startup with no
  developer options.
- Passed, san: the full ctest suite green under
  `-fsanitize=address,undefined`, plus one scripted scene cycle through
  the sanitized SDL application with `detect_leaks=1`, clean exit and
  clean shutdown, no sanitizer report.
- Passed, regress: `tests/harness/run.sh` and `tests/harness/t024.sh`
  green again after this task's edits.
- Passed: `ctest` 5 of 5 in Debug (`build/`) and Release (`build-rel`).

### Harness defects the run itself exposed

The pass failed twice before it passed, and both failures were harness
defects worth recording:

- The longplay RSS sampler captured the `timeout` wrapper's pid (a
  `pgrep -f` pattern matched the wrapper command line), so the first
  run's "flat 2 MB" recorded the wrapper, not the game. The cycles
  sampler had the same flaw through `$!` (the `/usr/bin/time` pid).
  Both now wait for a process literally named `stackfall` and stop when
  `/proc/<pid>/comm` stops matching, so a recycled pid cannot extend a
  sample into the next phase - a recycled sampler target once recorded a
  pacing run inside longplay's `rss.tsv`.
- A single-phase rerun with `T025_KEEP_DIR=1` inherited the earlier
  pass's artifacts, and the checker judged a stale `rss.tsv` whose
  sampler had never started. Every phase now starts from an empty
  directory of its own (T034's rule, applied per phase).
- The generator placed each game's unattended watch (about 2,289 ticks)
  after the navigation that started the next round, so gravity ended
  most rounds before their scripted input ran; the 1.2 s stall of game
  3 landed on a game-over scene, and `play_segment`'s own hard drops
  can stack the center columns into the spawn area even earlier. The
  stalls now run at the top of each round on the fresh board, and the
  checker asserts both after-stall checkpoints recorded an active
  round.

### Findings and open questions

- Real blocking vsync is unavailable on this host: Xvfb has no vblank,
  so "vsync on" means the renderer reports the flag while presentation
  returns at once, and the elapsed-time limiter does the pacing in
  every mode. The percentile table with the flag stripped covers the
  disabled case on the same renderer. The device gate (T018) must
  repeat the measurement on the RG503 panel; that check stays with
  `looper:finding:t025-vsync-percentile-device-pacing`.
- The game process under Xvfb and the opengl renderer settles at about
  123 MB resident with 19 threads - Mesa's llvmpipe software GL, not
  game state: the same binary under the dummy video driver and the
  software renderer runs near 12 to 16 MB, and the ten-minute session
  grows only 1.9 MB between its middle and last thirds. The device
  numbers will differ again.
- Two instances on one save directory need per-instance
  `STACKFALL_LOG_FILE`; without it they would share one default log.txt
  path. The launcher always sets it on the device (section 10.3).
- Killed saves leave `.highscore.dat.tmp.<pid>` files behind. They are
  harmless (the loader ignores them) but they accumulate; a future
  launcher cleanup could remove them at startup.
- Screenshot review stayed pixel-level in this session: the image
  upload service was down (see T024's notes), so the check verified
  960x544 dimensions, per-screen color structure, and board-region
  content by PIL census; T024's visual review already covered these
  screens with open inspection.

## T038: 27 px board geometry (2026-11-03)

Round M3 layout change, key `looper:finding:board-cells-27px`. The board
moved from 24 px cells at (360, 32) to the owner-approved geometry of
`docs/SPEC.md` section 6.1: 27 px cells, a 270x540 field at (345, 2) with
occupied bounds x=345..614 and y=2..541, and the one-pixel frame at
x=344..615 and y=1..542, centered on the 960x544 canvas. The owner's plan
note `/notes/stackfall-27px-board-plan-2026-11-03.md` is not in this
repository (`notes/` holds only the three running reports); the numbers
above are the ones the accepted backlog quotes from it in T038, in T037,
and in the focused-HUD decision note the backlog also carries.

What landed:

- `include/stackfall/renderer_sdl.hpp`: `kBoardCellSize` 27,
  `kBoardOriginX` 345, `kBoardOriginY` 2, plus `boardFrame()`, the
  272x542 frame rectangle at (344, 1) that `drawBoardFrame` now draws.
  The hold and next panels keep their columns and stay anchored to the
  board top, so they follow the field up 30 px.
- `src/renderer_sdl.cpp`: `drawBoardFrame` draws `boardFrame()`. Every
  other board draw already went through the origin and cell constants:
  the well, the grid, settled cells, the active piece, and the ghost all
  moved with them, and the overlays stay centered on the canvas, which is
  also the board center (480, 272).
- `tests/test_renderer_sdl.cpp`: a new `run_board_boundary_checks` holds
  the geometry contract and the top and bottom visible rows (whole cells,
  frame outside them, background past the frame on all four sides, the
  grid's first and last lines inside the field), the 1.5x scaling check
  carries the new device columns, and the pause check requires the veil
  to reach the frame's bottom-right corner. The render suite reports 985
  checks.
- `tools/design-review/check_fixes.py`: the hint band and the ghost
  region moved to the new coordinates, and the hint check now measures
  the columns it measures the rows of, so "stays inside the left panel"
  is a real bound (rightmost text column 309 against the panel edge 324)
  instead of the earlier vacuous row limit.
- `tools/design-review/build_prompt.py` and `docs/SPEC.md` section 6.1
  state the new geometry.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in `build/` and `build-rel/`.
- Passed: `tests/harness/run.sh` 163 of 163
  (`t038-board-27px-harness/`).
- Passed: `tests/harness/t024.sh` 287 of 287, including its X11
  letterbox session (`t038-board-27px-t024/`). Every letterboxed frame
  keeps 59 px bars top and bottom.
- Passed: fresh packet `design-review/t038-board-27px/packet` (seed
  31337, twelve images, `DR_RUN_ID=t038-board-27px
  tools/design-review/capture.sh`, native 960x544 scenes plus the
  640x480 letterbox scene). `check_fixes.py` passes all three required
  criteria: hint glyphs 14 logical px with the text inside the panel, the
  ghost outline at 5.82:1 against the well, letterbox bars 59 and 59 px.
  Report saved as `fix-checks.json`.
- Passed, measured from the packet pixels: the well spans exactly
  x=345..614 and y=2..541 (540 rows, 261 well columns plus nine grid
  lines), the frame holds x=344 and x=615 and y=1 and y=542 complete on
  all four sides in every play frame, canvas rows 0 and 543 and columns
  343 and 616 hold only background, the grid lines sit at x=372..588 and
  y=29..515 in 27 px steps, and no image has content on its outer edge.
- Passed, measured the same way: the left panel content stays in
  x=64..309 and y=4..277, the right panel content in x=702..837 and
  y=4..467, columns 325..343 are empty, so nothing overlaps the frame and
  nothing clips.
- Passed, open inspection of `gameplay-early`, `letterbox-640x480`,
  `tall-stack`, `pause`, `hold-locked`, and `gameover` through the image
  review service: the field is centered with a complete frame, the
  letterbox scene is centered with equal bars and readable text, the tall
  stack leaves a clear gap below the top frame, both overlays sit
  centered over the dimmed field, and no text touches the frame. Two of
  the service's first-pass claims did not survive the pixel census and
  were rejected: the top frame row is present at y=1 in every play frame,
  and the score values draw bright (232, 236, 246), not dim.

Commands:

```bash
ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel
HARNESS_RUN_ID=t038-board-27px-harness tests/harness/run.sh
T024_RUN_ID=t038-board-27px-t024 tests/harness/t024.sh
DR_RUN_ID=t038-board-27px tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t038-board-27px/packet \
  --json build/agent-checks/design-review/t038-board-27px/fix-checks.json
```

### Findings and open questions

- The HUD composition task T037 (`focused-gameplay-hud`) still owns the
  panel regrouping, the gameplay legend removal, and the pause-to-controls
  route. It must now run at this geometry: its contract already names the
  27 px numbers, and the panels it will move sit 34 px (left content) and
  87 px (right content) from the frame.
- T015's accepted `depends_on` still lists no layout task. The gate the
  owner's notes order - final desktop verification after the final layout
  task - needs Looper planning to add it; workers cannot edit the
  backlog. T015's blocker already records this.

## T039: settings file gains the ghost preference (2026-11-03)

Task T039, round M3, the settings follow-up to T012 and T013. Tested at
commit `c0fcb8e` plus uncommitted task changes; the tested source hashes
are in `build/agent-checks/t039-settings/source.sha256`.

What landed:

- `include/stackfall/storage.hpp` and `src/storage.cpp`: the settings file
  is now three lines, `version=1\nsound=on|off\nghost=on|off\n`.
  `formatSettingsFile(bool, bool)` renders it, and
  `parseSettingsFile(bytes, bool* soundOn, bool* ghostOn)` accepts exactly
  six strings: the four three-line forms plus the two legacy two-line
  forms, which parse with the ghost on so an upgrade never loses a saved
  sound choice. Everything else is a damaged file that recovers to both
  preferences on. `SettingsResult` gains `ghostOn`, `SettingsStore::save`
  takes both values over the unchanged atomic write, and
  `kSaveFormatVersion` stays 1 because the high-score record shares it.
- `src/main.cpp`: the session loads both preferences, logs
  `Settings: sound %s, ghost preview %s (%s)`, writes `ghost start=` to
  the input log, and carries the loaded ghost value through every save of
  the sound toggle. The player has no ghost control yet; T040 adds the
  options screen, so until then a session preserves what it loaded.
- `tests/test_storage.cpp`: the four canonical renderings, the six
  readable parses, both legacy upgrades on disk and in memory, and the
  ghost rejections (lost final newline, `ghost=maybe`, `ghost=On`, a file
  missing the sound line, a doubled ghost line, CRLF) beside every
  existing sound rejection.
- `tests/harness/t025_fuzz.py`: the settings corpus is the three-line
  form, both legacy files joined the mutation bases and the save-pair
  cases, and the oracle accepts the legacy renderings only with the ghost
  reported on. `tests/harness/t025.sh`: the persistence-failure grep of
  the two-instance phase covers the ghost setting save.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in `build/` (Debug), `build-rel/` (Release), and
  the sanitizer build. The core suite reports 338,576 checks, the render
  suite 985, the audio suite 4,005.
- Passed, the acceptance case: a legacy `version=1\nsound=off\n` file
  loads through the real binary as `Settings: sound off, ghost preview on
  (ok)`, and one sound toggle rewrites the file as
  `version=1\nsound=on\nghost=on\n` - the upgrade on the next save.
- Passed: `tests/harness/run.sh` 163 of 163 (`harness-2026-11-03-204845`)
  and `tests/harness/t024.sh` 287 of 287
  (`t024-2026-11-03-204904`), including the mute-toggle input-log pins;
  the saved `settings.dat` of those sessions is the three-line form.
- Passed: `T025_PHASES=fuzz T025_RUN_ID=t039-fuzz-check-2
  tests/harness/t025.sh`: the parser loop under ASan and UBSan and 229
  binary cases, the save pairs now seeded from both the current and the
  legacy settings file, no findings.
- Passed: `T025_PHASES=hardkill T025_RUN_ID=t039-hardkill-check
  tests/harness/t025.sh`: 24 kills (21 canonical records, 3 absent, none
  damaged), the repair-and-load run, and the two-instance check whose
  persistence-failure grep now covers the ghost setting.
- Passed, mutation checks: forcing the legacy parse to report the ghost
  off fails four storage pins, and a deliberately wrong legacy oracle in
  the fuzz driver is caught within seconds, so both the pins and the
  oracle are meaningful.

Commands:

```bash
ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel
HARNESS_RUN_ID=harness-2026-11-03-204845 tests/harness/run.sh
T024_RUN_ID=t024-2026-11-03-204904 tests/harness/t024.sh
T025_PHASES=fuzz T025_RUN_ID=t039-fuzz-check-2 tests/harness/t025.sh
T025_PHASES=hardkill T025_RUN_ID=t039-hardkill-check tests/harness/t025.sh
```

## T040: options screen with the ghost toggle (2026-11-03)

Scope, per the task contract: the title menu gains OPTIONS between CONTROLS
and QUIT (`kTitleItemCount` 4), the new value-free `Scene::Options` joins the
enum at its end, `Game::tickOptions` moves a two-row cursor that the state
hash never folds, the renderer gains `setShowGhost`/`setSoundOn` and gates
`drawGhostPiece` on the setting, and the platform layer turns the row select
into `toggleSound`/`toggleGhost` writes of `settings.dat`. QUIT moved from
title index 2 to 3, so every quit-from-title walk gained one down-press:
`scene-cycle-keyboard`, `scene-cycle-pad`, `fixture-four-line`,
`t024-shapes-keyboard`, `t024-shapes-pad`, `t024-clears-23`, `t024-i-spawn`,
`t024-focus-letterbox`, and `t025_gen.py quit_from_title` (`fixture-blockout`
and `fixture-lockout` exit through the script `quit` command, so they needed
nothing).

Verification, under `build/agent-checks/`:

- Passed: zero-warning `cmake --build` in `build/` (Debug) and `build-rel`
  (Release) after `--clean-first`. One pre-existing `-Wparentheses` warning
  in `tests/test_font.cpp:60` (present at the baseline, unrelated to this
  change) was repaired with explicit parentheses so the zero-warning gate
  holds.
- Passed: `ctest` 5 of 5 in Debug and Release. Check counts: core 338,593
  (the screens suite grew by the four-item title tests and
  `test_title_options_round_trip`), render 1,050 (the options-page checks
  and the ghost-off pixel check), audio 4,005, input 382.
- Passed: `tests/harness/run.sh` 163 of 163 (`t040-final-2026-11-03`) and
  `tests/harness/t024.sh` 287 of 287 (`t040-final-t024`): every session
  walks the four-item title menu and quits from `menu=title:3`.
- Passed: `T025_PHASES="failures san regress" T025_RUN_ID=t040-t025fail
  tests/harness/t025.sh` - the forced-failure logs, one scripted
  ASan+UBSan session, and fresh run.sh/t024.sh reruns. A two-cycle
  `t025_gen.py cycles` session exited through the menu quit with
  `menu=title:3` on `final-at-quit`.
- Passed: `t026.sh` verify - all four scenarios, `busy` at the full 6000
  frames (`t040-t026busy`; its pinned top-out stays at tick 3983, so the
  rules core is untouched). The menu scenario now alternates Controls and
  Options and never plays (`t040-t026verify`).
- Passed, the options round trip (`build/agent-checks/t040-manual/`): the
  scripted walk opens the screen (`scene=options`, `menu=options:0`),
  moves to and toggles the GHOST row, returns with the title cursor on
  OPTIONS, and starts a round. The input log gains exactly one
  `ghost hidden status=ok` line, `settings.dat` holds exactly
  `version=1\nsound=on\nghost=off\n`, and all 8 checkpoint records are
  byte-identical to the headless `stackfall_reference` stream. The SOUND
  row toggles through the menu path and the opening select toggles
  nothing (one `sound muted` line for one row select).
- Passed, the render gate measured on captured pixels: the well of
  `playing-start.bmp` after the toggle holds 0 pixels of the T ghost
  outline color (135, 87, 173) while the active piece draws identically;
  the same checkpoint with the ghost on holds 416 outline pixels.
- The options screenshots show the heading at y=96 (28 px tall) and the
  selected row marker at y=232 (SOUND) moving to y=276 (GHOST).

Commands:

```bash
ctest --test-dir build --output-on-failure
HARNESS_RUN_ID=t040-final-2026-11-03 tests/harness/run.sh
T024_RUN_ID=t040-final-t024 tests/harness/t024.sh
T025_PHASES="failures san regress" T025_RUN_ID=t040-t025fail tests/harness/t025.sh
T026_PHASES=verify T026_SCENARIOS=busy T026_RUN_ID=t040-t026busy tests/harness/t026.sh
```

T041 adds the registered options-ghost harness session and the SPEC and
AGENT_TESTING documentation of the screen.

## T041: options-ghost harness session and options documentation (2026-11-03)

Task T041, round M3, the agent-testing follow-up to T040 in the pattern of
T023 and T024. Tested at commit `789736a` plus uncommitted task changes; the
tested source hashes are in
`build/agent-checks/t041-final-harness/source.sha256`. No C++ source
changed: the screen, the settings write, and the ghost gate are T039/T040
work that this task registers and documents.

What landed:

- `tests/harness/scripts/options-ghost.script.txt`: the seventh session,
  keyboard, seed 20261103. A first round runs with the ghost at its on
  default and checks its first playing tick, the round is abandoned through
  the pause menu, the title walks to OPTIONS (two downs), the screen opens
  on SOUND, one down reaches GHOST, one select turns the ghost off, Esc
  returns with the title cursor still on OPTIONS, two ups and a select start
  a second round whose first playing tick is checked, and the session quits
  through the fourth title item. A `wait` follows every check, so no record
  carries the tick of the input after it.
- `tests/harness/check_harness.py`: the options-ghost branch (32 checks)
  pins the 13-record scene sequence, `menu=title:2` before the screen,
  `menu=options:0` and `menu=options:1` on the two options checkpoints, the
  select keeping the row, the back keeping the title cursor on OPTIONS, the
  title still holding four items (`menu=title:3`), the quit, both playing
  records with the active piece at a hidden row (`y=-1`) while the ghost
  names a visible landing row (`y=18`), the saved settings bytes, and four
  log lines. New helpers: `read_bmp_pixels` (a 30-line, dependency-free BMP
  reader: 24 or 32 bpp, bottom-up or top-down, four-byte stride, BGR(A)
  order), `check_well_below_piece` (a pixel census of the well from two rows
  under the live piece's origin - strictly below the piece, where section
  6.2 step 3 says the outline draws), `check_settings_file`, and
  `check_hidden_spawn_landing`. The ghost-on checkpoint before the toggle is
  the positive control: the same scan must find the outline there, so a
  broken reader cannot pass the zero proof.
- `tests/harness/run.sh`: registers the session and moves the header and
  session-count wording from six sessions to seven.
- `docs/SPEC.md`: section 4.7 TITLE reads Start Game / Controls / Options /
  Quit with a new OPTIONS bullet (two rows, select-to-change, back to title,
  persistence); section 5 gains the sentence that the options screen uses
  the same menu edges and adds no key bindings; section 6.2 step 3 notes the
  player toggle and the on default; sections 6.3 and 9 name `settings.dat`
  carrying both preferences over the same atomic write and recovery rules;
  the status line no longer lists the options screen as remaining.
- `docs/AGENT_TESTING.md`: seven sessions, the options-ghost bullet, and the
  well-scan and settings pins in the checker description.

Verification, under `build/agent-checks/`:

- Passed: `tests/harness/run.sh` 195 of 195 (`t041-final-harness`), up from
  163 with the seventh session green end to end; the headless `ctest` suites
  ran first inside it, 5 of 5 in `build/` (Debug) and 5 of 5 in `build-rel`
  (Release).
- Passed: the session's 13 records are byte-identical to the headless
  `stackfall_reference` stream, its exit is 0 with a clean shutdown, and
  `log.txt` sits in the passed `--save-dir`.
- Passed, the pixel proof: the ghost-on frame's well below the piece holds
  416 pixels besides the well and grid colors (the T outline at 135, 87,
  173); the ghost-off frame holds 0. The saved `settings.dat` is exactly
  `version=1\nsound=on\nghost=off\n`, and the input log holds
  `ghost start=on` and `ghost hidden status=ok`.
- Passed, mutation check: forcing `drawPlayScene` to draw the ghost whatever
  the setting says makes the checker fail `well-ghost-off` with exactly the
  416 pixels of the S outline color (57, 159, 83), so the zero proof detects
  a real regression. Evidence in `build/agent-checks/t041-mutation/`;
  reverted and rebuilt afterward.
- Passed, the acceptance case beside the harness: a fresh design-review
  capture (`t041-design-review-2026-11-03`, seed 31337, twelve images) still
  passes `check_fixes.py` 3 of 3 with the ghost on by default - the ghost
  outline measures 5.82:1 over 416 pixels. See `notes/design-review.md`.

Commands:

```bash
ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel
HARNESS_RUN_ID=t041-final-harness tests/harness/run.sh
DR_RUN_ID=t041-design-review-2026-11-03 tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t041-design-review-2026-11-03/packet \
  --json build/agent-checks/design-review/t041-design-review-2026-11-03/fix-checks.json
```

## T029: immediate spawn visibility (2026-11-03)

Sources: [design decision](stackfall-visible-spawn-decision-2026-11-03.md)
(approved 2026-11-03), [game specification](../docs/SPEC.md) section 4.2,
[agent testing guide](../docs/AGENT_TESTING.md) T024 step 4. Source
identities: `build/agent-checks/t029-harness-2/source.sha256` and
`build/agent-checks/t029-t024-2/source.sha256`.

Rule change, `src/pieces.cpp`: the matrix origin is `y=-1` for the I (its
first occupied row is local row 1) and `y=0` for every other piece. The
topmost occupied cell of each spawn sits at visible row 0. The same rule
applies to a new game, the piece after a lock, and both hold paths. Gravity,
scoring, controls, the hidden rows, and the game-over rules are unchanged.
Section 4.2 of the specification now states the rule and its approval.

Verified effects, each traced to the one-row shift:

- Every spawn frame shows its piece. Unit tests in `tests/test_game.cpp`
  cover all seven shapes: at game start, after a hold swap with an empty
  slot, after a hold swap with a filled slot, and after a lock. Each spawn
  has its top occupied row at 0.
- Each hard drop travels one cell less, so it scores 2 points less. The
  refreshed regression scores follow that arithmetic: soft-drop-moves
  108 to 98 (5 drops), hold-and-pause 107 to 99 (4 drops), stack-and-clear
  559 to 539 (10 drops), block-out 224 to 202 (11 drops; the round also
  ends one piece earlier).
- Block-out fires one row earlier. The rebuilt `blockout-ready` fixture
  fills visible row 0; the forced spawn cannot occupy its cells.
- Lock-out cannot follow from a plain spawn anymore, because no piece
  starts above row 0 and drops only descend. An upward SRS kick is now
  required. The test `test_lock_out_still_fires_after_an_upward_spawn_kick`
  grounds a T on a row-2 stack, rotates (the 0-to-R kick `-1,-1` lifts a
  cell to row -1), drops, and the game ends by lock-out. The rebuilt
  `lockout-ready` fixture and `fixture-lockout.script.txt` use the same
  path; the checker pins the hidden-row mask `0008` to prove it.
- Spawn rotations stay inside the board.
  `test_spawn_rotation_stays_inside_the_hidden_rows` turns each shape four
  times at spawn without leaving `y=-2..19`.

Changed regression pins in `tests/test_regression.cpp`, refreshed with the
deliberate procedure (`stackfall_tests --seed N` prints the new values):

```text
gravity-lock    0x13d2f17addd37dc3 -> 0x7c1eb4141e56df81  score 0
soft-drop-moves 0xd2d36cfc30b41f4d -> 0x133e70c548b0ee33  score 108 -> 98
hold-and-pause  0x5c5e0e5c177b7b1c -> 0x9daf6f8a2cbac975  score 107 -> 99
stack-and-clear 0x4a37e53632c5dfb9 -> 0x04958a77e3da0ce5  score 559 -> 539
block-out       0x0366befc1f4628e3 -> 0x33069c75c87fb7b7  score 224 -> 202
```

Test and harness results:

- Debug `ctest`: 5 of 5 suites pass. Release `ctest`: 5 of 5 suites pass.
- T023 harness, run `t029-harness-2`: 198 checks pass, 0 fail. Pin changes:
  scene-cycle-keyboard scores 196/198 (was 218/220), scene-cycle-pad scores
  196 with saved record 198 (was 218/220) and spawn `S,0,2,0`, fixture-lockout
  settles 7 with the hidden-row mask pinned, fixture-blockout settles 19
  (was 20).
- T024 harness, run `t029-t024-2`: 288 checks pass, 0 fail, including the
  X11 focus and letterbox session. Score pins moved by 2 points per hard
  drop: shapes-keyboard 194/206 (was 214/228), shapes-pad 238/200 (was
  262/222), clears-23 triple 868 (was 872); the saved high-score files
  follow the same arithmetic.

Seed-777 reproduction, session `t024-i-spawn`:

- Tick 5, `i-spawn-frame`: the J locks and the I spawns on the same tick
  at `I,0,3,-1`, so its only occupied row is visible row 0.
- Ticks 6 to 52, `i-visible-plus40..47`: the I holds row 0. Tick 53,
  `i-fell-plus48`: `I,0,3,0`. The first fall still comes 48 ticks after
  the spawn, so the level-1 gravity schedule is unchanged. Tick 57,
  `after-i-lock`: the I locks at rows 0 and 1 and the T spawns visibly.

Visual evidence, opened by image reader and confirmed by pixels: the frame
crops `build/agent-checks/t029-review/t029-i-spawn-frame.png` (four cyan
cells in the first board row on the spawn tick),
`t029-i-visible-plus47.png` (same row on tick 52), and
`t029-i-fell-plus48.png` (the row below on tick 53). The checker
`check_i_visible_pixels` also proves the row-0 band by pixel color. The
crops come from `build/agent-checks/t029-t024-2/t024-i-spawn/shots/`.

Open questions: none from this change. Physical button feel and human
enjoyment still wait for the operator pass (T018, T020).

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
HARNESS_RUN_ID=t029-harness-2 tests/harness/run.sh
T024_RUN_ID=t029-t024-2 tests/harness/t024.sh
```

## T037: focused gameplay HUD (2026-11-03)

Sources: [game specification](../docs/SPEC.md) sections 4.7 and 6.1 to
6.2, [agent testing guide](../docs/AGENT_TESTING.md), the accepted task
contract (`looper:finding:focused-gameplay-hud`). Source identity: the
changes were uncommitted worktree state during every run below; the exact
hashes are in `build/agent-checks/t037-smoke-2/source.sha256` and
`build/agent-checks/design-review/t037-fix/source.sha256`.

Composition that landed, on the preserved 27 px board geometry of T038:

- The HUD holds two 128 px columns beside the board, each 24 px from the
  frame: left x=192..319 (Hold above score, lines, level), right
  x=640..767 (five Next previews). Both derive from `boardFrame()`, so
  the group centers (256 and 704) stay symmetric about the board center.
  The hold and preview boxes lost their decorative outlines.
- `drawStatsPanel` centers label (dim) and value (bright) rows of 14 px;
  `statValueScale` drops a value that would not fit to glyph scale 1, so
  a 20-digit score (119 px) still fits the 128 px column. The first
  preview draws at full color, previews two to five dim.
- The permanent gameplay legend is gone (`drawHintsPanel` and its table
  were deleted). The footer hints of the title, pause, and game-over
  screens stay at 14 px, and the controls screen stays complete.
- `Scene::PauseControls` (appended at the enum end, so every pinned
  regression hash survived): the pause overlay's third item CONTROLS
  opens the mappings page with the footer "A OR B BACK TO PAUSE"; select
  or back returns to the overlay on the same item and the round stays
  frozen. `observe.cpp`, `main.cpp`, and `test_regression.cpp` name the
  scene `pausecontrols` or `pause-controls`.
- `GameTestPeer` gained `setScore` so the renderer suite can pin value
  extremes.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 suites in `build/` (Debug) and `build-rel/`
  (Release).
- Passed: `tests/harness/run.sh` run `t037-smoke-2`, 209 checks, 0
  failures. New pins: the pause detour records `paused-at-controls`
  (menu `pause:2`), `pause-controls-open` (scene `pausecontrols`, round
  fields identical), and `pause-controls-back` (scene `paused`, cursor
  still 2); the scene list of `scene-cycle-keyboard` grew to 27 entries;
  every quit-to-title walk in the pinned scripts and `t025_gen.py` now
  takes three downs (RESUME, RESTART, CONTROLS, QUIT TO TITLE).
- Passed: extreme statistics never clip. The renderer suite pins
  `UINT64_MAX` score and lines through `setScore`: `statValueScale`
  returns 1, the gap bands stay background, and no text enters the board.
  All seven shapes render in Hold and in every preview slot, full and
  dim, and a 59-seed scan sees each piece both ways at least once.
- Passed: `tests/harness/t024.sh` run `t037-t024-rerun`, 288 checks, 0
  failures, including the X11 focus-loss and letterbox session. Every
  letterboxed frame keeps its 59 px bars.
- Passed: `tests/harness/t025.sh` phase `cycles` rerun after the
  generator change (run `t025-2026-11-03-232703`): 52 restart and menu
  cycles at real-time pacing, no stuck input, no resource growth.
- Passed: fresh design-review packet `design-review/t037-fix/packet`
  (seed 31337, twelve images, `DR_RUN_ID=t037-fix
  tools/design-review/capture.sh`). `check_fixes.py` passes 4 of 4
  required criteria: seven 14 px statistics rows with quiet gaps, the
  NEXT heading alone with 900 piece pixels per slot, ghost outline
  5.82:1, letterbox bars 59 and 59. See `notes/design-review.md`.
- Passed: the pause route outside the packet, session
  `design-review/t037-fix/pause-controls/`: the overlay at round tick
  63, the mappings screen opened from the CONTROLS item, and the return
  to the overlay on the same item, all three records at the same frozen
  tick 63 with score 0 and an empty board.
- Passed, opened and inspected: every packet image plus the three
  pause-route frames, read through the codex image reader in five
  factual batches (reports in `design-review/t037-fix/inspect/`). The
  columns read as designed, the values dominate the labels, the first
  preview dominates the rest, no frame clips or overlaps text, and the
  letterboxed frame keeps every label readable. One pre-existing
  cosmetic observation is recorded in `notes/design-review.md`: the
  game-over panel edge cuts the dimmed statistics column.
- Updated: `docs/images/gameplay.png` now carries the fresh native
  `dr-hold-locked` frame (seed 31337, tick 127), and the README alt
  text names what it shows.

Open questions: none from this change beyond the cosmetic game-over
panel question in `notes/design-review.md`. Physical button feel and
human judgment still wait for the operator pass (T018, T020).

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
HARNESS_RUN_ID=t037-smoke-2 tests/harness/run.sh
T024_RUN_ID=t037-t024-rerun tests/harness/t024.sh
T025_PHASES=cycles tests/harness/t025.sh
DR_RUN_ID=t037-fix tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t037-fix/packet \
  --json build/agent-checks/design-review/t037-fix/fix-checks.json
```

## T015: M3 desktop gate (2026-11-04)

Round M3 gate, at commit `b543e76` ("Integrate T037", the T029 plus
T037 integrated source). One command runs the complete pass:

```bash
tests/harness/t015.sh
```

The recorded run `t015-gate-2026-11-04` reports 434 checks passed and
0 failed (`check-passed.txt`; sources, binaries, and scripts in
`source.sha256`). The driver phases and what each proved:

- Passed, builds: clean rebuild and `ctest` 5 of 5 in Debug
  (`build/`) and Release (`build-rel/`), zero compiler warnings in
  both compile logs.
- Passed, hashes: `stackfall_tests --seed 987654321` produced
  byte-identical output in both configurations, with the five pinned
  scenario hashes of the T029 refresh (`gravity-lock` 0x7c1eb414..., 
  `soft-drop-moves` 0x133e70c5..., `hold-and-pause` 0x9daf6f8a...,
  `stack-and-clear` 0x04958a77..., `block-out` 0x33069c75...). This is
  section 11, item 18, desktop half; the ARM64 half stays with T016.
- Passed, sessions: seven M3 sessions, each equal to the headless
  reference stream field by field, repeated byte-identically inside the
  run, and replayed identically in the Release build (`m3-release.status`).
  - `m3-das-arr` (seed 20261103, 39 checkpoints): the press tick moves
    (x 3 -> 2 on tick 6); nine charge ticks with no movement; the first
    repeat exactly ten ticks after the press; the next exactly two
    ticks later; repeats fail parked at the left wall; a direction
    switch moves on its own tick (tick 26); a pause after five charge
    ticks freezes the charge - the first post-resume repeat still lands
    on press + 10 (tick 36), where a reset charge would repeat ten
    ticks past the resume instead; left plus right held moves nothing,
    and releasing one leaves the other as a fresh press that moves.
  - `m3-lock-delay` (seed 20261103, 130 checkpoints): the soft-dropped
    T lands on tick 42 (the fall into row 18 observed tick by tick),
    stays active through tick 70, and locks exactly on tick 71 = 42 +
    29, the 30th grounded tick with the landing tick included; soft
    drop scored 1 per cell (18). The Z lands on tick 107, sixteen
    grounded moves follow at ticks 116..176 (columns alternate 5/6),
    records show it alive 31 ticks past its own no-reset lock point
    (136), and the lock lands on tick 201 = 15th reset 172 + 29,
    strictly inside the 16th reset's window (205) - the sixteenth move
    moved the piece without resetting the timer.
  - `m3-bag-20261103`, `m3-bag-31337`, `m3-bag-777` (18 locks each,
    spread placements): the checker rebuilds the piece stream from the
    queue windows (21/24 and 27 pieces) with every overlapping window
    consistent, and each complete block of seven draws is a permutation
    of I O T S Z J L (three full bags per seed). No line cleared, so
    one lock is exactly four settled cells throughout. The 777 session
    adds the hold rules at a bag boundary: the swap drew the queue head
    (T) into play at spawn, the second press changed nothing, the lock
    freed the hold, and the return swap respawned the stashed Z at
    orientation 0 while parking the other piece.
  - `m3-restart` (seed 31337): two hard drops scored 36 and 32 by the
    section 4.6 arithmetic; pause froze the round with the cursor
    reaching RESTART; RESTART reset score, board, hold, and clock
    (tick 5 after 23) with a fresh bag and a distinct seed; QUIT TO
    TITLE returned a zero clock and the title started a third distinct
    bag; a Left held across a restart moved on the fresh round's first
    ticks (x 2), kept repeating to the wall, and stopped on release
    with no leaked rotation, drop, or hold; twelve drops ended the
    round; PLAY AGAIN began another fresh round (score 0, settled 0,
    another distinct bag); the session quit from `menu=title:3`. The
    input log carries two distinct title-started round seeds.
  - `m3-highscore` (four processes over one save directory): run 1
    (seed 999) recorded 202 on a fresh save; run 2 (seed 999 again)
    replayed the round identically outside the storage fields, loaded
    202, saved nothing (an equal score is not a record), and left the
    file unchanged; run 3 (seed 5150) beat the record and the file
    became 212; run 4 (seed 20261103) started from a damaged file
    (binary garbage, preserved as `corrupt-highscore.dat`), logged
    `High score 0 (invalid)`, and saved 196. The driver snapshots the
    record file after every process; every process exited 0 with a
    clean shutdown.
- Passed, regress at this identity: `tests/harness/run.sh` 209 of 209
  (`t015-gate-2026-11-04-harness/`) and `tests/harness/t024.sh` 287 of
  287 in Debug and again in Release
  (`t015-gate-2026-11-04-t024-debug/`, `-t024-release/`), including
  the X11 focus-loss and letterbox sessions.
- Passed, stability (T025 subset): `failures` 17 checks, `restarts` 50
  process restarts carrying one record forward (final record 224, best
  224, weakest 188), and `san` (the headless suites plus one scripted
  session under AddressSanitizer and UndefinedBehaviorSanitizer, clean)
  - all under `t015-gate-2026-11-04-t025/`.
- Passed, performance: the T026 `verify` phase in Release proved every
  benchmark scenario's claim at this source
  (`t015-gate-2026-11-04-t026/`); `notes/performance.md` records no
  unresolved desktop target, so the gate does not claim a pass over an
  open failure. The report's unresolved limits remain device-scope
  items (RG503 frame times, the nested-runner permission layer), which
  the deferred T036 and final acceptance own.
- Passed, reports: the checker verified the required evidence exists -
  the T024 and T025 sections of this report, the T026 final regression
  gate with its unresolved-limits section, and the T027 and T028
  sections of `notes/design-review.md` with the closure recorded.

Mutation proofs that the new pins are live (evidence under
`build/agent-checks/t015-mutation/`, both reverted and rebuilt
afterward; the rebuilt Debug binary hash matches the gate's):

- `kDasTicks` 10 -> 9: the first repeat fires on tick 15 and the
  checker fails `m3-das-arr/das-charge` at exactly that checkpoint.
- `kLockDelayResets` 15 -> 16: the lock waits for the 16th reset's
  window (tick 205) and the checker fails
  `m3-lock-delay/reset-budget-15` (expected 201).

Commands:

```bash
tests/harness/t015.sh
T015_RUN_ID=t015-gate-2026-11-04 tests/harness/t015.sh   # the recorded run
T015_PHASES="sessions check" T015_RUN_ID=<id> tests/harness/t015.sh
python3 tests/harness/check_t015.py build/agent-checks/t015-gate-2026-11-04
```

Findings and open questions:

- One driver defect surfaced and was fixed before the recorded pass:
  `set -u` aborted the regress phase on an unset `T015_SKIP_X11`
  (now `${T015_SKIP_X11:-}`), and the checker first read the
  two-line T025 status files as failures (now it reads the first
  line). Both fixes are in the recorded run's `source.sha256`.
- The section 11 desktop inventory stands covered as follows: items
  1-17 and 19-20 by the headless suites in both configurations (the
  same ctest pass), item 18's desktop half by the hash phase, and the
  gameplay-facing halves of items 9, 10, 12, 13, 14, and 20 by the m3
  sessions above. The ARM64 hash comparison of item 18 and every
  on-device check remain open for T016 and the device gates.
- Real audio output stays unavailable on this host (no sound card);
  the sessions ran against the SDL dummy audio driver, as in every
  earlier round. Device latency and human judgment stay with the
  operator pass.

## T016: AArch64 build in the PortMaster-aligned environment (2026-11-04)

Environment: Docker with the QEMU aarch64 binfmt handler registered.
The image `stackfall-arm64-builder:22.04` from `tools/arm64/Dockerfile`
is the guide's Ubuntu 22.04 ARM64 chroot environment moved into a
container: the distribution gcc 11.4.0, CMake, Ninja, and pkg-config,
with the distribution SDL2 packages installed for their build
dependencies, purged, and replaced by SDL 2.26.2 built from the guide's
pinned tag URL. `tools/arm64/README.md` records the deviations from the
guide's package list. Every phase runs in its own container under
`--platform=linux/arm64`; the Looper labels let a registered cleanup
find a container the script leaves behind.

One command produces and checks the device binary:

```bash
tools/arm64/build-arm64.sh
```

The recorded run is `build/agent-checks/t016-arm64-2026-11-04/`, a
clean configure and build of the whole tree inside the environment
(`rm -rf build-arm64` first). Results, all passed:

- `file build-arm64/stackfall.aarch64`: `ELF 64-bit LSB pie
  executable, ARM aarch64`, interpreter `/lib/ld-linux-aarch64.so.1`,
  built for GNU/Linux 3.7.0.
- `ldd` and `readelf -d` (in `arm64-binary-report.txt`) document the
  runtime dependencies: `libSDL2-2.0.so.0` (the source-built 2.26.2 in
  `/lib`), `libstdc++.so.6`, `libgcc_s.so.1`, `libc.so.6`,
  `ld-linux-aarch64.so.1`, with `libm.so.6` through libstdc++. No
  hidden host dependency: the binary loads only ARM64 libraries.
- `stackfall_tests --seed 987654321` ran twice inside the environment:
  byte-identical output, 338822 checks, 0 failures
  (`arm64-hashes.txt`, `arm64-hashes-rerun.txt`).
- The full `ctest` pass inside the environment: 5 of 5 tests,
  including the SDL platform suites under SDL 2.26.2
  (`arm64-ctest.log`).
- Hash parity, section 11 item 18, ARM64 half: every state-hash line
  of the ARM64 runs equals the desktop Debug run's
  (`desktop-hash-lines.txt` diff `arm64-hash-lines.txt` is empty), and
  the desktop pinned scenario hashes are unchanged from the T015 gate.
- `source.sha256` ties the run to the source tree and commit.

Failure found and fixed before the recorded pass: the first run
(evidence kept in `build/agent-checks/2026-11-04-t016-arm64/`) failed
the ARM64 `ctest` at `stackfall_input_tests` with 6 failures, all on
the virtual pad: `SDL_IsGameController` stayed false and
`SDL_GameControllerOpen` returned null after the exact-GUID mapping was
added. The desktop SDL 2.30.0 accepted the same sequence. Probes
(compiled in the container and on the host) isolated the cause: the
virtual pad carried no USB IDs, so SDL derived its GUID from the pad
name, and the name bytes land in GUID fields SDL 2.26 also reads as a
controller CRC; SDL 2.26 then refuses the mapping it just accepted
(`Couldn't find mapping for device`). Explicit fictional
`vendor_id`/`product_id` values give the pad one stable GUID
(`ff00e0dec7fa000011fa000000007600`) that SDL 2.26.2 and 2.30.0 both
map. The fix is in `src/input_sdl.cpp`; the desktop Debug and Release
suites were rebuilt and re-run green after it. This is developer test
scaffolding only; no game-rule or desktop behavior changed, and the
pinned hashes above prove the core is untouched.

Cross-check against the guide's docker method, recorded 2026-11-04
00:24 in `build/agent-checks/2026-11-04-t016-arm64/
pm-builder-crosstest.txt`: the official builder image
`ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:
aarch64-latest` (Ubuntu 20.04, SDL 2.32.0 from `/usr/local`) also
builds the tree and prints the same five state hashes. That run predates
the virtual-pad fix and exercised the rules suite, which the fix does
not touch.

Host note: the image build and every container phase run with
`--network=host`. On this machine the Docker bridge network drops the
plain-HTTP apt traffic of an emulated ARM64 process, while the host
path works. The game needs no network at build or run time.

Findings and open questions:

- Section 11 item 18 is now closed on both halves: identical pinned
  hashes on desktop (Debug and Release) and inside the ARM64
  environment.
- The ARM64 binary is software-validated only. Nothing here runs on an
  RG503; launch, controls, frame rate, and saves stay with the blocked
  device tasks.
- The launcher and the local PortMaster package are separate work
  (T017, T019); `portmaster/` still holds only its README.

## T044: options screen and settings file gain grid, hold, next, and grayscale (2026-11-04)

Task T044, round M3, the settings and UI follow-up to the completed
T039/T040/T041. Tested in the detached worktree of `cacd666` plus uncommitted
task changes; the evidence below records the commands and artifacts, and the
harness runs wrote their own `source.sha256`.

What landed:

- `include/stackfall/game.hpp`: `kOptionsItemCount` 2 to 6, so the options
  cursor walks SOUND, GHOST, GRID, HOLD, NEXT PIECES, and GRAYSCALE with the
  same wrapping `moveCursor`; no other core state moved, and the pinned
  regression hashes are unchanged (verified below).
- `include/stackfall/storage.hpp` and `src/storage.cpp`: the settings file
  grows to `version=1` plus the five preference lines
  (`sound`, `ghost`, `grid`, `hold`, `next`, `grayscale`; `next=1..5`).
  `SettingsValues` carries the five preferences, `parseSettingsFile` accepts
  the 160 canonical renderings plus the legacy three-line and two-line forms
  (which keep their saved values and receive the new defaults), and
  `SettingsStore::save` writes the whole file through the same atomic write.
  `kSaveFormatVersion` stays 1.
- `include/stackfall/renderer_sdl.hpp` and `src/renderer_sdl.cpp`: the
  options page lays out six rows at `kOptionsMenuY = 144` with the heading
  at 64 and the footer at 456 (everything inside the 960x544 canvas, hint
  glyphs at the unchanged 14 px scale); the renderer mirrors the three new
  preferences (`setShowHold`, `setNextPreviews` clamped to 1..5,
  `setGrayscale`); NEXT draws exactly its count of previews in queue order;
  HOLD off hides the whole Hold group; grayscale swaps the piece palettes
  for seven fixed grays (J 130 .. O 235) whose ghost blends keep the 3:1
  floor, and off restores the color palettes exactly.
- `src/main.cpp`: `SettingsValues prefs` replaces the lone ghost bool; all
  five preferences load, log, and apply at startup; A on a row toggles or
  cycles it and one atomic save writes the whole file; the loop drops every
  hold edge while hold is off, before the round can see it, so no hold swap
  happens and no hold lock is spent.
- Tests and harness: `tests/test_storage.cpp` pins all 160 canonical forms,
  both legacy upgrades (including the acceptance case
  `version=1/sound=off/ghost=off`), and the new damage shapes (next=0/6/05,
  doubled and missing new lines, reordered lines, CRLF, lost newline);
  `tests/test_screens.cpp` walks the six-row wrap and round trip;
  `tests/test_renderer_sdl.cpp` draws all 160 options value combinations
  with pinned glyph counts, the grid-off piece check, the exact-count
  preview checks, the hold-off preview check, and the grayscale checks
  (achromatic fills, distinct shades, 3:1 ghost floor, byte-identical
  restore); `tests/harness/t025_fuzz.py` fuzzes the new canonical form plus
  both legacy forms; the `t025.sh` failure grep covers the four new
  preference names; `check_harness.py` pins the new settings bytes, the
  extended startup log line, and the five preference start lines.
- `docs/SPEC.md` sections 4.7, 5, 6.2, 6.3, and 9 and `README.md` describe
  the six rows and the five preferences.

Verification, under `build/agent-checks/`:

- Passed: clean rebuilds of both configurations with zero warnings
  (`build-t044-clean` Debug, `build-t044-clean-rel` Release; the build logs
  hold no warning or error line), and `ctest` 5 of 5 in each.
- Passed: `stackfall_tests --seed 987654321` in both configurations prints
  byte-identical output with the five pinned scenario hashes unchanged.
- Passed: `tests/harness/run.sh` (`t044-harness`) - all seven sessions green
  through the unchanged four-item title menu, 209 of 209 checker pins,
  including the options-ghost session against the new canonical
  `settings.dat` bytes and startup log line.
- Passed: the T025 fuzz phase (`t044-fuzz`) against the updated fuzz pass -
  the parser loop under ASan/UBSan accepted only canonical or legacy forms,
  and 224 binary cases through the sanitized binary closed clean. The first
  run failed its own oracle on the legitimate legacy three-line
  `sound=on/ghost=off` file (the oracle wrongly required ghost at its
  default); fixed in `t025_fuzz.py` and rerun green.
- Passed, the T044 acceptance sessions (`t044-manual`, keyboard, pad, and a
  legacy directory, seeds 20261104/20261105/20261106, Release binary):
  - The keyboard session toggles all six rows (NEXT selects twice,
    5 -> 1 -> 2), then plays a round in which the C and Left Shift hold
    presses leave the active piece, the queue, and the hold slot untouched
    and never appear as hold edges in the input log, and quits through the
    title menu. The saved file is exactly
    `version=1\nsound=off\nghost=off\ngrid=off\nhold=off\nnext=2\ngrayscale=on\n`.
  - The pad session relaunches on that directory - the startup log reads
    `Settings: sound off, ghost preview off, grid off, hold off, next
    previews 2, grayscale on (ok)` - then the virtual controller walks
    every row, cycles NEXT 2 -> 3 -> 4 -> 5 -> 1 -> 2 (the ring wraps),
    wraps the cursor down past GRAYSCALE back to SOUND, turns HOLD back on,
    and a round proves Y swaps again (`hold=J,locked`), a second press and
    L1 change nothing, per the once-per-piece rule.
  - The legacy session loads `version=1\nsound=off\nghost=off\n` with those
    two values plus grid on, hold on, next 5, and grayscale off, and leaves
    the file untouched until its next save.
  - Screenshot scans: the six options rows sit at y = 144..385 as single
    21 px text lines with the 28 px heading above and the 14 px footer at
    456 (nothing clipped); the grayscale round draws an achromatic board
    with exactly two Next slots filled, the hold band empty, and the
    statistics still labeled; the pad round shows the HOLD label back with
    hold on.
- Passed: `tests/harness/t024.sh` under Xvfb (`t044-t024`), the retained
  agent play and visual review driver, after the change.

Commands:

```bash
cmake -S . -B build-t044-clean -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build-t044-clean
ctest --test-dir build-t044-clean --output-on-failure
cmake -S . -B build-t044-clean-rel -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-t044-clean-rel
ctest --test-dir build-t044-clean-rel --output-on-failure
HARNESS_RUN_ID=t044-harness tests/harness/run.sh
T025_PHASES="fuzz" T025_RUN_ID=t044-fuzz tests/harness/t025.sh
T024_BUILD_DIR=build-rel T024_RUN_ID=t044-t024 xvfb-run -a -s "-screen 0 960x544x24" tests/harness/t024.sh
```

Open questions: none. The physical-device pass and publication stay with
their blocked tasks.

## T017: PortMaster launcher and install layout (2026-11-04)

Task T017, round M4. Sources:
[game specification](../docs/SPEC.md) sections 5, 9, and 10.3 and
[agent testing contract](../AGENT_TESTING.md). Tested in the detached
worktree of `b24643e` plus uncommitted task changes; the run wrote its own
`source.sha256`.

What landed:

- `portmaster/Stackfall.sh`: the launcher, written from the current
  official PortMaster launch template of
  <https://portmaster.games/packaging.html> (reviewed 2026-11-04, template
  block quoted in the run's evidence). Template order kept: control-folder
  discovery over the four candidates, `source control.txt`, the CFW
  modifier `mod_${CFW_NAME}.txt` when the install ships one,
  `get_controls`, `conf/` creation, `cd` to the game folder, the
  `log.txt` redirection through `tee`, the environment exports,
  `pm_platform_helper` on the executable, the run, `pm_finish`.
  Additions over the template, each defensive and commented in the file: a
  guarded `cd`, an explicit binary-name check with a `pm_message` failure
  path, the `stackfall.aarch64` fallback when the device reports another
  architecture name, `$ESUDO chmod +x` on the executable, the game's exit
  status echoed into `log.txt`, and argument forwarding to the game
  (PortMaster passes nothing; a shell run can reach the game's own
  developer options).
- Save paths of section 9: the launcher exports both
  `XDG_DATA_HOME="$GAMEDIR/conf"` and `STACKFALL_SAVE_DIR="$GAMEDIR/conf"`.
  The game resolves the explicit variable first, so the record, settings,
  and the game's own session log land in the port's `conf/`, while the
  launcher's `tee` keeps `log.txt` at the game-folder root: the two sinks
  never share a file.
- gptokeyb, section 10.3: started as `$GPTOKEYB "$EXEC" &` with the
  process name and no mapping file, which is gptokeyb's exit-hotkey-only
  mode (no `-c`, so no keyboard injection; the gamepad stays native SDL
  input and the platform's Select and Start chord is never captured by the
  game). It starts only when the installed PortMaster defines `$GPTOKEYB`
  and ships its `gptokeyb` binary, which is the detectable form of "as
  appropriate to the installed version"; an empty `$GPTOKEYB` would
  otherwise expand to running the game binary itself. `pm_finish` cleans
  the process up.
- `tools/portmaster/install-tree.sh`: stages the section 10.3 layout
  (`Stackfall.sh` plus `stackfall/stackfall.<arch>`) from a built binary,
  refuses a binary of the wrong machine, and stages no runtime files.
- `tests/harness/t017.sh` and `tests/harness/check_t017.py`: the driver and
  the independent checker, described in `docs/AGENT_TESTING.md`.

One command runs the check:

```bash
tests/harness/t017.sh
```

The recorded run is `build/agent-checks/t017-launcher-2026-11-04-final/`
(a clean run from its own configure and build, 167 checks, 0 failures),
re-verified by running the checker again against the evidence directory
alone. The evidence also holds `official-template.sh`, the reviewed
official template verbatim, so the template review can be reproduced
later. Results, all passed:

- Template review: the discovery block is the official template's, byte
  for byte; `control.txt`, the CFW modifier, `get_controls`, `conf/` before
  the run, the `log.txt` redirection, every environment export,
  `pm_platform_helper` before the run, and `pm_finish` after it are all
  present; no `.gptk` mapping file or option argument appears anywhere in
  the launcher.
- Six stub runs: base (exit 0, control folder discovered through
  `$XDG_DATA_HOME/PortMaster`, modifier sourced, `get_controls` called,
  `gptokeyb` argv exactly `['stackfall.aarch64']`, `pm_platform_helper`
  receiving the executable path, game cwd the game folder,
  `XDG_DATA_HOME`/`STACKFALL_SAVE_DIR` at `conf/`,
  `SDL_GAMECONTROLLERCONFIG` equal to the mapping `get_controls` built,
  `LD_LIBRARY_PATH` leading with `libs.aarch64`, one launch adding only
  `conf/` and `log.txt`); argument forwarding (`--frames 12 --seed 7`
  reached the game verbatim); a repeat launch replacing `log.txt` instead
  of appending; an install with no CFW modifier (sourced nothing, ran
  fine); an install defining no `GPTOKEYB` (no background process, the
  launcher says so in `log.txt`, the game still ran); and a port folder
  with no binary (exit 1, a `pm_message` naming the missing file, no
  gptokeyb, no game run).
- `/roms/ports` run: the same launcher at the literal device paths
  (`/roms/ports/Stackfall.sh`, `/roms/ports/stackfall/`, control folder
  discovered at `/roms/ports/PortMaster` through the fallback branch). The
  driver created `/roms` through sudo after registering a marker-checked,
  idempotent cleanup in the Looper cleanup file, and removed the tree when
  the run ended; `/roms` no longer exists on this host.
- Real binary run: the desktop build staged by `install-tree.sh` as
  `stackfall/stackfall.x86_64` and launched by the script under the dummy
  video and audio drivers with `--seed 20261103 --frames 240`. Exit 0; the
  banner, `Architecture: x86_64`, and a renderer line reached
  `stackfall/log.txt`; the game's own startup line resolved its session
  log to `stackfall/conf/log.txt`, which exists and carries the same
  startup block; the launch added only `conf/`, `conf/log.txt`, and
  `log.txt` to the staged tree.
- The five `ctest` suites pass at this source identity.

Failure found and fixed before the recorded pass: the first `/roms` run
failed (evidence kept in the overwritten run directory) because the driver
never created the stub `/roms/ports/PortMaster` folder before writing
`control.txt` into it, so the launcher sourced nothing and exited 1 at the
`cd`. The checker caught it as 5 failures; the driver now creates the
folder and fails loudly if it cannot.

Findings and open questions:

- The launcher is architecture-independent shell, so the stub binary and
  the x86-64 desktop build prove its behavior. The AArch64 binary is
  T016's evidence; launching it on a real RG503, the real PortMaster
  `control.txt`, and the platform exit chord stay with the blocked device
  task (T018).
- The gptokeyb condition is the launcher's detectable proxy for "the
  installed version requires it". Confirm on the device that a current
  ROCKNIX PortMaster both defines `$GPTOKEYB` and needs the process; if
  the platform delivers the chord itself, the launcher's guard keeps it
  from starting anything.
- `port.json`, `gameinfo.xml`, the screenshot, and the player readme are
  T019's release-archive work; `portmaster/` holds the launcher and its
  readme.

## T045: options-expansion harness coverage and six-settings documentation (2026-11-04)

Task T045, the review repair of the M3 window: the harness ran seven
sessions and none drove GRID, HOLD, NEXT PIECES, or GRAYSCALE end to end,
and three documents named six preferences but called them five. Tested in
the detached worktree of `d67272d` plus uncommitted task changes; every
harness run below wrote its own `source.sha256`.

What landed:

- Eleven scripts, three stage manifests, and two settings templates under
  `tests/harness/scripts/` and `tests/harness/settings/`:
  `options-six-keyboard` and `options-six-pad` walk the six rows on both
  inputs, select each of the four new rows, and wrap the cursor past
  GRAYSCALE back to SOUND; `hold-off` turns HOLD off, proves C and Left
  Shift (or Y and the left shoulder) move nothing while the Hold group
  draws nothing, then turns HOLD back on and proves the once-per-piece
  rule intact; `grayscale` captures a play scene with settled cells, an
  active piece, a ghost, and previews in both palettes; the four
  `options-relaunch-*` stages each toggle one preference and quit; the
  two `options-legacy-*` stages start from staged legacy files; and the
  five `next-count` stages share one script that advances the preview
  ring by one, so the shared save directory walks 5 -> 1 -> 2 -> 3 -> 4
  -> 5 across five processes.
- `tests/harness/run.sh`: fourteen sessions through the new `run_pair`
  helper (one reference-and-game pair) and `run_stages` (one game process
  per manifest line over one shared save directory, `settings=<name>`
  stages a legacy file, and every stage snapshots the settings bytes
  before and after); `source.sha256` now covers the scripts, manifests,
  and settings templates.
- `tests/harness/reference_replay.cpp`: `--settings FILE` loads the hold
  preference at startup, the loop drops hold edges exactly as the
  platform layer does, and a select on options row 3 flips the mirror -
  without this the reference cannot replay any HOLD-off session.
- `tests/harness/check_harness.py`: 209 to 660 checks. The staged
  sessions compare every stage's record stream, the exact settings bytes
  each stage started from and left behind, and the startup log line of
  every relaunch; the hold sessions prove the active piece, queue, board,
  and counters are untouched by ignored hold inputs; the grayscale
  session reads the captured frames with a dependency-free BMP reader and
  checks every preview and settled cell against the fixed gray palette
  while the text, frame, and ghost keep their specification colors; the
  next-count session proves the five observation streams byte-identical
  and the five screenshots hold exactly 1 to 5 previews.
- `README.md`, `docs/SPEC.md` (sections 4.7, 5, 6.1, 6.3, and 9), and
  `docs/AGENT_TESTING.md` now say six preferences everywhere, name the
  six rows and their keys, and describe the staged manifests.

Verification, under `build/agent-checks/`:

- Passed: `tests/harness/run.sh` (`t045-final`, recorded 2026-11-04
  03:33 UTC) - all fourteen sessions green, 660 of 660 checker pins, 527
  evidence files, evidence in `build/agent-checks/t045-final/`. An
  earlier full pass at the same sources is `t045-new`.
- Passed: `ctest` 5 of 5 in Debug (`build`, also rerun inside the
  harness) and Release (`build-rel`).
- Passed: the staged relaunch chain ends with exactly
  `version=1\nsound=on\nghost=on\ngrid=off\nhold=off\nnext=1\ngrayscale=on\n`
  (the single NEXT select wraps 5 to 1); every intermediate stage starts
  from the bytes the earlier stage wrote.
- Passed: the legacy three-line stage logs both saved values plus the
  four defaults and rewrites the file in the six-line form; the legacy
  two-line stage changes nothing and the file still holds the exact
  legacy bytes at exit - a read never rewrites it.
- Screenshots, converted to PNG and inspected one by one: the options
  screen shows six labeled rows with the marker on the visited row and
  the wrap back to SOUND (`options-grid-row`, `options-wrapped-to-sound`,
  both inputs); the hold-off round shows no HOLD label or held-piece box
  anywhere and the statistics directly below the empty corner, while the
  hold-on round shows the boxed held piece (`hold-group-absent`,
  `hold-group-present`); the grayscale round renders every piece, stack
  cell, and preview in grays with the text and outlines unchanged, and
  the same round shape in color shows the distinct piece hues back
  (`gray-scene`, `color-scene`); the five next-count stages show exactly
  1, 2, 3, 4, and 5 filled preview slots (`next1-playing-start`,
  `next3-after-lock`, `next5-after-lock` shown here).

Commands:

```bash
HARNESS_RUN_ID=t045-final tests/harness/run.sh
ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
```

Open questions: none. The physical-device pass and publication stay with
their blocked tasks.

## T046: shared bounded file loading between the stores (2026-11-04)

Task T046, refactor-review item 1: `HighScoreStore::load` and
`SettingsStore::load` repeated the directory check, the file-status check,
the regular-file check, and the bounded read. Tested in the detached
worktree of `62476cf` plus uncommitted task changes; the worktree carries
no commit for this change, so the tested sources are
`src/storage.cpp` `b0e6a7d763cfba3dc746a35fc162af764e5ff8fc4f5d1d6793afc07f5b9cb0cf`
and `tests/test_storage.cpp` `1d808717a2ece5369b21f7b66661d7a371d611183b3cbb86cb241b0450d84371`.

What landed:

- `src/storage.cpp`: one private helper, `readSaveFile(directory,
  fileName)`, returns a `SaveFileBytes` (a `SaveStatus` plus the bytes) and
  owns the absent-directory, missing-file, not-a-regular-file, and
  unreadable mappings beside the `readSmallFile` cap. Both `load` methods
  call it and keep their own parser, their own default values, and their
  own `catch (...)` recovery boundary, so a parser exception still cannot
  escape the store that threw it. No store framework, no template, and no
  change to the public header.
- `tests/test_storage.cpp`: `testSharedFileLoading` pins the shared
  mapping for both stores at once - no directory, fresh install, a
  directory at a file's name, a file with the read bit removed, an
  oversized file, and a good record beside a good, then legacy, settings
  file - including that a read changes nothing on disk and that both
  files load again once the read bit returns. The unreadable case skips
  under root, as the write-side case already does.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in Debug (`build`) and Release (`build-rel`),
  339,394 checks and zero failures in the headless suite.
- Passed: a differential probe. A driver that walks 34 on-disk
  conditions - both files valid, either one empty, partial, foreign,
  duplicated, NUL-bearing, binary, oversized, a directory, or unreadable,
  an absent save directory, a read-only save directory, and the six
  legacy settings renderings beside a good record - was linked once
  against `HEAD:src/storage.cpp` and once against the changed file.
  The two outputs are byte-identical, so every status and every default
  is unchanged. The same probe under AddressSanitizer and
  UndefinedBehaviorSanitizer reports nothing and stays identical.
- Passed: `tests/harness/run.sh` (`t046-storage-refactor`) - all fourteen
  sessions green, 660 of 660 checker pins, 132 evidence files. The three
  staged persistence sessions ran through the shared read: the four
  relaunch stages, both legacy stages (the two-line file still holds its
  exact legacy bytes at exit), and the five next-count stages.
- No change to gameplay, pinned hashes, save formats, input timing, or
  any log or observation string: the harness compares both observation
  streams field by field and pins the scenario hashes, and `storage.cpp`
  writes no log line of its own.

Commands:

```bash
ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
HARNESS_RUN_ID=t046-storage-refactor tests/harness/run.sh
git show HEAD:src/storage.cpp > storage_old.cpp
g++ -std=c++17 -Wall -Wextra -I include storage_old.cpp probe.cpp -o probe_old
g++ -std=c++17 -Wall -Wextra -I include src/storage.cpp probe.cpp -o probe_new
g++ -std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I include src/storage.cpp probe.cpp -o probe_san
```

The probe driver and its three outputs are kept beside this run's evidence
in `build/agent-checks/t046-storage-refactor/storage-probe/`.

Open questions: none. The remaining refactor-review items stay with their
own tasks; this change touches the two loads only.

## T047: scoped owners for the platform resources (2026-11-04)

Task T047, refactor-review item 2 with its second validation pass: the
normal shutdown, the script-load failure, and the three startup failures
repeated file, controller, joystick, renderer, window, SDL, and log
cleanup by hand. Tested in the detached worktree of `b1739ba` plus
uncommitted task changes; the worktree carries no commit for this change,
so the tested sources are
`src/main.cpp` `f72abd889237194169e1915e64ce2f24034fef6f17f8a34e0c44546b8040ef3c`
and `tests/harness/t025.sh` `47f662a6ce9b3f9eb8b6f96225632e964ab788f8b6fa86f4bdc736c3a32f4131`.
The comparison side is `HEAD:src/main.cpp`
`8b26df95f578770009689ce196dee6c263c3479a0615d69f81ef00a4a72837f8`.

What landed:

- `src/main.cpp`: six scoped owners in main, and no hand-written cleanup
  block anywhere. `SessionLog` opens the sink first and closes it last;
  `SdlLibrary` calls `SDL_Quit` and is declared before every SDL owner, so
  the audio device, the virtual pad, the controller set, the renderer, and
  the window all release first; `SdlWindow` and `SdlRenderer` wrap the
  creation calls that already logged every attempt; `ControllerSet` closes
  the handles it opened; `VirtualController` owns the attached virtual
  device and its joystick handle, with an idempotent `reset()` the script
  `detach` command calls, so no exit path can close the pad twice and a
  pad whose `SDL_JoystickOpen` failed no longer stays attached;
  `SessionFile` owns the three evidence streams and prints the same
  warning per path that cannot open. `AudioDriver` keeps its own device
  and now closes it through destruction order alone - declared after every
  other owner, so its device still stops before `SDL_Quit`, as the second
  validation pass requires - and the session log still writes the
  clean-shutdown line after `SDL_Quit`, because the owners sit in an inner
  block of main and the log owner sits outside it. A startup failure is
  now one `return`.
- `tests/harness/t025.sh`: the `san` phase gains the exit paths the
  scoped cleanup owns, all under `ASAN_OPTIONS=detect_leaks=1`: a forced
  renderer failure (exit 1), a script the parser rejects with every
  evidence stream and the virtual pad open (exit 1), and the pad
  scene-cycle with its mid-session detach (exit 0, and the removal must
  still be logged). Until now the phase held one clean scripted session,
  so a future owner that stops releasing its resource on an exit path
  would not have failed it.

Verification, under `build/agent-checks/`:

- Passed: a differential probe (`t047-owners/`). Seven cases - a plain
  bounded startup, both scene cycles (the pad one detaches mid-session), a
  rejected script with every evidence stream open, a forced renderer
  failure, a forced video failure, and `--help` - ran one game process per
  side against HEAD and the candidate. Exit status, stdout, stderr,
  `save/log.txt`, the input log, and the observations are identical in
  every case after folding out the run's own directory path and the
  unseeded session seed.
- Passed: `tests/harness/run.sh` (`t047-harness`) - all fourteen sessions
  green, 660 of 660 checker pins, both observation streams compared field
  by field against the headless reference.
- Passed: `tests/harness/t024.sh` under Xvfb (`t024-2026-11-04-060320`) -
  288 of 288 checks, real X11 input, focus loss, and controller removal.
- Passed: `tests/harness/t025.sh` phases `failures` (17 checks, including
  renderer failure, software fallback, controller removal, and plain
  startup), `restarts` (50 process restarts carrying one record forward,
  every one exiting 0 through a logged clean shutdown), `cycles` (52
  in-process restart and menu cycles at real-time pacing, one controller
  open event, no removals, no growth in memory or threads), and `san`
  (ctest, one scripted session, and the three new exit-path cases; both
  sanitizers and leak detection silent).
- Passed: the sanitizer build of HEAD shows nothing on the same three
  exit paths either (`t047-owners/asan/baseline-head/`), which is what
  the review predicted - this change removes repeated cleanup, not a
  demonstrated leak - and the new phase cases keep it that way.
- Passed: `ctest` 5 of 5 in Debug and Release, 339,399 headless checks and
  zero failures, zero compiler warnings, and the five pinned replay
  hashes identical across Debug and Release.

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
HARNESS_RUN_ID=t047-harness tests/harness/run.sh
T025_RUN_ID=t047-t025 T025_PHASES="failures san restarts" tests/harness/t025.sh
T025_RUN_ID=t047-t025-cycles T025_PHASES="cycles" tests/harness/t025.sh
xvfb-run -a -s "-screen 0 960x544x24" tests/harness/t024.sh
```

Open questions: none. The remaining refactor-review items (script
extraction, the CMake target, and the scene-name switch) stay with their
own tasks. One handoff note for T048: `ScriptContext` now carries a
`VirtualController*` instead of the raw joystick handle and device index,
so moving the script driver out of `main.cpp` takes the pad owner - or a
small interface to it - along.

## T051: one scene-name mapping (2026-11-04)

Task T051, refactor-review item 6: `main.cpp` kept a private `sceneName`
switch whose body was identical to the public `observationSceneName`
formatter in `observe.cpp`. Tested in the detached worktree of `c7affac`
plus uncommitted task changes; the worktree carries no commit for this
change, so the tested sources are `src/main.cpp`
`b61b4d7e7b8dd96580cd1485ccbde0a371b263d85458b7cee3767ee79dab172f` and
`include/stackfall/observe.hpp`
`35afd153bc672120c55c6d2d04f2af879b4c277abbcad3c5048371a83e8d75d1`.

What landed:

- `src/main.cpp`: the private `sceneName` function is gone. The four
  scene-transition call sites - the `Scene a -> b` session-log line and
  the `scene a -> b` input-log line, each naming the old and the new
  scene - now call `stackfall::observationSceneName`, reached through
  the file's existing using-declaration list. `observe.hpp` was already
  included; no new module.
- `include/stackfall/observe.hpp`: one comment sentence records that the
  platform session log prints these names on its transition lines.
- `src/observe.cpp`: unchanged. One Scene-to-name mapping remains in the
  application sources. The `sceneName` switch in
  `tests/test_regression.cpp` stays: it prints different strings
  ("pause-controls", "game-over") for the headless test report and was
  never the duplicate this task removes.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in Debug (`build`) and Release (`build-rel`),
  zero compiler warnings, and `stackfall_tests --seed 987654321`
  byte-identical across both configurations with the five pinned replay
  hashes unchanged.
- Passed: a before-and-after harness pair. `t051-baseline` ran on the
  unchanged tree first, then `t051-harness` ran after the change; both
  passed all fourteen sessions with 660 of 660 checker pins. Every
  evidence file of the two runs - each session log, both observation
  streams per session, every input log, and every screenshot, 527 files
  per run - is byte-identical once the run id inside recorded paths is
  normalized. The two expected exceptions are `source.sha256`, which
  names the changed sources, and the plain-startup log, which prints a
  fresh random session seed because that run passes no `--seed`.
- The compared logs cover every scene name - boot, title, controls,
  options, playing, paused, pausecontrols, and gameover - on both the
  session-log and the input-log transition lines, so the recorded scene
  names are proven byte-identical, not merely unexercised.

Commands:

```bash
HARNESS_RUN_ID=t051-baseline tests/harness/run.sh
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
HARNESS_RUN_ID=t051-harness tests/harness/run.sh
```

Open questions: none. The remaining refactor-review items (script
extraction and the CMake target) stay with their own tasks.

## T048: developer scripts and checkpoint capture in one module (2026-11-04)

Task T048, refactor-review item 3 with its second validation pass:
`src/main.cpp` held the script driver, the virtual pad, the checkpoint
capture, and the three evidence streams, and `runMainLoop` took 18
parameters. Tested in the detached worktree of `6a34283` plus uncommitted
task changes; the worktree carries no commit for this change, so the tested
sources are `src/main.cpp`
`89610d42bf8b0a49b0e0a6596c87ff73953cb82c2d5c864c38632098d88536e9`,
`src/developer_session.cpp`
`d8d3243a1b76e9c02db935ee9e95b9125054dea426c2acfc491e909fc830224a`,
`include/stackfall/developer_session.hpp`
`a5c8ff4ceacd200ca059936410e32c0bc4e9881ecce8b28dbb9fa3fc3ccbcbcf`, and
`CMakeLists.txt`
`8c47e16402cf27e12cfbe1f971b580e3b7465903d5b0b713973a110e10d2122f`. The
comparison side is `HEAD:src/main.cpp`
`b61b4d7e7b8dd96580cd1485ccbde0a371b263d85458b7cee3767ee79dab172f`, the
source T051 tested. One comment line of the header changed after the runs
below (the member-order note; the hashes name the committed file), so every
behavioral comparison ran on sources identical to these apart from that
comment.

What landed:

- `include/stackfall/developer_session.hpp` and
  `src/developer_session.cpp` (new): `stackfall::DeveloperSession` owns the
  developer automation of one session and exposes it as the review asked,
  input injection on one side (`attachPad`, `loadScript`, `pumpScript`) and
  capture on the other (`captureCheckpoints`). The pad owner
  (`VirtualController`) and the evidence-stream owner (`EvidenceFile`, the
  old `SessionFile`) moved with it and are the module's own parts, the way
  `audio_sdl.hpp` keeps its three. The script binds the two application
  objects its `restart` and `fixture` commands act on - the round and the
  cue detector - and nothing else; no context carries the renderers, the
  stores, or the audio. Every line the module prints reaches stdout or
  stderr and the session log main opened (`SessionLog::file()`), which it
  never closes, so log.txt keeps its bytes.
- `src/main.cpp`: 2,062 lines down to 1,508. The loop calls
  `pumpScript()` before its event poll and `captureCheckpoints()` after its
  render, and keeps the accumulator, the tick batch, the frame limiter, and
  the render call where they were. `runMainLoop` takes 13 parameters, down
  from 18: the script pointer, the script context, both evidence streams,
  and the screenshot directory became the one `DeveloperSession&`, whose
  streams the loop reads through `inputLog()` and `frameTimes()`. The
  developer session is declared after the controller set, so its virtual
  pad still releases before the controller handles close, as T047 ordered.
- `CMakeLists.txt`: a `stackfall_developer` static library beside the
  input, render, and audio ones, linking the core plus those three and SDL2
  through the same imported-target block its neighbours use. The
  executable links it; `stackfall_core` still carries no SDL and the test
  suites link exactly what they did.
- `tests/harness/reference_replay.cpp` is untouched: its parser, key table,
  and pad table stay independent of the module they check, and only the
  bounded count rule of `stackfall/script.hpp` remains shared.

Verification, under `build/agent-checks/`:

- Passed: `ctest` 5 of 5 in Debug (`build`) and Release (`build-rel`), zero
  compiler warnings, 339,399 headless checks with zero failures, and
  `stackfall_tests --seed 987654321` byte-identical across both
  configurations with the five pinned replay hashes of
  `tests/harness/check_t015.py` unchanged.
- Passed: a differential probe (`t048-probe/run/`). Ten cases - every
  script command in one session (keyboard and pad presses, waits, a forced
  70 ms stall, a labeled fixture, a same-seed restart, checks with
  screenshots, a shot, a mid-session detach, pad commands after the
  detach, echo markers, the clean quit), a keyboard-only cycle, checks
  with no screenshot directory, a fixture command with no fixture
  directory, a script the parser rejects on its first line, a script whose
  last line is malformed, a script path that does not exist, the
  `--virtual-controller` option alone, a plain bounded startup, and a
  real-time paced run with the frame-time record open - each ran one game
  process per side against the pre-change binary and the candidate. Exit
  status, stdout, stderr, `save/log.txt`, the input log, the observations,
  and every screenshot are identical in all ten after folding the run's
  own directory out of the recorded paths; the paced case compares the
  frame-time record by line count and whole-microsecond format, because
  its values are wall-clock measurements.
- Passed: a before-and-after harness pair. `t048-baseline` ran on the
  unchanged tree first, then `t048-harness` ran after the change; both
  passed all fourteen sessions with 660 of 660 checker pins. Every
  evidence file of the two runs - 527 per side: each session log, both
  observation streams per session, every input log, and every screenshot -
  is byte-identical once the run id inside recorded paths is normalized.
  The two expected exceptions are `source.sha256`, which names the changed
  sources, and `plain-startup/run.log`, which prints a fresh random session
  seed because that run passes no `--seed`. The baseline copy was deleted
  afterwards only to keep the evidence disk under its limit; this section
  records the comparison, and `t048-harness` keeps the candidate side.
- Passed: `tests/harness/t024.sh` under Xvfb (`t024-2026-11-04-065126`) -
  288 of 288 checks, real X11 input, focus loss, and controller removal.
- Passed: `tests/harness/t025.sh` phases `failures` (the forced-failure
  matrix, including the renderer failure and the software fallback),
  `restarts` (50 process restarts carrying one record forward, every one
  exiting 0 through a logged clean shutdown), and `san` (ctest, a scripted
  session, and the scoped-cleanup exit paths under both sanitizers with
  leak detection on: `render-fail` and `script-reject` exit 1, `pad-detach`
  exits 0, and the rejected script's report lands in stdout and log.txt
  through the module's own dual sink).

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
HARNESS_RUN_ID=t048-baseline tests/harness/run.sh
HARNESS_RUN_ID=t048-harness tests/harness/run.sh
build/agent-checks/t048-probe/probe.sh \
  "$PWD/build/agent-checks/t048-baseline-bin/stackfall-debug" \
  "$PWD/build/stackfall" "$PWD/build/agent-checks/t048-probe/run"
xvfb-run -a -s "-screen 0 960x544x24" tests/harness/t024.sh
T025_RUN_ID=t048-t025 T025_PHASES="failures san restarts" tests/harness/t025.sh
```

Open questions: none. The remaining refactor-review items stay with their
own tasks: T049 (preference application, optional) and T050 (the SDL CMake
target). One handoff note for T050: `stackfall_developer` repeats the same
imported-target block as its four neighbours, so the one-variable
resolution that task plans now covers five blocks.

## T050: the SDL CMake dependency resolved once (2026-11-04)

Task T050, refactor-review item 5 with its second validation pass:
`CMakeLists.txt` repeated the `if(TARGET SDL2::SDL2)` imported-target check
and its legacy variable fallback five times - for `stackfall_input`,
`stackfall_render`, `stackfall_audio`, `stackfall_developer` (the block
T048 copied), and the executable. Tested in the detached worktree of
`252473f` plus uncommitted task changes; the worktree carries no commit for
this change, so the tested source is `CMakeLists.txt`
`471d9d5869ce455e33b4a7fc1fd596617b0f9c73b905ba257a2dff71baa1adb5`
(baseline `8c47e16402cf27e12cfbe1f971b580e3b7465903d5b0b713973a110e10d2122f`,
the file T048 tested). No C++ source changed, so gameplay, saves, input
timing, and the log and observation formats carry their existing proofs.

What landed:

- `CMakeLists.txt`: one resolution block directly under
  `find_package(SDL2 REQUIRED)`. When the package exports `SDL2::SDL2`,
  the `STACKFALL_SDL` variable holds that target. When it does not, one
  `stackfall_sdl2` interface target wraps the legacy
  `SDL2_INCLUDE_DIRS` and `SDL2_LIBRARIES` variables, and the variable
  holds that target instead - a fallback only when needed, as the review's
  second pass asks, not a wrapper for every configuration.
- The four platform libraries link
  `stackfall_core ${STACKFALL_SDL}` in one line each and keep their
  existing comments; nothing else in their blocks changed.
- The executable states no SDL link of its own. It receives SDL2 through
  the public dependencies of those libraries, as the review proposed. The
  stale "Only this target links SDL2" comment is replaced by one that says
  so. `stackfall_core` and the two headless executables
  (`stackfall_tests`, `stackfall_reference`) keep no SDL dependency.

Verification, under `build/agent-checks/t050/`:

- Passed: clean Debug and Release builds. Both directories were removed
  and reconfigured from scratch; the compile logs (`build-debug.log`,
  `build-release.log`) hold zero warnings, and `ctest` passed 5 of 5 in
  both configurations (`ctest-debug.log`, `ctest-release.log`).
- Passed: the pinned replay hashes. `stackfall_tests --seed 987654321`
  output, captured from the unchanged tree before the edit, is
  byte-identical after it in Debug (`baseline-seed-debug.txt` vs
  `candidate-seed-debug.txt`), in Release (`baseline-seed-release.txt` vs
  `candidate-seed-release.txt`), and in the legacy-config build below.
- Passed: the core tests have no SDL dependency. `ldd` lists no libSDL2
  for `stackfall_tests` or `stackfall_reference`, and the core test link
  line carries no SDL entry, while the game binary lists
  `libSDL2-2.0.so.0` through the transitive link alone.
- Passed: the game binary runs on that transitive link - smoke runs under
  the dummy video and audio drivers exit 0 with the logged "Clean
  shutdown" (`smoke/`, `smoke-legacy/`).
- Passed: the legacy package path, as far as this host allows. The host
  `libsdl2-dev` 2.30 config always defines `SDL2::SDL2`, so no genuine
  variables-only package is installed. A synthetic
  `legacy-prefix/SDL2Config.cmake` mimics one: no imported target, only
  `SDL2_INCLUDE_DIRS` and `SDL2_LIBRARIES` pointing at the real host SDL
  files. Against it (`legacy-build/`), the project configures, builds with
  zero warnings, passes `ctest` 5 of 5 (`ctest-legacy.log`), replays the
  seed byte-identically, and passes the same `ldd` and smoke checks. The
  graphviz exports prove which branch ran: `graph-legacy.dot` shows the
  `stackfall_sdl2` interface target carrying SDL, `graph-target.dot` shows
  `SDL2::SDL2` alone and no `stackfall_sdl2`.
- Unavailable: a real vendor legacy SDL2 package. Only its synthetic
  equivalent ran; a distribution that truly ships one (SDL 2.0.8-era
  configs) remains untested.
- The ARM64 environment needs no fallback either: its guide-pinned
  source-built SDL 2.26.2 and the docker cross-check's SDL 2.32.0 both
  export `SDL2::SDL2` (task T016), so `STACKFALL_SDL` resolves to the
  imported target there too. The native build task re-verifies on its next
  run.

Commands:

```bash
ctest --test-dir build --output-on-failure   # baseline, before the edit
ctest --test-dir build-rel --output-on-failure
rm -rf build build-rel
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build && ctest --test-dir build --output-on-failure
cmake -S . -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
cmake -S . -B build/agent-checks/t050/legacy-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSDL2_DIR=$PWD/build/agent-checks/t050/legacy-prefix
cmake --build build/agent-checks/t050/legacy-build
ctest --test-dir build/agent-checks/t050/legacy-build --output-on-failure
```

Open questions: none. The refactor-review items are now all landed or
downgraded: T046 (storage loading), T047 (resource owners), T051
(scene-name mapping), T048 (developer module), and this task; T049 stays
optional by the second validation pass.

## T049: one preference path for startup, shortcuts, and options (2026-11-04)

Task T049, refactor-review item 4 with its second validation pass. The
pass downgraded the item to optional: with two preferences, startup and
both toggle paths kept their consumers in step and no synchronization
defect existed, so a new owner could add more machinery than it removes.
The review names the resume condition itself: revisit when settings are
added. Task T044's options expansion had added four settings since, so
the platform carried six preferences and three change routes - startup,
the M-key and controller-Select shortcut, and six options rows - each
setting its consumers by hand. Tested in the detached worktree of
`a09a54b` plus uncommitted task changes; the tested sources are
`src/main.cpp`
`74890e61fe0ece395aad06e3518928a5324c909be6a043827a9085af949cf2e2`,
`include/stackfall/storage.hpp`
`b23c1a9afa7e96f5e325d42e161d1cbc7568be3ef52d1097ee3ba9c576869375`, and
`include/stackfall/game.hpp`
`203419a3495a61a759df90e2d552edbe622b57af9aad23bfa933d8e45ab7d85c`.

What landed:

- `src/main.cpp`: one `applyPreferences(prefs, audio, renderer)` function
  is now the only place that maps the six preference values onto their
  owners - the audio mute and the renderer's option gates. Startup calls
  it right after the settings load, and every change route flips its
  value in `prefs` first and then calls the same function. A future
  preference needs one field, one line here, and one toggle function.
- `toggleSound` no longer flips the audio mute and reads the value back:
  it flips `prefs.soundOn` like every other route, so no route can leave
  the file value and the mute disagreeing. `savePreference` stays the one
  persistence path every route already shared, so each change still
  writes the whole settings file at once.
- The preference value is the existing `SettingsValues` of
  `include/stackfall/storage.hpp`. No new struct, no settings framework,
  and no change to the rules core: `game.hpp` and `storage.hpp` carry
  comment fixes only (the set grew from five to six), and the options
  screen still reads its row labels from the renderer alone.

Verification, under `build/agent-checks/t049/`:

- Passed: `ctest` 5 of 5 in Debug (`build`) and Release (`build-rel`).
  The seed replay `stackfall_tests --seed 987654321` is byte-identical
  between the two builds; the core sources changed in comments only.
- Passed: `tests/harness/run.sh` (`HARNESS_RUN_ID=t049-harness`) - 660 of
  660 checker pins over all fourteen sessions, including the options and
  persistence sessions this task names: `options-ghost`,
  `options-six-keyboard`, `options-six-pad`, `hold-off`, `grayscale`, the
  four staged relaunches of `options-relaunch`, both `options-legacy`
  stages, and the five `next-count` stages, each compared field by field
  against the headless reference.
- Passed: the T049 probe (`probe.sh`, `check_probe.py`, 42 checks) over
  the three acceptance shapes the pinned sessions leave open:
  - Consistency. A save directory seeded with `sound=off`; startup
    applies the mute; the M key unmutes through the shortcut route; the
    options SOUND row then shows ON - the pixel census of the row band
    reads 1152 pixels for ON and 1251 for OFF with the marker on the row
    in both shots - and one select mutes again through the options row.
    The input log carries each change exactly once with `status=ok`, no
    other preference line appears, and the file ends at the canonical
    `sound=off` rendering. A relaunch over the same directory restores
    the value, and a stage that only opens Options and leaves writes
    nothing: no preference save line, `settings.dat` byte-identical, and
    the same OFF label pixels.
  - Missing audio. With `SDL_AUDIODRIVER=stackfall-missing-driver` the
    subsystem line reports the failure, both change routes still flip,
    apply, and save, the cue summary reports the count without a device,
    and the session exits 0 through a clean shutdown.
  - Immediate persistence. A session toggles GHOST off, then a SIGKILL
    ends it during a long idle with no clean shutdown. `settings.dat`
    already holds the canonical `ghost=off` rendering, so the toggle
    tick's own save wrote it, not an exit path.
- Passed: baseline equivalence. A binary built from `git archive HEAD`
  ran the consistency stage with the same seed and save seed file: the
  per-tick input log, the observation stream (shot paths stripped), the
  stdout log (paths normalized), and the written settings file are all
  byte-identical to the candidate's, so the refactor changed no log or
  observation format.

Commands:

```bash
ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
HARNESS_RUN_ID=t049-harness tests/harness/run.sh
build/agent-checks/t049/probe.sh
```

Open questions: none. The refactor-review list is closed: T046, T047,
T048, T050, T051, and this task landed; every other item kept its
documented no-change or downgrade.

## T052: the record persists when a round is abandoned, closed, or killed (2026-11-04)

Task T052, owner approved 2026-11-04. Before the change the record saved
only on the tick the scene became game over (the `lastScene` check in
`src/main.cpp`), so Quit to Title, a window close, or a power loss
discarded a running best; a live abandon session at score 68 wrote no
`highscore.dat`. Tested in the detached task worktree of `dbb5dcc` plus
uncommitted task changes; the tested sources are `src/main.cpp`
`0281b97915c831f79385d452b12401803bb5332fa79c1ee734c6bf47d93482a8`,
`tests/harness/reference_replay.cpp`
`9f4525b76da25a497ef1f275a59ae608852a545f9c100b6c35cd16825ef65493`,
`tests/harness/run.sh`
`721b8db0aa065f9766e03ab69355423ab4a775d0c59416d0d198ac695859c0a2`,
`tests/harness/check_harness.py`
`dc68f8a94f05c35418957bf6982a4dd5c18218118c57e85ec4ad38fce5b6b9a1`,
`tests/harness/scripts/record-abandon-save.script.txt`
`a170999622e6127d7ef9793a18d548d42b8993c73f276c6892e27b97f1af0cc4`,
`docs/SPEC.md`
`edfbda480323288157ca1d046b9ad7f22f7a3493d6cd97e930d9faaf4aac809f`, and
`docs/AGENT_TESTING.md`
`85adcd4bacb52615f6134ce6a0c7e237f2ee1f3deada187dda3fbf82398a5684`.

What landed:

- `src/main.cpp`: the rules core is untouched. After every simulated tick
  the platform layer compares `game.score()` with a locally tracked
  `savedRecord` - seeded from the load that opened the session - and, the
  moment the score passes it, writes it through `HighScoreStore` (the
  atomic temp+fsync+rename write) and raises `savedRecord`. Between rounds
  only - at game over, Quit to Title, or Restart, detected per tick from
  the scene and the round's tick count - it feeds the best record back
  with `game.setHighScore(savedRecord)`, never mid-round, so `endRound`'s
  `newRecord` flag still compares against the record the round began with
  and Play Again compares against the best so far. The existing game-over
  save path is unchanged; a session with no save directory writes nothing
  and its in-session feedback still works. New evidence lines only:
  `Record <score> saved to <path>` in the session log (the failure line
  stays `Saving the high score <score> failed: <status>`, so the T025
  contention grep still catches a failed record write) and
  `record save=<score> tick=<tick> status=<status>` in the input log.
- `tests/harness/reference_replay.cpp`: the reference mirrors the platform
  persistence, the same way it mirrors the HOLD gate. `--high-score FILE`
  seeds its record from the session's `highscore.dat` (record 0 when the
  file is absent), its in-memory record rises with every score that passes
  it, and the feedback runs at the same round boundaries, so every
  checkpoint's `high` field still names what the application must show -
  including titles reached by abandoning or restarting a scoring round.
  The mirror never writes the record file.
- `tests/harness/run.sh` + the new session
  `tests/harness/scripts/record-abandon-save.script.txt` (keyboard, seed
  20261103): the round scores 36 and 68 through two hard drops, pauses,
  and leaves through QUIT TO TITLE with no game over. The checker
  (`tests/harness/check_harness.py`) asserts `highscore.dat` holds
  `high_score=68`, the input log carries `record save=36 tick=6` and
  `record save=68 tick=14` - both before the `scene paused -> title` tick
  43 - no game-over save line exists, and the title after the abandon
  shows `high=68` while the abandoned round's counters reset. `run.sh`
  passes the reference `--high-score "$pair_save/highscore.dat"`.
- Checkers refreshed to the approved persistence semantics, none of them
  weakened: `next-count` compares its five stages outside the two
  storage-dependent fields (`strip_storage_fields`, the pattern the T015
  high-score session already used) while every stage's own reference
  stream still proves those two for exactly the file it loaded, and five
  new pins carry the persistence itself (stage 1 loads 0, stages 2-5 load
  the 36 the abandoned round persisted). `check_t024.py` re-pins
  `t024-shapes-keyboard`: the seven-shapes round reaches 229 and is then
  dropped by the pause-menu RESTART, so the restarted round's 194 game
  over is not a record and the file holds the abandoned 229 - pinned at
  `after-t-lock`, `restart-first-frame`, both game overs, and the file.
  `t025.sh` hard-kill accepts the standing record: since a killed run now
  leaves a canonical mid-round best, the verify run that loads it and
  finishes below it saves nothing, and that is checked instead of a
  mandatory save line.
- `docs/SPEC.md` sections 4.7 and 9 and `docs/AGENT_TESTING.md` state the
  persistence timing, the new session, and the reference mirror.
- Comment-only touches beside the change: `include/stackfall/game.hpp`
  (the `highScore()` contract now names the load, the mid-round save, and
  the between-rounds feedback) and `include/stackfall/storage.hpp` (the
  `save()` contract names both save moments). No code moved in either
  header.

Verification:

- Passed: `ctest` 5 of 5 in Debug (`build`) and Release (`build-rel`);
  `stackfall_tests --seed 987654321` is byte-identical between the two
  builds (the rules core did not move).
- Passed: `HARNESS_RUN_ID=t052-harness tests/harness/run.sh` - 689 of 689
  checker pins over all fifteen sessions, every session's observation
  stream byte-identical to its reference (shot paths stripped), including
  the new `record-abandon-save` session, the two scene cycles, the three
  fixtures, and all staged options sessions. The last rerun used the final
  binaries after the comment touches.
- Passed: `T015_PHASES=sessions tests/harness/t015.sh` after rebuilding
  `build-rel` - all seven M3 sessions (DAS/ARR, lock delay, three bag
  seeds, `m3-restart` with its pause-restart, quit-to-title, and
  play-again paths, and the four-process `m3-highscore`) match their
  reference streams, repeat byte-identically, and replay identically in
  the Release build. `check_t015.py` over the sessions evidence: 416
  checks passed; the only failures are the phases this subset did not run.
  `m3-highscore` evidence: run1 saves 202 (10 mid-round saves plus the
  game-over save), run2 replays the same seed, loads 202, writes nothing
  (an equal score is not a record), run3 beats it to 212 (2 mid-round
  saves plus the game-over save), run4 recovers from the damaged file to
  0 and saves 196.
- Passed: the complete M3 desktop gate, `tests/harness/t015.sh` - M3 GATE
  PASS, every phase included (clean Debug and Release builds, pinned
  hashes byte-identical, all seven sessions, the retained `run.sh` and
  `t024.sh` suites in both configurations, the T025 `failures`, `restarts`,
  and `san` phases, and the T026 `verify` phase), evidence
  `build/agent-checks/t015-2026-11-04-090807/`. The gate's Release build
  carries the final sources; its Debug build predates only the two
  comment-only header touches, and the final reruns above used the final
  binaries.
- Passed: `T024_RUN_ID=t052-t024 tests/harness/t024.sh` - 291 of 291.
- Passed: `T025_PHASES="hardkill restarts" tests/harness/t025.sh` and
  `T025_PHASES="failures cycles" tests/harness/t025.sh` - 24 SIGKILLs
  during the delayed saves leave 24 canonical records and 0 damaged, the
  verify run loads the standing 230, two instances on one directory end
  with the record grown from 10 to 204, 50 process restarts carry the
  record to 224, the failure matrix passes 17 checks, and 52 real-time
  restart cycles hold resources flat.
- Passed: `T026_PHASES=verify tests/harness/t026.sh` - every benchmark
  scenario proves its content claim.
- Passed probes (commands below): a SIGKILL mid-round at score 122 leaves
  `high_score=122` (saves at 36, 68, 96, 122); an SDL_QUIT window close
  mid-round at 96 leaves `high_score=96` with a clean shutdown and no
  game over; both lose at most the piece in play.

Commands:

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
HARNESS_RUN_ID=t052-harness tests/harness/run.sh
T024_RUN_ID=t052-t024 tests/harness/t024.sh
T015_PHASES=sessions T015_RUN_ID=t052-t015-sessions tests/harness/t015.sh
python3 tests/harness/check_t015.py build/agent-checks/t052-t015-sessions
tests/harness/t015.sh
T025_PHASES="hardkill restarts" T025_RUN_ID=t052-t025 tests/harness/t025.sh
T025_PHASES="failures cycles" T025_RUN_ID=t052-t025-fc tests/harness/t025.sh
T026_PHASES=verify T026_TAG=t052 T026_RUN_ID=t052-t026-verify tests/harness/t026.sh
# evidence: build/agent-checks/{t052-harness,t052-t024,t052-t015-sessions,
#   t015-2026-11-04-090807,t052-t025,t052-t025-hardkill,t052-t025-fc,
#   t052-t026-verify}
```

Open questions: none for the desktop software. The device pass (T018)
should watch one thing this change adds: a soft-drop hold raises the score
by 1 per tick, so a player holding soft drop past the stored record costs
one atomic save (fsync plus rename) per tick on the RG503's storage. The
desktop frame-time measurements of the T026 verify phase show no claim
broken; the panel-side pacing check stays with the final device
acceptance.

## T053: the DAS charge carries across a lock (2026-11-04)

Task T053; the owner chose DAS preservation on 2026-11-04.
`spawnPiece` (`src/game.cpp`) resets the gravity timer, the grounded
streak, and the reset budget, but not `shiftDir_` and `shiftTicks_`, so a
held direction keeps its charge into the next piece and slides it by ARR
with no new press. That is the intended behavior, but neither the section
4.5 table nor the suite pinned it, so a refactor could have changed the
feel silently. No production code changed. Tested in the detached worktree
of `d24692a` plus uncommitted task changes; the tested sources are
`docs/SPEC.md`
`7596df0d680c536483dd5e2a8f5f46c0d8e4af57cb5d77e1ce7dd0a28367b4cb` and
`tests/test_game.cpp`
`e82c78656106834f052e5ea9bd133c4cb1296b34e36cd816c051986fc774cef5`.

What landed:

- `docs/SPEC.md`: one row in the section 4.5 table, "Held direction across
  a lock". The row states the carry - the held direction and its DAS/ARR
  charge cross a lock into the piece that lock spawns, and a charged carry
  slides that piece by ARR with no new press, within two ticks of spawn -
  and keeps the two override rules beside it: a fresh press or a reversal
  still restarts the charge, and both directions held park it.
- `tests/test_game.cpp`: three tests in the DAS/ARR group plus one shared
  setup, `startOAgainstRightWall`. The setup parks an O on the floor
  against the right wall, where every right repeat fails, so no
  lock-delay reset is spent and the piece locks on its thirtieth grounded
  tick while the charge keeps cycling at the ARR pace:
  - `test_das_charge_carries_across_a_lock`: thirty held-right ticks lock
    the O and spawn the queue head. The new piece sits exactly at its
    spawn on the lock tick, the direction (`+1`) and the charge (nine of
    ten) crossed unchanged, and with no new press the carried charge
    slides the new piece right on its first tick, holds one ARR gap, and
    slides again two ticks later.
  - `test_reversal_across_a_lock_moves_immediately`: the same carried
    charge, then a reversal on the first tick after the spawn. The new
    piece moves left immediately, the charge restarts at zero, and nothing
    moves for the next nine ticks, so the carried ARR cadence is gone and
    a full DAS wait runs again.
  - `test_both_directions_held_across_a_lock_move_nothing`: both halves of
    the rule. With both directions held before the lock, no move ever
    happens and the spawned piece keeps the parked charge; with a charge
    that did cross the lock, holding both on the far side discards it
    without one step. Each half still names the spawned piece, so a lock
    in between cannot mask the result.
- `src/game.cpp` and every other production source are untouched; the
  diff names only the two files above and this note. The suite grew from
  339399 to 339433 checks (`--seed 987654321` run).

Verification:

- Passed: clean Debug rebuild and `ctest` 5 of 5 in Debug (`build`) and
  Release (`build-rel`), zero compiler warnings in both.
- Passed: `stackfall_tests --seed 987654321` byte-identical between Debug
  and Release, and all ten printed replay hashes - the five pinned
  scenarios at their pinned seeds plus the same five at seed 987654321 -
  identical to a pre-change run of the unmodified tree. Only the check
  count moved.
- Passed: `HARNESS_RUN_ID=t053-harness tests/harness/run.sh` - 689 of 689
  checker pins over all fifteen sessions, every session's observation
  stream byte-identical to its headless reference.
- Passed: the complete M3 desktop gate on the final sources
  (`T015_RUN_ID=t053-t015-final`), M3 GATE PASS with 434 of 434 checker
  checks: clean Debug and Release builds behind the warning gate, pinned
  hashes byte-identical, all seven M3 sessions repeating byte-identically
  and replaying in the Release build, the retained `run.sh` and `t024.sh`
  suites in both configurations, the T025 `failures`, `restarts`, and
  `san` phases - the new test code ran clean under AddressSanitizer and
  UndefinedBehaviorSanitizer - and the T026 `verify` phase.
- Passed: baseline equivalence. A binary built from `git archive d24692a`
  (evidence `build/agent-checks/t053-baseline/`) replayed the
  `m3-das-arr` and `m3-lock-delay` sessions at seed 20261103 with the same
  script, save directory, screenshots, and input log options: both
  observation streams, both input logs, and all 169 screenshots are
  byte-identical to the candidate's, so no observation format moved.

Commands:

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
HARNESS_RUN_ID=t053-harness tests/harness/run.sh
T015_RUN_ID=t053-t015-final tests/harness/t015.sh
```

Open questions: none. The row states the behavior the core already had; a
future change to it now breaks a pin on purpose.

## T054: bounded, palettized checkpoint screenshots (2026-11-04)

Sources: the task contract of T054, papercut
`pc_01M6JJR4BAFHC9KZM6GQ1EKK04`, and
[docs/AGENT_TESTING.md](../docs/AGENT_TESTING.md). Tested at HEAD `479d176`
plus uncommitted task changes; the tested sources are
`src/developer_session.cpp`
`afb55cff67f584ee957c790cf948d9e4e8803a50637e37ea000f75f221b24752`,
`include/stackfall/developer_session.hpp`
`ca8476dd808428cb912425cdcc55b32af245de8160c067e6f59b8de31fe8b601`, and
`src/main.cpp`
`da1abad5134928ce0cbc2e2e5cd4f42328c15d196da6bbb184511a1d5e0ca800`.

What landed:

- The checkpoint writer in `src/developer_session.cpp` replaces
  `SDL_SaveBMP`. It builds each frame's palette, writes one 8-bit BI_RLE8
  BMP, and falls back to the uncompressed 8-bit form when RLE would grow
  the rows and to the 24-bit form when a frame passes 256 colors. Every
  image stays lossless; no image library is involved.
- `--screenshot-budget BYTES` bounds the images one session writes. The
  default is 256 MiB; 0 removes the cap. When the next image would pass
  the budget, the session logs one line, skips that image, and the record
  reads `shot=none` exactly as when a save fails. `main.cpp` parses the
  option, `--help` documents it, and `docs/AGENT_TESTING.md` carries both
  halves.
- `read_bmp_pixels` in `tests/harness/check_harness.py` gained the RLE8
  decode path plus the uncompressed 8-bit and palette reads, still with no
  image library. `check_bmp` and the `check_bmp_size` copy in
  `tests/harness/check_t024.py` accept the 8-bit depth. PIL reads RLE8
  BMPs natively, so visual review keeps working.

Sizes on the 960x544 canvas: the old writer spent 1,566,774 bytes per
image. The new one spends 21,942 to 46,550 (median 21,942) on these
frames; no frame of the runs below hit a fallback.

Verification (all on the final sources unless noted):

- Passed: `HARNESS_RUN_ID=t054-harness-final tests/harness/run.sh` - 689
  of 689 checker pins, every session's observation stream byte-identical
  to its headless reference. The fifteen sessions wrote 350 BMPs totaling
  8,089,658 bytes, far under the 268,435,456-byte default, and every pixel
  scan (the ghost well scan, the options page, the previews, the grayscale
  proof, the letterbox check) read the new form.
- Passed: the budget refusal. A six-check session under
  `--screenshot-budget 100000` wrote 5 images (94,070 bytes), logged one
  skip line each for the remaining two, and still wrote all six records -
  the two refused ones with `shot=none`. Under `--screenshot-budget 0` the
  same script wrote all 7 images and skipped nothing. Evidence:
  `build/agent-checks/t054-budget/`.
- Passed: the order-of-magnitude drop. A 320-piece scripted session with
  one image per piece (`build/agent-checks/t054-long/`) wrote 322 images:
  504,501,228 bytes from a binary built at HEAD `479d176` (the SDL_SaveBMP
  baseline) against 7,600,944 bytes from the candidate, 66 times less. The
  two observation streams are byte-identical after the `shot=` path, and
  sampled frames (title, spawn, mid-stack, late checks) hold identical
  pixels in PIL, so only the container changed.
- Passed: the BMP decoder cross-check. `read_bmp_pixels` agreed with PIL
  on old 24-bit and new RLE8 files, and synthetic BMPs pinned the
  uncompressed 8-bit rows (bottom-up and top-down), absolute runs of odd
  length, the pair flush, and the refusal paths.
- Passed: `T024_RUN_ID=t054-t024-final tests/harness/t024.sh` - 291 of 291
  checks, including the I-spawn and 640x480 letterbox pixel proofs.
- Passed: the complete M3 desktop gate (`T015_RUN_ID=t054-t015`), M3 GATE
  PASS with 434 of 434 checker checks, zero compiler warnings in both
  configurations. The gate built a binary that differs from the final one
  only by one `--help` wording and a line wrap, so the two suites above
  were rerun on the exact final sources.
- Passed: preservation. `ctest` 5 of 5 in Debug and Release;
  `stackfall_tests --seed 987654321` reports 339,433 checks and zero
  failures, byte-identical between Debug, Release, and the pre-change
  baseline binary, with all ten replay hashes unchanged. Gameplay, save
  formats, and input timing are untouched: the rules core, the stores, and
  the loop did not change.

Commands:

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build-rel --output-on-failure
./build/stackfall_tests --seed 987654321
./build-rel/stackfall_tests --seed 987654321
HARNESS_RUN_ID=t054-harness-final tests/harness/run.sh
T024_RUN_ID=t054-t024-final tests/harness/t024.sh
T015_RUN_ID=t054-t015 tests/harness/t015.sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/stackfall --seed 20261103 \
  --tick-per-frame --input-script build/agent-checks/t054-budget/script.txt \
  --save-dir build/agent-checks/t054-budget/save \
  --screenshot-dir build/agent-checks/t054-budget/shots \
  --screenshot-budget 100000 \
  --observations build/agent-checks/t054-budget/obs.txt --frames 400
```

Open questions: none. The papercut's tmpfs workaround is no longer needed:
the same 322-image session now costs 7.2 MB under the default budget.

## T019: PortMaster release package assembled locally (2026-11-04)

Task T019, round M5, local preparation only; publication stays with the
blocked operator task T021 and the device pass with T018. Sources:
[game specification](../docs/SPEC.md) sections 10.2 to 10.4,
[agent testing contract](../AGENT_TESTING.md), and the current PortMaster
packaging guide (<https://portmaster.games/packaging.html>) with the build
system that defines the archive layout
(<https://github.com/PortsMaster/PortMaster-New> `tools/build_release.py`,
both reviewed 2026-11-04; the guide's structure and the referenced
`ports/defblade` port were fetched and compared entry by entry). Tested in
the detached worktree of `155c7e6` plus uncommitted task changes; the runs
wrote their own `source.sha256`.

What landed:

- `portmaster/port.json`: package metadata, format version 4, following the
  upstream field list and example (`name` `stackfall.zip`, `items`
  `["Stackfall.sh", "stackfall"]`, title STACKFALL, genre `puzzle` from the
  upstream genre list, `rtr` true, `runtime` empty, `arch` `["aarch64"]`,
  `min_glibc` `2.34` from the binary's own symbol versions).
- `portmaster/gameinfo.xml`: one game entry, `path` at the launcher,
  `image` at the archived screenshot, description equal to the port.json
  description, release date 2026-11-04.
- `portmaster/README.md`: rewritten as the readme that ships inside the
  archive (what the game is, controls, options, saves, the port contents,
  credits); the developer documentation of the portmaster directory moved
  to `tools/portmaster/README.md`.
- `portmaster/release-notes.md` and `portmaster/BUILDING.md`: the changes
  of release 1.0.0 and the reproduction steps that ship in the archive.
  BUILDING.md records the environment, the one-command build, the
  step-by-step commands, the binary's `file` form, its runtime libraries,
  the glibc span 2.17 to 2.34, and the SHA-256 of the binary it shipped
  with. `portmaster/licenses/` carries the MIT license and the SDL 2.26.2
  zlib license, the only linked third-party library.
- `tools/portmaster/capture-screenshot.sh` and `bmp2png.py`: the 4:3
  screenshot, captured from the desktop build. The driver runs the real
  application under a private `Xvfb` display with `openbox`, drives the
  rehearsed nine-placement opening of the `normal-game-clears` session
  (`tests/harness/scripts/t019-screenshot.script.txt`, seed 20261103),
  resizes the window to 640x480 at the `screenshot-ready` checkpoint, and
  takes the game's own checkpoint capture of the following frame: a real
  letterboxed render, not a scaled copy of the 960x544 canvas. `bmp2png.py`
  converts the checkpoint BMP (8-bit BI_RLE8 in this run) to PNG with the
  standard library alone; this host ships `unzip` but no `zip` binary and
  no image library, which is logged as a papercut.
- `tools/portmaster/package.sh`: assembles `build/portmaster-package/`
  (staged port directory plus `stackfall.zip`) from the ARM64 binary, the
  package sources, and a captured screenshot, and writes the package
  report (entry list, per-file SHA-256, archive SHA-256, source commit)
  into the run's evidence directory. The archive entry list reproduces
  what the upstream build system writes: launch script and game folder at
  the archive root, metadata inside the game folder, the readme renamed to
  `stackfall.md`. Entries are sorted, timestamps pinned, and modes fixed,
  so the archive bytes follow deterministically from its files.
- `tests/harness/t019.sh` and `check_t019.py`: the driver and the
  independent gate checker, described in `docs/AGENT_TESTING.md`.
- Documentation: specification section 10.4 now records the assembled
  layout, the capture method, and the determinism rule; the testing guide
  documents the check; the front-page status and the packaging sections of
  `README.md` match the repository (the standing rule of T022).
- The version bump to 1.0.0, which task T035 requires to land before this
  package: `include/stackfall/version.hpp`, `CMakeLists.txt`, and the
  `tests/test_sanity.cpp` pin now say 1.0.0. The version string leads the
  state hash by design, so the five pinned replay hashes of
  `tests/test_regression.cpp` and the second pin set in
  `tests/harness/check_t015.py` were refreshed from the suite's own
  printed values; scores, lines, and tick counts are unchanged. T035's
  scope is complete except for its own record keeping.

One command assembles and checks the package:

```bash
tests/harness/t019.sh
```

The recorded run is `build/agent-checks/t019-package-2026-11-04/` (78
checks, 0 failures), re-verified by running the checker again against the
evidence directory alone. Package identity:

- archive `build/portmaster-package/stackfall.zip`, SHA-256
  `7d5e0f6382662b248933b8d9d45c2da5ae448480fc862cb17e404fd21146fa25`;
- binary `stackfall/stackfall.aarch64`, SHA-256
  `dc5343c2d6c68b7ccb113dc6bc9a74c9bb489c81cc8ecc5f818c96f210567c89`,
  `file` reports `ELF 64-bit LSB pie executable, ARM aarch64`, and the
  BUILDING.md inside the archive names the same hash.

Results, all passed:

- Metadata: `port.json` parses with every upstream field in place and
  genres from the upstream list; `gameinfo.xml` parses with one entry,
  title and description equal to the port.json values, the ES date form,
  and the image pointing at the archived screenshot.
- Archive: ten entries and only those ten; every CRC passes; the launcher,
  the metadata, the readme, the notes, the build instructions, and both
  licenses are byte-equal to the repository sources; the readme carries
  the upstream rename to `stackfall.md`.
- Unpacking into a fresh directory reproduces the section 10.3 install
  layout (`Stackfall.sh` plus `stackfall/stackfall.aarch64`) with the
  section 10.4 items beside the binary, and ships no runtime `conf/` or
  `log.txt`.
- Binary: `ARM aarch64`, no x86 executable in the archive, the archived
  bytes equal the ARM64 build's product, and the shipped BUILDING.md names
  that hash and the SDL2 dependency.
- Reproduction: the documented step-by-step commands of BUILDING.md,
  re-run in a fresh container from the pinned image into a fresh build
  directory, printed the same SHA-256 `dc5343c2...` as the archived
  binary. Two earlier full builds (the release build of
  `build/agent-checks/t019-arm64-2026-11-04-v100/` and a second clean
  container build) had already agreed byte for byte at 0.1.0 and again at
  1.0.0.
- Screenshot, measured with no image library: 640x480 (4:3), PNG
  truecolor; centered letterbox bars of 59 rows above and below the scene;
  the board column of the 27 px geometry holds a settled stack of at least
  four rows with pieces of at least three colors and a piece in flight
  above it; both HUD columns carry readable text pixels and the right
  column shows the Next previews; no title screen and no blank frame.
- Launcher: the archive's own `Stackfall.sh`, run against a stub PortMaster
  control folder from the unpacked tree with the desktop binary staged
  beside the archived one (an ARM64 binary cannot run on this host), under
  the dummy video and audio drivers: exit 0, `get_controls`,
  `pm_platform_helper`, `pm_finish`, `conf/` created, `log.txt` written,
  the game's session log in `conf/` closing cleanly with the banner
  `STACKFALL 1.0.0`.
- Naming: no shipped file uses the forbidden name or trade dress; the
  launcher and the readme name STACKFALL.
- Determinism: the checker rebuilt the archive bytes from the staged files
  with the pinned rules and got the same SHA-256.
- Regression after the version bump: `ctest` 5 of 5 in Debug and Release
  (339,433 checks, the five re-pinned replay hashes); the full harness
  `tests/harness/run.sh` passed 689 checks over its fifteen sessions
  (`build/agent-checks/harness-2026-11-04-123351/`); the launcher check
  `tests/harness/t017.sh` passed its 167 checks again
  (`build/agent-checks/t017-launcher-2026-11-04/`, its own section records
  the method), and the desktop gate `tests/harness/t015.sh` passed at this
  source identity (`build/agent-checks/t015-2026-11-04-123954/`: rules
  hashes identical in Debug and Release, all sessions green, 434 gate
  checks at zero failures, with the re-pinned replay hashes).

Dogfooding commands for the local handoff:

```bash
tools/arm64/build-arm64.sh                 # build-arm64/stackfall.aarch64
tools/portmaster/package.sh                # build/portmaster-package/stackfall.zip
cat build/agent-checks/t019-package-2026-11-04/package-report.txt  # the identity
tests/harness/t019.sh                      # the 78-check acceptance gate
unzip build/portmaster-package/stackfall.zip -d /tmp/ports   # inspect by hand
```

Findings and open questions:

- The screenshot is a real-time-paced capture, so the exact fall row of
  the piece in flight can differ by a row between captures; the board, the
  HUD, and the letterbox layout do not. Re-running `package.sh` refreshes
  `screenshot.png` and the archive hash; the checker re-derives every
  property from the fresh capture.
- The launcher and the environment it exports are proven with the desktop
  binary standing in for the ARM64 one. Running the archived ARM64
  binary itself, the real `control.txt`, and the platform exit chord stay
  with the blocked device task T018, as does the fresh-install gate T020
  and the publication T021.
- Upstream's `build_release.py` moves `port.json`, `gameinfo.xml`, the
  screenshot, and the readme into the game folder when it writes the zip.
  `package.sh` reproduces that mapping directly, so the local archive and
  a future upstream-built release unpack identically.

## T035: release version 1.0.0 owned and gated (2026-11-04)

Task T035, round M5, the release-prep round of T019. Sources: the task
contract of T035, [agent testing contract](../AGENT_TESTING.md), and the
version rule `include/stackfall/version.hpp` states for itself (its three
constants compose to one dotted string, kept in sync with the
`project(... VERSION ...)` of `CMakeLists.txt`). Tested in the detached
worktree of `94139ae` plus uncommitted task changes; the run wrote its own
`source.sha256`.

What T035 owns, and where it landed:

- The bump itself landed inside the T019 package assembly, so the archive
  shipped self-consistent; see the T019 section above for that record.
  `include/stackfall/version.hpp`, `src/version.cpp` (which hands out the
  constant and states no literal of its own), `CMakeLists.txt`, and the
  `tests/test_sanity.cpp` pin all say 1.0.0. The version string leads the
  state hash by design, so the five pinned replay hashes of
  `tests/test_regression.cpp` and the second pin set of
  `tests/harness/check_t015.py` were refreshed with it; scores, lines, and
  tick counts are unchanged.
- What this task adds is the standing gate, so the bump keeps an owner
  after closure: `tests/harness/t035.sh` and `tests/harness/check_t035.py`.
  The driver builds the desktop tree, runs the headless suites, starts a
  bounded headless session for the startup banner, and hands the evidence
  to the checker. The checker re-derives every expectation from the
  sources alone: the three constants compose to `kVersionString`;
  `version.cpp` holds no second literal; the CMake project version equals
  the header; the header, `CMakeLists.txt`, the sanity pin, and the
  release notes state exactly the shipped version and no other dotted
  value; no file that ships in the archive contradicts it; the state hash
  still folds `kVersionString` (so a later bump again breaks the pins
  instead of passing silently) and the t015 gate holds the same pin set as
  the suite; the release notes headline the version with their date and
  `gameinfo.xml` carries the same date; the banner run exits 0 with a clean
  shutdown and both the stdout banner and the session `log.txt` report
  `STACKFALL 1.0.0`.

One command runs the gate:

```bash
tests/harness/t035.sh
```

The recorded run is `build/agent-checks/t035-version-2026-11-04/`
(31 checks, 0 failures). Results, all passed:

- Version sources: the constants compose to `1.0.0`; `version.cpp` states
  no literal of its own; the CMake `project()` version is `1.0.0`; the
  header, `CMakeLists.txt`, `test_sanity.cpp`, and `release-notes.md` each
  carry `1.0.0` and no other dotted value (`cmake_minimum_required` names
  the two-part `3.16`, which is not a dotted triple).
- Shipped files: `port.json`, `gameinfo.xml`, the port readme, and
  `Stackfall.sh` state no version that contradicts the shipped one.
  `port.json` carries no version field at all, because the upstream format
  version 4 has none; the release notes are the version metadata a package
  browser reads, and they headline `## 1.0.0 (2026-11-04)`.
- Pins: the state hash folds `kVersionString`; the suite pins five replay
  scenarios and `check_t015.py` holds the same five values.
- Banner: exit 0, clean shutdown, `STACKFALL 1.0.0` on stdout and in the
  session `log.txt` beside the record.
- Suites at this version: `ctest` 5 of 5 in Debug and in Release (the
  sanity pin and the five re-pinned replay hashes).

The gate was tested against drift, not only against the passing tree. With
copies under `/tmp` the checker failed every planted fault: a CMake version
left behind (2 failures), a header bumped with nothing following it (10),
a sanity pin left at the old value (2), one t015 pin not refreshed with the
suite (1), a banner run reporting the old version (2), and release notes
never updated (2). The restored tree passed all 31 again.

Findings and open questions:

- The ARM64 binary's own banner is not re-proven here: it cannot run on
  this x86-64 host, `build-arm64/` is not present in this worktree, and
  rebuilding the PortMaster environment image is T019's recorded scope.
  The binary is compiled from the same `version.hpp`, the T019 record
  holds its ARM64 build, hash parity with the desktop state hashes, and a
  clean-container reproduction at 1.0.0, and the T019 checker reads this
  same header for the shipped release notes and the launcher banner.
- The banner on the physical device stays with the blocked device task
  T018, as do the fresh-install gate T020 and the publication T021.

## T022: front pages state the integrated state (2026-11-04)

Task T022, the standing documentation repair: the two front pages state
the actual integrated state and agree with each other. Sources: the task
contract of T022 and the milestone definitions of
[game specification](../docs/SPEC.md) section 13. Tested in the detached
worktree of `1792ede` plus uncommitted task changes. No source file
changed.

The repair: the specification status line still read "M4 in progress;
... device validation and release packaging remain", written in the same
commit that assembles and checks the package, and the README opened its
status section with the M3 gate alone. `docs/SPEC.md` now reads
"M5 in progress", names the six-setting options screen, and records that
the release package is assembled locally and checked with its version
pinned at 1.0.0 (T019, T035), so device validation and publication are
what remain. `README.md` states the milestone the same way and keeps its
existing closing sentence, which already said the same thing.

Verification, all passed:

- A 27-point cross-check of the two front pages against the repository:
  `tests/harness/run.sh` names fifteen sessions and both pages say
  fifteen; the four version sources and the release notes state 1.0.0;
  every gate and tool the status text names exists (`t015.sh`, `t017.sh`,
  `t019.sh`, `t035.sh`, `run.sh`, `package.sh`, `install-tree.sh`,
  `build-arm64.sh`); the stale phrases are gone; and every line the repair
  touched keeps the file's width (78 columns in the specification, 80 in
  the README).
- `tests/harness/t035.sh`: 31 checks, 0 failures, evidence in
  `build/agent-checks/t035-version-2026-11-04/`. The gate rebuilds the
  Debug tree, runs the five headless suites, and proves the banner, the
  state-hash pins, and the package metadata at 1.0.0, which the new
  status sentence rests on.
- Release `ctest` in `build-rel`: 5 of 5 suites.

Findings and open questions:

- The blocked device and publication gates are unchanged: T018 (the RG503
  checklist), T020 (the fresh ROCKNIX install), T021 (publication), T036
  (device frame-time percentiles), T042, and T043.

## T055: 90-degree portrait play on the RG503 (2026-11-04)

Task T055; the rotation direction and the final cluster roles were pinned in
`notes/design-review.md` ("Portrait mode: rotation and cluster roles") before
the mapping landed. Tested in the detached worktree of `cbe5147` plus
uncommitted task changes; the harness's `source.sha256` in each run directory
ties every run below to the exact sources.

What landed:

- `include/stackfall/types.hpp`: the `Orientation` enum with landscape as the
  default.
- `include/stackfall/renderer_sdl.hpp` and `src/renderer_sdl.cpp`: the
  portrait constants (544x960 canvas, 40 px cells, the 400x800 field at
  (72, 120), the HUD strip above the board, the recomposed title, controls,
  and options pages, the two overlay panels) and one rotation inside the
  drawing primitives: `deviceRect()` maps portrait rect (x, y, w, h) to
  device rect (y, 544-x-w, h, w), axis-aligned lines become 1-px filled
  rects (pixel-identical in landscape), and every draw call reads its
  geometry through orientation accessors, so landscape keeps its own
  constants and its own pixels. `Renderer` carries the layout orientation
  and the ORIENTATION row's preference value separately.
- `include/stackfall/input_sdl.hpp` and `src/input_sdl.cpp`: `SdlInput`
  carries an orientation; the portrait controller and keyboard tables beside
  the section 5 tables; orientation-aware `mapControllerButton` and
  `mapKeyboardScancode` for the controls screen.
- `include/stackfall/storage.hpp` and `src/storage.cpp`: the seventh
  preference line `orientation=landscape|portrait`, canonical rendering,
  legacy six-, three-, and two-line forms parsing with orientation at its
  default, and the same atomic write.
- `src/main.cpp`: `applyOrientation` at startup (layout and both input
  tables turn together, one log line names the canvas and cell size), the
  seventh options row through `cycleOrientation` (the stored preference and
  the row's label flip at once, the running layout does not, one input-log
  line records the change), and the startup settings and input-log lines
  naming the orientation.
- `include/stackfall/game.hpp`: `kOptionsItemCount` 7 with ORIENTATION
  appended last, so every earlier row index keeps its meaning.
- `tests/test_storage.cpp`, `tests/test_screens.cpp`,
  `tests/test_renderer_sdl.cpp`: canonical pins ending in the orientation
  line, the 64-boolean options-value enumeration, the seven-row wrapping,
  and three new portrait pixel groups in the SDL suite: the rotated board
  frame, well, grid, and cell bands asserted at exact device pixels through
  a restated rotation; the HUD strip, title, options, controls, and overlay
  bands inside the tall canvas; and a landscape invariance check that draws
  a paused round through a portrait excursion and requires identical canvas
  bytes. `tests/test_input_sdl.cpp`: the portrait keyboard and pad tables
  through the real SDL event path (the same case list as the landscape
  tables with the rotated expectations) plus a pad round proving D-pad up
  moves the piece right and B rotates clockwise.
- `tests/harness/reference_replay.cpp`: the portrait key and pad tables
  transcribed from the design-review pin, the orientation read from the
  staged settings file, and the ORIENTATION-row select mirrored as a
  preference flip that changes no table of the running process. The
  settings load also gained its missing parent-directory step: the replay
  had passed the file path where the store wants its directory, so it
  always read the fresh-installation defaults; no earlier session depended
  on a staged startup file, and the portrait relaunch stage does.
- `tests/harness/scripts/orientation-portrait.script.txt`,
  `portrait-keyboard.script.txt`, `portrait-pad.script.txt`, and the two
  `.stages.txt` manifests; `options-six-keyboard` and `options-six-pad`
  now walk the seventh row and wrap past it; `tests/harness/run.sh`
  registers the two staged sessions; `tests/harness/check_harness.py`
  adds the portrait geometry, panel, quiet-row, settings-byte, and ordered
  rotated-mapping pins and carries the seventh row and settings line
  through every options and settings pin.
- `docs/SPEC.md` (sections 2, 4.7, 5, 6.1, 6.3, 9, 11, 12) and
  `docs/AGENT_TESTING.md` describe the mode, the rotated tables, and the
  two sessions.

Verification:

- Passed: `ctest` 5 of 5 - `stackfall_tests` 339920 checks,
  `stackfall_input_tests` 726, `stackfall_render_tests` 8068.
- Passed: `HARNESS_RUN_ID=t055-harness-final tests/harness/run.sh` -
  833 of 833 checker pins over seventeen sessions (the same green state
  first reached as `harness-portrait-pass3` before the checker's own pixel
  rules were corrected; the final run exercises the shipped scripts,
  reference, and checker together). Stage two of each
  portrait session replays byte-identically against the reference running
  the rotated tables (25 records keyboard, 24 pad). The checker pins the
  rotated frame (both edges of device x=119..920 and y=71..472 in frame
  color for their whole span, the lines one past them background, so the
  rotation and the 40 px cell size prove together), the HUD strip, the
  quiet margins, both overlay panels at their rotated boxes, the quiet rows
  below the board on every clearing page, the portrait settings bytes with
  the landscape stage-one startup line, and the ordered rotated-mapping
  bits for the keyboard (10 steps) and the pad (9 steps) from the per-tick
  input log.
- Passed: landscape invariance against the pre-change baseline
  (`build/agent-checks/baseline-landscape/`). Every landscape session the
  scripts did not touch plays byte-identically: nine observation streams
  stripped of `shot=` compare equal (scene-cycle keyboard and pad,
  normal-game-clears, options-ghost, hold-off, grayscale, record-abandon-
  save, and the three fixtures); of 350 shared screenshots, 280 are
  byte-identical and all 70 differing files are options-page shots that
  legitimately draw the seventh row; the two options-six streams differ by
  exactly one added `options-orientation-row` record each, with the wrap
  checkpoint's `menu=` value unchanged.
- Passed: behavior probes of the rotated mapping through the real binary
  before the harness landed - in a portrait session, D-pad up reads `R=1`,
  D-pad down `L=1`, Z `cw=1`, X `ccw=1`, C `drop=1`, Space `hold=1`, left
  arrow `menu=1000` (three menu-downs then one menu-up land the pause
  cursor on CONTROLS, and confirm opens the mappings screen).
- Passed: visual review of rotated portraits of the harness frames (title,
  playing, paused, game-over). The pause and game-over panels center on the
  board, the strip reads across the top, the marker sits on the selected
  row, and nothing clips; the remote image service rejected PNG uploads, so
  the review ran on JPEG copies.

Dispositions of the two failed harness passes on the way to the green run:

- `harness-2026-11-04-154727`: the stage-one script walked one row instead
  of six, so its select toggled GHOST and left the file landscape; stage
  two then replayed against landscape tables and never quit. Script fixed
  to six downs.
- `harness-portrait-pass2`: the reference passed the settings file path
  where the store wants its directory (see the repair above), so it
  replayed the portrait stage on the landscape tables. Fixed; no earlier
  session's stream moved, confirmed by the byte-identical comparison
  above.
- `harness-portrait-pass3`: the sessions and streams were green; the
  checker's own pixel rules were wrong in three places - the frame-edge
  walk demanded frame color one pixel outside the frame, the below-board
  quiet rule ran on overlay pages whose veil covers those rows by design,
  and one branch still pinned the old six-field settings log line. The
  checker was corrected and re-run on the same evidence, then the final
  end-to-end run above confirmed the shipped state.

Commands:

```bash
ctest --test-dir build --output-on-failure
HARNESS_RUN_ID=t055-harness-final tests/harness/run.sh
```

Open questions: the physical half - that the marked buttons on a real RG503
sit where the pinned rotation puts them, and that the sideways grip reads
well in a long session - stays with the blocked device task (T018 and the
section 12 checklist item).

## T022: front pages state the portrait state (2026-11-04)

Task T022, the standing documentation repair: the two front pages state the
actual integrated state and agree with each other. Sources: the task
contract of T022 and the T055 record above. Tested in the detached worktree
of `4bc5d06` plus uncommitted task changes. No production source changed.

The repair: `README.md` still said "all six options settings" while the
options screen carries seven rows, still said the harness "drives fifteen
sessions" while `tests/harness/run.sh` and the testing guide say seventeen,
and the words portrait and orientation appeared nowhere in it. The README
now names screen orientation in the Options step, in the status paragraph,
and in the saved-preferences sentence, counts seven settings, counts
seventeen sessions and names the portrait round among them, adds a portrait
paragraph to "How it plays" (the 544 x 960 frame, the 40-pixel squares, the
rotated controls), and closes the status section with the panel research of
T056 plus the owner's pending per-class choice, with a reports-table row
for `notes/anbernic-resolutions.md`. `docs/SPEC.md` adds portrait play to
its status line, so both front pages agree again.

Verification, all passed:

- `HARNESS_RUN_ID=t022-readme-check tests/harness/run.sh`: 17 sessions,
  833 checks, 0 failures, evidence in
  `build/agent-checks/t022-readme-check/`. The run proves the session
  sentence of the status paragraph: `sessions.txt` lists the seventeen
  names, the two portrait sessions pass on the rotated tables, and the
  five headless suites pass first in the Debug tree the harness builds.
- Release `ctest` in `build-rel`: 5 of 5 suites.
- Every relative link in both front pages resolves, including the new
  `notes/anbernic-resolutions.md` row, and the lines the repair touched
  keep each file's width.

Findings and open questions:

- The blocked gates are unchanged: T018 (the RG503 checklist), T020 (the
  fresh ROCKNIX install), T021 (publication), T036 (device frame-time
  percentiles), T042, and T043, plus the owner decision T057 with T058 and
  T059 behind it.

## T061: the release package refreshed at the portrait revision (2026-11-04)

Task T061. The package was stale at HEAD: `portmaster/BUILDING.md` still
pinned the binary of commit 262a3a5, while T055 changed nine files under
`src/` and `include/` after it, and no task owned the refresh. Sources: the
task contract of T061 and [agent testing contract](../AGENT_TESTING.md).
Tested in the detached worktree of `f86e2bf` plus uncommitted task changes.
No game code changed; the only repository edit is the pin.

The refresh, in the order the task contract lists:

- `tools/arm64/build-arm64.sh` reran against the pinned image
  (`STACKFALL_ARM64_SKIP_IMAGE_BUILD=1`, the same invocation `tests/harness/
  t019.sh` uses). Evidence `build/agent-checks/t061-arm64-2026-11-04/`: the
  binary reports `ARM aarch64`, links only `libSDL2-2.0.so.0`,
  `libstdc++.so.6`, `libgcc_s.so.1`, `libc.so.6`, and `libm.so.6`, and hashes
  `dd51324f7e91ad042c6fa6e7108ce314b81f92c2458cf1d05e00c06b9392cfe5`. The
  glibc span is still 2.17 to 2.34, so `min_glibc` 2.34 stays. The suite ran
  twice inside the environment and `ctest` passed 5 of 5 there; the printed
  state hashes equal the desktop run's.
- `portmaster/BUILDING.md` now names that hash. Nothing else in the file
  moved: the environment, the commands, the `file` form, the runtime
  libraries, and the glibc span are unchanged, because only game sources
  changed since 262a3a5.
- `tests/harness/t019.sh` repackaged and drove the archive end to end.
  Evidence `build/agent-checks/t019-package-2026-11-04-t061/` (78 checks, 0
  failures), including the failing check of the stale run: BUILDING.md names
  the SHA-256 of the shipped binary. The `repro` phase rebuilt the tree in a
  fresh container from the pinned image into a clean build directory and
  printed the same hash.

Package identity of the refreshed archive:

- archive `build/portmaster-package/stackfall.zip`, SHA-256
  `e7d908be02227d33a5b7343b376986ef33ffb4ecc7bd5505476d7d2e1326c5bb`;
- binary `stackfall/stackfall.aarch64`, SHA-256
  `dd51324f7e91ad042c6fa6e7108ce314b81f92c2458cf1d05e00c06b9392cfe5`,
  named by the BUILDING.md inside the archive.

Supporting checks, all passed:

- Desktop suite before the build (the hash-comparison half of the ARM64
  script needs it): `build/stackfall_tests --seed 987654321`, 339,925 checks,
  0 failures.
- `tests/harness/t035.sh` after the edit (31 checks, 0 failures), because
  BUILDING.md ships in the archive and that gate reads every shipped file.
- The archived screenshot was opened and read as an image, beyond its pixel
  measurements: 640x480 with centered letterbox bars, the Hold group and the
  statistics on the left, five Next previews and the score column on the
  right, a settled stack of several colors in the lower board, and a piece in
  flight above it. Nothing clips.

Commands:

```bash
STACKFALL_ARM64_SKIP_IMAGE_BUILD=1 \
  STACKFALL_ARM64_RUN_ID=t061-arm64-2026-11-04 tools/arm64/build-arm64.sh
T019_RUN_ID=t019-package-2026-11-04-t061 tests/harness/t019.sh
T035_RUN_ID=t035-version-2026-11-04-t061 tests/harness/t035.sh
```

Findings and open questions:

- The archive hash changes between package runs by design: the screenshot is
  a real-time-paced capture, so the fall row of the piece in flight can move
  by a row. The binary hash does not move; a clean container reproduced it.
- The blocker note of the blocked device task T018 still names the retired
  archive hash `7d5e0f63...`. Workers cannot edit the backlog, so the
  refreshed hash above is recorded here and reported as a follow-up against
  T018. The operator who runs the device pass must take the archive from
  `build/portmaster-package/stackfall.zip` and its `package-report.txt`.
- The blocked gates are otherwise unchanged: T018 (the RG503 checklist),
  T020 (the fresh ROCKNIX install), T021 (publication), and T036 (device
  frame-time percentiles).


## T062: the generated twelve-track soundtrack (2026-11-04)

Task T062, the whole of MUSIC.md: the score representation, the sequencer and
voice engine, the twelve-track catalogue, the offline preview path, the
playback and scene policy, the music settings page with persistence, and the
desktop validation. The owner decision of 2026-11-04 approved the
specification change and delegated the creative choices to this
implementation. Tested in the detached worktree of `5ba04cd` plus uncommitted
task changes; the harness `source.sha256` ties the harness run below to the
exact sources.

What landed:

- `include/stackfall/music.hpp` and `src/music.cpp`: musical time at 96
  ticks per quarter, the ten-instrument band-limited additive bank with a
  Galois LFSR noise table, integer envelopes, the twelve-voice engine with a
  16-slot single-producer single-consumer command queue and latest-value
  gain atomics, sample-rate-exact transport (32.32 fixed-point ticks
  advanced per sample, so playback never drifts with the 60 Hz game tick),
  cumulative intensity layer masks with layer release at phrase boundaries,
  bar- and phrase-scheduled switches with pre-boundary fades, and the
  offline renderer plus WAV writer used by tests and the preview tool.
- `src/music_tracks.cpp`: the deterministic `Arranger` composer (motifs,
  progressions, five bass styles, six drum styles, four chord styles, swing
  offsets) and the twelve hand-written track specifications, expanded once
  into validated note lists - no randomness, no assets, no files.
- `include/stackfall/music_director.hpp` and `src/music_director.cpp`: the
  behavior table of MUSIC.md's playback section - title theme at low
  intensity, pool draws without repetition on a session-seeded PCG32 stream,
  the level bands 1-5, 6-12, and 13-20 over the shared timeline, pause
  freeze, game-over stop, mode Off, and the Next track action.
- `include/stackfall/storage.hpp` and `src/storage.cpp`: the settings file
  grows three lines (`music`, `musicvolume`, `effectsvolume`); the parser is
  now one strict line reader that accepts exactly the shipped eras (2, 3, 7,
  8, and 11 lines), so the orientation-era file upgrades with the Auto/6/8
  defaults and rejects bad mode names and volumes past 10.
- `src/audio_sdl.cpp`: the engine joins the device at its real rate; cues
  sum into one 32-bit bus, music renders into its own, each volume step and
  the master mute ramp in 15 ms, clear and game-over cues duck the music
  about 4 dB with hold and release, and one final clamp covers both buses.
- The options screen grows its eighth row MUSIC and the `MusicOptions`
  scene behind it (game core, renderer page with the four rows and the track
  title line, observation `musicoptions`/`music:` fields), and `src/main.cpp`
  wires the director: scene transitions, the level feed, per-frame updates,
  the settings-page actions with immediate saves, and the music lines in the
  session and input logs.
- `tools/music/render_previews.cpp`: the offline preview tool with its level
  report; `docs/music/` holds the index and twelve composition records.

Evidence:

- `./build/stackfall_tests` (Debug): 340,000+ checks, 0 failures, including
  the new music suite (catalogue pins, validation rejections, boundary math
  against rational schedules at 22050/44100/48000, chunk independence,
  deterministic renders, loop-wrap clicks, layer levels and phrase switches,
  transport and queue behavior, director policy, WAV bytes) and the extended
  storage suite (the ten-line form, every shipped era, the new rejections).
- `./build/stackfall_audio_tests`: the two-bus mixer through the real
  callback path - music beside cues, independent volumes, the loudest
  stress track at high intensity with every cue voice leaving the format,
  and the master mute fading both buses while the timeline keeps moving.
- `./build/stackfall_render_tests`: the eight-row options page and the
  music page pixel pins (exact glyph counts per row and per mode and volume
  combination, the track title line, layout bounds, navigation markers).
- `ctest` in Debug and in Release (`build-rel`): 5 suites, 0 failures.
- `HARNESS_BUILD_DIR=build tests/harness/run.sh`: PASS, 931 checks, 0
  failures. The new `music-settings` and `music-settings-pad` sessions drive the
  real binary through the page on both inputs - open, one select per row
  (keyboard: mode to Arcade, volumes to 7 and 9, Next track; pad: mode and
  Next track), back keeping the MUSIC row, quit - and prove the saved
  bytes, the input-log lines, and the page layout from screenshots, all
  against the headless reference replay.
- The desktop callback budget of MUSIC.md: the audio suite times the real
  mixer at the device's own 512-frame buffer with the densest track at high
  intensity and every cue voice retriggered - p50 0.047 ms, p99 0.084 ms,
  about 1 percent of the 11.61 ms buffer, two orders of magnitude inside
  the proposed 25 percent device budget (recorded in
  `notes/performance.md`). The incremental audio memory of MUSIC.md's budget
  stays far under the 8 MiB cap: the wave and noise tables are 40 KiB, the
  twelve scores hold 19,197 notes (225 KiB), and the cue buffers add about
  380 KiB.
- `tools/arm64/build-arm64.sh` (run id `t062-music-2026-11-04`, cached
  image): the ARM64 Release build compiled the new sources, the full ctest
  pass ran twice inside the ARM64 environment with 0 failures, and the state
  hashes match the desktop build. Evidence in
  `build/agent-checks/t062-music-2026-11-04/`.
- `build/stackfall_music_preview --intensity all`: 36 WAV renders, 0
  clipped samples, DC within 61 of 32768, voices at or under the 12-voice
  budget, loudest peak 28955 of 32767; report in
  `build/music-previews/report.txt`.

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
HARNESS_BUILD_DIR=build tests/harness/run.sh
./build/stackfall_music_preview --intensity all --out build/music-previews
STACKFALL_ARM64_SKIP_IMAGE_BUILD=1 \
  STACKFALL_ARM64_RUN_ID=t062-music-2026-11-04 tools/arm64/build-arm64.sh
```

- The release package was refreshed with the music binary: `package.sh`
  rebuilt `build/portmaster-package/stackfall.zip` (archive SHA-256
  `37a7796c...b7991`), `portmaster/README.md` and `release-notes.md` now
  describe the eight-row Options screen, the music settings page, portrait
  play, and the soundtrack (the portrait revision had left both stale at six
  settings), `BUILDING.md` pins the new ARM64 hash `2e7790b8...c5f3a`, and
  `tests/harness/t019.sh` and `t035.sh` pass (run ids `...-t062b`),
  including the clean-container binary reproduction.

Findings and open questions:

- The 12-second level windows the test suite pins stay under 24000; the
  full-length previews reach 28955 (about -1.1 dBFS) in the loudest high
  bars. No render clips, and the device bus adds the music volume default of
  6 (about -6.6 dB) under the final clamp, so the shipped mix sits lower
  than the preview peaks.
- The listening pass stays open by rule: an agent must not claim a listening
  check from source inspection alone. The twelve records in `docs/music/`
  carry listening status pending until a human plays the previews and the
  game on headphones and on the handheld speakers.
- The creative choices (instrument set, track forms, mixes) were delegated
  to this implementation and are recorded per track in `docs/music/`; they
  go to the design review, not to this note.

### Review repair: the testing guide catches up (2026-11-04)

The T062 review found `docs/AGENT_TESTING.md` still describing the
seventeen-session, seven-row era. The guide now drives nineteen sessions,
lists `music-settings` and `music-settings-pad` (page opened on both inputs,
one select per row, back keeping the MUSIC row, saved bytes and input-log
lines against the reference replay), calls the options screen eight-row
with the wrap past MUSIC, and names the eleven-line settings form; the
`run.sh` header says nineteen and eight-row too, so README, guide, and
script agree; the `check_harness.py` proof docstring gained the T062
bullet. Fresh run at the final sources (`HARNESS_RUN_ID=t062-review-repair`,
`build/agent-checks/t062-review-repair/`, `source.sha256` pins the exact
files): ctest 5 of 5, nineteen sessions, 931 of 931 checks, 0 failures.

## T043: game-over overlay nests between the HUD columns (2026-11-04)

Source identity: `6b69d5114a4d` (detached worktree) plus the uncommitted
T043 candidate changes; the capture's `source.sha256` pins the exact
`src/renderer_sdl.cpp` (`6863f9b6...`) and
`include/stackfall/renderer_sdl.hpp` (`37c6f38c...`) the reviewed binary
was built from (`build/agent-checks/design-review/t043-fix/`).

What changed: the owner decided on 2026-11-04 (choice delegated to
review) that the game-over panel nests between the HUD columns, matching
the pause overlay of T037, so the dimmed SCORE, LINES, and LEVEL
fragments no longer peek past the panel edge. `gameoverPanel()` now
shares `pausePanel()`'s horizontal extent - x=320..639 with the outline
at x=319 and x=640 - and keeps its 440 px height and canvas center; the
decision record is in `notes/design-review.md` "Game-over overlay
nesting". The decision, the criterion, and the packet expectation landed
together:

- `include/stackfall/renderer_sdl.hpp`, `src/renderer_sdl.cpp`: the
  nested `gameoverPanel()` and the comments that now describe both
  overlay panels as nesting.
- `docs/SPEC.md` section 6.1: both panels nest between the two columns.
- `tests/test_renderer_sdl.cpp`: `run_gameover_overlay_checks()` pins
  the nesting from pixels - the panel spans exactly the gap from the
  left column's edge to the right column's edge, matches
  `pausePanel()`'s extent, and neither column band holds a panel-fill
  pixel - and pins the row budget (both 56 px margins, a five-glyph
  label, and an eight-digit score at the menu scale fit the 320 px
  width).
- `tools/design-review/check_fixes.py`: the new required
  `gameover-panel-nests` criterion (no panel pixel in either HUD
  column, the panel face inside the gap between them).
- `tools/design-review/packet.tsv`: the `dr-gameover` expectation states
  the nested position; `tools/design-review/prior-findings.json` records
  the finding as decided and implemented, so later rounds do not report
  the position as a defect.

Verification:

- `ctest` 5 of 5 in Debug (`build/`) and Release (`build-rel/`), both
  rebuilt with zero warnings.
- The nesting assertions bite: restoring only the old panel
  `{240, 52, 480, 440}` fails `stackfall_render_tests` at the new pins
  (`panel.x == kLeftPanelX + kLeftPanelWidth` and the two beside it)
  before any screenshot check runs; restoring the candidate passes
  again.
- `tests/harness/run.sh` twice (run ids `t043-harness`,
  `t043-harness-final`, the latter at the final tree): seventeen
  sessions, 931 of 931 checks, including the landscape and portrait
  scene cycles whose game-over checkpoints pass unchanged (the portrait
  panel of T055 keeps its board-centered geometry).
- `tests/harness/t024.sh` (run id `t043-t024`): 291 of 291 checks,
  including its own game-over, restart, and X11 letterbox sessions.
- Fresh packet `t043-fix` (`DR_RUN_ID=t043-fix
  tools/design-review/capture.sh`, seed 31337, the four `packet.tsv`
  sessions, twelve images): `check_fixes.py` passes 5 of 5 required
  criteria, `gameover-panel-nests` measuring 0 panel pixels in the left
  column, 0 in the right column, and 134070 inside the gap; the report
  is saved beside the packet as `fix-checks.json`. Measured from the
  `gameover.png` pixels, the panel face spans exactly x=320..639 and
  y=52..491 with 442 outline pixels at each of x=319 and x=640.
- `gameover.png` inspected: the panel sits between the columns, the
  veiled statistics labels `(53, 57, 70)` and all five veiled previews
  read whole beside it, no dimmed fragment hides under the panel edge,
  and the heading, the three value rows, the NEW RECORD note, the menu
  items, and the footer hint sit whole inside the narrowed panel with
  nothing clipped or overlapping.

Commands:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
cmake --build build-rel && ctest --test-dir build-rel --output-on-failure
HARNESS_RUN_ID=t043-harness-final tests/harness/run.sh
T024_RUN_ID=t043-t024 tests/harness/t024.sh
DR_RUN_ID=t043-fix tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/t043-fix/packet \
  --json build/agent-checks/design-review/t043-fix/fix-checks.json
```

Open questions: none from this change. The overlay geometry question of
`notes/design-review.md` is decided and closed; physical-device checks
stay with the blocked T018/T020 gates.

## T061: the release package refreshed at the T043 revision (2026-11-04)

Task T061, second refresh. The package was stale at HEAD again: commit
`a3a290b` (T043, the game-over overlay nesting) changed
`src/renderer_sdl.cpp` and `include/stackfall/renderer_sdl.hpp` after the
T062 refresh had pinned `2e7790b8...c5f3a` in `portmaster/BUILDING.md`, and
T043's file list includes no `portmaster/` file, so the pin described a
binary two game-source revisions old. Sources: the task contract of T061 and
[agent testing contract](../AGENT_TESTING.md). Tested in the detached
worktree of `afe92e1` plus uncommitted task changes. No game code changed;
the only repository edit is the pin.

The refresh, in the order the task contract lists:

- `tools/arm64/build-arm64.sh` reran against the pinned image
  (`STACKFALL_ARM64_SKIP_IMAGE_BUILD=1`, the same invocation
  `tests/harness/t019.sh` uses). Evidence
  `build/agent-checks/t061-arm64-2026-11-04-refresh/`: the binary reports
  `ARM aarch64`, links only `libSDL2-2.0.so.0`, `libstdc++.so.6`,
  `libgcc_s.so.1`, `libc.so.6`, and `libm.so.6`, and hashes
  `9220a074bd785b072a77bb37c418ae0f7a3ebda320db6697bead9aca86b69041`. The
  glibc symbol span is still 2.17 to 2.34, so `min_glibc` 2.34 stays. The
  suite ran twice inside the environment and `ctest` passed 5 of 5 there; the
  printed state hashes equal the desktop run's. The hash equals the one the
  review build at this revision printed, so the build is deterministic and
  only the pin was stale.
- `portmaster/BUILDING.md` now names that hash. Nothing else in the file
  moved: the environment, the commands, the `file` form, the runtime
  libraries, and the glibc span are unchanged, because T043 touched only
  renderer sources since the T062 refresh.
- `tests/harness/t019.sh` repackaged and drove the archive end to end.
  Evidence `build/agent-checks/t019-package-2026-11-04-t061b/` (78 checks, 0
  failures), including the check the stale pin failed: BUILDING.md names the
  SHA-256 of the shipped binary. The `repro` phase rebuilt the tree in a
  fresh container from the pinned image into a clean build directory and
  printed the same hash.
- `tests/harness/t035.sh` after the edit (31 checks, 0 failures), because
  BUILDING.md ships in the archive and that gate reads every shipped file.

Package identity of the refreshed archive:

- archive `build/portmaster-package/stackfall.zip`, SHA-256
  `4b9d10f0ad28130593dd7319916b1935fba31d6e41b93f2349ff5853e42baea0`;
- binary `stackfall/stackfall.aarch64`, SHA-256
  `9220a074bd785b072a77bb37c418ae0f7a3ebda320db6697bead9aca86b69041`,
  named by the BUILDING.md inside the archive.

Supporting checks, all passed:

- Desktop suite before the build (the hash-comparison half of the ARM64
  script needs it): `build/stackfall_tests --seed 987654321`, 342,539
  checks, 0 failures.
- The t019 checker decoded and measured the archived screenshot (640 x 480,
  4:3, centered bars) as part of its 78 checks.

Commands:

```bash
cmake --build build
STACKFALL_ARM64_SKIP_IMAGE_BUILD=1 \
  STACKFALL_ARM64_RUN_ID=t061-arm64-2026-11-04-refresh \
  tools/arm64/build-arm64.sh
T019_RUN_ID=t019-package-2026-11-04-t061b tests/harness/t019.sh
T035_RUN_ID=t035-version-2026-11-04-t061b tests/harness/t035.sh
```

Findings and open questions:

- The archive hash changes between package runs by design: the screenshot is
  a real-time-paced capture, so the fall row of the piece in flight can move
  by a row. The binary hash does not move; a clean container reproduced it.
- The blocker note of the blocked device task T018 still names the retired
  pin `dd51324f...` and archive hash `7d5e0f63...`, both now three package
  generations old. Workers cannot edit the backlog, so the refreshed hashes
  above are recorded here and reported as a follow-up against T018. The
  operator who runs the device pass must take the archive from
  `build/portmaster-package/stackfall.zip` and its `package-report.txt`.
- The blocked gates are otherwise unchanged: T018 (the RG503 checklist),
  T020 (the fresh ROCKNIX install), T021 (publication), and T036 (device
  frame-time percentiles).


## T065 profile and stutter fix (2026-11-05)

Round post-v1.0 (device feedback). Status: passed.

- Source identity: commit `d7d2783` plus the working tree of this round;
  the measured baseline's hashes are in
  `build/agent-checks/t065-20261105/host-baseline/source.sha256` (HEAD plus
  the audio measurement counters alone) and the fixed build's in
  `host-fix1/source.sha256`. Evidence:
  `build/agent-checks/t065-20261105/`.
- Commands: `T065_RUN_ID=t065-20261105 tests/harness/t065.sh` (baseline),
  the same with `T065_TAG=fix1` and `T065_RENDERERS=dummy-software` for the
  fixed build, plus the x11 spot check listed in `notes/performance.md`.
  Regression gate: `ctest --test-dir build`, `ctest --test-dir build-rel`,
  `tests/harness/run.sh` on both builds, and the T025 `hardkill` phase.
- Scenarios and their verify claims: `rotate-move` (play 100%, 175
  rotations each way), `drops` (play 100%, 2632 soft-drop ticks, 94 hard
  drops), `clears` (play 100%, 125 labeled four-line fixture cycles),
  `pause` (65% play, 35% paused, 41 pause and resume cycles),
  `pad-mixed` (94.9% play, both rotations, movement, soft drop, hard drop,
  and pause on the virtual controller). Every scenario held game over at
  0.0% of its ticks and restarted its round at least three times.
- Gate outcomes: `ctest` 5/5 suites in Debug and in Release (pinned replay
  hashes included); `tests/harness/run.sh` passed 931 checks with 0 failures
  on both builds (`build/agent-checks/t065-regress-debug/`,
  `t065-regress-release/`); the T025 `hardkill` phase passed 24 kills with
  every record file canonical (`build/agent-checks/t065-hardkill/`).
- Result: the T052 mid-round record save is the confirmed stutter cause
  (2.59 ms median, 6.20 ms max per save on this host, paid on the gameplay
  thread once the round passes the stored record); it moved to a writer
  thread with the T052 guarantee kept. Music synthesis, pacing, resource
  growth, and diagnostic logging measured and found not actionable. The
  full tables and the fix's before/after evidence are the T065 section of
  `notes/performance.md`.
- Open questions: the device tail measurements stay with task T036; the
  capture procedure `tools/perf/device-capture.sh` is what runs them.

## T067 hard drop on the logical up direction (2026-11-05)

Round post-v1.0 (device feedback). Status: passed.

The default controller hard drop moved from X to D-pad up in landscape, and
to the logical up direction of the rotated grip in portrait - the device
D-pad's left arm - so X and the portrait cluster buttons it freed carry
nothing. The desktop keyboard defaults are unchanged. The pin and its
rationale are the T067 section of `notes/design-review.md`, which supersedes
the cluster-role half of the T055 pin.

- Source identity: commit `036616b` plus the working tree of this round; the
  harness recorded the per-file hashes in
  `build/agent-checks/t067-harness-final/source.sha256`.
- What changed: the two controller tables and the two portrait tables of
  `src/input_sdl.cpp` (the one mapping location of T009), the controls rows
  of `src/renderer_sdl.cpp`, the return-to-play gate of `src/main.cpp`
  (below), the mirrored tables and gate of
  `tests/harness/reference_replay.cpp`, the pad scripts that hard-dropped on
  X, and the specification, both player READMEs, and this guide.
- The return-to-play gate. D-pad up now carries MenuUp and HardDrop in one
  press, so a queued press must not drop the piece of a round that opens
  next. The rules core already spends the gameplay half of every menu tick,
  because the tick that changes the scene reads one half only; the one
  remaining path is a scene change between ticks, and a scripted `restart`
  is exactly that. The platform layer now spends every gameplay edge of the
  first tick a round owns before the core sees the frame, exactly like the
  HOLD gate beside it, and the reference replay mirrors the gate so both
  streams stay identical. The gate changes nothing when no edge waits, so
  every pinned replay hash held.
- Commands and outcomes, all at the same tree:
  - `ctest --test-dir build` and `ctest --test-dir build-rel`: 5 of 5 suites
    each, with the pinned replay hashes unchanged (the inputs of the rules
    core did not move). The input suite grew to 740 checks, the render suite
    to 16932.
  - `HARNESS_RUN_ID=t067-harness-final tests/harness/run.sh`: PASS, twenty
    sessions, 978 of 978 checks (`build/agent-checks/t067-harness-final/`);
    the same command against the Release build
    (`HARNESS_BUILD_DIR=build-rel HARNESS_RUN_ID=t067-harness-release`) also
    passed 978 of 978 (`build/agent-checks/t067-harness-release/`).
  - `T024_RUN_ID=t067-t024 tests/harness/t024.sh`: PASS, 291 of 291
    (`build/agent-checks/t067-t024/`).
  - `T015_RUN_ID=t067-t015-full tests/harness/t015.sh`: PASS, 434 of 434,
    including the nested harness and T024 reruns and the pinned M3 hashes
    (`build/agent-checks/t067-t015-full/`).
- The new session `harddrop-pad` (T023 harness, both inputs of the landscape
  table) proves the acceptance end to end against the reference stream: an X
  press leaves the piece, the board, and the score standing still; D-pad up
  locks with the section 4.6 arithmetic; the same D-pad-up press moves the
  cursor up on the title, pause, and game-over menus; the resume, Play
  Again, and a scripted restart from the title each return to play with
  nothing settled and no score; and the desktop keyboard defaults ride the
  same session (X rotates one step, Space hard drops). The portrait sessions
  keep their rotated pins on the new table: the checker's ordered walk now
  requires `key left`/`pad left` to carry `drop=1 menu=1000` together, which
  the landscape table cannot produce.
- The negative control for the gate: a build with the gate disabled
  hard-drops the fresh round's first piece on the restart (`settled=4`,
  `score=36`, and the input-log tick after `script restart` reads
  `drop=1 menu=1000`). Evidence saved under
  `build/agent-checks/t067-negative/`; the enabled build reads `drop=0
  menu=1000` and settles nothing. The controls-screen rows have their own
  negative control inside the render suite: drawing `X` for the hard-drop
  row fails the exact glyph-count pin.
- Script retuning: the portrait scripts' hard-drop spam was trimmed from 45
  presses to the 9 that top the round out, because `pad left`/`key left`
  now carry MenuUp and the surplus presses moved the game-over cursor
  instead of vanishing; the ninth press is the one that blocks the next
  spawn, so the over cursor opens on PLAY AGAIN. The T065 generator's pad
  scenario hard-drops on D-pad up now; a generated 300-tick `pad-mixed`
  scenario ran clean with four drops logged.
- Open questions: none for the desktop half. Physical confirmation that the
  marked D-pad arm sits where the rotated grip puts it stays with the
  blocked device task, and the final input regression pass stays with T070.

## T068 the orientation selection applies in the running session (2026-11-05)

Round post-v1.0 (device feedback). Status: passed.

The Landscape and Portrait selection now takes visible effect in the session
that selected it: the composition, the rotated drawing, both input tables,
and the ORIENTATION row's own value all turn inside the tick that consumed
the select edge, so no restart and no relaunch is needed. The turn also
forgets every held level and pending press edge, because both store the
action the *old* table gave the control and the release that follows arrives
through the new table, where it cannot clear the level - without the clear,
a direction held across the turn stays held for the rest of the session. The
pin and the safe-boundary rationale are the T068 section of
`notes/design-review.md`, which replaces the next-launch half of the T055
pin; T055's own record stands.

- Source identity: the working tree of this round; the harness recorded the
  per-file hashes in `build/agent-checks/t068-harness/source.sha256`.
- What changed: `SdlInput::setOrientation()` clears the adapter
  (`InputAdapter::clearHeldAndPending()`, the new counterpart of the
  focus-loss `releaseAllHeld()`), `cycleOrientation()` in `src/main.cpp`
  calls `applyOrientation()` on every select instead of writing the
  preference alone, the renderer's separate orientation-preference mirror is
  gone (the row reads the layout the renderer draws, so the two can no longer
  disagree), and the headless reference of `tests/harness/reference_replay.cpp`
  swaps its own key and pad tables and clears its adapter on the same select
  edges. The device canvas never moves: it stays 960x544 in both
  orientations because the renderer rotates every primitive inside it, so
  the window, the letterbox, and the screenshot capture needed no change and
  the game has no pointer input to remap.
- The staged portrait sessions became the same-session proof. Stage one
  (`tests/harness/scripts/orientation-keyboard.script.txt` and
  `orientation-pad.script.txt`, replacing the shared
  `orientation-portrait.script.txt`) walks to the ORIENTATION row in
  landscape, holds a direction across the select - key left or D-pad left,
  which holds move-left in landscape and is the logical up direction that
  hard drops in portrait - and then plays the turned session: the title walk
  on the rotated roles, a round whose logical right moves right, logical
  left moves left, Z or B rotates clockwise, and logical up hard drops, the
  pause overlay walked on the rotated roles, a turn back to landscape, a
  second turn to portrait, and the quit through the portrait table. Three
  turns in one process, and the settings file the stage leaves holds
  portrait for the relaunch of stage two.
- The checker's new pins (`tests/harness/check_harness.py`):
  `check_portrait_options_layout` reads the turned options page in the
  vertical device bands the rotation gives its heading, eight rows, and
  footer, with the highlighted row bright and marked, which a landscape page
  cannot satisfy because its bands are horizontal; and
  `check_turn_spends_input` reads the per-tick input log: every
  `orientation ... status=ok` line follows the tick that carried the select
  edge (`menu=0010`), the first turn's tick also carries the probe's held
  level (`L=1`), and every tick after each save carries no bit at all until
  the script presses again. The stage also requires the exact portrait
  settings bytes, the rotated play frame and quiet title from pixels, and
  that neither log carries next-launch wording.
- The negative control: a build with `setOrientation()` reduced to the plain
  assignment (the clearing removed) fails the same sessions - the held
  move-left level survives the turn, the piece of the round that follows
  starts at column 1 instead of 3 and slides while the script presses
  nothing, the reference streams diverge on the first round record, and
  `check_turn_spends_input` reports `only 0 tick(s) after the orientation
  save ... are empty`. Evidence kept under
  `build/agent-checks/t068-negative-leak/` (12 failures across the two
  portrait sessions); the passing build reads the turn's ticks empty and the
  round's spawn at column 3.
- Runs at the working tree of this round:
  - `ctest --test-dir build` and `--test-dir build-rel`: 5 suites green in
    Debug and Release (13,271 render-suite checks, 0 failures), including
    the new `check_orientation_turn_clears_input` cases of
    `tests/test_input.cpp` and `tests/test_input_sdl.cpp` and the
    options-page turn case of `tests/test_renderer_sdl.cpp`.
  - `HARNESS_RUN_ID=t068-harness tests/harness/run.sh`: PASS, twenty
    sessions, 1052 of 1052 checks (`build/agent-checks/t068-harness/`);
    the same command against the Release build
    (`HARNESS_BUILD_DIR=build-rel HARNESS_RUN_ID=t068-harness-release`)
    also passed 1052 of 1052 (`build/agent-checks/t068-harness-release/`).
  - `T015_RUN_ID=t068-t015-hashes T015_PHASES="builds hashes"
    tests/harness/t015.sh`: M3 GATE PASS, the rules suite byte-identical in
    Debug and Release and the pinned scenario hashes unchanged
    (`build/agent-checks/t068-t015-hashes/`).
- Landscape keeps its bytes. Against the pre-change baseline run
  (`build/agent-checks/baseline-t068/`, the same twenty sessions at the
  integrated revision before this change), the observation streams of all 26
  unaffected stages are byte-identical outside the screenshot path, and the
  screenshots of the landscape sessions compared (`normal-game-clears`,
  `options-ghost`, `harddrop-pad`) are byte-identical file for file. The
  four portrait stages are the only differences: stage one is the new
  script, and stage two differs in the `high` field alone (34, the record
  the new stage one's abandoned round persisted mid-round, against 0
  before) - every tick field, and so every timing pin, is unchanged.
- Open questions: none for the desktop half. Physical confirmation that a
  mid-session turn reads well on a real RG503 stays with the blocked device
  task (T018), and the final performance and input regression pass stays
  with T070.

## T066 the controls customization screen (2026-11-05)

Round post-v1.0 (device feedback). Status: passed.

Every gameplay action of both orientation tables is now player-editable. The
CONTROLS screen's confirm opens a customization screen with one row per
action - the eight of specification section 5 - showing the controls each
device holds in the orientation in force, plus RESTORE DEFAULTS. Confirm arms
a capture; the next press of a rebindable control of either device names the
new binding and never reaches the round or a menu. Pressing a control the
action already uses cancels, a capture that would give one control two
gameplay actions in one table is rejected with a message that names the
conflict, and the menu roles never rebind, so every menu stays reachable
whatever the tables hold. Custom bindings persist as `controls.dat` beside
the record through the same atomic write, recover to the section 5 defaults
under the storage recovery rule when the file is damaged, and the file is new
so it has no legacy form.

- Source identity: the working tree of this round; the harness recorded the
  per-file hashes in `build/agent-checks/t066-final/source.sha256`.
- What changed: the two constexpr mapping tables of `src/input_sdl.cpp`
  became the `ControlBindings` model of `include/stackfall/bindings.hpp` and
  `src/bindings.cpp` (four tables - keyboard and controller in landscape and
  portrait - because the portrait cluster reversal is not a rotation of the
  landscape table; the defaults reproduce the pinned tables byte for byte),
  `controls.dat` joined the record and the settings in
  `include/stackfall/storage.hpp` and `src/storage.cpp`, `SdlInput` grew the
  binding tables, the capture, and the `setControlBindings` clear, the scene
  machine gained `Scene::Rebind` appended after `PauseControls` so every
  pinned numeric scene value kept its meaning, and the renderer, the
  developer script language (`findKey` resolves every rebindable word), and
  the reference replay mirror the whole path. The two new bugs the fresh
  `tests/test_bindings.cpp` caught before any device run: the formatter
  wrote the keyboard block before the pad block while the parser, the
  specification, and the staged templates all pin pad first - so a file the
  game wrote, the game itself rejected on the next launch - and the validity
  rule accepted one control twice inside one action. Both are fixed and
  pinned.
- The harness grew four sessions. `controls-keyboard` and `controls-pad`
  walk the screen end to end on one input each: open, capture on HARD DROP
  (D on the keyboard, X on the pad), a rejected capture that names ROTATE
  CCW as the holder of Z/B, a cancelled capture, the round-level proof that
  the freed control settles nothing while the bound one locks the piece,
  egress through both menus, and RESTORE DEFAULTS ending on the exact
  section 5 bytes. The pad session adds the unplug - a held direction dies
  with the pad while no counter moves - and the replug, whose new instance
  answers the custom binding at once. `controls-relaunch` runs two stages
  over one save directory, both marked `settings=none` (the new manifest
  field for a stage that must write no settings file; the driver removes the
  file first and fails the run if the stage grows one), so it also proves
  the customization path writes no settings file of its own: stage one saves
  both custom tables, stage two reads them at startup, plays them, and
  restores the defaults. `controls-invalid` starts one stage from
  `controls=broken-controls.dat` - Z bound to hard drop and rotate
  counter-clockwise together - and requires the recovery to the defaults,
  every menu reachable, and the damaged file byte-identical to how the
  stage found it. The reference mirrors the path through `--controls` and
  its own capture state, so all five game streams equal their reference
  streams field for field.
- The checker pins the sessions from the specification alone: the canonical
  `controls.dat` bytes are rebuilt in `check_harness.py` from the section 5
  tables - never read back from the game's formatter - with the per-session
  overrides applied, and every stage's before and after snapshot must equal
  them exactly. The input log must carry the capture evidence lines
  (`controls capture ... result=applied|rejected|cancelled`,
  `controls restore result=defaults`), the startup lines name default or
  custom bindings and how they were obtained, and the scene walks and menu
  cursors are pinned step by step.
- The headless suites grew `tests/test_bindings.cpp` (defaults, name
  vocabularies, menu roles, the conflict rule, the file's canonical,
  damaged, and store round trips; registered in `stackfall_tests`),
  `tests/test_input_sdl.cpp` gained six checks (the id-to-SDL translations,
  custom tables on both devices with the roles intact, capture semantics
  including auto-repeat and release, the mid-session clear, custom bindings
  driving a round with held levels and one edge per catch-up frame, two
  devices at once, unplug and replug), and `tests/test_renderer_sdl.cpp`
  gained `run_rebind_screen_checks`: every cell of the eight rows, the
  restore row, the accent capture row, the status line, both footers, and
  the portrait half of the page at the portrait metrics, pinned by exact
  glyph counts.
- Runs at the working tree of this round:
  - `ctest --test-dir build` and `--test-dir build-rel`: 5 suites green in
    Debug and Release - 342,915 headless checks, 850 input checks, 13,329
    render checks, all with 0 failures.
  - `HARNESS_RUN_ID=t066-final tests/harness/run.sh`: PASS, twenty-four
    sessions, 1232 of 1232 checks (`build/agent-checks/t066-final/`); the
    same command against the Release build
    (`HARNESS_BUILD_DIR=build-rel HARNESS_RUN_ID=t066-final-release`) also
    passed 1232 of 1232 (`build/agent-checks/t066-final-release/`).
  - `T015_RUN_ID=t066-t015-hashes T015_PHASES="builds hashes"
    tests/harness/t015.sh`: M3 GATE PASS - the rules suite is
    byte-identical in Debug and Release and every pinned scenario hash is
    unchanged with the default bindings
    (`build/agent-checks/t066-t015-hashes/`).
- The pre-change baseline: a detached worktree of the integrated revision
  (`ec14225`, before this change) ran the same harness
  (`build/agent-checks/baseline-t066/`, twenty sessions, PASS). All 30
  observation streams of the sessions and stages that existed before T066
  are byte-identical to the final run's outside the screenshot path, and
  the screenshots of every compared session are byte-identical except the
  three controls-page captures (`controls-screen.bmp` of both scene cycles
  and `pause-controls-open.bmp`), which differ exactly by the new
  "A CUSTOMIZE" footer the mappings page gained.
- Open questions: none for the desktop half. Physical confirmation that the
  capture reads well on a real RG503 - the timing of a capture against the
  pad's debounce, and the landscape D-pad-up hard drop staying reachable
  after players rebind it - stays with the blocked device task (T018), and
  the final input regression pass stays with T070.

## T022: front pages state the harness state (2026-11-05)

Task T022, the standing documentation repair: the two front pages state the
actual integrated state and agree with each other. Sources: the task
contract of T022 and the T066, T067, and T068 records above. Tested in the
detached worktree of `54f6be3` plus the uncommitted task changes. No
production source changed.

The repair: `README.md` still said the harness "drives nineteen sessions"
while `tests/harness/run.sh` and the testing guide drive twenty-four, and
its coverage list omitted the controls-customization sessions of T066 and
the default-hard-drop session of T067. The README now counts twenty-four
sessions and names them: the scene cycle on both inputs, a planned game
from an empty board, the landscape default hard drop on D-pad up with X
carrying nothing, the controls customization screen on both inputs with
the custom table kept across a relaunch and a damaged file recovered to
the defaults, and the three labeled board fixtures, beside the options,
music, settings-file, hold-off, Next-count, grayscale, record, and portrait
sessions it already named. The status paragraph also lists the controls
customization screen, the D-pad-up default, and the orientation switch
that applies in the running session, and it states that the assembled
archive predates them, because the package was last built before T065
through T068 changed the sources. `docs/SPEC.md` adds the soundtrack
(T062), the same-session orientation switch (T068), the controls screen
and the D-pad-up default (T066, T067), and the pending archive refresh
(T071) to its status line, so both front pages agree again.

Verification, all passed:

- `HARNESS_RUN_ID=t022-readme-check-2026-11-05 tests/harness/run.sh`:
  PASS, twenty-four sessions, 1232 of 1232 checks, 0 failures, evidence in
  `build/agent-checks/t022-readme-check-2026-11-05/` (`source.sha256`
  pins the exact sources). The run proves the rewritten sentence:
  `sessions.txt` lists the twenty-four names, and the checker pins the
  claims it makes - `harddrop-pad/x-carries-nothing` and
  `harddrop-pad/up-hard-drops`, `controls-relaunch/stage2-read-custom-file`
  beside `stage2-space-dead` and `stage2-d-drops`, and
  `controls-invalid/damaged-file-staged` with
  `controls-invalid/d-carries-nothing` after the recovery to defaults.
- Release `ctest` in `build-rel`: 5 of 5 suites.
- The session count agrees across `README.md`, `docs/AGENT_TESTING.md`,
  and the `run.sh` header (twenty-four in each), no "nineteen" remains,
  every relative link in `README.md` resolves, and the lines the repair
  touched keep each file's width.

Findings and open questions:

- The release archive still carries the pre-T065 binary, so the README and
  the specification now say its refresh is pending; that refresh is task
  T071, and the blocked gates are unchanged: T018 (the RG503 checklist),
  T021 (publication), T063 and T064 (the soundtrack listening and design
  passes), and T070 (the final regression pass after the device-feedback
  features).

## T073: the release scope names no shipped feature excluded (2026-11-05)

Task T073, a documentation repair from the specification review: the
section 2 exclusion list of `docs/SPEC.md` still ended with "and an
in-game controller rebinding UI. Can be considered for v1.1+", while
section 5 (the customization subsection headed "Controls customization
(task T066)", now lines 263 to 303), the REBIND state of section 4.7,
and the `controls.dat` paragraph of section 9 specify that same
customization screen as the shipped task T066 feature. Tested in the
detached worktree of `ec438a9` plus the uncommitted task changes. No
production source changed.

The repair: the exclusion sentence drops the rebinding clause and now ends
at "and touch controls", so it names only features the release does not
contain. A second paragraph states that the customization screen of
section 5 (task T066) is in scope for v1.0 and must not be re-added from
earlier drafts of the list, because the frozen specification snapshot
`81C99D2E-4C46-4F86-BDA9-D95D758ECE0F.md` still carries the old sentence
unchanged as the approved proposal of the specification phase, and a later
sync from it would restore the contradiction. The snapshot stays
untouched on purpose: it records what was approved before any code
existed, and the living specification alone states the shipped scope.

Verification, all passed:

- The exclusion paragraph no longer contains the word "rebind", and the
  fifteen items it still lists were each checked against the
  specification and the sources: no online play, multiplayer,
  leaderboard, achievement, custom shape, 180-degree rotation, T-spin,
  combo, user-facing replay, theme, audio asset, sprite or font asset,
  particle effect, or touch control ships. The soundtrack of section 6.4
  is generated code and reads no audio file, and `grep` over `src/` and
  `include/` finds no touch event, no `Mix_Music`, and no `.wav` or
  `.ogg` reference.
- Section 5 still specifies the customization screen, task T066, the
  rebindable actions, and `controls.dat`; sections 4.7 and 9 are
  unchanged, and the status block at the top of the specification already
  names the controls customization screen (T066, T067), so no page of the
  living specification contradicts another.
- The controls-keyboard harness session ran at this revision from a clean
  temporary directory: exit 0, "Clean shutdown" in the log, thirty
  checkpoints, the scenes `title`, `controls`, `rebind`, `playing`, and
  `paused` in the observation stream, the full set of rebinding
  screenshots from `rebind-open` through `defaults-restored`, and the
  canonical thirty-three-line `controls.dat` in the save directory. The
  shipped screen the exclusion sentence denied is the screen the session
  drives.
- Debug and Release `ctest`: 5 of 5 suites in each configuration, and no
  test or checker reads the exclusion sentence, so the documentation
  change touches no pinned expectation.

Findings and open questions:

- None. The exclusion lists of the frozen snapshot and of the
  specification-phase briefs stay as history; only the living
  specification states the shipped scope.
