# Screenshot design review

Date: 2026-11-03.

Sources: [design review contract](../docs/DESIGN_REVIEW.md),
[agent testing contract](../docs/AGENT_TESTING.md),
[game specification](../docs/SPEC.md), and the T024 evidence in
[agent testing notes](agent-testing.md).

## T027: capture, review, and route findings

One command captures the packet and one command reviews it:

```bash
tools/design-review/capture.sh
tools/design-review/review.sh
```

`capture.sh` builds the game, records the source identity, runs the
headless `ctest` suites, then drives four sessions of the real SDL
application on the T023 harness. `review.sh` sends the packet to
`codex exec` in two bounded batches and validates the reports. The
[tool README](../tools/design-review/README.md) documents the
environment options and the evidence layout.

Verified results:

- Passed: `tools/design-review/capture.sh` twice, into two packets
  (`initial`, reviewed, and `reuse-check`, the reusability proof).
- Passed: the headless `ctest` suites inside both captures, 5 of 5.
- Passed: `tests/harness/run.sh` after the new fixture and scripts
  landed, 157 of 157 checks (`build/agent-checks/t027-regression/`).
- Passed: the codex review round, two calls, exit 0, both reports
  schema-valid with every attached image inspected.
- Passed: `review.sh` revalidation with `DR_SKIP_CODEX=1`, no new
  reviewer call, still zero validation problems.

### The packet

Packet: `build/agent-checks/design-review/initial/packet/`, seed 31337
everywhere, 12 images, source identity in `source.sha256` next to it
(game binary `690f3d44...`, commit `179107b` plus the new review files).
The packet definition lives in
[packet.tsv](../tools/design-review/packet.tsv), so the same command
recaptures the same frames.

| id | session | tick | covers |
| --- | --- | --- | --- |
| title | scenes | 0 | title menu |
| controls | scenes | 0 | controls screen |
| gameplay-early | scenes | 64 | early gameplay, ghost, empty hold, five previews |
| hold-locked | scenes | 127 | hold unavailable, stashed Z dimmed |
| pause | scenes | 181 | pause overlay |
| gameover | scenes | 217 | game over over the topped board |
| restart-spawn | scenes | 0 | restart; spawn sequence frame 1 |
| spawn-visible | scenes | 52 | spawn sequence frame 2 |
| clear-before | clears | 11 | four-line clear, before (labeled fixture) |
| clear-after | clears | 18 | four-line clear, after |
| tall-stack | tall-stack | 7 | tall uneven stack (labeled fixture) |
| letterbox-640x480 | letterbox | 152 | smaller letterboxed window |

One new labeled fixture and four new session scripts produced it:
`tall-stack-ready.board.txt` (an uneven skyline between rows 6 and 10
with a forced T piece, no complete row, free spawn cells) with the
`tall-stack` session built on it, and the `design-review-*` scripts.
The clear frames reuse the existing `four-line-ready` fixture, and the
scenes and letterbox sessions are normal games from an empty board. The
manifest (`manifest.json`, `manifest.md`) pairs every image with its
observation record, its input steps, and its expected content.

### The review run

`review.sh`, round `initial`, two calls of six images, 38 s and 47 s of
the 600 s bound, both exit 0. Reviewer identity on record in
`review-initial/runner.json`: `codex-cli 0.166.0`, model `gpt-6.1-sol`
from the existing configuration, `--sandbox read-only`, prompt from
standard input, strict output schema, full JSONL transcript, saved
standard error, and exit status per call. The reviewer read
`docs/SPEC.md` and `docs/DESIGN_REVIEW.md` from the repository, which
the read-only sandbox allows, and edited nothing.

`check_review.py` validated both reports against the schema and the
packet: every attached image id appears in `inspected_images`, every
finding cites attached images only, priorities are 1 to 5, and the
confidence, evidence, and scope values are legal. Zero validation
problems. The two reports merged by finding key into
`review-initial/merged.json`: five findings, one of them reported by
both calls and merged (`ghost-outline-low-contrast`).

### Findings and dispositions

I reproduced every finding against the current application with
`check_fixes.py`, which measures the packet images from pixels alone.
Its full output is saved as
`review-initial/fix-checks-before.json`.

Accepted and routed to Looper as fix tasks:

- **control-hint-glyph-scale**, priority 2, approved fix. The gameplay
  control hints and the menu footer hints draw at glyph scale 1, so
  their text is 7 px tall while panel labels run at 14 px and menu
  items at 21 px (`kHintScale` in `src/renderer_sdl.cpp:36`). Measured
  on `gameplay-early`: seven text rows of exactly 7 px in the left
  panel. In the letterboxed frame the same rows measure 4 to 5 device
  pixels, which supports the reviewer's fragmentation claim. Two
  reviewer keys described this one root cause
  (`small-control-and-menu-hints`, `control-hints-too-small`); the
  routing merges them into one task. Acceptance: hint glyphs at least
  14 logical pixels high, reflowed inside the approved left panel,
  nothing clipped.
