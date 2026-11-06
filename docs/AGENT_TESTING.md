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
suites first, then drives thirty-three sessions through the real SDL
application
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
- `stick-pad` (T86): the landscape thumbstick session on the developer
  virtual controller. The Y axis carries the D-pad arms' menu roles, so the
  stick walks the title menu both ways before A starts the round. The round
  then moves the piece one column with X negative and one back with X
  positive, holds the piece still through a reading of 7999 on both axes,
  moves it again at the deadzone boundary 8000 itself, soft drops two cells
  with Y positive and hard drops with Y negative, and closes with a D-pad
  and a keyboard move that prove neither changed. The checker pins the
  direction and the arithmetic from the records and the ordered action bits
  of the round's per-tick input log, so a dead or inverted axis fails it
  whatever the reference says.
- `stick-portrait` (T86): the turned thumbstick session on the developer
  virtual controller. A landscape session walks to the ORIENTATION row and
  selects it with the stick deflected across the press - X negative, the
  D-pad's left arm - so the turn must spend the deflected arm with every
  held level and pending edge, which the input-log probe names. The turned
  session then walks the title on the stick's X axis, plays a round whose
  physical down reading moves the piece left, physical up moves it right,
  physical right soft drops, and physical left hard drops - the portrait
  table of section 5, reading by reading - with B, A, and Y rotating and
  holding beside the stick, X carrying nothing, and a deadzone reading
  holding the turned round too. The session turns back to landscape and to
  portrait again, so the saved preference it leaves is portrait, and the
  quit runs through the turned roles.
- `harddrop-rebind` (T87): the staged hard-drop rebind session. See the
  staged paragraph below.
- `normal-game-clears`: a planned game from an empty board with rotations,
  taps, hold, soft drop, delayed auto-shift, one line clear, and a pause.
- `options-ghost`: the options screen round trip. A first round runs with
  the ghost at its on default, then the session opens OPTIONS, moves to the
  GHOST row, selects it to turn the ghost off, returns to the title, and
  plays a second round from the same empty board.
- `options-six-keyboard`, `options-six-pad` (T045, T055, T062, T084, and
  T095): the ten-row options screen on both inputs. Each session reaches
  every row, wraps past PIECE DESIGNS back to SOUND, and changes the four
  settings of the expansion - GRID off, HOLD off, NEXT PIECES cycled,
  GRAYSCALE on - while the ORIENTATION, MUSIC, BACKGROUNDS, and PIECE
  DESIGNS rows are reached but never selected, so both sessions stay
  landscape, keep the designs off, and never open a submenu. The
  pad session then plays a round in which its own hold inputs, Y and the
  left shoulder, change nothing while HOLD is off.
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
- `backgrounds-keyboard` (T084): the backgrounds page behind the
  BACKGROUNDS row on the keyboard. The MODE ring walks Auto -> Off ->
  Fixed, the DESIGN row walks every stable ID 1 through 20 with the wrap
  back to 1 and ends on 7 - each select of the walk a saved Fixed ID - and
  the mode then steps Auto and lands on Off with the preferred design kept,
  before back returns to the options screen on the BACKGROUNDS row. Every
  checkpoint pins the two-row page layout from pixels, the input log
  carries the whole design ring in order, and the saved file holds
  `background=off` and `backgroundid=7`.
- `backgrounds-pad` (T084): the same page on the developer virtual
  controller, with the same-session application the acceptance names: the
  round opened after the mode landed Off draws the legacy flat wallpaper on
  its first play frame, the round opened after Fixed draws the retained
  design 2 on the next play frame of the same session, and the round opened
  after Auto draws the level 1 design with its corner ink - the Auto
  boundary - all proven from probe pixels of the quiet span and the guard
  gaps. The options cursor keeps the BACKGROUNDS row across the round
  trips, and the file ends at `auto` with the retained design 2.
- `backgrounds-relaunch` (T084): the staged backgrounds session. See the
  next paragraph.
- `piece-designs-keyboard` (T095): the PIECE DESIGNS row of the options
  screen on the keyboard, tenth after BACKGROUNDS. One select turns the
  per-piece in-cell marks on, a round over the labeled `all-pieces` fixture
  draws every mark in color, the same select walks the value off for a
  control round over the same fixture, and a final visit turns it back on,
  so the saved file ends at `piecedesigns=on` and the input log carries both
  directions. The checker reads one settled cell of every piece type from
  the captured frame - the interior holds only the palette fill and dark
  shade, the dark pixels form the mark, and no two pieces share one - and
  the plain frame proves the flat cell draws no dark pixel at all.
- `piece-designs-pad` (T095): the same row and the same fixture round on
  the developer virtual controller, the input path of `scene-cycle-pad`,
  ending with the saved `piecedesigns=on`.
