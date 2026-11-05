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


## Music callback cost, desktop (T062, 2026-11-04)

MUSIC.md's callback budget checked on the desktop build through the real
mixer path: `stackfall_audio_tests` times `AudioDriver::mixFrames` at the
dummy device's own buffer size, with the densest catalogue track
(arcade-redline, 150 BPM) at high intensity and all five cue voices
retriggered every fifth callback.

- Obtained device: 44100 Hz, 2 channels, 512-frame callbacks (11.61 ms).
- 2000 measured callbacks after 50 warm-up callbacks: p50 0.047 ms,
  p99 0.084 ms, about 1 percent of the buffer duration.
- Budget: MUSIC.md proposes p99 below 25 percent of the buffer on the
  target device; the desktop headroom is two orders of magnitude, and the
  same measurement prints from every `ctest` run of the audio suite.
- The 25 percent budget, the 30-minute dropout watch, and the RG503's
  obtained rate and buffer size stay device measurements (task T036
  follow-up); this row is the desktop evidence that the engine cannot
  plausibly overrun them.

Command:

```bash
cmake --build build --target stackfall_audio_tests &&
./build/stackfall_audio_tests 2>&1 | grep "music callback"
```


## T065: the integrated build profiled and the stutter cause fixed (2026-11-05)

Round post-v1.0, the device-feedback wave. The owner reported the game
"sometimes stutters after initially smooth play" on the RG503 and named CPU
load, memory use, and disk writes as hypotheses, not causes. This section
records what the integrated Release build (the T052 mid-round record saves,
the T055 portrait layout, and the T062 soundtrack included) measured under
deterministic, repeated, real SDL input, which of the stated hypotheses the
measurements confirmed, and the fix that followed. Evidence:
`build/agent-checks/t065-20261105/` (ignored build output; `host-baseline/`
holds the measured baseline's source identity, `host-fix1/` the fixed
build's). Every number below is a desktop-host measurement; nothing here
certifies RG503 pacing, battery use, or what a player feels.

### Method

The driver `tests/harness/t065.sh` (with `t065_gen.py`, `t065_verify.py`,
`t065_scenes.py`, `t065_monitor.py`, `t065_pressure.py`,
`t065_pressure_row.py`, `t065_sustained.py`, `t065_summarize.py`) builds on
the T026 method and adds this round's own requirements:

- Scenarios (fixed seed 31, all deterministic, all through the real SDL
  event path): `rotate-move` (rotations both ways, taps, DAS holds, wall
  rotations), `drops` (held soft drop and hard drop), `clears` (labeled
  four-line fixture cycles), `pause` (pause and resume over live play), and
  `pad-mixed` (the same coverage on the developer virtual controller). A
  `restart` command begins a fresh round on a per-scenario cadence, and the
  `verify` phase measures the game-over share of every scenario from its
  own scene transitions and fails any sample that sat mostly at a game over
  screen: all five held game over at 0.0% of simulated ticks while playing
  64-100% of them.
- Save modes, the pair that isolates the write cost inside one workload:
  `fresh` starts the save directory empty, so the T052 mid-round record
  saves fire the moment the round scores; `banked` preloads a record no
  scripted round can reach, so the same session runs without one save.
  Everything else is identical.
- Work mode (`--tick-per-frame`, pacing off) measures poll + tick + render
  + present with no deliberate wait; paced mode (`--frame-times`, real
  time) measures the whole 16.67 ms slot. Three samples per scenario,
  renderer, and save mode in both.
- Renderer modes: `x11-opengl` (Mesa llvmpipe under Xvfb), `x11-software`,
  and `dummy-software`. As T026 found, the x11 rows present at about the
  frame slot even with pacing off, so they are supporting evidence; the
  dummy rows carry the work decision. The x11 work rows run the same
  scripts truncated to 1200 frames to keep the phase bounded.
- Memory, write counts, and write bytes come from `/proc/<pid>/status` and
  `/proc/<pid>/io`, sampled four times a second by `t065_monitor.py` from
  outside the process, so no timed sample carries instrumentation of its
  own. Audio callback work and command traffic come from two relaxed
  counters per side (`Audio: N callbacks, M frames` and
  `Music commands: P pushed, R rejected, D drained` in every session log);
  the callback clock pair is opt-in through
  `STACKFALL_PROFILE_AUDIO_CALLBACK` and runs only in its own session.
  Callgrind runs in the `profile` phase, never inside a timed sample.
- One sustained case (180 s paced, memory and writes sampled over time),
  one bounded pressure case (four kinds - idle, CPU, storage, both - on two
  renderers; the worker caps itself in time, bytes, and cores, writes only
  inside the evidence directory, and is registered in the Looper cleanup
  file before it starts), and the portrait layout row of the hypotheses
  phase.

Host: the recorded host of the T026 loop (Linux 6.8.0-147 x86_64, 12th Gen
Intel i9-12900HK, 8 cores, ext4 on `/dev/sda1`, SDL 2.30.0, Release
`-O3 -DNDEBUG`). Baseline source identity: commit `d7d2783` plus the
working tree whose hashes `host-baseline/source.sha256` records - that tree
is HEAD plus this round's measurement instrumentation (the two audio
counters and the callback clock), nothing else. The fixed build is the same
tree plus the record writer (`host-fix1/source.sha256`).