- **ghost-outline-contrast**, priority 3, approved fix. The ghost is a
  one-pixel outline in a color blended 60 percent toward the well
  (`ghostColor`, `include/stackfall/renderer_sdl.hpp:249`). Measured on
  `gameplay-early`: outline `(35, 91, 53)` against the well
  `(10, 12, 19)`, a 2.44:1 luminance contrast. The active piece's fill
  reaches about 9.8:1 against the same well, so the landing preview is
  the weakest shape on the board. The specification asks for a readable
  landing preview, and the fix stays inside "same piece outline or dim
  fill". Acceptance: at least 3:1 contrast with the outline still
  hollow.
- **letterbox-scene-centered**, priority 3, approved fix. In the
  640x480 window the scene scales by two thirds but anchors at the top
  of the window: 0 px of bar above and 118 px below. Reproduced twice:
  the reviewed frame (top 0, bottom 118) and a 720x480 probe with the
  same driver (top 0, bottom 72). The window is resizable and
  `SDL_RenderSetLogicalSize` runs once at startup
  (`src/main.cpp:1402`), so a later resize never recenters the
  viewport. The device runs fullscreen at the native 960x544, so this
  is a desktop-window defect, which section 6.1 still requires to
  letterbox. Acceptance: both bars exist and differ by at most 2 px.

Optional cosmetic work, recorded and deferred (it does not gate T028):

- **hold-lock-status-implicit**. The locked hold signals only through
  the dimmed stash. The dim signal itself works: the stashed Z draws at
  77 percent of the preview's peak brightness (0.77 measured), and T024
  already verified it reads clearly. A "LOCKED" label would add words
  the panel does not need to stay clear. Deferred with the reviewer's
  own `optional_cosmetic` classification.

No finding was rejected as unsupported. Every region the reviewer cited
matched the measured geometry, and no finding claimed what a static
image cannot establish. The reviewer's own limitations list records
those bounds: no latency, audio, animation, or enjoyment judgments, and
no intermediate clear feedback from a before-and-after pair.

### Capture defect found and fixed during reproduction

The `gameover` frame records `record=0` although its score equals the
saved high, and its panel therefore lacks the NEW RECORD line. The
cause is my capture run, not the game: `capture.sh` reused the run
directory across two passes, so the second pass started with the first
pass's saved high of 216. `capture.sh` now removes each session
directory before it runs, so a reused run id cannot carry stale save
state into a packet.

Evidence, from a clean rerun after the fix
(`build/agent-checks/design-review/reuse-check/`, kept as the
reusability proof; it was never reviewed):

- `gameover` records `record=1` and `title` shows high 0, the state a
  first run produces. The NEW RECORD line measures 752 accent pixels
  there against 144 in the reviewed frame.
- Every frame that does not read the save is byte-identical between the
  two captures (`gameplay-early`, `hold-locked`, `pause`,
  `clear-after`, `tall-stack`); only `title` and `gameover` differ,
  exactly the two frames whose content depends on the saved high.

The reviewed packet stands: the mismatch affects one expected line, the
reviewer reported it honestly as a limitation, and re-reviewing the
same twelve images again would spend the round's image budget on
evidence that is already understood.

### Task routing

Findings route through the Looper result contract; neither the reviewer
nor this worker edits `to-do.json`. Three fix tasks were reported, one
per accepted root cause, each depending on T027 only:

| routed key | task | reviewer keys | priority | files |
| --- | --- | --- | --- | --- |
| control-hint-glyph-scale | T030 | small-control-and-menu-hints, control-hints-too-small | 2 | src/renderer_sdl.cpp, include/stackfall/renderer_sdl.hpp |
| ghost-outline-contrast | T031 | ghost-outline-low-contrast | 3 | src/renderer_sdl.cpp, include/stackfall/renderer_sdl.hpp |
| letterbox-scene-centered | T032 | letterbox-scene-not-centered | 3 | src/main.cpp |

Duplicate suppression, verified against the accepted backlog
(`to-do.json.context/.../backlog.json` at commit `179107b`):

- No open task covers any of the three keys. T010 and T011, which
  introduced the renderer and screens, are done, so a new key is the
  correct route for a defect found in them.
- The hidden I-piece spawn question was already routed as T029 with the
  key `looper:finding:t024-i-spawn-hidden-delay-design-question`. The
  review prompt told the reviewer that a separate task owns it, the
  reviewer did not report it, and no duplicate task was created.
- One root cause that two reviewer keys described became one task.

