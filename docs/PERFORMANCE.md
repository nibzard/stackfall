# Performance optimization

T026 runs after the T025 measurements and before the T015 desktop gate.
The executing agent owns a bounded profile, change, and measurement loop.
Human and physical device tests remain deferred to final acceptance.

## Goals and constraints

Target smooth 60 frames per second with a fixed 60 Hz simulation. One frame
has about 16.67 milliseconds. Reduce unnecessary CPU use, frame stalls, and
memory growth. Preserve rules, input behavior, visuals, sound, and saves.
Preserve all deterministic regression hashes. Do not change expected hashes
or disable tests to accept an optimization.

Desktop results apply to the recorded host and renderer. The specification's
device memory target and RG503 performance still require final device checks.

## Runner

Use `claude -p` as the nested coding agent. Resolve the actual
executable and inspect its local help before use. Make the executable path
configurable. Pass arguments as an array; do not use `eval` to build commands.
Do not substitute another model or provider without an existing instruction.

The owner confirms that the existing `claude` configuration uses GLM 5.3.
Use that configuration without overriding the model or provider. The local
executable is available and documents `-p` for noninteractive output.

If the executable is unavailable, the executing agent performs the same loop
itself. Record that fallback. Do not block the performance task on an optional
runner. Do not install a provider or change credentials as part of this task.

Use the existing unattended tool permissions. A child process must not wait
for interactive approval. Record a denied tool or missing capability and
continue through the executing agent where possible.

## Optimization loop

1. Save the initial source identity and T025 results. Create a reproducible
   Release benchmark with fixed seeds, input sequences, warmup, and duration.
   Cover busy gameplay, line clears, previews, menus, and pause. Include
   accelerated and software rendering, with presentation blocking and without.
2. Run at least three baseline samples per selected scenario. Record host,
   renderer, build flags, elapsed time, frame counts, simulation ticks, CPU,
   memory, frame-time percentiles, and measured work per frame. Separate
   deliberate frame waits from work. Keep profiling overhead out of benchmark
   results. Use an available sampling profiler or bounded instrumentation.
3. Choose one measured bottleneck. Give the nested agent a prompt file with
   requirements, source identity, baseline evidence, profiler output, the
   hypothesis, allowed files, verification commands, and output paths.
   Request one small patch and a report. Prohibit recursive delegation,
   publication, commits, backlog edits, and unrelated changes.
4. Run the nested agent with `-p` and the prompt file as input, subject to
   verified local syntax. Capture output and exit status. Limit each call to
   ten minutes and terminate its child processes on timeout. Use an isolated
   working copy or worktree. Preserve existing work and collect the patch.
5. Inspect the patch and independently run Debug and Release tests, pinned
   hashes, and affected application scenarios. Repeat the matching Release
   benchmarks at least three times. Use identical settings and inputs.
   Reject incorrect changes, visual or input regressions, and improvements
   smaller than observed run variance. Keep only measured improvements that
   do not materially worsen other scenarios or frame-time tails.
6. Continue from the last accepted source. Stop after five candidate changes,
   two consecutive rejected changes, or 90 minutes of total wall time.
   Stop earlier when measured work fits the frame budget and profiling shows
   no actionable waste. Report any remaining bottleneck or unmet target.
7. Run the final regression gate and repeat affected T025 stability checks.
   Save a before-and-after table, commands, profiler evidence, accepted and
   rejected patches, runner details, and remaining limits in
   `notes/performance.md`. Save large artifacts under an ignored build folder.

A nested agent's report is a proposal, not proof of improvement. The executing
agent checks the patch and measurements. In fallback mode, apply the same
checks to its own changes. Never optimize through several unmeasured patches.

## Acceptance

- Provide reproducible measurements and source identities for both versions.
- Keep all rules, hashes, and affected application checks passing.
- Show repeated improvement beyond measurement noise for every retained patch.
  A documented no-change result is valid if profiling finds no actionable
  bottleneck and the desktop target is met.
- Reaching a time or iteration limit does not establish success. Report an
  unmet target as unresolved and keep the desktop gate from claiming a pass.

## Later rounds

Task T065, the post-v1.0 device-feedback round, reuses this method through
`tests/harness/t065.sh`, which adds the round's own requirements to the T026
set: scenarios that keep playing across restarts instead of parking at a
game over screen, keyboard and virtual-controller coverage, a paired save
mode that runs one workload with the mid-round record saves on and off, a
sustained case with memory and write sampling, one bounded CPU and
storage-pressure case, audio callback work and command traffic, and the
device capture procedure `tools/perf/device-capture.sh`. Its results and
commands live in `notes/performance.md`; `notes/agent-testing.md` carries
the run evidence.
- Keep display synchronization and the fallback limiter intact. High render
  counts caused by disabled pacing do not count as improved performance.
- Do not require a nested runner, human playtest, or physical device to run
  the desktop optimization task.