Commands (repository root, Release binary at `build-rel/stackfall`):

1. The whole round: `T065_RUN_ID=t065-20261105 tests/harness/t065.sh`
   (defaults: seed 31, three samples, five scenarios, three work renderers,
   60 s paced samples, 180 s sustained, 60 s pressured).
2. The fix samples on the same scripts and seed:
   `T065_RUN_ID=t065-20261105 T065_TAG=fix1 T065_PHASES="verify bench
   sustained pressure hypotheses profile summarize"
   T065_RENDERERS=dummy-software tests/harness/t065.sh`, then the x11 spot
   check: `T065_RUN_ID=t065-20261105 T065_TAG=fix1 T065_PHASES=bench
   T065_SCENARIOS=drops T065_RENDERERS=x11-opengl tests/harness/t065.sh`.
3. Tables from the evidence directory alone:
   `python3 tests/harness/t065_summarize.py --run-dir
   build/agent-checks/t065-20261105`.
4. The durable-save cost probe: `./build-rel/stackfall_save_probe DIR 300`.

### Hypothesis 1: the mid-round record save. Confirmed, and fixed

Task T052 persists the record the moment a running score passes the stored
best (SPEC sections 4.7 and 9). The save runs the store's atomic write:
temporary file, write, flush, **fsync**, close, rename. Three measurements
agree on what that costs and when the game pays it:

1. The probe (`stackfall_save_probe`, the store's own `save()` in a loop on
   this host's ext4): p50 2.59 ms, p95 3.24 ms, p99 4.77 ms, max 6.20 ms.
   The same write without the fsync costs p50 43 us - **98% of the save is
   the fsync**, and the fsync is what the power-loss guarantee is made of,
   so it cannot simply be dropped.
2. Inside the workload (work mode, dummy-software, three samples each, the
   fresh-minus-banked pair): the cost appears exactly where saves fire and
   in proportion to how many fire. `clears` saved 390 times per sample and
   paid +207 us per frame (+38%); `drops` saved 127 times and paid +111 us
   (+27%); `pad-mixed` saved 73 times and paid +41 us (+10%); `rotate-move`
   never scored, never saved, and paid 0.0 us - the control that rules out
   "fresh vs banked differ for another reason".
3. When it fires: from the moment a round's score first passes the stored
   record, **every further point that round scores pays the same write on
   the gameplay thread** - a held soft drop awards one point per cell, so a
   soft-dropping player who is past their record can reach one fsync per
   tick. That is precisely "initially smooth play, then stutters": the
   smooth part is the round below the stored record, where no save fires.

Per save, the in-workload cost is 3.2-5.3 ms of frame time (the pair's
delta times the frames over the save count) - 19-32% of the 16.67 ms slot
on this host, per scoring tick. The paced percentiles on this host do not
show it, because the dummy renderer's per-frame work is under 1 ms and the
deliberate wait absorbs the save; that is a property of this host's 16 ms
of slack, not of the cause. On the handheld the same events land in a frame
with several times the work (its CPU is an order of magnitude slower than
this host's) and an fsync that plausibly costs more on eMMC or a card than
the 2.6 ms measured here on an SSD. No handheld pacing is inferred from
these numbers; the tail measurement stays with T036.

**The fix** moves the write off the gameplay thread and changes nothing
else about it: `include/stackfall/record_writer.hpp` and
`src/record_writer.cpp` own a writer thread that performs the store's
identical atomic write. The gameplay thread calls `submit(score, tick)`,
which moves two integers under a lock the writer never holds across a
write, and polls the completed writes once per frame for the session-log
and input-log lines, whose formats are unchanged. Because the file holds
one number and every later number contains every earlier one, a value that
arrives while a write is still running replaces the next one: the file
never lags the round by more than one write, and the writer cannot fall
behind however fast the score rises. Round boundaries - game over, Quit to
Title, restart - and clean shutdown flush, so the record is complete on
disk before the title screen, the game-over screen, or the next round reads
it back; a power loss costs at most the write in flight, which is task
T052's own bound ("the points of the piece in play") kept.

Regression tests: `tests/test_record_writer.cpp` (in `stackfall_tests`,
so it runs headless on every machine) pins the three properties the fix is
made of - `submit()` returns in a small fraction of an 80 ms injected write,
the flushed write lands in the store's own file, a session without a save
directory stays inert, rapid scores coalesce to the newest value in order,
a failed write reports its status, the per-frame poll empties the queue,
and sustained submission stays current. The suite also ran clean under
ThreadSanitizer. The T052 behavior itself is pinned by the existing harness
sessions, which the fix passes unchanged (`record-abandon-save` still
requires `record save=36 tick=6 status=ok` and `record save=68 tick=14
status=ok`, both ticks before the abandon at tick 43, and the file holding
68 after the round was abandoned).

Measured before and after (work mode, dummy-software, three samples per
cell, same scripts and seed):

| Scenario, save mode `fresh` | baseline us/frame | fixed us/frame | fixed `banked` us/frame | baseline saves | fixed saves |
| --- | --- | --- | --- | --- | --- |
| clears | 751.6 | 544.2 | 544.2 | 390 | 119 |
| drops | 528.7 | 417.6 | 417.6 | 127 | 33 |
| pad-mixed | 454.3 | 413.5 | 413.5 | 73 | 18 |
| pause | 792.8 | 751.4 | 751.4 | 59 | 10 |
| rotate-move (never scores) | 294.6 | 294.6 | 294.6 | 0 | 0 |

Every fixed `fresh` row sits exactly on its `banked` control: the write no
longer occupies the gameplay thread, and the +10 to +38% the saves cost are
gone. The x11 spot check agrees where it can see anything at all: the
`drops` pair under `x11-opengl` runs at the presentation floor (16,083 us
per frame, the value every presentation-bound x11 row of both states
holds), with the baseline's one elevated row (`fresh` at 16,294 us, its
only x11 row above the floor) gone. The paced percentiles hold at the same time (baseline `fresh` median
16.75-16.85 ms, p99 17.68-17.69; fixed `fresh` median 16.65-16.82 ms, p99
17.44-17.71), every mode stays at 60.00 ticks/s, and across all 30 paced
samples of each state (108,000 frames) the count of frames over 20 ms is 0
in the baseline and 1 in the fixed set - one scheduler outlier, not a
regression. The sustained session repeats the same result at 180 s: median
16.661 ms against the baseline's 16.694 ms, p99 17.693 against 17.694, the
same 63 record saves, the same 228 log lines, 1.37 against 1.40 MiB
written, and resident memory flat (13,460 to 13,480 KiB; the writer's own
pages are the 20 KiB).

In fast-forward work mode the writer also coalesces - 127 score rises can
land while one 2.6 ms write runs, which is why the fixed samples issue
fewer writes (33 against 127) - while the round still ends with its final
score durable, and the paced rows show the same save count as the baseline
(127 for `drops`, 390 for `clears`), because real-time play raises the
score at most once per tick.

### Hypothesis 2: music synthesis and callback cost. Not a cause

- The audio suite's own budget print, in this run:
  `p50 0.009 ms, p99 0.016 ms` against an 11.61 ms buffer (0% of it); the
  T062 note measured the densest catalogue track at p50 0.047 ms, p99
  0.084 ms.
- The instrumented run (`STACKFALL_PROFILE_AUDIO_CALLBACK=1`, its own
  session): 5367 callbacks, mean 0.017 ms, max 0.214 ms - 1.8% of one
  buffer at the worst single callback.
- Command traffic: the director pushes one `SetIntensity` command per
  simulation tick while playing - 10,343 pushes over the 180 s sustained
  session, 57.5/s, with **zero rejections** (the 16-slot queue never
  overflowed in real time; the rejections that appear in fast-forward runs
  are the producer outrunning the audio thread's real-time clock, a test
  artifact). Sixty relaxed atomic stores a second on the queue's tail cell
  is orders of magnitude below the frame budget, and the paced soundtrack
  pair (off against a playing pool, three samples each) shows no difference
  beyond run variance: off at median 16.69-16.73 ms / p99 17.67-17.69 ms against a playing arcade pool at median 16.68-16.73 ms / p99 17.66-17.69 ms, three samples each in both states - no difference beyond run variance.

Nothing here is actionable at the frame budget; the traffic is recorded,
not optimized.

### Hypothesis 3: pacing. Holds, and the fallback limiter is what holds it

Every paced sample of both states holds 60.00 ticks/s, with median frame
times of 16.65-16.86 ms and p99 at 17.4-17.7 ms. Under the dummy video
driver, presentation returns early, so the elapsed-time fallback limiter
carries the 60 Hz simulation; each such session logs its single engage line
(`Fallback frame limiter engaged: presentation returned early; pacing at
60 Hz`). Rendering versus waiting, per the work/paced pair: the busiest
scenario's work is under 1 ms of the 16.67 ms slot on this host (5% at
worst), so the slot is mostly deliberate wait here. No frame stall above
the 50 ms warning threshold was logged in any sample of either state.