T028 must depend on the three created fix tasks through Looper
planning; the fix tasks depend on T027 and never on T028, so the
dependency graph stays acyclic. That wiring is now in the accepted
backlog: T028 depends on T027, T030, T031, and T032. The durable copy
of the three required findings, with their acceptance criteria, lives in
[tools/design-review/prior-findings.json](../tools/design-review/prior-findings.json),
because the initial packet itself is untracked build output.

### Fix record: control-hint-glyph-scale (T030, 2026-11-03)

The hint glyph scale rose from 1 to 2 (`kHintScale`,
`src/renderer_sdl.cpp:39`), so the gameplay control hints and the title,
pause, and game-over footer hints now draw 14 px tall, the height of the
panel labels. The action column of the play hints moved from x=176 to
x=192, because the widest control (`LEFT/RIGHT`, 118 px at scale 2) now
ends at 182; the widest action (`ROTATE CCW`) ends at 310, inside the
panel edge at 324. The glyph table and the rectangle drawing are
untouched.

Regression cover: `tests/test_renderer_sdl.cpp` restates the hint scale
and counts the text rows of the play panel, the title footer, and the
pause footer from pixels. Each row must measure exactly 14 px and no hint
text may cross the left panel edge. Reverting only the renderer constant
fails nine of those checks.

Fresh evidence, packet `build/agent-checks/design-review/t030-fix/`
(seed 31337, twelve images, `DR_RUN_ID=t030-fix
tools/design-review/capture.sh`):

- Passed: `check_fixes.py` `control-hint-glyph-scale`. Hint glyph height
  14 logical px (`text_row_runs` `[12, 14, 14, 14, 14, 14, 14]`, the 12
  being the CONTROLS heading cut by the measured band). In the 640x480
  letterbox the same rows measure 9 to 10 device px, about 15 logical,
  against 4 to 5 device px before the fix. Report saved as
  `fix-checks.json` next to the packet.
- Passed: the hint text spans x=64..309 and y=148..307, measured from
  the `gameplay-early` pixels, so nothing reaches the panel edge at 324
  or the board frame at 359.
- Passed: visual inspection of `gameplay-early` and `title`. All seven
  hint rows read, the two columns stay aligned, and the title footer
  line sits whole inside the canvas.
- Passed: `ctest` 5 of 5, `tests/harness/run.sh` 157 of 157
  (`build/agent-checks/harness-2026-11-03-111423/`), and
  `tests/harness/t024.sh` 287 of 287
  (`build/agent-checks/t030-regress-t024/`).

Still failing on the same packet, both owned by their own tasks:
`ghost-outline-contrast` at 2.44 (T031) and `letterbox-scene-centered`
with bars 0 and 118 px (T032). One of three required criteria passes
until they land.

### Fix record: ghost-outline-contrast (T031, 2026-11-03)

The ghost outline now blends one quarter of the piece fill toward the
well instead of three fifths (`ghostColor`,
`include/stackfall/renderer_sdl.hpp:249`), the least blend that keeps
every one of the seven outlines above the 3:1 floor. The measured
colors: S `(57, 159, 83)` at 5.82:1 (was `(35, 91, 53)` at 2.44:1), and
the dimmest fills J at 3.48:1 and Z at 3.49:1. The outline stays one
pixel wide and hollow, so the ghost cannot read as a settled or live
cell, and each outline stays at least 1.5 times dimmer than the live
fill of its own piece. The fix stays inside the approved "same piece
outline or dim fill" text: no new shape, no second outline pixel.

Regression cover: `tests/test_renderer_sdl.cpp` restates the WCAG
relative-luminance formula of `check_fixes.py` and the quarter blend
with its own arithmetic, then checks all seven pieces for the 3:1 floor,
the dimmer-than-live rule, and the one-pixel hollow outline from drawn
pixels. The render suite reports 900 checks. Reverting only the header
constant fails 14 of them, so the cover is live.

Fresh evidence, packet `build/agent-checks/design-review/t031-fix/`
(seed 31337, twelve images, `DR_RUN_ID=t031-fix
tools/design-review/capture.sh`, source identity in `source.sha256`):

- Passed: `check_fixes.py` `ghost-outline-contrast`. Outline
  `(57, 159, 83)` against the well `(10, 12, 19)`, 5.82:1, criterion
  at least 3.0. Report saved as `fix-checks.json` next to the packet.
- Passed: the T ghost of the `tall-stack` fixture, measured from pixels
  because the checker only reads the S ghost of `gameplay-early`.
  Outline `(135, 87, 173)` at 3.72:1 across 368 px, and the interior of
  all four cells holds the well color, so that ghost is hollow too.
