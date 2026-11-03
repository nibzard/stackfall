# Agent testing

Develop and verify the game without intermediate human or physical device
tests. Request those tests only after the local release package is complete.
Keep final device acceptance and publication as separate blocked tasks.

`docs/SPEC.md` defines the game rules. This file defines how agents verify
the software. Older briefs do not override this testing schedule.

## Evidence

Store each run in an ignored build directory, such as
`build/agent-checks/<run-id>/`. Keep a short report in `notes/agent-testing.md`.
Include the date, source identity, commands, seeds, results, artifact paths,
verified findings, and open questions. Record a commit identifier when one
exists. Otherwise, record hashes of the tested source files.

Label checks as passed, failed, or unavailable. Save failures before fixing
them. Rerun affected checks after a fix. Never mark unavailable checks as
passed. Do not require a human to replace a missing desktop test tool.
Document tool setup so another agent can reproduce the run.

## T023: Desktop test harness

Build a bounded driver for the real SDL application. Use a virtual display
when needed. Keep the headless rules suite separate from these platform tests.

1. Add developer options for a fixed seed, bounded runtime, and explicit save
   directory. Preserve normal startup when no test options are supplied.
2. Inject keyboard events through the SDL adapter. Use an SDL virtual
   controller to exercise controller mappings, connection, and disconnection.
   Record unsupported controller tests as unavailable until the tool is added.
3. Capture screenshots at named checkpoints. Export structured observations
   for scene, active piece, board, hold, queue, score, lines, level, and ticks.
4. Drive title, controls, play, pause, resume, game over, restart, and quit.
   Bound every wait. On failure, save the last screenshot and log, then exit
   with a failing status. Clean up the game and virtual display.
5. Check observations against independent expectations. A successful process
   exit or a screenshot file alone does not prove that gameplay works.

Deterministic scenarios may use explicit developer fixtures for difficult
board states. Label fixtures in the evidence. Keep them out of normal play.
Include normal games from an empty board so fixtures cannot hide integration
failures. Verify repeatable seeds and fixture provenance.

### Running the harness

One command runs the scene cycle unattended:

```bash
tests/harness/run.sh
```

It builds `stackfall` and `stackfall_reference`, runs the headless `ctest`
suites first, then drives six sessions through the real SDL application under
`SDL_VIDEODRIVER=dummy` with the SDL dummy audio device, a fixed seed, an
explicit save directory, a frame bound, and a process timeout:

- `scene-cycle-keyboard`: title, controls, play, pause, resume, restart,
  game over, play again, game over, title, quit on the keyboard.
- `scene-cycle-pad`: the same cycle on the developer virtual controller,
  plus a mid-game detach while a direction is held.
- `normal-game-clears`: a planned game from an empty board with rotations,
  taps, hold, soft drop, delayed auto-shift, one line clear, and a pause.
- `fixture-four-line`, `fixture-lockout`, `fixture-blockout`: labeled board
  fixtures for the four-line clear and both game-over rules.

Every session runs twice. The headless `stackfall_reference` replays the same
script through the rules core alone and writes the checkpoint records the
core prescribes; then the real application writes its own. The checker
compares the two streams field by field, so a difference names the field that
diverged. On top of the comparison it asserts expectations pinned from
`docs/SPEC.md` alone - the section 4.6 scoring table, hard-drop and soft-drop
points per cell, the pause freeze, hold restriction, line-clear counts, scene
sequences, and the saved record - so a defect shared by the game and the
reference still fails. Screenshots are checked as 960x544 BMP files, the game
must exit 0 with a clean shutdown, and the normal-game session must repeat
byte-identically. A plain run without developer options proves the hooks
change nothing about normal startup.

The harness exits nonzero on any failure and keeps the log, the input log,
both observation streams, and every screenshot in
`build/agent-checks/<run-id>/`, next to `source.sha256`, which ties the run
to the exact sources and binaries. Environment variables: `HARNESS_BUILD_DIR`
(default `build`), `HARNESS_RUN_ID`, `HARNESS_VIDEO` (default `dummy`;
`x11` under `xvfb-run` also works), `HARNESS_TIMEOUT_S` (default 120).
A reused run id - `HARNESS_RUN_ID` here, `T024_RUN_ID` in
`tests/harness/t024.sh` - removes that run directory before the run starts,
so a rerun cannot inherit the earlier pass's saved high score or its
evidence.

### Developer options of the game binary

```
--seed N              start the round from seed N
--frames N            render N frames, then exit (the runtime bound)
--save-dir DIR        write the high score, settings, and log here
--input-script FILE   run a scripted input session
--input-log FILE      record one line per simulation tick
--observations FILE   write one record per checkpoint command
--screenshot-dir DIR  write one BMP per checkpoint command
--fixture-dir DIR     resolve `fixture` script commands here
--virtual-controller  attach the developer virtual gamepad
--tick-per-frame      one simulation tick per frame, pacing off
```

`--tick-per-frame` is what makes a scripted session exactly comparable: each
frame runs one tick, so a checkpoint's tick number never depends on how the
wall clock batched frames. The script language (`tests/harness/scripts/`):