- `piece-designs-gray` (T095): the marks where color is gone. One options
  visit turns GRAYSCALE and PIECE DESIGNS on together, the fixture frame
  captures every mark in gray, and the same session then turns portrait
  through the ORIENTATION row and captures the turned frame, so the checker
  reads all seven marks through the fixed rotation of task T055 too; the
  quit runs through the turned menu roles of the portrait table.
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
stream. The nine staged sessions are:

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
- `backgrounds-relaunch` (T084): three keyboard stages over one save
  directory. Stage one starts from `settings=fixed-seven-portrait.dat` -
  portrait, `background=fixed`, `backgroundid=7` - so the relaunch opens the
  backgrounds page with the saved FIXED and DESIGN 7 values at the portrait
  metrics (the checker reads the turned bands from screenshots), moves the
  preference to 8, and its first play frame draws the saved design 7 in
  portrait, proven from base-field probe pixels of the tall frame. Stage two
  starts from `settings=broken-background.dat`, whose `backgroundid=99` sits
  outside the catalog, so the whole file is damaged under the recovery
  policy: the session opens on every default - the startup input-log lines
  name `background start=auto` and `backgroundid start=1` - changes nothing,
  and the file must still hold its exact broken bytes, because a load never
  rewrites the settings. Stage three starts from
  `settings=legacy-music-eleven.dat`, the eleven-line file of the music era:
  every saved value loads, the three new lines fill with the Auto, design
  1, and piece-designs Off defaults, and one MODE select saves Off and
  upgrades the file to the current fourteen-line canonical form with every
  legacy value kept.
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
- `harddrop-rebind` (T87): three pad stages over one save directory, the
  first marked `settings=none`. Stage one plays the task T067 defaults with
  the stick's edge rule pinned from both sides of the deadzone - a 7999
  reading and a wobble across it drop nothing, one full deflection drops
  once, a held deflection drops nothing more - then rebinds HARD DROP to
  the face button X, to the right shoulder R1, and, on the keyboard table,
  to D, and plays the rebound tables: the freed D-pad up, the stick reading
  that reaches it, and X all settle nothing, while R1 and D drop. Stage two
  relaunches on that file - its startup log names custom bindings - plays
  the loaded tables, turns the session portrait through the ORIENTATION
  row, and proves the untouched portrait defaults still hard drop from the
  grip's up arm with the same edge rule, so the landscape rebind leaked
  into no other table. It then rebinds the portrait HARD DROP to R1 through
  the turned menu roles and plays the rebound portrait table. Stage three
  relaunches onto the saved orientation and the saved custom tables, proves
  the persisted rebind in a round, and ends on RESTORE DEFAULTS, whose
  snapshot is the exact section 5 bytes again, played once more before the
  quit. The checker pins every capture's input-log line, the freed triggers
  settling nothing, the drop-edge count of each round, the controls-file
  bytes after every stage, and the quiet rebind page in both orientations.

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
the highlighted row, footer, and nothing under the footer, so every row of
the page fits the canvas), the Hold group hidden to the last pixel while HOLD is off
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
back on both inputs; the menu-cursor state at every step of the nine rows
of the customization screen;
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

### Parallel ctest runs

Every harness and gate script runs `ctest` serially, with no `-j`. A manual
`ctest -j4` is also safe: the `stackfall_tests` and `stackfall_tests_seed`
entries share a CTest `RESOURCE_LOCK`, so the two whole-suite runs - which
both time the record writer's real waits (`tests/test_record_writer.cpp`)
- never overlap. The record-writer checks themselves tolerate a loaded
machine: the submit ceiling is half the injected 80 ms write, which the old
synchronous path still fails by 2x, and the one check that needs a write to
be in flight blocks that write on a latch instead of racing the clock. This
is the repair of the one-time parallel flake of task T093 (Papercut
pc_01M6NVGZY80XPVSZW5317AGBQ4): a loaded first `-j4` run of a fresh Debug
build reported three record-writer failures in `stackfall_tests_seed` that
a serial rerun cleared.

The audio suite's callback ceiling (`tests/test_audio_sdl.cpp`) no longer
races the scheduler either, for the same reason (task T096, which
reproduced the sibling flake under `taskset -c 0-3 ctest -j4` against 24
busy loops): the wall clock it printed measures the load a host carries as
readily as the mix, and a starved run read a p99 of 105 percent of the
buffer while its median stayed near 0.05 ms. The pin now reads the
calling thread's own CPU clock, which preemption cannot inflate, so host
load cannot trip it and the half-buffer ceiling is unchanged; a genuinely
more expensive mix still misses the ceiling by far. The wall percentiles
keep printing for `notes/performance.md`, and the second printed line names
the clock the pin read, so a host without a POSIX thread clock - which
falls back to the wall clock and stays load-sensitive - says so in its own
output.

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
stick <x|y> <value>   set the virtual pad's left-stick axis reading,
                      -32768..32767; 0 rests the stick in the deadzone
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

