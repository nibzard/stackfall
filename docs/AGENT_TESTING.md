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
suites first, then drives twenty-four sessions through the real SDL application
under `SDL_VIDEODRIVER=dummy` with the SDL dummy audio device, a fixed seed,
an explicit save directory, a frame bound, and a process timeout:

- `scene-cycle-keyboard`: title, controls, play, pause, resume, a pause-menu
  detour through the controls screen and back to the same frozen round,
  restart, game over, play again, game over, title, quit on the keyboard.
- `scene-cycle-pad`: the same cycle on the developer virtual controller,
  plus a mid-game detach while a direction is held.
- `harddrop-pad` (T067): the landscape default-hard-drop session on the
  developer virtual controller. X carries nothing while a press of it stands
  still, D-pad up hard drops with the section 4.6 arithmetic, the same
  D-pad-up press moves the cursor up on the title, pause, and game-over
  menus, and no press that returns a round to play drops a piece - the
  resume from the pause overlay, Play Again from the game-over menu, and a
  scripted restart from the title, the one scene change that happens outside
  a tick, whose queued press the per-tick input log must show spent of its
  gameplay edge. The desktop keyboard defaults ride the same session: X
  rotates one clockwise step and Space hard drops.
- `normal-game-clears`: a planned game from an empty board with rotations,
  taps, hold, soft drop, delayed auto-shift, one line clear, and a pause.
- `options-ghost`: the options screen round trip. A first round runs with
  the ghost at its on default, then the session opens OPTIONS, moves to the
  GHOST row, selects it to turn the ghost off, returns to the title, and
  plays a second round from the same empty board.
- `options-six-keyboard`, `options-six-pad` (T045, T055, and T062): the
  eight-row options screen on both inputs. Each session reaches every row,
  wraps past MUSIC back to SOUND, and changes the four settings of the
  expansion - GRID off, HOLD off, NEXT PIECES cycled, GRAYSCALE on - while
  the ORIENTATION and MUSIC rows are reached but never selected, so both
  sessions stay landscape and never open the music settings page. The pad
  session then plays a round in which its own hold inputs, Y and the left
  shoulder, change nothing while HOLD is off.
- `music-settings`, `music-settings-pad` (T062): the music settings page
  behind the MUSIC row, on both inputs. Each session opens the page from the
  MUSIC row, takes one select per row - the MUSIC mode cycles Auto ->
  Arcade, MUSIC VOLUME and EFFECTS VOLUME step once, NEXT TRACK requests
  another track without changing a setting, and a select never moves the
  cursor off its row - and back returns to the options screen with the
  cursor still on the MUSIC row. The keyboard session selects all four rows
  and quits with the mode and both volumes saved; the pad session selects
  the mode and NEXT TRACK, returns on B, and quits with the mode alone
  saved. The checker compares every checkpoint with the reference replay
  and pins the four-row page layout from screenshots, the exact saved
  bytes, and the input-log lines.
- `hold-off` (T045): HOLD off, a round whose C and Left Shift presses move
  nothing and spend no lock while the Hold group draws nothing at all, then
  HOLD back on for a round that proves the once-per-piece rule survived.
- `grayscale` (T045): GRAYSCALE on for a round with settled cells, a live
  piece, a ghost, a stashed Hold piece, and five previews, then off for the
  same shape of round in color.
- `options-relaunch`, `options-legacy`, `next-count` (T045): the staged
  sessions. See the next paragraph.
- `portrait-keyboard`, `portrait-pad` (T055, T068): the staged portrait
  sessions. See the next paragraph.
- `controls-keyboard` (T066): the customization screen on the keyboard -
  open, capture, reject, cancel, restore, and the way back out through both
  menus - with the round-level proof that the freed Space settles nothing
  and the bound D locks the piece.
- `controls-pad` (T066): the same walk on the developer virtual controller,
  plus the unplug and replug: a held direction dies with the pad without
  moving a counter, and the replugged instance answers the custom binding
  at once.
- `controls-relaunch`, `controls-invalid` (T066): the staged controls
  sessions. See the next paragraph.
- `record-abandon-save` (T052): the mid-round record persistence of
  specification sections 4.7 and 9. A round scores through two hard drops,
  pauses, and leaves through QUIT TO TITLE, so it never reaches a game over;
  the checker requires the record file to hold the abandoned round's score,
  the per-tick input log to carry each mid-round save (`record save=` lines)
  on a tick that precedes the abandon, the log to carry no game-over save at
  all, and the title after the abandon to show the persisted record while
  the abandoned round's own counters reset.