```
key <name> down|up    push a keyboard event into the SDL queue
pad <name> down|up    press a virtual pad button
wait <ticks>          hold the script until that many ticks ran
stall <ms>            delay the frame, forcing a catch-up batch
detach                detach the virtual controller
restart               restart the round with the same seed
fixture <name>        load a board fixture from --fixture-dir
check <name>          record one observation and one screenshot
shot <name>           record one screenshot, no observation
quit                  leave the main loop cleanly
echo <text>           log a marker line
```

A `check` flushes after its frame's tick and render, so the record and the
image describe the state that was drawn; put a `wait` between a check and the
next input when the record must not include that input's tick.

One observation record looks like:

```
check=<name> scene=<scene> tick=<n> score=<n> lines=<n> level=<n> high=<n>
  record=<0|1> active=<id,rotation,x,y|none> ghost=<...|none>
  hold=<id,locked|id,free|none> queue=<five ids> menu=<menu:cursor|none:-1>
  quit=<0|1> settled=<n> board=<22 four-digit hex masks, y=-2 first>
  colors=<220 hex digits> shot=<path|none>
```

### Fixtures

A fixture is one text file `tests/fixtures/<name>.board.txt`: 22 rows of ten
cells from the hidden row `y=-2` down to `y=19`, `.` empty and a letter
`I O T S Z J L` settling that cell in its piece color, plus `name:`,
`description:`, and `provenance:` fields the evidence copies, and an optional
`active: <letter>` that forces the piece in play. The strict parser
(22 rows, ten cells, letters only, no unknown field) is pinned by the
headless suite in `tests/test_fixture.cpp`. Fixtures exist only behind the
`fixture` script command and the `--fixture-dir` option: no normal startup
path reads them, and every session that uses one labels it in its log. The
pinned sessions keep a normal empty-board game beside them, so a fixture
cannot hide an integration failure.

## T024: Agent play and visual review

Use T023 after the first playable desktop build. The agent must open and
inspect captured images. Pixel checks alone do not satisfy visual review.

1. Play from title to game over, then restart and quit. Repeat through keyboard
   and virtual controller input. Cover all seven shapes across fixed seeds.
2. Check movement, both rotations, wall kicks, ghost landing, hard drop, soft
   drop, hold restriction, and all four line-clear counts against observations.
3. Inspect spawn, hold, queue, ghost, stack, score, and every screen at 960x544.
   Include a smaller letterboxed window. Check clipping, text, contrast, and
   which preview comes next. Check whether unavailable hold is clear.
4. Capture the first frame after each spawn and hold swap. Report any invisible
   active piece or unclear shape. The current I spawn is fully hidden until
   its first downward move. Measure that delay against the rules. Treat a
   proposed spawn-rule change as a design decision; do not silently change it.
5. Check input transitions: pause while moving, resume, restart while a button
   is held, title-to-play confirmation, focus loss, and controller removal.
   Check for stuck movement, lost presses, and unintended hard drops.
6. Inspect landing and clear feedback. Where audio is enabled, check that
   cues occur once per event and mute works. Save available audio evidence.
   Device latency and human judgments remain for final acceptance.
7. Fix reproducible defects within the approved rules and rerun affected
   scenarios. Record unresolved design questions with images and reproduction
   steps. Distinguish completed checks from inferred behavior.

## T025: Stability and failure checks

Run this task after persistence, audio, and platform error handling exist.

- Run a bounded Release session for at least ten minutes of wall time.
  Include normal real-time input and simulated play across several seeds.
  Fast-forward core ticks are a separate test and do not prove frame pacing.
- Run at least 50 automated restart and menu cycles. Check for stuck input,
  crashes, stale state, and growth in controller or audio resources.
- Check save persistence across process restarts. Exercise missing, partial,
  and corrupt saves, plus an unwritable save directory.
- Exercise missing audio, software rendering, renderer failure, focus loss,
  controller removal, and a simulated long frame stall.
- Check frame pacing with vertical synchronization enabled and disabled.
  Record elapsed time, render count, simulation ticks, CPU use, memory use,
  median frame time, 95th percentile, 99th percentile, and maximum frame time.
  A fallback renderer must have a frame limiter when presentation does not
  block. Verify that pacing preserves the 60 Hz simulation.
- Run the rules suite with compiler memory and undefined-behavior sanitizers
  when supported. Investigate failures and retain useful regression cases.

Document host and renderer details. A virtual display can check pacing and
integration, but it cannot certify RG503 frame rates or battery use.

## Final handoff

After T024, run T027 and T028 from `docs/DESIGN_REVIEW.md`.
These tasks add an independent screenshot review and verify required fixes.
After T025, run the T026 optimization loop in `docs/PERFORMANCE.md`.
Then finish the desktop gate, native build, launcher, and local archive. Include one
report with agent results, unresolved questions, reproduction commands, and
the exact package identity. Then request the final human and device pass.
Do not describe the release as device-validated until that pass succeeds.