The `stick` command (T86) names the exact reading the virtual pad reports,
so a script can hold the deadzone boundary and the far ends of an axis. The
value runs through the one bounded signed parser of `stackfall/script.hpp`,
shared with the reference replay exactly as the `wait` count is.

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

## T078: Background renderer and clock

The procedural level backgrounds of `docs/backgrounds` split across two
suites. Both build with the ordinary `ctest` pass; neither needs a display.

- The headless suite `run_backgrounds_tests` of `tests/test_backgrounds.cpp`
  links the core alone. It pins the approved catalog of twenty designs
  against `docs/backgrounds/level-XX.md` (names, palettes, chapters, motion,
  transitions, budgets, the rising intensity arc), the componentwise caps of
  the brief, the cosmetic `hash32` against the `h32()` values of
  `tools/backgrounds/designs.py`, the presentation clock (advance, the
  per-frame clamp, freeze that drops time, resume without a jump, reset, the
  bounded wrap), the clipping layer (no shape inside the well or frame,
  accents only in the loud margins, canvas clamping), the wallpaper state
  machine (Off, Auto with its level clamp, Fixed, cuts, stepped fades with
  their exact integer colors, mid-fade restarts, round resets), and the
  independence of the wallpaper from gameplay randomness: two rounds on
  different seeds diverge while their wallpaper streams stay identical.
- `run_background_checks` of `tests/test_renderer_sdl.cpp` pins the SDL
  half: `backgroundZones()` equals the design package mask of
  `docs/backgrounds/index.md` in both orientations, the play scene paints a
  design's base outside the well and frame at exact pixels, Off keeps the
  legacy flat `#10121A` pixels at any level, Fixed holds one design across
  level changes, a cut lands in one draw, a fade steps through its record's
  visible steps with the clock freeze holding them, pause and game-over
  keep the selected wallpaper beneath the veil, grayscale keeps the
  wallpaper palette, the menu pages keep the flat ground, and a same-session
  orientation turn rebuilds the geometry without changing the landscape
  bytes.

The chapter grammars of T079 through T083 land in `paintGrammar()` of
`src/backgrounds.cpp`. The Still chapter landed with T079, so the level 1
play scene carries the corner steps of Still ground instead of the legacy
flat pixels; the suites that pin the board, the HUD, and the letterbox on
the flat ground run in background mode Off (`useFlatPlayGround()`), and
`run_background_checks` pins the level 1 pixels themselves. The designs of
the Pressure and Peak chapters have not landed; they paint their base
field alone. The
options submenu and the saved mode land with T084; the full visual,
behavioral, and cost validation is T085.

## T079: The Still chapter

Levels 1 through 4 draw their approved grammars, ported formula by formula
from `tools/backgrounds/designs.py` so both orientations reproduce the
design package's plates pixel for pixel. Three checks hold them:

- `run_backgrounds_still_checks()` of `tests/test_backgrounds.cpp` pins each
  design's plate against its record on both orientations - the corner steps
  of Still ground (160 x 121 and 62 percent of that, landscape and portrait
  sizes exact), the rule row and the under-bands of First light (440, 448,
  456 landscape; 784, 792, 800 portrait), the 28 hash-ranked stars of
  Distant stars (a digest over the whole approved stream, per-margin counts,
  the color and size split, and anchors), and the eight strata of Quiet
  strata with their stepped ends (40 elements landscape, 36 portrait). The
  same suite holds the chapter contract: the emitted fills stay inside the
  budget class, the accent colors stay under two percent of the canvas and
  inside the loud margins, nothing above the secondary cap draws outside
  them, the stream is byte-identical at six arbitrary clock values
  including the 32-bit wrap, and adjacent levels stay distinct at the
  one-eighth review scale of the design package.
- `run_background_checks()` of `tests/test_renderer_sdl.cpp` pins the same
  designs in the real frame, both orientations: the corner ink at exact
  pixels through the portrait rotation, the rule cut by the frame ring, the
  star anchors of the approved placement, the strata rows and their steps,
  the quiet strips under the secondary cap, a whole turned frame redrawn
  byte-identically after forty seconds of presentation clock, and the
  Off-mode suites above keeping the legacy flat pixels.
- `tools/backgrounds/still_check.py` reads the real captures of
  `tools/backgrounds/still-capture.script.txt` and its portrait twin: two
  scripted sessions walk the level ladder the only way the rules allow -
  four-line clears of the `four-line-ready` fixture, twelve lines to level
  2, twenty to level 3, thirty-two to level 4 - and write two frames per
  level about a second apart on the presentation clock. The check reads the
  BMPs through the renderer's rotation, pins the record pixels, holds the
  caps over the regions the wallpaper owns alone, proves no wallpaper color
  reaches the opaque well, proves the loud margins byte-identical between
  each level's two frames while the round itself moved on, and compares the
  adjacent levels at the one-eighth scale over the whole captured frame.