- Passed: inspection of the two ghost regions. The remote image service
  rejected every upload this session (error 1210, PNG and JPEG, crop and
  full frame), so the inspection reads the pixels directly and renders
  the regions as a per-pixel map: the S outline draws as one clean
  hollow S at the floor and the T outline as one hollow T resting on the
  stack top, both distinct from the solid settled cells beside them.
- Passed: `ctest` 5 of 5, `tests/harness/run.sh` 157 of 157
  (`build/agent-checks/t031-regress-harness/`), and
  `tests/harness/t024.sh` 287 of 287
  (`build/agent-checks/t031-regress-t024/`).

Still failing on the same packet, owned by its own task:
`letterbox-scene-centered` with bars 0 and 118 px (T032). Two of three
required criteria pass until it lands.

### Fix record: letterbox-scene-centered (T032, 2026-11-03)

The finding had two causes, and neither was the one the packet first
suggested. Probes under the same X11 driver showed that SDL recenters the
letterbox viewport itself when the window resizes: an `xwd` capture of the
presented window held the board at device rows 80..399, the centered
position. What the packet measured instead was the capture and the bars:

- The capture read the wrong region. `SDL_RenderReadPixels` with a null
  rectangle reads in the logical space of the current viewport, so with the
  letterbox active it returned only the scene, anchored at the top of the
  640x480 image, padded with black below. That is the reviewed 0 px above,
  118 px below, and it is why a 720x480 probe measured 0 and 72.
- The bars were never drawn. `SDL_RenderClear` clears the whole target and
  ignores the viewport, so the scene's own background clear painted the
  whole window in the canvas color, and the slack outside the viewport kept
  whatever the window buffers held from before the resize.

What landed, in the renderer module so the game and its pixel suite share
one implementation:

- `applyCanvas` states the 960x544 canvas. The platform layer calls it once
  at startup and again on every `SDL_WINDOWEVENT_SIZE_CHANGED`, so a build
  that does not recenter on its own cannot keep the old viewport offset.
- `beginFrame` paints a black base across the whole output before it
  restores the canvas, so the slack bars are black in every presented frame
  instead of stale pixels. At the native canvas size it draws nothing,
  because there are no bars.
- `Renderer::clearBackground` fills the canvas rectangle instead of
  clearing the whole target, so the scene no longer repaints the bars.
- `readOutputPixels` captures the whole output in device pixels, bars and
  scene, and puts the canvas back for the next frame.

Regression cover: `tests/test_renderer_sdl.cpp` draws frames through the
same path the game draws, onto a surface pre-filled with a bright color
that stands for the stale pixels a resized window holds. It measures the
black rows of a 640x480 and a 720x480 window from pixels, requires both
bars and a split within 2 px, requires the stale color to be gone, reads
the capture back through `readOutputPixels`, and compares a native frame
against a scene-only frame byte for byte. The render suite reports 924
checks. Reverting the renderer changes alone fails 7 of them, so the cover
is live.

Fresh evidence, packet `build/agent-checks/design-review/t032-fix/`
(seed 31337, twelve images, `DR_RUN_ID=t032-fix
tools/design-review/capture.sh`, source identity in `source.sha256`):

- Passed: `check_fixes.py` `letterbox-scene-centered`. Window 640x480,
  top bar 59 px, bottom bar 59 px, criterion both bars within 2 px. Report
  saved as `fix-checks.json`. All three required criteria pass on this
  packet, the first packet to do so.
- Passed: a 720x480 probe with the same driver measures two 36 px bars
  (`design-review/t032-probe720/`), the size that measured 0 and 72 in the
  T027 reproduction.
- Passed: the native 960x544 output is unchanged. The `scenes` session ran
  twice, once from a build of the previous commit and once from this one,
  same seed, script, and save directory. All nine checkpoint BMPs are
  byte-identical, and the observation records differ only in the screenshot
  path.
- Passed: the T024 X11 session, which letterboxes the same window for play,
  pause, resume, title, and quit. Every letterboxed frame there measures
  bars of 59 and 59 px, and every native frame stays full bleed
  (`build/agent-checks/t032-regress-t024/`).
- Passed: `ctest` 5 of 5, `tests/harness/run.sh` 157 of 157
  (`t032-regress-harness/`), and `tests/harness/t024.sh` 287 of 287.

The device runs fullscreen at 960x544, so `beginFrame` draws nothing there
and the fix changes no device pixel.

## T028: verify fixes and close the review (2026-11-03)

The three required fixes landed through T030, T031, and T032, and T028
depends on all three plus T027, so the closure gate could run. One
capture and one bounded review round verified them.

Commands, run in the T028 worktree at commit `130cc66`:

```bash
DR_RUN_ID=verify tools/design-review/capture.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/verify/packet \
  --json build/agent-checks/design-review/verify/fix-checks.json
DR_ROUND=verify \
DR_IMAGES=gameplay-early,hold-locked,spawn-visible,tall-stack,letterbox-640x480 \
DR_PRIOR_FINDINGS=tools/design-review/prior-findings.json \
tools/design-review/review.sh
HARNESS_RUN_ID=t028-verify-harness tests/harness/run.sh
T024_RUN_ID=t028-verify-t024 tests/harness/t024.sh
```

The initial packet's `open-findings.json` no longer exists in this
worktree, so the round read the required findings from the durable
tracked copy `tools/design-review/prior-findings.json`, which restates
the three accepted findings with their acceptance criteria.

### Independent pixel verification

`check_fixes.py` decides every criterion from pixels alone. The before
side is the pre-fix packet `t028-preflight` (commit `13e337d`, binary
`c82d264d...`, seed 31337, captured 2026-11-03; its worktree keeps it
at
`../../task-33a11f7dfadedbac/build/agent-checks/design-review/t028-preflight/packet`,
relative to this one); the after side is the fresh `verify` packet
(commit `130cc66`, binary `ec0c12aa...`, seed 31337, twelve images).
Both packets come from the same `packet.tsv` sessions and scripts, so
the frames differ only by the fixes.

| criterion | before (`t028-preflight`) | after (`verify`) |
| --- | --- | --- |
| control-hint-glyph-scale | FAIL: tallest text run 12 px, hint rows 7 px | PASS: 14 logical px, runs `[12, 14, 14, 14, 14, 14, 14]`, text ends at y=287 inside the panel |
| ghost-outline-contrast | FAIL: outline `(35, 91, 53)`, 2.44:1 | PASS: outline `(57, 159, 83)`, 5.82:1, 368 outline px |
| letterbox-scene-centered | FAIL: top bar 0 px, bottom bar 118 px | PASS: 640x480 window, bars 59 px and 59 px |

Three of three required criteria pass; the report is saved as
`verify/fix-checks.json`.

The reviewer's one limitation — the menu footer hints were not attached
— is closed by direct measurement of the fresh frames: the title,
pause, and game-over footer lines each measure exactly one 14 px text
row on `title.png`, `pause.png`, and `gameover.png` (exact
`kTextDim`/`kText` pixel match), the height the T030 record claims and
the render suite pins.

### The verification review round

`review.sh`, round `verify`, one call of five images
(`gameplay-early`, `hold-locked`, `spawn-visible`, `tall-stack`,
`letterbox-640x480`), 39 s of the 600 s bound, exit 0. Reviewer
identity in `review-verify/runner.json`: `codex-cli 0.166.0`, model
`gpt-6.1-sol` from the existing configuration, read-only sandbox,
prompt from standard input, strict output schema, full JSONL transcript,
saved standard error, exit status. The transcript shows the reviewer
read `docs/DESIGN_REVIEW.md` and `check_fixes.py`, ran the checker
itself inside the read-only sandbox, and inspected every attached image;
`check_review.py` validated the report with zero problems.

All three prior findings came back `verified:` with direct evidence and
high confidence, and the reviewer reported zero new findings, so nothing
new needs routing:

- `control-hint-glyph-scale`: verified. 14 px glyphs, both columns
  inside the panel.
- `ghost-outline-contrast`: verified. 5.82:1, hollow and distinct in
  `gameplay-early` and `tall-stack`.
- `letterbox-scene-centered`: verified. 59 black rows above and below,
  native placement unchanged.

One evidence wart, recorded here so an audit reads it right: the
manifest stamps every capture with round label `initial`
(`make_packet.py` records the capture round, which `capture.sh` always
names `initial`), so `review-verify/merged.json` carries
`"round": "initial"` although the round is `verify`. The round is on
record in `review-verify/runner.json`, the directory name, and
`call-01.exit`.

### Affected tests, rerun

- Passed: `ctest` 5 of 5 inside the capture, including the pinned-seed
  regression hashes and the 924-check render suite that covers the hint
  scale, the ghost blend, and the letterbox bars from pixels.
- Passed: `tests/harness/run.sh`, 163 of 163
  (`build/agent-checks/t028-verify-harness/`).
- Passed: `tests/harness/t024.sh`, 287 of 287
  (`build/agent-checks/t028-verify-t024/`), including the letterboxed
  X11 play session.

### Dispositions and closure

| finding | disposition |
| --- | --- |
| control-hint-glyph-scale (T030) | fixed; pixel check, reviewer, and tests agree |
| ghost-outline-contrast (T031) | fixed; pixel check, reviewer, and tests agree |
| letterbox-scene-centered (T032) | fixed; pixel check, reviewer, and tests agree |
| hold-lock-status-implicit | optional cosmetic, deferred; does not gate closure |
| hidden I-piece spawn rule (T029) | deferred product question for the owner; does not gate closure. Decided and implemented 2026-11-03 by T029: pieces spawn at visible row 0 |

