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
  before the physical rounds.

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