The captures are evidence, not fixtures: they land under
`build/agent-checks/<run-id>/`, and the script pair is retained so the run
reproduces. The run also builds `still-review-sheet.png`, the eight frames
beside the approved mockups at one-eighth scale (the portrait frames turned
back for a like-for-like read), for the visual pass against the approved
direction; a re-run of both sessions against the final binary reproduced
every frame byte for byte.

## T080: The Stir chapter

Levels 5 through 8 draw their approved grammars with their one moving
layer each, ported formula by formula from `tools/backgrounds/
designs.py` with the displacement carried at milli-pixel precision
through the wrap and rounded once at the end, halves to even, so both
orientations reproduce the design package's plates pixel for pixel at
every whole-millisecond clock. Three checks hold them:

- `run_backgrounds_stir_checks()` of `tests/test_backgrounds.cpp` pins
  each design's plate and fill stream against its record on both
  orientations - the 56 stars and 10 static dust flecks of Slow drift
  (a digest over the approved stream, per-margin counts, anchors, the
  272 s landscape and 15639 s portrait wrap returns, and the exact
  round-to-even steps at the 250, 750, and 1250 ms halves), the 18 zone
  rectangles and 120 rising dither squares of Dither dusk (the 278 s and
  407 s seamless wrap returns), the 14 towers, 28 roofs, and 54 or 168
  breathing windows of Sleeping skyline (the swell one through the
  middle half of the 8 s cycle, quiet at its exact-half edges 2000 and
  6000 ms, the bottom row of every window holding), and the four or six
  scanline bursts and four bars of Low signal (the 10/3 s texture
  return). The chapter contract holds the caps, the accent share under
  two percent at rest and mid-motion, the small-element budget, one
  moving layer per design, the stream a pure function of the clock
  across the 32-bit wrap, the review-scale mark density rising over the
  Still chapter in both orientations, and distinctness at one-eighth
  scale across the chapter boundary and inside the chapter.
- `run_background_checks()` of `tests/test_renderer_sdl.cpp` pins the
  same designs in the real frame: the star anchors of level 5 at clock
  zero sinking one pixel over half a second with the dust still, the
  dusk zones and caps of level 6 at exact pixels with a patch square
  lifting, the corner tower, roofs, and a breathing window of level 7
  swelling at 4 s into the cycle, and the bars and scrolling rules of
  level 8, in landscape and through the portrait rotation. The veil,
  grayscale, and portrait-zone suites that read a flat margin moved to
  the not-yet-landed design 9; grayscale now compares the whole margin
  byte for byte with the pieces graying inside the well alone.
- `tools/backgrounds/stir_check.py` reads the real captures of
  `tools/backgrounds/stir-capture.script.txt` and its portrait twin: two
  scripted sessions walk the level ladder to levels 5 through 8 and
  write three frames per level, 62 ticks apart, over the tall-stack
  board. The check solves the one frame offset K of
  `clock = floor((tick + K) * 50/3) ms` jointly over every frame, then
  requires each frame's loud margins to equal the design package's own
  plate at that exact clock - so the captures pin the designs at six
  known phases of their motion - beside the caps, the accent share, the
  well purity, the motion between the phases, and the adjacent levels
  at one-eighth scale over the whole captured frame.

The captures are evidence, not fixtures: they land under
`build/agent-checks/<run-id>/`, and the script pair is retained so the
run reproduces. A bridge check painted both orientations of all four
designs through the runtime and through the design package's own
grammars at 2536 clock values - every 7 ms of the first two seconds,
the rounding and pulse edges, the wrap spans, and the 32-bit clock wrap
- and compared the full canvases pixel for pixel: zero differing
pixels. Contract correction of the task, following the First light
precedent: the tower-height share of Sleeping skyline is
`0.10 + 0.24 * (1 - corner closeness)`, which the machinery, mockups,
and runtime draw; the record prose mistyped 0.30. Fixed in
`docs/backgrounds/level-07.md` and in the generator.

## T081: The Build chapter

Levels 9 through 12 draw their approved grammars with their one moving
layer each - a sinking tick field, a breathing core pulse, a rising
ribbon layer, and one shared diagonal drift - ported formula by formula
from `tools/backgrounds/designs.py`, the band staircase through the
package's floor-division phase and both drift axes at once through
`exactDots()`, so both orientations reproduce the design package's
plates pixel for pixel at every whole-millisecond clock. Three checks
hold them:

- `run_backgrounds_build_checks()` of `tests/test_backgrounds.cpp` pins
  each design's plate and fill stream against its record on both
  orientations - the two twelve-step staircases of Fracture (24 seam
  blocks of the record's step width beside 24 offset ledges, 44 whole
  ticks that sink 3 px/s over their margin span while the staircases
  hold still, and the 544 s landscape and 802 s portrait wrap returns),
  the sixteen right-angle paths of Circuit hush (64 traces or 62 in the
  narrow portrait margins, 16 square nodes, 6 lit cores that swell to
  3 x 3 px through the middle half of the 7 s cycle - quiet at the
  exact-half edges 1750 and 5250 ms - and the 7 s cycle return), the
  three stepped ribbons of Band aurora (nine staircase bands of 162
  landscape or 45 portrait segments at rest, eight riding ticks, the
  4 px/s rise audited between adjacent 250 ms probes, a whole-pass
  repaint bound that a wrap pop would break, and the 29347.5 s and
  132996.5 s least-common-multiple stream returns), and the stars and
  trails of Long trails (72 stars in the ranked 40/32 size split, 72
  staircase steps of 3 x 1 px walking 4 px left and 2 px down, 12 accent
  heads, the shared 2.4/3.2 px/s diagonal pinned by exact digests and a
  plate-difference bound per quarter second, and the 1360 s landscape
  return). The chapter contract holds the caps, the accent share under
  two percent at rest and mid-motion, the element ceilings through whole
  motion passes (92, 86, 180, 156 landscape; 95, 84, 98, 158 portrait),
  one moving layer per design, the stream a pure function of the clock
  far along it and across its 32-bit wrap, the arc over the landscape
  plates - Build at least the review-scale marks of Stir, more elements,
  and a faster peak speed - and distinctness at one-eighth scale across
  the chapter boundary and inside the chapter.
- `run_background_checks()` of `tests/test_renderer_sdl.cpp` pins the
  same designs in the real frame, both orientations: the seam, ledge,
  and tick pixels of level 9 with the ticks sunk one pixel at 333 ms
  while the staircase holds, a node core of level 10 swelled to 3 x 3 px
  at 4 s into the cycle and quiet again at 3 s, the staircase of level
  11 with its lifts of 0, 1, 2, 1, 0 times 8 px risen one pixel at
  250 ms - its ribbons cross the guard gaps at their secondary shade -
  and the diagonal of level 12 moved a pixel left and down at 333 ms,
  through the portrait rotation. The veil, grayscale, and portrait-zone
  suites that read a flat margin moved to the not-yet-landed design 13.
- `tools/backgrounds/build_check.py` reads the real captures of
  `tools/backgrounds/build-capture.script.txt` and its portrait twin:
  two scripted sessions walk the level ladder to levels 9 through 12 and
  write three frames per level, 64 ticks apart, over the tall-stack
  board. The check solves the one frame offset K of
  `clock = floor((tick + K) * 50/3) ms` jointly over every frame, then
  requires each frame's loud margins to equal the design package's own
  plate at that exact clock - the captures pin the designs at twelve
  known phases of their motion - beside the swell phases of the pulse,
  the caps, the accent share, the well purity, the HUD strip free of
  the chapter's accent shades, guard gaps that change only inside the
  design's own shades, and the adjacent levels at one-eighth scale over
  the whole captured frame: 210 checks, no failures, K = 0 in both
  orientations.

The captures are evidence, not fixtures: they land under
`build/agent-checks/<run-id>/`, and the script pair is retained so the
run reproduces; a re-run of both sessions against the final binary
reproduced every frame byte for byte, the title frames apart, which
carry the persisted high score. A bridge check painted both
orientations of all four designs through the runtime and through the
design package's own grammars at 4240 clock values - every 7 ms of the
first two seconds, the pulse and rounding edges, the wrap spans, and
random clocks across the full 32-bit range - and compared the full
canvases pixel for pixel: zero differing pixels. The visual pass read
the full-resolution frames and the one-eighth review sheet beside the
approved mockups: each motif reads at a glance, no shape forms a
letter, a digit, an arrow, or a tetromino cluster, every mark is an
axis-aligned rectangle or a staircase of them, and the board and HUD
stay the brightest planes of the frame. Contract corrections of the
task: the Band aurora wrap phase must carry the ribbon's own extent -
one extent below the base row, so a ribbon leaves the top edge fully
before re-entering below the bottom edge every 161.25 s landscape and
276.5 s portrait; the record prose popped all three ribbons back at
their base rows at the wrap - and the Long trails drift note's periods
are 12.2 s per bottom strip height, 24.6 s per side margin width, and
170 s per landscape height. Fixed in `docs/backgrounds/level-11.md`,
`docs/backgrounds/level-12.md`, and in the generator.

## T082: The Pressure chapter

Levels 13 through 16 draw their approved grammars with their one moving
layer each - a scrolling rule set, a falling 160-flake field, a
phase-mirrored band stack sliding left, and a breathing contour nest -
ported formula by formula from `tools/backgrounds/designs.py`, the
guide spacing through the package's floor-division placement, the pulse
through exact integer millionths of a pixel, and the leftward slide
through a negatively advancing offset, so both orientations reproduce
the design package's plates pixel for pixel at every whole-millisecond
clock. Three checks hold them:

- `run_backgrounds_pressure_checks()` of `tests/test_backgrounds.cpp`
  pins each design's plate and fill stream against its record on both
  orientations - the twenty-two staircase lines of Deep grid (24
  scrolling rules over the horizon offsets 6, 11, 18, 27, 38 and 51 px
  at the mid row, 40 guide segments stepping toward the canvas center,
  10 accent nodes that hold still, the 5 px/s displacement audited over
  adjacent 250 ms probes, and the 108.8 s landscape and 160.4 s
  portrait wrap returns), the ranked field of Fine snow (exactly 160
  flakes in the 87/73 and 82/78 landscape splits, three frame-split
  static bands that paint after the field exactly as the package
  orders, digests at six probes including the count-breathing frames
  where an accent flake straddles a margin edge, a quarter-second
  repaint bound of the falling footprint, and the 272 s landscape and
  5213 s portrait returns), the seven bands of Undertow (252 visible
  staircase segments landscape and 63 portrait in the 1:2:1 lift
  distribution, 28 and 31 foam ticks that hold their slots, the one
  shared velocity proven between the 250 ms and 500 ms probes where
  every segment moves one pixel left while the re-cut edge pieces
  anchor an edge, and the 120 s period return), and the nine broken
  contours of Slow pulse (72 bars in the manifest with the frame ring
  splitting the wide ones, 12 landscape and 4 portrait corner tips,
  the swell quiet at the exact phase edges 83/84 and 7916/7917 ms and
  at the 24 px crest of 4000 ms, and the 8 s cycle return). The
  chapter contract holds the caps, the accent share under two percent
  at rest and mid-motion, the element ceilings through whole motion
  passes (74, 167, 287, 97 landscape; 74, 168, 101, 36 portrait), one
  moving layer per design, the stream a pure function of the clock far
  along it and across its 32-bit wrap, the arc over the landscape
  plates - Pressure at 155 review-scale marks per 1000 against Build's
  65, 153 elements per design against 126, and a faster peak speed -
  and distinctness at one-eighth scale across the chapter boundary and
  inside it.
- `run_background_checks()` of `tests/test_renderer_sdl.cpp` pins the
  same designs in the real frame, both orientations: the rule, guide,
  and node pixels of level 13 with the rule set scrolled one pixel
  down at 200 ms while the guides hold, a dim and a bright flake of
  level 14 sunk three pixels at 500 ms over its three static bands,
  the staircase and foam of level 15 slid three pixels left at 500 ms
  with the bands crossing the guard gaps at their secondary shade, and
  the bars, openings, and corner tips of level 16 swelled to inset 34
  at 4 s into the cycle and back at rest at 8 s - the outer contour's
  own opening hiding under the board well, read instead at the second
  contour - through the portrait rotation. The veil, grayscale, and
  portrait-zone suites that read a flat margin moved to the
  not-yet-landed design 17.
- `tools/backgrounds/pressure_check.py` reads the real captures of
  `tools/backgrounds/pressure-capture.script.txt` and its portrait
  twin: two scripted sessions walk the level ladder to levels 13
  through 16 and write three frames per level, 64 ticks apart, over
  the tall-stack board. The check solves the one frame offset K of
  `clock = floor((tick + K) * 50/3) ms` jointly over every frame, then
  requires each frame's loud margins to equal the design package's own
  plate at that exact clock - the captures pin the designs at twelve
  known phases of their motion - beside the swell phases of the pulse,
  the caps, the accent share, the well purity, the HUD strip free of
  the chapter's accent shades, guard gaps that change only inside the
  design's own shades, and the adjacent levels at one-eighth scale over
  the whole captured frame: 210 checks, no failures, K = 0 in both
  orientations.

The captures are evidence, not fixtures: they land under
`build/agent-checks/<run-id>/`, and the script pair is retained so the
run reproduces. A bridge check painted both orientations of all four
designs through the runtime and through the design package's own
grammars at 69336 clock values - every 2 ms of the first two seconds,
every 7 ms through the twentieth, 50 ms steps through the wrap spans,
the pulse and rounding edges, the exact halves of the 5 and 6 px/s
displacements, the 32-bit clock wrap, and 3000 random clocks across
the full range - and compared the canvases pixel for pixel outside the
board frame: zero differing pixels. The visual pass read the
full-resolution frames and the one-eighth review sheet beside the
approved mockups: each motif reads at a glance - the receding grid,
the snow field, the two-current wave stack, the breathing contour
nest - no shape forms a letter, a digit, an arrow, or a tetromino
cluster, every mark is an axis-aligned rectangle or a staircase of
them, and the board and HUD stay the brightest planes of the frame.
Contract correction of the task: the Undertow slide direction - the
generator's offset advanced positively, sliding the staircase right
against its own declared velocity, its record, and the approved
concept, which all name the leftward 6 px/s slide. Fixed in
`docs/backgrounds/level-15.md` through the generator
(`tools/backgrounds/designs.py`, `records.py`), noted in the index
alongside the earlier corrections; the retained t = 0 mockups never
moved.

## T083: The Peak chapter