No finding was rejected in this round and no new required defect
appeared, so no follow-up task needs routing. Every required finding of
the review is resolved, so the round-M2 closure gate closes. The two
review rounds stand as run: `initial` (T027) and `verify` (T028).

## Geometry change: 27 px board cells (T038, 2026-11-03)

The owner-approved board geometry change (`looper:finding:board-cells-27px`,
task T038) moved the field from 24 px cells at (360, 32) to 27 px cells at
(345, 2), so every pixel region this toolchain measures moved with it. No
reviewed finding reopened: this is an approved layout decision, not a
defect, and the packet images come from the same `packet.tsv` sessions.

Tooling changes:

- `tools/design-review/check_fixes.py`: `HINT_REGION_Y` is now (120, 260)
  and `GHOST_REGION` is derived from the board origin and cell
  (x=426..534, y=488..542, the same S ghost of seed 31337 at columns 3..6
  and rows 18..19). The hint check also measures the columns of the hint
  band now, so its "stays inside the left panel" criterion bounds the
  rightmost text column (309) against the panel edge (324); the earlier
  row-only comparison against 588 could not fail.
- `tools/design-review/build_prompt.py`: the approved-scope block states
  the 27 px geometry, so a reviewer does not report it as a defect.

Fresh packet `t038-board-27px` (seed 31337, twelve images): all three
required criteria pass - hint glyphs 14 logical px inside the panel, the
ghost outline at 5.82:1, letterbox bars 59 and 59 px. Measured from the
packet pixels, the well spans x=345..614 and y=2..541 with the frame
complete at x=344 and x=615 and y=1 and y=542 in every play frame, and no
image has content on its outer edge. The T030/T031/T032 dispositions
above are unchanged; the HUD composition change of T037
(`focused-gameplay-hud`) still waits and will need its own packet and
checks when it runs.

## Fresh capture after the options screen (T041, 2026-11-03)

The options screen of T040/T041 persists a GHOST preference, so the packet
had to prove the reviewed frames still draw the ghost. A fresh capture
(`t041-design-review-2026-11-03`, seed 31337, the same four `packet.tsv`
sessions and twelve images) passes `check_fixes.py` with all three required
criteria: hint glyphs 14 logical px inside the left panel, the ghost outline
at 5.82:1 over 416 pixels, and letterbox bars of 59 and 59 px. Every packet
session starts from an empty save directory, so both preferences load at
their on defaults - the ghost check measures the default the options screen
can now turn off, not a fixture state. This was a capture and pixel check
only; no review round ran, and the two closed rounds above stay as they
are. The registered harness session that proves the off state is
`options-ghost` in `tests/harness/run.sh` (see `notes/agent-testing.md`).
One stale line remains in the packet definition: the `dr-title` expected
text of `packet.tsv` still lists the three pre-options title items, so a
future review round that quotes it should update that row first.

## Focused gameplay HUD (T037, 2026-11-03)

The approved composition of task T037 (`looper:finding:focused-gameplay-hud`)
replaced the gameplay HUD the earlier rounds reviewed: the statistics group
(Hold, score, lines, level) moved to a 128 px column at x=192..319 beside the
board, the five Next previews to the matching column at x=640..767, both
24 px from the frame, the permanent gameplay legend was removed (superseding
only the gameplay-hint half of T030, whose footer-hint half still holds),
and the pause overlay gained a CONTROLS item that opens the mappings page
over the frozen round and returns to the same item.

Tooling changes:

- `tools/design-review/check_fixes.py`: the superseded
  `control-hint-glyph-scale` criterion is gone. Two required criteria
  replace it. `focused-hud-stats-left` requires seven 14 px text rows in
  the left column (HOLD, three labels, three values), nothing below the
  group, and quiet gaps from the column to the frame and to the canvas
  edge. `focused-hud-next-right` requires the NEXT heading as the only
  text of the right column, a piece in every one of the five slots, and
  the same quiet gaps. The letterbox variants of both stay informational;
  they now map their boxes through a centering `window_box` (smallest
  axis ratio, ceil top-left, floor bottom-right), so the black letterbox
  bars no longer fall inside the measured bands. Before that fix the
  informational slot 0 of the letterbox frame read 3096 non-background
  pixels - the top bar inside a band scaled without its vertical offset;
  after it, all five slots read exactly 400 device pixels, which is the
  native 900 scaled by two thirds squared.
