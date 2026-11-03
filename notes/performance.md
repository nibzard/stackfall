# T026 performance notes

Round M3. The bounded profile, change, and measurement loop of
`docs/PERFORMANCE.md`, run after the T025 stability baseline and before the
T015 desktop gate. Evidence lives under `build/agent-checks/t026-20261103/`
(ignored build output; the large artifacts stay there). All numbers are
desktop-host measurements; nothing here certifies RG503 performance, battery
use, or human perception, which stay deferred to final acceptance.

## Loop bounds and clock

- First loop window: started 2026-11-03T14:42:59Z (baseline sampling,
  profiling, and the candidate 1 nested call). Interrupted at about
  15:40Z with candidate 1's patch applied to the isolated copy but
  unverified; its baseline evidence is reusable because the source identity
  is unchanged (below).
- Resumed window: 2026-11-03T15:46Z, fresh 90-minute bound. The candidate
  loop stopped at 16:01Z on the early-stop rule: measured work fits the
  frame budget and no actionable waste remains (evidence under "Loop log").
  Not stopped by time or iteration bounds; 15 of 90 minutes were used. The
  final-state measurements and the regression gate ran afterward and sit
  outside the candidate loop bound.
- Bounds honored: two candidates of five, zero consecutive rejections, under
  90 minutes. The accepted source of record is baseline + candidate 1.

## Method

The benchmark driver `tests/harness/t026.sh` (with `tests/harness/
t026_gen.py` and `tests/harness/t026_summarize.py`) builds every measurement
from fixed seed 31 and scripted real-SDL sessions. Two modes per scenario:

- Work mode: `--tick-per-frame --frames 6000` with pacing off, so the wall
  clock measures poll + tick + render + present with no deliberate frame
  wait. CPU and peak resident memory come from `/usr/bin/time -v`.
- Paced mode: `--frames N --frame-times` at real-time pacing for the
  frame-time percentiles (the whole 16.67 ms slot, deliberate wait
  included): 2400 frames (40 s) for the baseline samples, the driver
  default 3600 (60 s) for the final section. This is where frame waits and
  work separate.