- `fixture-four-line`, `fixture-lockout`, `fixture-blockout`: labeled board
  fixtures for the four-line clear and both game-over rules.

Every session runs twice. The headless `stackfall_reference` replays the same
script through the rules core alone and writes the checkpoint records the
core prescribes; then the real application writes its own. The checker
compares the two streams field by field, so a difference names the field that
diverged. On top of the comparison it asserts expectations pinned from
`docs/SPEC.md` alone - the section 4.6 scoring table, hard-drop and soft-drop
points per cell, the pause freeze, hold restriction, line-clear counts, scene
sequences, the saved record, the options screen's menu cursors with the
settings file its GHOST select writes, and a scan of the screenshot's board
well that reads the BMP without an image library and requires zero pixels
besides the well and grid-line colors once the ghost is off - so a defect
shared by the game and the reference still fails. Screenshots are checked as
960x544 BMP files, the game must exit 0 with a clean shutdown, and the
normal-game session must repeat byte-identically. A plain run without
developer options proves the hooks change nothing about normal startup.

A session with a `tests/harness/scripts/<session>.stages.txt` manifest runs
one game process per listed stage over one shared save directory, in the
order the manifest lists, and a stage may carry `settings=<name>` to copy a
file from `tests/harness/settings/` into that directory first,
`controls=<name>` to copy a file from `tests/harness/controls/` the same way
(T066), or `settings=none` to remove the settings file and require the stage
to leave none - the marker for a stage that exercises no settings write. The
driver snapshots the settings and controls files before and after every
stage, the reference replays each stage against the files it started from,
and the checker compares every stage's records with its own reference
stream. The seven staged sessions are:

- `options-relaunch`: four keyboard processes, each selecting one of the four
  new rows once and quitting. The snapshot after every stage is one exact
  canonical rendering of the preferences, the snapshot before the next
  stage is the same file a relaunch read, and every startup log line names
  the values that process restored.
- `options-legacy`: stage one starts from the three-line file of the
  sound-and-ghost era (`sound=off`, `ghost=off`), keeps both saved values,
  receives the later defaults, and upgrades the file to the current form on
  its one GRID save; stage two starts from the two-line file of the
  sound-only era (`sound=off`), receives every later preference at its
  default, and changes nothing, so the file must still hold the exact legacy
  bytes when the process ends.
- `next-count`: five keyboard processes over one script that selects the
  NEXT PIECES row once, so the ring walks 5 -> 1 -> 2 -> 3 -> 4 -> 5 across
  the stages. Every stage plays the same round from the same title seed, so
  the five observation streams are identical outside the loaded record - the
  round each stage abandons persists its score mid-round (T052), and every
  stage after the first loads that record - while the checker reads each
  stage's screenshots and requires exactly its count of previews in queue
  order, the first at full color and the rest dim.
- `portrait-keyboard`, `portrait-pad` (T055, T068): two stages over one save
  directory each. Stage one runs landscape, walks to the seventh OPTIONS row,
  ORIENTATION, and selects it with a direction held across the press - key
  left or D-pad left, which holds move-left in landscape and is the logical
  up direction that hard drops in portrait. The turn applies inside the tick
  that consumed the select edge: the same process then navigates the title on
  the rotated roles, plays a round whose key up or D-pad up moves right, key
  down or D-pad down moves left, Z or B rotates clockwise, and key left or
  D-pad left hard drops, walks the pause overlay on the rotated menu roles,
  turns back to landscape, turns to portrait a second time, and quits through
  the portrait table, so the stage covers the portrait turn, the landscape
  turn, and repeated changes in one process and leaves the settings file on
  portrait. Stage two is the relaunch that reads that saved value at startup:
  the round plays the full rotated table - X or A rotates counter-clockwise,
  C or Y holds, and Space or X carries nothing (T067) - through the
  game-over screen and the title quit. The reference replays both stages on
  the tables in force at each press: it swaps its own key and pad tables and
  clears its adapter on the same select edges the game applies.
- `controls-relaunch` (T066): two stages over one save directory, both marked
  `settings=none`, so the run also proves the customization path writes no
  settings file of its own. Stage one walks to the customization screen from
  the title, binds HARD DROP to D on the keyboard and to X on the pad, meets a
  rejected capture and a cancelled one on the way, and quits with both custom
  tables saved; the snapshot after the stage is the exact canonical
  `controls.dat` with the two overrides. Stage two relaunches on that file -
  its startup log names custom bindings, Space and D-pad up carry nothing, D
  and pad X hard drop - and ends on RESTORE DEFAULTS, whose snapshot is the
  exact section 5 bytes again, played once more in a round before the quit.