- `tools/design-review/packet.tsv`: the `dr-title` row now lists the four
  title items including OPTIONS (this was the deferred task T042; see
  below), `dr-gameplay-early` states the focused-HUD expectation of the
  reviewed frame, `dr-pause` states the nested panel with its four items
  and the route to CONTROLS, and `dr-restart-spawn` was corrected to the
  immediate-spawn rule of T029 (its old text still described the piece
  entering below the visible rows).
- `tools/design-review/build_prompt.py`: the approved-scope block states
  the focused HUD, the quiet outer regions, and the pause route, and
  tells the reviewer not to report the missing gameplay legend or the
  quiet previews as defects; the assessment list adds the controls
  screen and the pause overlay's route to it.
- `tools/design-review/prior-findings.json`: the
  `control-hint-glyph-scale` entry carries a `superseded` note now, so a
  verification round that loads it knows the gameplay-legend half was
  replaced by T037 while the 14 px footer hints of T030 still apply.

Fresh packet `t037-fix` (seed 31337, the same four `packet.tsv` sessions,
twelve images): all four required criteria pass - the seven statistics
rows at exactly 14 logical px with quiet gaps, the NEXT heading alone
with 900 piece pixels in every slot, the ghost outline at 5.82:1, and
letterbox bars of 59 and 59 px. `fix-checks.json` sits beside the packet.
The informational letterbox rows now agree with the native frame, and the
hold dim still reads 0.77 of the full color.

Every packet image was opened and inspected (factual description batches
through the codex reader, reports under `t037-fix/inspect/`): the left
column reads HOLD then SCORE, LINES, LEVEL with bright values over dim
labels, the right column reads NEXT with the first preview bright and the
four below quieter, no gameplay legend appears anywhere, and no frame
shows text clipped or overlapping. The pause overlay nests between the
columns with the frozen statistics and previews readable beside it, and
the letterboxed frame keeps every label readable at two-thirds size. One
cosmetic observation, pre-existing and outside this task's composition:
the game-over panel's left edge (x=239) cuts across the dimmed background
statistics column (x=192..319), so fragments of the dimmed SCORE, LINES,
and LEVEL labels peek out at the panel edge. The gameplay HUD and the
pause overlay own this task's geometry; the game-over overlay kept its
earlier panel, and any change there belongs to a new finding.

The pause route itself was captured outside the packet, because the
packet's scenes session ends its pause at the overlay: the ad hoc session
`t037-fix/pause-controls/` (seed 31337, keyboard, `--tick-per-frame`)
records the overlay on RESUME at round tick 63, the mappings screen
reached from the CONTROLS item with the footer "A OR B BACK TO PAUSE",
and the return to the overlay with the cursor still on CONTROLS and the
same frozen tick 63.

T042 (the stale `dr-title` row) is done by this packet refresh: the row
at `packet.tsv` line 22 lists START GAME, CONTROLS, OPTIONS, and QUIT,
and the fresh `dr-title` frame shows all four items. The task can be
closed with this note as evidence.

## Portrait mode: rotation and cluster roles (T055, 2026-11-04)

The owner requested 90-degree portrait play on the RG503 (task T055): the
board gains height, every shape square grows, the D-pad sits on the right of
the grip and the X, Y, A, and B buttons on the left, and every direction is
remapped into the rotated frame. This section pins the two decisions the
request leaves open - which way the device turns, and which button does what
once it has - before any mapping lands in the source. The device checks stay
with the blocked physical task; everything here is a logical decision a
desktop harness can verify.

### The rotation direction

**Decision: the device turns 90 degrees clockwise in the player's hands.**

The RG503 is a landscape handheld: its D-pad occupies the left end and its
X/Y/A/B cluster the right end. Turning the device clockwise moves the left
end to the top of the grip and the right end to the bottom, so the right hand
holds the D-pad and the left hand holds the action cluster - exactly the grip
the owner describes. The counter-clockwise turn is the only alternative, and
it puts the D-pad in the left hand, which contradicts the request.

The screen content rotates the other way, so it stands upright in that grip.
The portrait canvas is 544 wide by 960 tall in the player's frame, and a
portrait pixel (x, y) draws at device pixel (y, 543 - x) on the unrotated
960x544 output. The window, the letterbox rules, the screenshot size, and
the observation records all stay in device space, so every existing evidence
format survives unchanged; only the scene inside the canvas turns.

### The cluster roles

**Decision: portrait reverses each pair of cluster roles. B takes rotate
clockwise and select, A takes rotate counter-clockwise and back, Y takes hard
drop, and X takes hold. The left shoulder stays a second hold, and Start
stays pause.**