Scenarios (all deterministic; the verify phase proves each claim from the
run's own logs):

| Scenario | Claim, as verified |
| --- | --- |
| busy | reaches play and stays there; exactly one deterministic top-out at tick 3983; never paused |
| clears | labeled `four-line-ready` fixture cycles; lines grow 4 per cycle |
| menu | title and controls screens only |
| pause | at least three pause/resume cycles under the overlay |

Known limit of the set: the menu script's cursor walk selects QUIT on its
second title cycle, so every menu sample ends after 265 frames (identical in
every tag, so the comparison stays valid, but the menu row carries timer
granularity of about 38 us and is supporting evidence only).

Renderer modes: x11-opengl (Mesa llvmpipe under Xvfb; "accelerated" in SDL's
flags, software GL in fact), x11-software, dummy-software, plus vsync
requested versus stripped (`SDL_RENDER_VSYNC=0`) on opengl in the paced
mode, exactly the T025 comparison. In work mode the x11 rows sit at about
15.8 ms per frame: presentation under Xvfb blocks near the slot even with
pacing off (CPU 19-33%), so the x11 work rows are pacing-dominated; the
dummy-software rows are the work measurements and carry the decision.

Profiling: this host blocks `perf` (`kernel.perf_event_paranoid = 4`), so
the evidence uses callgrind (bounded instrumentation, instruction counts,
`--tick-per-frame`, software renderer under the dummy video driver), run
separately from every timed sample so instrumentation overhead never lands
in a benchmark number.

## Host and build

(Recorded by the host phase into `build/agent-checks/t026-20261103/host/`.)

- Date: 2026-11-03.
- Host: Linux 6.8.0-147-generic x86_64; 12th Gen Intel Core i9-12900HK, 8
  cores, 11.7 GiB RAM.
- SDL: compiled and linked 2.30.0.
- Build: Release (`-O3 -DNDEBUG`), Ninja; Debug for the rule suites.
- Renderer probes: opengl (llvmpipe under Xvfb), software, dummy fallback.
- Source identity at the baseline: commit 80dad75 plus the working tree
  (`host/source.sha256` records every hashed file). The accepted final state
  is that source plus the candidate 1 patch below; the candidate evidence
  directories carry each step's own `source` record and full patches.

## Runner

- Nested runner: `claude -p` (Claude Code CLI 2.1.280), resolved from PATH
  at `/home/agent/.local/bin/claude`, driven by
  `tests/harness/t026_candidate.sh` with the prompt on stdin, the
  transcript, standard error, exit status, and duration saved per call, and
  a hard bound that kills the whole child process group (`setsid`, group
  kill; 540 s for candidate 1, 420 s for candidate 2).
- Model and provider: the existing owner configuration, never overridden.
  The owner confirms that configuration uses GLM 5.3; the runner record
  notes the same and passes no model, provider, or credential flags.
- Isolation: the driver copies the sources into a candidate directory
  without `.git`, build output, or the Looper backlog files, so the nested
  agent cannot commit or touch the backlog at all; the whole tree diff
  becomes `candidate.patch`.
- Missing capability, on record: the nested runner's harness denies all
  file writes, builds, and tests for nested agents. Candidate 1's
  transcript records the probes; a fresh probe on 2026-11-03
  (`candidates/probe/`) reconfirmed it: the runner reads and reasons, but
  every Write is "permission not granted". Per docs/PERFORMANCE.md the loop
  continued through the executing agent: the nested runner still produced
  both candidate patches as text (its read-only analysis works), and the
  executing agent applied, built, tested, and measured every patch
  independently. No permission settings were changed; that is an operator
  decision.

## Baseline

Work per frame (dummy-software work mode, 3 samples per cell; microseconds;
spread is max-min of the three):

| Scenario | mean | min | max | spread | CPU | max RSS |
| --- | --- | --- | --- | --- | --- | --- |
| busy | 1065.6 | 1061.7 | 1071.7 | 10.0 | 99% | 12544 KiB |
| clears | 341.7 | 340.0 | 343.3 | 3.3 | 98% | 12544 KiB |
| menu | 339.6 | 339.6 | 339.6 | 0.0 | 66% | 12544 KiB |
| pause | 642.2 | 641.7 | 643.3 | 1.6 | 99% | 12544 KiB |

x11 work rows (pacing-dominated, CPU 19-33%): 15786-16264 across scenarios;
peak RSS 122-126 MiB under Xvfb/llvmpipe. Paced percentiles (baseline,
`bench/baseline/paced-stats.tsv`): every renderer mode holds 60.00 ticks/s;
median 16.50-16.73 ms, p95 17.24-17.52 ms, p99 17.57-17.69 ms,
max 17.74-19.15 ms; vsync on and off are statistically identical under
Xvfb (presentation never blocks; the elapsed-time fallback limiter holds
the 60 Hz simulation in every mode, as T025 found).

Callgrind baseline (busy, 400 frames): 1,121,596,101 instructions, 2.80 M
per frame. Caller attribution of the SDL fill path: 46.72% flows from
`drawHintsPanel` (444,800 fill entries) and 19.28% from `drawText`
(183,600) - about 1,570 per-pixel `SDL_RenderFillRect` calls per frame for
text alone; `drawPlayScene` 10.79%, previews 2.68%, score panel 2.28%,
cells 1.10%.

## Loop log

Candidate 1 - batch the glyph pixel rectangles of one text draw.
`drawText` collected its pixel rectangles and submitted them with one
`SDL_RenderFillRects` per call (35-glyph capacity, flush and continue)
instead of one `SDL_RenderFillRect` per font pixel. Proposed by the nested
runner as text; applied by the executing agent in the isolated copy
(`candidates/cand1/`). Verification: Debug and Release ctest 5/5 each
(pinned hashes included); four BMP screenshots byte-identical to the
baseline binary under the software renderer and, in an addendum run, under
opengl/llvmpipe with both binaries compared. Measurements (3 samples each,
`bench/cand1/`): busy 1065.6 -> 956.1 (-109.5), clears 341.7 -> 247.8
(-93.9), pause 642.2 -> 530.0 (-112.2), all 11x to 70x beyond the baseline
spread; menu 339.6 -> 251.6 (supporting, short run). Callgrind busy total
1.122 G -> 0.486 G instructions (-57%). RSS unchanged. **Accepted**; kept
in `src/renderer_sdl.cpp`.

Candidate 2 - paint the board well once, not twice. `drawBackground` filled
the well first and the four canvas strips around it, saving 115,200 painted
pixels per frame on paper. Proposed by the nested runner (99 s, exit 0);
applied and verified by the executing agent (`candidates/cand2/`): both
ctest suites green, BMPs byte-identical under both renderers. Measurements
against the last accepted state: busy 956.1 -> 978.9 (+22.8, every sample
worse), clears 247.8 -> 268.9 (+21.1), pause +4.4 - regressions beyond the
cand1 spreads. The five smaller fills pay more in per-call and per-row
setup than the one contiguous fill paid for the overdraw. **Rejected** and
reverted; the patch and both measurement sets stay in the evidence.
(Protocol lesson recorded there: visual-identity sessions need a fresh
save directory per run - a reused one loads the saved high score, flips
`newRecord()`, and changes the game-over frame.)

Stop decision after two candidates: the frame budget is met with room
(busy work 956 of 16,667 us, 5.7%) and the measured evidence shows no
remaining actionable waste - text pixel cost is the glyphs themselves
(changing them changes approved visuals), the background decomposition
measured worse than the status quo, and the remaining line-call batching
would only remove SDL's per-call dispatch of about 26 instructions
(`candidates/cand1` profile: 1.5 M dispatch cost over 58,520 line calls)
while the rasterization per segment stays identical - orders of magnitude
below run variance. Stopped per docs/PERFORMANCE.md step 6.

## Final state

Source of record: commit 80dad75 + candidate 1 (`git diff` of
`src/renderer_sdl.cpp`: 23 insertions, 3 deletions).

Final work-mode rows (`bench/final/`, dummy-software, 3 samples each):

| Scenario | baseline mean | final mean | change | final spread |
| --- | --- | --- | --- | --- |
| busy | 1065.6 | 952.2 | -113.4 | 6.7 |
| clears | 341.7 | 247.8 | -93.9 | 1.6 |
| menu | 339.6 | 251.6 | -88.0 | 37.8 (timer noise) |
| pause | 642.2 | 523.3 | -118.9 | 0.0 |

CPU and memory, before and after: work-mode CPU stays 98-99% of one core
(work mode has no waits; the same job simply takes 9-28% less time, which
is the work-per-frame column). Paced CPU: x11-opengl 23% -> 23%, x11
software 26% -> 25%, dummy 3% -> 3% (integer percent; the per-frame work
dropped 9-28% in the same runs). Peak RSS unchanged: 12.1-12.7 MiB dummy,
120.9-125.7 MiB under Xvfb/llvmpipe, before and after.

Final paced percentiles (60 s runs; baseline rows were 40 s runs, the
per-frame statistics compare directly):

| Mode | baseline median/p95/p99/max (ms) | final median/p95/p99/max (ms) |
| --- | --- | --- |
| x11-opengl vsync | 16.65-16.67 / 17.51-17.52 / 17.65-17.68 / 17.74-17.83 | 16.65-16.67 / 17.50-17.51 / 17.64-17.66 / 17.74-17.80 |
| x11-opengl no vsync | 16.67-16.68 / 17.50-17.51 / 17.67-17.69 / 17.75-17.82 | 16.66 / 17.49-17.52 / 17.64-17.65 / 17.75-17.76 |
| x11-software vsync | 16.50-16.51 / 17.38-17.44 / 17.57-17.59 / 17.96-19.15 | 16.55-16.57 / 17.39-17.42 / 17.56-17.59 / 17.71-17.76 |
| dummy-software vsync | 16.72-16.73 / 17.24-17.28 / 17.67-17.68 / 17.82-17.84 | 16.73-16.75 / 17.22-17.29 / 17.67 / 17.83-18.39 |

Every mode holds 60.00 ticks/s. The tails do not regress (the x11-software
max actually tightens; one dummy sample shows an 18.39 ms max, inside the
baseline set's own 17.82-19.15 max range). The frame budget is met with
headroom: the busiest measured scenario costs 952 us of work per frame,
5.7% of the 16,667 us slot.

## Final regression gate

- ctest Debug: 5/5 tests passed (includes the pinned regression hashes).
- ctest Release: 5/5 tests passed.
- `tests/harness/run.sh` (full unattended SDL scenario suite: scene cycle
  keyboard and virtual controller, normal planned game, block-out,
  lock-out, four-line fixtures; `build/agent-checks/t026-final-regress/`):
  163 checks passed, 0 failed.
- Affected stability check: the paced percentile section above reruns the
  T025 method (frame-time record, vsync requested and stripped, all
  renderer modes, fallback limiter active); 60.00 Hz simulation holds in
  every mode and the percentiles match the T025 findings for this host.

## Commands

Every number above reproduces from the repository root (Release binary at
`build-rel/stackfall`, built by `cmake -S . -B build-rel -G Ninja
-DCMAKE_BUILD_TYPE=Release && cmake --build build-rel`):

1. Host record, scenario verification, baseline samples, profile, and the
   decision table:
   `tests/harness/t026.sh` (defaults: run id `t026-<date>`, tag `baseline`,
   seed 31, three samples, all scenarios and renderers).
2. Candidate samples after a patch, work mode only, same inputs:
   `T026_RUN_ID=t026-20261103 T026_TAG=candN T026_PHASES=bench
   T026_RENDERERS=dummy-software T026_SKIP_PACED=1 tests/harness/t026.sh`.
3. Final state with the full paced section:
   `T026_RUN_ID=t026-20261103 T026_TAG=final T026_PHASES=bench
   T026_RENDERERS=dummy-software tests/harness/t026.sh`.
4. Re-profile an accepted state: `T026_RUN_ID=t026-20261103
   T026_TAG=<tag> T026_PHASES=profile tests/harness/t026.sh`.
5. Decision table only: `python3 tests/harness/t026_summarize.py --run-dir
   build/agent-checks/t026-20261103`.
6. Nested candidate call (bounded, isolated, transcript and exit status
   saved): `tests/harness/t026_candidate.sh
   build/agent-checks/t026-20261103/candidates/candN <prompt-file>`.
7. Visual identity, per renderer, each with a FRESH save directory:
   `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build-rel/stackfall
   --seed 31 --tick-per-frame --frames 900 --save-dir /tmp/<fresh>
   --input-script <identity.script.txt> --screenshot-dir /tmp/<fresh-shots>`
   (under Xvfb for the opengl path), then compare the four BMP sha256
   values against `candidates/cand1/baseline-shots.sha256`.
8. Final gate: `ctest --test-dir build && ctest --test-dir build-rel &&
   HARNESS_RUN_ID=t026-final-regress tests/harness/run.sh`.

## Unresolved limits

- Host scope: these numbers are one desktop host (X11 under Xvfb and the
  dummy driver). They do not establish RG503 performance, battery use, or
  the real blocking-vsync frame-time distribution; that stays with the
  deferred device measurement (task T036) and final acceptance.
- The nested runner cannot write, build, or test in this environment
  (recorded per call). Its patches arrive as text and the executing agent
  applies and verifies them; unattended patch application would need an
  operator decision on the nested permission layer.
- Remaining measured costs are the approved visuals themselves (glyph
  pixel fills, grid lines, preview cells, the single full-canvas
  background fill, which measured faster than the decomposed alternative)
  plus SDL per-segment line rasterization; no further reduction exists
  inside the approved rules that measurement supports.
- The menu benchmark row is short (265 frames, scripted quit) and carries
  timer granularity; it is supporting evidence only, as noted in Method.
