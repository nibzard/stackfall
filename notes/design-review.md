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

| routed key | reviewer keys | priority | files |
| --- | --- | --- | --- |
| control-hint-glyph-scale | small-control-and-menu-hints, control-hints-too-small | 2 | src/renderer_sdl.cpp, include/stackfall/renderer_sdl.hpp |
| ghost-outline-contrast | ghost-outline-low-contrast | 3 | src/renderer_sdl.cpp, include/stackfall/renderer_sdl.hpp |
| letterbox-scene-centered | letterbox-scene-not-centered | 3 | src/main.cpp |

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
dependency graph stays acyclic. That wiring is reported as a follow-up
attached to T028 itself.

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

### What T028 does next

All three required findings now have fixes and passing pixel checks
(T030, T031, T032), so the second review round can start once Looper
planning wires T028 to the three fix tasks. Recapture a fresh packet and
run the bounded review round over the affected frames (`gameplay-early`,
`hold-locked`, `spawn-visible`, `tall-stack`, and the letterbox session):

```bash
DR_RUN_ID=verify tools/design-review/capture.sh
DR_ROUND=verify \
DR_IMAGES=gameplay-early,hold-locked,spawn-visible,tall-stack,letterbox-640x480 \
DR_PRIOR_FINDINGS=build/agent-checks/design-review/initial/packet/review-initial/open-findings.json \
tools/design-review/review.sh
python3 tools/design-review/check_fixes.py \
  --packet build/agent-checks/design-review/verify/packet
```

`open-findings.json` in the initial packet carries the three required
findings with their acceptance criteria, so the verification round can
judge each one against fresh frames. Capture rebuilds all twelve images
(the capture side is unbounded); `DR_IMAGES` keeps the review round
inside its image bound. `check_fixes.py` decides every criterion from
pixels alone. Optional cosmetic work and the deferred product questions
(T029, the final owner review) do not block closure.

### Open questions

- None new. The hidden I-piece spawn rule stays with T029, physical
  controller and audio judgments with T018 and T020, and the final
  human pass with the release owner.