The landscape table of specification section 5 orders the cluster by how
close each button sits to the holding thumb: A rotates clockwise and
confirms, B rotates the other way and goes back, X hard drops, and Y holds.
Turning the device clockwise puts the cluster under the left thumb and turns
the diamond with it: in the player's frame X sits on the right, A at the
bottom, B on the left, and Y at the top. Reversing each pair places the same
ordering under the left thumb - B nearest, then A, then Y, then X - so the
most-used action keeps the closest button and the least-used keeps the
farthest, mirroring the landscape arrangement instead of leaving it turned.

### The direction remap

Every direction remaps by the same physical rule in both tables: a control
keeps its device identity, and its action is the one whose on-screen
direction in the rotated frame matches the direction that press points to in
that frame.

| Device control | Player-frame direction | Portrait action |
| --- | --- | --- |
| D-pad up / Up arrow | right | move right |
| D-pad down / Down arrow | left | move left |
| D-pad right / Right arrow | down | soft drop, menu cursor down |
| D-pad left / Left arrow | up | menu cursor up |
| B / Z key | - | rotate clockwise, select |
| A / X key | - | rotate counter-clockwise, back |
| Y / C or left Shift | - | hard drop |
| X / Space | - | hold |
| Start / Enter or Esc | - | pause and resume |

Enter and Esc keep the menu roles they already carry: they are keyboard
controls of their own, not stand-ins for cluster buttons, so the rotation
does not move them. The keyboard columns of the controls screen and every
scripted session follow the same table, so a desktop run drives the rotated
frame exactly as the handheld does.

### The portrait composition

The portrait canvas keeps the rules of the approved landscape composition
(task T037): one board, quiet outer regions, no permanent gameplay legend,
and nothing clipped. The 40 px cell grows from the landscape 27 px, the
10x20 field measures 400x800 at (72, 120) with its one-pixel frame at
x=71..472 and y=119..920, and the HUD moves from two side columns into one
strip above the board: score, lines, and level as three label-over-value
fields across the top, Hold at the left below them, and the five Next
previews in one row at the right. The outer regions stay quiet and the
bottom 39 rows stay empty. Landscape changes none of this: the 960x544
layout, its hashes, and its timing are pinned by the harness and must stay
byte-identical.

### What stays outside this decision

The OPTIONS row that selects the orientation, the settings line that
persists it, the next-launch application rule, and the harness sessions that
prove both frames are implementation, not design; they follow this pin. The
physical confirmation - that the marked buttons on a real RG503 sit where
this rotation puts them, and that the grip reads well in a long session -
stays with the blocked device task.

## Game-over overlay nesting (T043, 2026-11-04)

**Decision: the game-over panel nests between the HUD columns, matching
the pause overlay geometry of T037.** The owner recorded the decision on
2026-11-04 and delegated the choice to review; the review chose nesting,
because the pause overlay had already established the pattern - one modal
panel inside the gap between the columns, the frozen statistics and
previews reading whole beside it under the veil - and the pre-existing
game-over panel (x=240..719) cut across both dimmed columns, leaving
fragments of the dimmed SCORE, LINES, and LEVEL labels peeking out at its
left edge (observed on the `t037-fix` packet's `gameover.png`, recorded
above in the T037 section).

`gameoverPanel()` now shares `pausePanel()`'s horizontal extent
(`include/stackfall/renderer_sdl.hpp`): the panel sits at x=320..639 with
its outline at x=319 and x=640, keeps its 440 px height for the three
final-statistics rows over its menu, and stays centered on the canvas
like the pause panel. The vertical offsets, the 56 px row margins, and
the portrait geometry of T055 - where no columns exist and the panel
already centers on the board, which owns the canvas alone - are
unchanged. `docs/SPEC.md` section 6.1 now states that both panels nest
between the columns.

Verification at this change (full commands and evidence in the T043
section of `notes/agent-testing.md`): `check_fixes.py` gained the
`gameover-panel-nests` criterion - no panel pixel in either HUD column,
the panel face inside the gap - and the fresh packet
`build/agent-checks/design-review/t043-fix/` passes it with 0 panel
pixels in each column and 134070 inside the gap, measuring the panel at
exactly x=320..639 and y=52..491 from pixels; the veiled statistics
labels `(53, 57, 70)` and the five veiled previews read whole beside the
panel. `ctest` passes in Debug and Release, and the seventeen-session
harness passes 931 of 931 checks. The `dr-gameover` row of
`packet.tsv` and `prior-findings.json` carry the decision, so later
review rounds judge the nested position as the accepted design.

## Open questions

- The game-over overlay nesting question above is decided and
  implemented by task T043 on 2026-11-04; see the section
  "Game-over overlay nesting".
- The hidden I-piece spawn rule stayed with T029 until it was decided and
  implemented there on 2026-11-03 (pieces spawn at visible row 0; see the
  T029 section of `notes/agent-testing.md`). Physical controller and
  audio judgments stay with T018 and T020, and the final human pass with
  the release owner.