- `controls-invalid` (T066): one stage that starts from
  `controls=broken-controls.dat`, a file binding Z to both hard drop and
  rotate counter-clockwise, which no screen could have written. The session
  recovers every table to the section 5 defaults, keeps every menu reachable
  through the customization screen and back, plays a round on the defaults -
  D carries nothing, Space hard drops - and leaves the damaged file
  byte-identical to how it found it, because nothing customized anything.

The T055 and T068 portrait sessions add their own proofs to the checker: the
play frame read from pixels through the rotation (portrait x=71..472,
y=119..920 is device x=119..920, y=71..472, with both frame edges in the
frame color for their whole span and the lines one past them background,
which pins the rotation and the 40 px cell size together), the HUD strip
above the board, the quiet portrait margins, the pause and game-over panels
at their rotated device boxes, the quiet rows below the board on every page
that clears the canvas, the portrait options page read in the vertical bands
the rotation gives its heading, rows, and footer, the exact settings bytes
of the portrait preference with the landscape startup line that still names
landscape, the rotated mapping itself - the per-tick input log must carry
the ordered action bits of the rotated table for the keyboard and for the
pad, and a landscape table fails every step of the ordered walk, because key
up would raise a clockwise rotation before any move-right - and the turn's
own input rule (T068): every orientation save line follows the tick that
carried the select edge, that tick also carries the probe's held level, and
the ticks after it carry no bit at all until the script presses again, so
the select, the held direction, and every pending edge are all spent by the
turn. No log may carry next-launch wording. Landscape keeps its own proofs
unchanged: the pixel suite pins the landscape geometry constants, and the
observation streams and screenshots of every session whose script did not
change stay byte-identical to the runs before the feature, outside the
options page that legitimately gained its seventh row with ORIENTATION and
its eighth with MUSIC (T062).

The T045 sessions add their own pixel and file proofs to the checker: the
options page itself (heading, one text line per row with the accent marker on
the highlighted row, footer, and nothing under the footer, so the eight rows
fit the canvas), the Hold group hidden to the last pixel while HOLD is off
and drawn with the stashed piece's exact shape and shade while it is on, the
preview slots' shapes and shades against the record's queue, the grayscale
play scene (every piece pixel of the well gray, the text and the board frame
in their color-mode colors, the ghost outline in its expected blend) with the
color round as the positive control, the exact settings bytes after every
staged change, and the hold-off proof that no mapped hold input moves the
round, spends the lock, or reaches a tick of the input log as a hold edge.
The reference replays the HOLD gate itself: it reads the session's settings
file (`--settings`, the same default when the file is absent) and flips its
own mirror on a select of the HOLD row, exactly as the platform layer does,
so a session that starts or becomes HOLD-off compares against a reference
that drops the same edges. It also mirrors the record persistence the
platform layer owns since T052 (specification sections 4.7 and 9): it reads
the session's record file (`--high-score`, record 0 when the file is
absent), raises its own saved record the moment the running score passes it,
and feeds the best record back into the session between rounds at the same
round boundaries the application uses, so every checkpoint's `high` field
still names what the game binary must show - including a title reached by
abandoning a scoring round. The mirror never writes the record file; the
`m3-highscore` reference of the T015 gate deliberately runs without
`--high-score`, because it must mirror the first process of the four, whose
save directory was still fresh.

The T066 controls sessions add the customization proofs. The reference
mirrors the whole path: it reads the session's controls file (`--controls`,
the section 5 defaults when the file is absent or damaged) and swaps its own
binding tables on the same capture edges the game applies, so a session with
custom tables compares against a reference that drives the same ones. The
checker pins, per session: the scene walk onto the customization screen and
back on both inputs; the menu-cursor state at every step of the nine rows;
the input-log evidence lines of an applied capture, a rejected capture that
names the action that holds the control, a cancelled capture, and a restore;
the exact `controls.dat` bytes before and after every staged change; the
startup log line that names default or custom bindings and how they were
obtained; the round-level proof that a freed control settles nothing and a
bound control locks the piece; and, for the pad session, the unplug that
costs only the held level with no counter moving and the replug that answers
the custom binding on the new instance at once.

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
--screenshot-budget BYTES
                      cap the checkpoint images one session writes at BYTES
                      (default 268435456, 0 for no cap)