Levels 17 through 20 draw their approved grammars with their one
moving layer each - 190 embers rising at 7 px/s past static stepped
columns, 200 rain dashes with four stepped trails falling at 8 px/s
as one combined layer, 30 broken bands pressing inward 54 px on the
12 s triangle, and 280 debris fragments at the outer edges falling at
10 px/s past static slabs - ported formula by formula from
`tools/backgrounds/designs.py`, the ranked placement through the
package's hash rank, the columns, trails, and slabs through its own
staircase arithmetic. Two rounding depths carry the motion: the drift
of the embers, dashes, and fragments runs through exact integer
millionths of a pixel, the wrap rounding the whole `home + wrapped`
position over each margin's height plus the element's own extent, so
an element straddling a margin edge crosses as one piece; the inward
press of the bands runs through IEEE-754 doubles - `std::fmod` of the
12 s phase and `std::nearbyint` of `54 * triangle(t)` - because the
exact-half clocks of the 54 px pulse land on inexact phases where
CPython's round of the double product is the only bit-exact match.
The integer micro-precision of levels 7 and 16 stops one short of
that. Three checks hold them:

- `run_backgrounds_peak_checks()` of `tests/test_backgrounds.cpp`
  pins each design's plate and fill stream against its record on both
  orientations - the 190 embers of Rising ember over their static
  stepped columns (the 544.0 s landscape and 31278.0 s portrait wrap
  returns, the 7 px/s rise audited between adjacent probes), the 200
  dashes and four stepped trails of Storm lines as one layer (the
  68.75 s and 4545 s returns, the changed-pixel bound of the falling
  field), the 30 bands of Compression (press digests at the twelve
  exact-half clocks of the cycle, the quiet window through 55 ms and
  from 11945 ms with the first press step at 56 ms, the 54 px crest
  at the half cycle, the 12 s return), and the 280 fragments of
  Collapse over their slabs (the 55.1 s and 3721.4 s returns, the
  outer-biased ranking that keeps the middle clear) - plus the
  chapter contract: the caps, the accent
  share under two percent at rest and mid-motion, the element
  ceilings through whole motion passes (268, 220, 301, and 292 fills
  landscape; 261, 222, 80, and 293 portrait - Compression's 260
  placed elements sit inside the 300-element budget while the frame
  ring's clip splits its wide bars, the documented primitive
  expansion), one moving layer per design, the stream a pure function
  of the clock far along it and across its 32-bit wrap, the arc over
  the landscape plates - Peak at 166 review-scale marks per 1000
  against Pressure's 155, 267 elements per design against 153, and
  the faster peak speed, 10 px/s against 6 - and distinctness at
  one-eighth scale across the chapter boundary and inside it.
- `run_backgrounds_checks()` of `tests/test_renderer_sdl.cpp` pins
  the same designs in the real frame, both orientations: an ember
  pair of level 17 risen four pixels at 500 ms over its static
  columns, a dim and a bright dash of level 18 sunk two pixels at
  250 ms beside its stepped trail, the bars, the well cut, and the
  tick of level 19 pressed to the 54 px crest at 6 s and back at rest
  at 12 s, and a debris pair of level 20 fallen five pixels at 500 ms
  over its slabs, through the portrait rotation. The Fixed-mode,
  fade, and pause suites that read a flat margin now read base-only
  windows of the landed grammars - the 48 x 48 block of design 20
  clear of the ranked fragments, the 40 x 40 block of design 17 above
  its columns.
- `tools/backgrounds/peak_check.py` reads the real captures of
  `tools/backgrounds/peak-capture.script.txt` and its portrait twin:
  two scripted sessions walk the level ladder to levels 17 through 20
  and write three frames per level, 62 ticks apart, over the
  tall-stack board. The check solves the one frame offset K of
  `clock = floor((tick + K) * 50/3) ms` jointly over every frame,
  then requires each frame's loud margins to equal the design
  package's own plate at that exact clock - the double-rounded press
  included - beside the press phases 31, 22, and 12 of the three
  captured pulse frames, the caps, the accent share, the well purity,
  the HUD strip free of the chapter's accent shades with the scene's
  own dim text shade #8A94AC the one documented exception, guard gaps
  that change only inside the design's own shades, and the adjacent
  levels at one-eighth scale over the whole captured frame: 210
  checks, no failures, K = 0 in both orientations.