### Hypothesis 4: resource growth. None measured

The sustained case (180 s, 10,800 frames, nine round restarts, 63 record
saves, 228 session-log lines): resident memory 13,372 KiB at the first
sample after bring-up, 13,372 KiB at the last, peak 13,372 KiB - **0 KiB of
growth over the session**. Writes stayed bounded (586 write syscalls,
1.34 MiB) and the audio callback count ran at the device rate for the whole
session. There is no leak to fix.

### Hypothesis 5: diagnostic logging. Not a cause at the rates a session produces

A session logs its startup block, scene transitions, save outcomes, and
stalls, and each line flushes to `log.txt`: 228 lines in the 180 s
sustained session and 207 in a 3,000-frame logging pair sample. The logging
pair (the same workload writing `log.txt` against writing `/dev/null`)
measured median 16.685 ms and p99 17.676 ms with `log.txt` on disk against median 16.703 ms and p99 17.661 ms with the log on `/dev/null` (93 lines written, 60 s each) - identical within run variance, and the write counters move only by the log's own bytes (328 against 304 KiB for the whole session). At one to two lines per second the per-line flush is
microseconds and does not reach the frame budget; the lines worth watching
are the save outcomes, whose cost the record writer now carries.

### Pressure case (bounded)

`T065_PRESSURE_SECONDS=60` on the `drops` scenario, four kinds on two
renderers, with the durable-save probe measured inside the same window
(percentiles over the frames after the first second, so Xvfb bring-up
lands in no kind's numbers):

| kind / renderer | median ms | p95 ms | p99 ms | max ms | save p50 us |
| --- | --- | --- | --- | --- | --- |
| idle / dummy | 16.73 | 17.30 | 17.67 | 22.90 | 2454 |
| cpu / dummy | 16.67 | 17.41 | 17.67 | 23.93 | 2613 |
| storage / dummy | 16.71 | 17.36 | 17.69 | 17.87 | 2469 |
| both / dummy | 16.69 | 17.37 | 17.65 | 18.31 | 2627 |
| idle / x11-opengl | 16.55 | 17.52 | 17.65 | 17.83 | 2321 |
| cpu / x11-opengl | 16.70 | 17.38 | 17.65 | 19.45 | 2518 |
| storage / x11-opengl | 16.56 | 17.51 | 17.64 | 17.85 | 2418 |
| both / x11-opengl | 16.71 | 17.41 | 17.66 | 23.13 | 2649 |

On this host the bounded pressure changes the tails little (eight cores,
an SSD with headroom): the save probe stays at 2.3-2.6 ms median and the
percentiles stay inside the idle spread. The case is recorded as the
procedure the device round reuses, and as the bound that the pressure
worker never exceeded (256 MiB written, one directory inside the evidence
tree, cleaned up and registered in the Looper cleanup file).

### Portrait layout

The portrait row of the hypotheses phase (the `drops` scenario paced with
the stored orientation set to portrait, three samples): median 16.64-16.65 ms, p95 17.56-17.59 ms, p99 17.67-17.71 ms, max 17.86-17.95 ms - the same band as the landscape `drops` row (median 16.72-16.73 ms, p99 17.69 ms)
The rotated frame draws 40 px cells and a different HUD strip, so its work
differs from the landscape row; on this host both hold 60.00 ticks/s.

### Regression gate at the fixed source

- `ctest` Debug: 5/5 suites passed (including the pinned replay hashes and
  the new record-writer suite).
- `ctest` Release: 5/5 suites passed.
- The unattended SDL harness `tests/harness/run.sh` (19 sessions through
  the real SDL application, each replayed against the headless reference
  and compared field by field; the `record-abandon-save` session of T052,
  the staged `next-count` relaunches that carry a mid-round record forward,
  and the portrait sessions of T055 among them): 931 checks passed, 0
  failed, on the Debug build (`build/agent-checks/t065-regress-debug/`).
- The same harness on the Release build: 931 checks passed, 0 failed
  (`build/agent-checks/t065-regress-release/`).
- The T025 hard-kill phase (SIGKILL inside save writes through a 120 ms
  fsync delay shim, 24 kills): every resulting record file was canonical -
  24 canonical, 0 damaged, 0 absent - and the repair-and-load run on the
  killed directory exited 0 and replaced the record
  (`build/agent-checks/t065-hardkill/`). The writer thread never leaves a
  partial file behind, because the store's atomic write is unchanged.

### Device capture procedure, and what stays open

`tools/perf/device-capture.sh` is the repeatable procedure for the RG503:
it runs the same scenarios at real-time pacing with the frame-time record,
samples `/proc` from outside the process with the same monitor, enables the
audio callback clock for its own session, and runs the durable-save probe
on the device's own storage. Everything lands in one directory with its
inputs recorded. Copy to the device: the game binary, the same-architecture
`stackfall_save_probe`, `t065_monitor.py`, and the scenario scripts
(`tests/harness/t065_gen.py <scenario> 6000` generates them). Actual device
tail measurements stay open with T036: no RG503 was available to this round,
and this report's desktop numbers deliberately infer nothing about the
handheld's blocking vsync, its storage latency, or its battery.