--fixture-dir DIR     resolve `fixture` script commands here
--virtual-controller  attach the developer virtual gamepad
--tick-per-frame      one simulation tick per frame, pacing off
--frame-times FILE    record one microsecond frame duration per frame of a
                      real-time paced run (the T025 measurement)
```

Checkpoint BMPs are palettized (task T054): the game builds each frame's
palette and writes an 8-bit BI_RLE8 BMP, roughly 30 to 80 KB per image at
the 960x544 canvas instead of the 1.5 MB SDL_SaveBMP wrote. A frame with
more than 256 colors, or one whose rows RLE would grow, falls back to an
uncompressed BMP, so every image stays lossless. The budget bounds the
bytes one session writes: once the next image would pass it, the session
skips that image with one log line and records `shot=none`, exactly as when
a save fails, while every observation record keeps coming. The checker
reads every BMP form with no image library, and PIL reads RLE8 BMPs
natively, so visual review keeps working.

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
   active piece or unclear shape. Since task T029 (approved 2026-11-03), every
   spawn is visible on its first frame, including the I. The session
   `tests/harness/scripts/t024-i-spawn.script.txt` pins the seed-777 I spawn,
   its first visible frame, and the gravity tick it first falls. Treat a
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

### Running the T025 checks

One command runs the whole stability pass, phase by phase, into
`build/agent-checks/<run-id>/`:

```bash
tests/harness/t025.sh
```

Each phase leaves a `<phase>.status` file plus its evidence: `fuzz`
(bounded mutation fuzz of the save and script parsers through the
sanitizer build), `hardkill` (SIGKILL during save writes through a delay
shim, every resulting file classified and reloaded, then two instances on
one save directory), `cycles` (52 restart and menu cycles at real-time
pacing with movement probes and resource sampling), `restarts` (50 process
restarts carrying one record forward), `longplay` (the ten-minute Release
session under Xvfb with frame times, CPU, and memory), `pacing`
(frame-time percentiles for opengl with the vsync flag, opengl with
`SDL_RENDER_VSYNC=0` stripping it, the software renderer, and the dummy
fallback; the logged renderer flags pin which is which),
`failures` (the forced-failure matrix), `san` (the suites under
AddressSanitizer and UndefinedBehaviorSanitizer), and `regress` (the T023
and T024 drivers again). Pick phases with `T025_PHASES`, rerun one phase
into an existing evidence set with `T025_KEEP_DIR=1`, and read the script's
header for every knob. The session scripts come from
`tests/harness/t025_gen.py`, the frame-time statistics from
`tests/harness/frame_stats.py`, and the fuzz pass from
`tests/harness/t025_fuzz.py`.

### Running the T015 M3 desktop gate

One command runs the complete round-M3 verification pass:

```bash
tests/harness/t015.sh
```

Phases, each writing into `build/agent-checks/<run-id>/`:

- `builds`: a clean rebuild and the full `ctest` pass in Debug and in
  Release, with the compile logs kept so the checker holds the
  zero-warning gate.
- `hashes`: `stackfall_tests --seed 987654321` in both configurations.
  The gate requires byte-identical output and the five pinned scenario
  hashes (section 11, item 18, desktop half; the ARM64 half stays with
  T016).
- `sessions`: seven M3 sessions through the real SDL application. Every
  deterministic session runs through the headless reference, repeats
  inside the run with byte-identical records, and replays in the Release
  build against the same reference stream:
  - `m3-das-arr` (seed 20261103): the move on the press tick, the
    ten-tick DAS charge, the two-tick ARR cadence, repeats failing at
    the wall, the immediate move on a direction switch, left plus right
    canceling, and a pause mid-charge that freezes the charge instead of
    resetting it.
  - `m3-lock-delay` (seed 20261103): a soft-dropped piece locks exactly
    on its 30th grounded tick, grounded moves reset the timer, the
    budget is 15 resets, and a 16th grounded move still moves the piece
    without resetting the timer.
  - `m3-bag-20261103`, `m3-bag-31337`, `m3-bag-777`: 18 hard drops each
    with spread placements; the checker rebuilds the piece stream from
    the queue windows and proves every block of seven draws is a
    permutation of I O T S Z J L. The 777 session adds the hold rules at
    a bag boundary: the swap draws the queue head, a second press is a
    no-op, a lock re-enables hold, and the return swap respawns the
    stashed piece in orientation 0.
  - `m3-restart` (seed 31337): pause-restart, quit-to-title, and
    play-again each begin a fresh seeded round with reset counters, and
    a direction held across a restart moves on the fresh round's first
    ticks and stops on release.
  - `m3-highscore`: one script, four game processes, one save directory.
    A fresh save records 202 (seed 999); the same seed again replays the
    round identically and does not save (a record is a strict
    improvement); a better round replaces the file with 212 (seed 5150);
    a damaged file recovers to 0 and the round saves 196 (seed 20261103).
    The driver snapshots the record file after every process.
- `regress`: the retained full-game suites at the same source identity:
  `tests/harness/run.sh` and `tests/harness/t024.sh` in Debug and in
  Release.
- `stability`: the T025 phases `failures`, `restarts`, and `san` (the
  restart phase is the 50-process record-carrying evidence).
- `performance`: the T026 `verify` phase in Release.
- `check`: `tests/harness/check_t015.py`, the independent gate checker.
  It re-derives every pin above from the evidence directory, compares
  every record stream against the reference, verifies the screenshots,
  and also checks that the required reports exist: the T024 and T025
  sections of `notes/agent-testing.md`, the T026 final gate with no
  unresolved desktop target in `notes/performance.md`, and the T027 and
  T028 sections of `notes/design-review.md`.

Environment variables: `T015_BUILD_DIR` (default `build`),
`T015_RELEASE_DIR` (default `build-rel`), `T015_RUN_ID`,
`T015_TIMEOUT_S` (default 180), `T015_PHASES`, `T015_SKIP_X11=1` to skip
the X11-dependent retained checks (recorded unavailable), and
`T015_T025_PHASES` for the stability subset. A reused `T015_RUN_ID`
removes that run directory before the run starts, so a rerun cannot
inherit the earlier pass's evidence.

### Running the T017 launcher and install-layout check

One command reviews the PortMaster launcher and drives it against a stub
PortMaster install:

```bash
tests/harness/t017.sh
```

Phases, each writing into `build/agent-checks/<run-id>/`:

- `static`: `bash -n`, plus a review of `portmaster/Stackfall.sh` against
  the current official template (the four control-folder candidates,
  `control.txt`, the CFW modifier, `get_controls`, `conf/`, the `log.txt`
  redirection, the environment exports, `pm_platform_helper`, the run, and
  `pm_finish`) and against the specification (the `ports/stackfall` folder,
  `XDG_DATA_HOME` and the explicit save directory at `conf/`, and no
  button-mapping injection).
- `stage`: `tools/portmaster/install-tree.sh` stages the section 10.3
  layout, and the checker proves the staged tree is exactly `Stackfall.sh`
  plus `stackfall/stackfall.<arch>` with no runtime `conf/` or `log.txt`.
- `sandbox`: six launcher runs through a stub `control.txt`, a stub
  `gptokeyb`, and a stub game binary that records its process environment:
  a base run, argument forwarding, a repeat launch that replaces
  `log.txt`, an install with no CFW modifier, an install that defines no
  `GPTOKEYB`, and a port folder with no binary (which must fail with a
  message and no game run).
- `roms`: the same launcher at the literal `/roms/ports` paths. The driver
  creates `/roms` through `sudo` only when the host has none, registers an
  idempotent, marker-checked cleanup in `LOOPER_RESOURCE_CLEANUP_FILE`
  before creating anything, and removes the tree when the run ends. Set
  `T017_SKIP_ROMS=1` to skip the phase.
- `game`: the real desktop binary, staged by the install tool, launched by
  the script under the dummy video and audio drivers. The checker reads
  the game's own startup log to prove the launcher's environment reached
  it: the banner and renderer in both `log.txt` files, and the session log
  resolving to `conf/log.txt`.
- `check`: `tests/harness/check_t017.py`, the independent gate checker. It
  re-derives every expectation from the template and the specification,
  reads the evidence directory alone (so a pass can be re-verified after
  the `/roms` tree is gone), and exits nonzero on any failure.

The launcher is architecture-independent shell, so the stub binary and the
x86-64 desktop build prove its behavior here. The AArch64 binary inside
the ARM64 environment and the real device stay with T016's evidence and
the blocked device task. Environment variables: `T017_BUILD_DIR` (default
`build`), `T017_RUN_ID`, `T017_TIMEOUT_S` (default 60), `T017_SKIP_BUILD`,
and `T017_SKIP_ROMS`.

### Running the T019 release-package check

One command assembles the distributable archive of specification section
10.4 and drives it end to end:

```bash
tests/harness/t019.sh
```

Phases, each writing into `build/agent-checks/<run-id>/`:

- `static`: the package sources exist, none uses the forbidden name or
  trade dress of the game STACKFALL imitates, and the source identity is
  recorded.
- `arm64`: `build-arm64/stackfall.aarch64` when it is missing, built by
  the documented command (`T019_SKIP_ARM64=1` requires it pre-built), and
  its `file` report must say `ARM aarch64`.
- `package`: `tools/portmaster/package.sh`, which captures the 4:3
  gameplay screenshot from the desktop build under a virtual X server,
  stages the port directory, and writes the deterministic
  `build/portmaster-package/stackfall.zip`.
- `unpack`: the archive is unpacked into a fake ports directory and every
  CRC is checked.
- `run`: the archive's own launcher, driven against a stub PortMaster
  control folder, with the desktop binary staged beside the archived one
  under the host architecture name, under the dummy video and audio
  drivers. The ARM64 binary cannot run on an x86-64 host, so this proves
  the launcher, the environment it exports, and the `conf/` and `log.txt`
  layout of the unpacked archive.
- `repro`: the documented clean-environment rebuild of the shipped
  `BUILDING.md` in a fresh container from the pinned image; its SHA-256
  must equal the archived binary's (`T019_SKIP_REPRO=1` skips).
- `check`: `tests/harness/check_t019.py`, the independent gate checker.
  It re-derives the expectations from the PortMaster packaging guide, the
  format of its build system, and the specification: the archive entry
  list, the metadata fields and genres, the section 10.3 install layout
  after unpacking, the binary's machine and its documented hash, the
  screenshot's 640x480 4:3 form with centered letterbox bars, settled
  pieces, a piece in flight, and readable HUD columns, the launcher run,
  the naming rule, and the byte-determinism of the archive.

Environment variables: `T019_BUILD_DIR` (default `build`),
`T019_ARM64_BINARY` (default `build-arm64/stackfall.aarch64`),
`T019_RUN_ID`, `T019_TIMEOUT_S` (default 120), `T019_SKIP_BUILD`,
`T019_SKIP_ARM64`, and `T019_SKIP_REPRO`. The package tool accepts the
same options directly; see `tools/portmaster/README.md`.

### Running the T035 release-version check

One command proves the shipped version is one value everywhere a player, a
package browser, or a test can read it:

```bash
tests/harness/t035.sh
```

It builds the desktop tree, runs the headless `ctest` suites, starts a
bounded headless session so the startup banner lands on stdout and in the
session `log.txt`, and then runs `tests/harness/check_t035.py`. The
checker re-derives its expectations from the sources alone:

- The three constants of `include/stackfall/version.hpp` compose to
  `kVersionString`, and `src/version.cpp` hands out that constant without
  stating a literal of its own, so the value has exactly one home.
- The `project(... VERSION ...)` of `CMakeLists.txt` equals
  `kVersionString`, as the header's own comment requires.
- Every location that names a version - the header, `CMakeLists.txt`, the
  `tests/test_sanity.cpp` pin, and `portmaster/release-notes.md` - states
  exactly the shipped version and no other dotted value, and no file that
  ships in the archive states one that contradicts it.
- The state hash of `tests/test_regression.cpp` still folds
  `kVersionString`, so a later bump again breaks the five pinned replay
  hashes instead of passing silently, and `check_t015.py` holds the same
  pin set as the suite it double-checks.
- The release notes headline the shipped version with its date,
  `gameinfo.xml` carries the same release date, and the banner run exits 0
  with a clean shutdown while both the stdout banner and the session
  `log.txt` report `STACKFALL <version>`.

The version bump of the first release landed inside the T019 package
assembly, so this check closes T035 over work that already exists. The
assembled archive, its launcher banner, and the ARM64 binary stay with the
T019 package check, which reads the same header.

Environment variables: `T035_BUILD_DIR` (default `build`), `T035_RUN_ID`
(default `t035-version-<date>`), `T035_TIMEOUT_S` (default 120), and
`T035_SKIP_BUILD=1` to reuse the desktop build.

## Final handoff

After T024, run T027 and T028 from `docs/DESIGN_REVIEW.md`.
These tasks add an independent screenshot review and verify required fixes.
After T025, run the T026 optimization loop in `docs/PERFORMANCE.md`.
Then finish the desktop gate (`tests/harness/t015.sh`), native build,
launcher, and local archive. Include one
report with agent results, unresolved questions, reproduction commands, and
the exact package identity. Then request the final human and device pass.
Do not describe the release as device-validated until that pass succeeds.