The captures are evidence, not fixtures: they land under
`build/agent-checks/peak-capture/`, and the dated record
`build/agent-checks/t083-peak-2026-11-05/` holds the copies, the
review sheets, and the re-run sets that prove the frames reproduce
byte for byte. A bridge check painted both orientations of all four
designs through the runtime and through the design package's own
grammars at 16648 (level, orientation, clock) plates - every
millisecond of the first 260 ms, every 7 ms through the 12 s cycles,
the exact-half neighborhoods of the 7, 8, and 10 px/s drifts and the
54 px press, the wrap spans, the late clocks, and the 32-bit wrap -
and compared the fill streams element for element: zero mismatches.
Both capture sessions re-ran against the final binary: 26 of 26
frames byte-identical, title frames included. The visual pass read
the full-resolution frames, the one-eighth review sheet beside the
approved mockups, and the twenty-level thumbnail arc with level 17
beside level 9: each motif reads at a glance - the rising specks
over stepped columns, the rain field with its stepped trails, the
closing band ring, the edge debris over slabs - no shape forms a
letter, a digit, an arrow, or a tetromino cluster, the warm tones
live in the tiny ember marks alone, and the board and HUD stay the
brightest planes of the frame. Contract corrections of the task, both
in level 18: the Storm-lines cell pitch is 16 px, which the
generator, the retained mockups, and the runtime all draw against
the record prose's 22 px, and its dashes wrap over the margin height
plus the tallest dash's 6 px exactly as its trails do, not at the
bare margin span. Fixed in `tools/backgrounds/designs.py` and
regenerated through `records.py` into `docs/backgrounds/level-18.md`,
noted in the index alongside the earlier corrections.

## T085: Integrated background validation

The one command is `tools/backgrounds/t085.sh` (phases: `generate capture
focus determinism legacy perf gates`; name a subset to rerun just those,
`SKIP_CAPTURE=1` reuses the captures in the run directory). Everything
lands under `build/backgrounds/t085/` (ignored build output); the evidence
report is the T085 section of `notes/agent-testing.md`, the cost tables the
T085 section of `notes/performance.md`. The phase scripts live beside the
driver: `catalog_gen.py` (every scripted session, a pure function of its
name), `catalog_check.py` (the integrated acceptance check),
`focus_session.sh` with `focus_check.py` (the X11 focus probe under
`xvfb-run`, `openbox`, and `xdotool`), `determinism_runs.sh`,
`legacy_off_compare.sh` (the private temporary tree of revision `cd54ba2`,
registered in the Looper resource cleanup file), and `perf_runs.sh` (the
Release build in `build-rel`).

What the capture phase drives, all through the real SDL application under
the dummy driver at `--tick-per-frame`:

- `catalog-landscape`, `catalog-portrait`: the ladder walk of
  `catalog_gen.py` clears four lines a cycle to level 20, so Auto crosses
  every design change on the way. Every level contributes its entry
  transition (three stepped samples for a fade, the landed frame for a
  cut), a settled frame, a second frame 62 ticks later, and - for a pulse
  design - a third phase 186 ticks on, with fixture reloads inside the
  wide waits so peak gravity cannot top the board out. Past level 20: two
  more cycles (the level cap), a 96-frame pause, a resume, and a restart.
  The landscape walk starts from a fresh save; the portrait walk from the
  legacy settings form without background fields, which must land on Auto
  as well.
- `pair` staged four ways: Fixed 17 and Fixed 20, each with grayscale on
  and off, and Off - one play frame each, so the grayscale pairs prove the
  wallpaper bytes hold while the pieces change and Off proves the legacy
  flat alone remains.
- `orientation`: Fixed design 20 (the fastest drift) across three turns of
  the ORIENTATION row at one unbroken clock.
- `rapid-modes`: five MODE selects and seventeen DESIGN selects back to
  back on the developer virtual controller, then the play frame.

`catalog_check.py` reads those captures and holds, per frame, the plate
equality against the design package at one solved presentation clock, the
fade arithmetic (the runtime's own integer blend, truncating division
included), the caps and the two-percent accent share on the complete
canvas, well purity, the static proofs of levels 1 through 4, the motion
proofs (one moving layer) of levels 5 through 20, the pause freeze with a
resume that banks no jump, the restart cut, the cap, the orientation clock
continuity, the rapid ring result, and the arc distinctness at the
one-eighth review scale. It writes the two captured twenty-level thumbnail
sheets. The current pass: 968 checks, 0 failures.

The remaining phases: `focus` minimizes the real window under a window
manager and proves the wallpaper clock froze and resumed without a jump;
`determinism` replays one scripted round to level 20 under a fresh save,
Off, Auto, and Fixed 17 and compares the stripped observation streams byte
for byte; `legacy` rebuilds the pre-backgrounds revision `cd54ba2` in a
private temporary tree and compares every frame of a scripted round
against Background Off; `perf` records the draw counts, the work-mode wall
times, and the paced frame-time percentiles of the same workload under
Off, Fixed 17 through 20, and Auto in both orientations; `gates` runs the
Debug and Release CTest suites and the full unattended harness.

## Final handoff

After T024, run T027 and T028 from `docs/DESIGN_REVIEW.md`.
These tasks add an independent screenshot review and verify required fixes.
After T025, run the T026 optimization loop in `docs/PERFORMANCE.md`.
Then finish the desktop gate (`tests/harness/t015.sh`), native build,
launcher, and local archive. Include one
report with agent results, unresolved questions, reproduction commands, and
the exact package identity. Then request the final human and device pass.
Do not describe the release as device-validated until that pass succeeds.
