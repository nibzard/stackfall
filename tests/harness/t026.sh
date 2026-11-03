#!/bin/bash
# T026 performance benchmark and profile driver (docs/PERFORMANCE.md).
#
#   tests/harness/t026.sh
#
# One command builds the reproducible Release measurements the T026 loop
# needs, from fixed seeds and scripted real-SDL sessions:
#
#   host       host, toolchain, renderer, and source identity on record
#   verify     one bounded run per scenario proving what the script measures
#              (busy stays in play; clears clear lines and top out; menu
#              stays on the title; pause cycles the overlay)
#   bench      the samples: work mode (--tick-per-frame, pacing off, so the
#              measurement is poll + tick + render + present with no
#              deliberate frame wait) and paced mode (--frame-times, the
#              whole slot including the deliberate wait, for percentiles)
#   profile    bounded instrumentation runs under callgrind (this host
#              blocks perf: kernel.perf_event_paranoid=4), kept separate
#              from the timed samples so profiling overhead never lands in
#              a benchmark number
#   summarize  every sample of the current evidence set into one table with
#              per-group spread, the beyond-run-variance decision metric
#
# Samples land in build/agent-checks/<run-id>/bench/<tag>/, the profile in
# <run-id>/profile/. Rerun after a candidate patch with T026_TAG=cand1 (or
# final) and the same seed, frames, and scenarios: the numbers stay
# comparable because the sessions replay exactly.
#
# Options through environment variables:
#   T026_RUN_ID       evidence directory (default: t026-<date>; reused names
#                     are kept, so one run id carries the whole loop)
#   T026_TAG          sample label, baseline / candN / final (default:
#                     baseline; a reused tag's samples are replaced)
#   T026_BUILD_DIR    Release build (default: build-rel)
#   T026_PHASES       space-separated phase list (default: all)
#   T026_SAMPLES      samples per scenario and renderer (default: 3)
#   T026_WORK_FRAMES  frames per work-mode sample (default: 6000, one tick
#                     each, so 100 s of simulation without waits)
#   T026_PACED_FRAMES frames per paced sample (default: 3600, 60 s of real
#                     time including the deliberate wait)
#   T026_SEED         the fixed round seed (default: 31, the T025 seed, so
#                     the pacing numbers compare with that baseline)
#   T026_SCENARIOS    space-separated scenario list (default: all four)
#   T026_RENDERERS    work-mode renderer list (default: x11-opengl
#                     x11-software dummy-software)
#   T026_SKIP_X11     set to 1 when xvfb-run is missing; the x11 modes are
#                     then recorded unavailable, never silently dropped
#   T026_SKIP_PACED   set to 1 to skip the paced percentile section (the
#                     candidate decision uses work mode; the paced section
#                     reruns in full for the baseline and the final state)
#   T026_PROFILE_FRAMES  frames per callgrind run (default: 400)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T026_BUILD_DIR:-$repo/build-rel}
run_id=${T026_RUN_ID:-t026-$(date +%Y-%m-%d-%H%M%S)}
tag=${T026_TAG:-baseline}
phases=${T026_PHASES:-"host verify bench profile summarize"}
samples=${T026_SAMPLES:-3}
work_frames=${T026_WORK_FRAMES:-6000}
paced_frames=${T026_PACED_FRAMES:-3600}
seed=${T026_SEED:-31}
scenarios=${T026_SCENARIOS:-"busy clears menu pause"}
renderers=${T026_RENDERERS:-"x11-opengl x11-software dummy-software"}
profile_frames=${T026_PROFILE_FRAMES:-400}
run_dir="$repo/build/agent-checks/$run_id"
gen="$repo/tests/harness/t026_gen.py"
frame_stats="$repo/tests/harness/frame_stats.py"
summarizer="$repo/tests/harness/t026_summarize.py"
game="$build_dir/stackfall"

fail_count=0
fail() {
  echo "t026: $*" >&2
  exit 1
}

case "$run_id" in
  */*|""|.|..) fail "T026_RUN_ID must name one directory, not '$run_id'" ;;
esac
[ -x "$game" ] || fail "no Release game binary at $game; build it first"
mkdir -p "$run_dir" || fail "cannot create $run_dir"

want_phase() {
  case " $phases " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

status() {
  echo "t026: phase $1: $2${3:+ ($3)}"
  if [ "$2" != passed ]; then
    fail_count=$((fail_count + 1))
    echo "$1: $2${3:+ ($3)}" >> "$run_dir/failures.txt"
  fi
}

# work_run DIR VIDEO DRIVER SCRIPT: one work-mode sample into DIR. VIDEO is
# x11 or dummy; DRIVER may be empty for the default chain. The run is paced
# off (--tick-per-frame), so the wall clock is work alone. Observations and
# the fixture directory stay on: the clears scenario loads its labeled
# fixture and every cycle records its cleared-line counter, so each sample
# carries proof of what it measured.
work_run() {
  local dir=$1 video=$2 driver=$3 script=$4
  mkdir -p "$dir/save"
  if [ "$video" = x11 ]; then
    xvfb-run -a -s "-screen 0 960x544x24" \
      timeout --foreground 300 \
      env SDL_AUDIODRIVER=dummy ${driver:+SDL_RENDER_DRIVER=$driver} \
      /usr/bin/time -v -o "$dir/time.txt" \
      "$game" --seed "$seed" --tick-per-frame --frames "$work_frames" \
      --save-dir "$dir/save" --input-script "$script" \
      --fixture-dir "$repo/tests/fixtures" \
      --observations "$dir/observations.txt" \
      > "$dir/run.log" 2>&1
  else
    timeout --foreground 300 \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      ${driver:+SDL_RENDER_DRIVER=$driver} \
      /usr/bin/time -v -o "$dir/time.txt" \
      "$game" --seed "$seed" --tick-per-frame --frames "$work_frames" \
      --save-dir "$dir/save" --input-script "$script" \
      --fixture-dir "$repo/tests/fixtures" \
      --observations "$dir/observations.txt" \
      > "$dir/run.log" 2>&1
  fi
  local code=$?
  echo "$code" > "$dir/exit"
  return $code
}

# paced_run DIR VIDEO DRIVER SCRIPT VSYNC_EXTRA: one paced sample into DIR.
# The frame-time record carries the whole slot, deliberate wait included.
paced_run() {
  local dir=$1 video=$2 driver=$3 script=$4 extra=${5:-}
  mkdir -p "$dir/save"
  if [ "$video" = x11 ]; then
    xvfb-run -a -s "-screen 0 960x544x24" \
      timeout --foreground 300 \
      env SDL_AUDIODRIVER=dummy ${driver:+SDL_RENDER_DRIVER=$driver} \
      ${extra:+$extra} \
      /usr/bin/time -v -o "$dir/time.txt" \
      "$game" --seed "$seed" --frames "$paced_frames" \
      --save-dir "$dir/save" --input-script "$script" \
      --fixture-dir "$repo/tests/fixtures" \
      --observations "$dir/observations.txt" \
      --frame-times "$dir/frame-times.txt" > "$dir/run.log" 2>&1
  else
    timeout --foreground 300 \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      ${driver:+SDL_RENDER_DRIVER=$driver} ${extra:+$extra} \
      /usr/bin/time -v -o "$dir/time.txt" \
      "$game" --seed "$seed" --frames "$paced_frames" \
      --save-dir "$dir/save" --input-script "$script" \
      --fixture-dir "$repo/tests/fixtures" \
      --observations "$dir/observations.txt" \
      --frame-times "$dir/frame-times.txt" > "$dir/run.log" 2>&1
  fi
  local code=$?
  echo "$code" > "$dir/exit"
  return $code
}

# The renderer a run actually used, from the game's own log line.
renderer_of() {
  sed -n 's/^Renderer: //p' "$1/run.log" | head -1
}

# Elapsed seconds of a /usr/bin/time -v record ("0:12.34" or "1:02:03.45").
elapsed_of() {
  sed -n 's/^.*Elapsed (wall clock) time (h:mm:ss or m:ss): //p' \
    "$1/time.txt"
}

# The frames or ticks of a run's "Loop finished" line.
loop_stat() {  # loop_stat DIR frames|ticks
  if [ "$2" = frames ]; then
    sed -n 's/.*Loop finished: \([0-9]*\) frames rendered.*/\1/p' \
      "$1/run.log"
  else
    sed -n 's/.*frames rendered, \([0-9]*\) ticks simulated.*/\1/p' \
      "$1/run.log"
  fi
}

cpu_of() {
  awk -F': *' '/Percent of CPU/{print $2}' "$1/time.txt" | tr -d '%'
}

rss_of() {
  awk '/Maximum resident set size/{print $NF}' "$1/time.txt"
}

# ---------------------------------------------------------------------------
if want_phase host; then
  echo
  echo "=== phase host ==="
  h="$run_dir/host"
  mkdir -p "$h"
  {
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "host: $(uname -srm)"
    echo "cpu: $(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -1)"
    echo "cores: $(nproc)"
    echo "memory_kib: $(awk '/MemTotal/{print $2}' /proc/meminfo)"
    echo "game binary: $game"
    echo "build type: $(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' \
      "$build_dir/CMakeCache.txt")"
    echo "build flags (CXX_FLAGS_RELEASE): $(sed -n \
      's/^CMAKE_CXX_FLAGS_RELEASE:STRING=//p' "$build_dir/CMakeCache.txt")"
    echo "compiler: $($build_dir/compile_commands.json >/dev/null 2>&1; \
      sed -n 's/.*"\(.*g++[^ ]*\).*/\1/p' \
      "$build_dir/compile_commands.json" 2>/dev/null | head -1)"
    echo "xvfb-run: $(command -v xvfb-run || echo missing)"
    echo "valgrind: $(valgrind --version 2>/dev/null || echo missing)"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid \
      2>/dev/null || echo unknown)"
  } > "$h/host.txt"
  # Source identity: the commit when a repository is present, and always the
  # hashes of the files a candidate patch may touch, so an isolated copy
  # without .git still identifies its sources.
  if git -C "$repo" rev-parse HEAD >/dev/null 2>&1; then
    {
      echo "commit: $(git -C "$repo" rev-parse HEAD)"
      echo "dirty: $(git -C "$repo" status --porcelain -- src include tests \
        CMakeLists.txt | wc -l) changed files"
    } > "$h/source.txt"
    git -C "$repo" ls-files -- src include tests CMakeLists.txt |
      (cd "$repo" && xargs sha256sum) > "$h/source.sha256" 2>/dev/null
  else
    echo "commit: none (isolated working copy)" > "$h/source.txt"
    (cd "$repo" && find src include tests CMakeLists.txt -type f |
      sort | xargs sha256sum) > "$h/source.sha256" 2>/dev/null
  fi
  # The renderer chain the benchmark host actually carries.
  timeout --foreground 30 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --frames 10 --save-dir "$h/save" > "$h/probe.log" 2>&1
  echo "probe exit: $?" >> "$h/host.txt"
  grep -E 'STACKFALL|Architecture|SDL compiled|Renderer:|video driver' \
    "$h/probe.log" >> "$h/host.txt" || true
  cat "$h/host.txt"
  status host passed
fi

# ---------------------------------------------------------------------------
if want_phase verify; then
  echo
  echo "=== phase verify ==="
  v="$run_dir/scenarios"
  mkdir -p "$v"
  verify_ok=1
  : > "$run_dir/verify.txt"
  for scenario in $scenarios; do
    python3 "$gen" "$scenario" "$work_frames" \
      "$v/$scenario.script.txt" > "$v/$scenario.gen.log" ||
      { status verify failed "generator failed for $scenario"; continue; }
    mkdir -p "$v/$scenario/save"
    timeout --foreground 120 \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$game" --seed "$seed" --tick-per-frame --frames "$work_frames" \
      --save-dir "$v/$scenario/save" \
      --input-script "$v/$scenario.script.txt" \
      --fixture-dir "$repo/tests/fixtures" \
      --observations "$v/$scenario/observations.txt" \
      > "$v/$scenario/run.log" 2>&1
    code=$?
    echo "$code" > "$v/$scenario/exit"
    if [ "$code" -ne 0 ]; then
      echo "$scenario: run exited $code" >> "$run_dir/verify.txt"
      verify_ok=0
      continue
    fi
    # The deterministic content claims of each scenario, from the game's own
    # scene, game-over, and fixture log lines plus the observation records
    # of the clears cycles. A scenario that does not measure what its name
    # says makes every benchmark built on it meaningless.
    claim=$(python3 - "$v/$scenario/run.log" "$v/$scenario/observations.txt" \
      "$scenario" <<'PYEOF'
import os, re, sys
log_text = open(sys.argv[1]).read()
obs_path, scenario = sys.argv[2], sys.argv[3]
scenes_to = [l.split()[3] for l in log_text.splitlines()
             if l.startswith("Scene ")]
overs = [l for l in log_text.splitlines() if "Game over:" in l]
lines_total = sum(int(l.split("lines ")[1].split(",")[0]) for l in overs)
ok, detail = False, "?"
if scenario == "busy":
    # The 6000-tick session is deterministic: play from tick 1, then exactly
    # one top-out (tick 3983 for seed 31) whose game-over tail is part of
    # every sample alike, and never a pause. The claim pins that shape so a
    # changed round breaks the verify instead of silently redefining the
    # benchmark.
    overs_at = [l.split()[-1] for l in log_text.splitlines()
                if "-> gameover at tick" in l]
    ok = "playing" in scenes_to and "paused" not in scenes_to and \
        len(overs_at) == 1 and "Game over:" in log_text
    detail = "play from tick 1, one deterministic top-out at tick %s, " \
             "never paused" % (overs_at[0] if overs_at else "?")
elif scenario == "clears":
    records = []
    if os.path.exists(obs_path):
        records = [re.search(r"lines=(\d+)", l) for l in
                   open(obs_path).read().splitlines()]
        records = [int(m.group(1)) for m in records if m]
    fixtures = log_text.count("Fixture \"four-line-ready\" applied")
    ok = (len(records) >= 3 and records == sorted(records) and
          records[-1] >= 4 * len(records) and fixtures >= len(records))
    detail = "%d labeled fixture cycles, lines %d -> %d" % \
        (len(records), records[0] if records else -1,
         records[-1] if records else -1)
elif scenario == "menu":
    ok = "controls" in scenes_to and "playing" not in scenes_to
    detail = "title and controls only (%d scene changes)" % len(scenes_to)
elif scenario == "pause":
    ok = "paused" in scenes_to and "playing" in scenes_to and \
        log_text.count("playing -> paused") >= 3
    detail = "%d pause cycles" % log_text.count("playing -> paused")
print(("%s: " % scenario) + (("OK, " + detail) if ok else
      "FAILED, content claim not met") +
      (" [gameover lines=%d]" % lines_total if overs else ""))
sys.exit(0 if ok else 1)
PYEOF
)
    claim_status=$?
    echo "$claim" >> "$run_dir/verify.txt"
    [ "$claim_status" -eq 0 ] || verify_ok=0
  done
  cat "$run_dir/verify.txt"
  if [ "$verify_ok" -eq 1 ]; then
    status verify passed "every scenario measures its claim"
  else
    status verify failed "a scenario claim failed; tune t026_gen.py"
  fi
fi

# ---------------------------------------------------------------------------
if want_phase bench; then
  echo
  echo "=== phase bench (tag $tag) ==="
  b="$run_dir/bench/$tag"
  mkdir -p "$b"
  bench_ok=1
  x11_available=1
  if ! command -v xvfb-run >/dev/null 2>&1; then
    x11_available=0
    echo "t026: xvfb-run missing; the x11 modes are unavailable" >&2
  fi
  [ "${T026_SKIP_X11:-0}" = "1" ] && x11_available=0

  # One row per sample; the summarizer aggregates by group.
  printf 'tag\tscenario\trenderer_mode\tmode\tsample\telapsed_s\tframes\t'\
'ticks\tus_per_frame\tcpu_percent\tmax_rss_kib\n' > "$b/samples.tsv"

  # row DIR SCENARIO RENDERER_MODE MODE SAMPLE: one parsed sample row.
  row() {
    local dir=$1 scenario=$2 rmode=$3 mode=$4 sample=$5
    local frames ticks elapsed cpu rss
    frames=$(loop_stat "$dir" frames)
    ticks=$(loop_stat "$dir" ticks)
    elapsed=$(elapsed_of "$dir")
    cpu=$(cpu_of "$dir")
    rss=$(rss_of "$dir")
    python3 - "$b/samples.tsv" "$tag" "$scenario" "$rmode" "$mode" "$sample" \
      "$frames" "$ticks" "$elapsed" "$cpu" "$rss" <<'PYEOF'
import sys
(out, tag, scenario, rmode, mode, sample, frames, ticks,
 elapsed, cpu, rss) = sys.argv[1:12]
parts = elapsed.split(":")
seconds = 0.0
for part in parts:  # h:mm:ss.cc, m:ss.cc, or ss.cc
    seconds = seconds * 60 + float(part)
frames = int(frames or 0)
uspf = seconds * 1e6 / frames if frames else 0
with open(out, "a") as stream:
    stream.write("\t".join([tag, scenario, rmode, mode, sample,
        "%.3f" % seconds, str(frames or "?"), str(ticks or "?"),
        "%.1f" % uspf, cpu or "?", rss or "?"]) + "\n")
PYEOF
  }

  for rmode in $renderers; do
    case "$rmode" in
      x11-*) video=x11 ;;
      dummy-*) video=dummy ;;
      *) fail "unknown renderer mode $rmode" ;;
    esac
    driver=""
    case "$rmode" in
      x11-opengl) driver="" ;;
      *software*) driver=software ;;
    esac
    if [ "$video" = x11 ] && [ "$x11_available" -eq 0 ]; then
      echo "unavailable: xvfb-run missing or skipped" > "$b/$rmode.status"
      bench_ok=0
      continue
    fi
    # Warm the renderer once per mode (Mesa's llvmpipe settles over the
    # first stretch), outside the recorded samples.
    echo "t026: warming $rmode"
    work_run "$b/warm-$rmode" "$video" "$driver" \
      "$run_dir/scenarios/busy.script.txt" ||
      { echo "t026: warmup run of $rmode failed" >&2; bench_ok=0; }
    for scenario in $scenarios; do
      for sample in $(seq 1 "$samples"); do
        dir="$b/$rmode-$scenario-work-s$sample"
        echo "t026: work $rmode $scenario sample $sample"
        work_run "$dir" "$video" "$driver" \
          "$run_dir/scenarios/$scenario.script.txt" ||
          { echo "t026: work run exited nonzero" >&2; bench_ok=0; continue; }
        # Every work sample must have run one tick per frame; a dropped or
        # batched tick would change what the number means.
        frames=$(loop_stat "$dir" frames)
        ticks=$(loop_stat "$dir" ticks)
        if [ "$frames" != "$ticks" ]; then
          echo "t026: $dir frames ($frames) != ticks ($ticks)" >&2
          bench_ok=0
        fi
        row "$dir" "$scenario" "$rmode" work "$sample"
      done
    done
  done

  # The paced percentiles: busy gameplay on every renderer mode, vsync
  # requested and stripped on the opengl one, exactly the T025 comparison.
  if [ "${T026_SKIP_PACED:-0}" = "1" ]; then
    echo "t026: T026_SKIP_PACED=1; skipping the paced section"
  else
  python3 "$gen" busy "$paced_frames" \
    "$run_dir/scenarios/busy-paced.script.txt" \
    > "$run_dir/scenarios/busy-paced.gen.log"
  paced_script="$run_dir/scenarios/busy-paced.script.txt"
  # paced_one RENDERER_MODE VSYNC EXTRA
  paced_one() {
    local rmode=$1 vsync=$2 extra=${3:-} video driver dir sample code
    case "$rmode" in
      x11-*) video=x11 ;;
      dummy-*) video=dummy ;;
      *) return 0 ;;
    esac
    driver=""
    case "$rmode" in
      *software*) driver=software ;;
    esac
    if [ "$video" = x11 ] && [ "$x11_available" -eq 0 ]; then
      return 0
    fi
    for sample in $(seq 1 "$samples"); do
      dir="$b/$rmode-busy-paced$vsync-s$sample"
      echo "t026: paced $rmode vsync=$vsync sample $sample"
      paced_run "$dir" "$video" "$driver" "$paced_script" "$extra"
      code=$?
      if [ "$code" -ne 0 ]; then
        echo "t026: paced run exited $code" >&2
        bench_ok=0
        continue
      fi
      python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
        --json "$dir/frame-stats.json" > "$dir/stats.txt" 2>&1 ||
        { echo "t026: frame stats flagged $dir" >&2; bench_ok=0; }
      printf 'paced\tbusy\t%s\tpaced-vs%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$rmode" "$vsync" "s$sample" "$(elapsed_of "$dir")" \
        "$(grep -c . "$dir/frame-times.txt")" \
        "$(loop_stat "$dir" ticks)" "0.0" "$(cpu_of "$dir")" \
        "$(rss_of "$dir")" >> "$b/samples.tsv"
      python3 - "$dir" >> "$b/paced-stats.tsv" <<'PYEOF'
import json, sys
d = sys.argv[1]
s = json.load(open(d + "/frame-stats.json"))
print("\t".join([d.split("/")[-1],
      "%.1f" % s["elapsed_seconds"], "%.2f" % s["ticks_per_second"],
      "%.2f" % s["median_frame_ms"], "%.2f" % s["p95_frame_ms"],
      "%.2f" % s["p99_frame_ms"], "%.2f" % s["max_frame_ms"]]))
PYEOF
    done
  }
  if [ ! -f "$b/paced-stats.tsv" ]; then
    printf 'run\telapsed_s\thz\tmedian_ms\tp95_ms\tp99_ms\tmax_ms\n' \
      > "$b/paced-stats.tsv"
  fi
  paced_one x11-opengl yes ""
  paced_one x11-opengl no "SDL_RENDER_VSYNC=0"
  paced_one x11-software yes ""
  paced_one dummy-software yes ""
  [ -s "$b/paced-stats.tsv" ] && cat "$b/paced-stats.tsv"
  fi

  cat "$b/samples.tsv"
  if [ "$bench_ok" -eq 1 ]; then
    status bench passed "tag $tag"
  else
    status bench failed "tag $tag; see failures above"
  fi
fi

# ---------------------------------------------------------------------------
if want_phase profile; then
  echo
  echo "=== phase profile (tag $tag) ==="
  p="$run_dir/profile/$tag"
  rm -rf "$p"
  mkdir -p "$p"
  profile_ok=1
  command -v valgrind >/dev/null 2>&1 ||
    { status profile unavailable "valgrind missing"; profile_ok=0; }
  if [ "$profile_ok" -eq 1 ]; then
    for scenario in busy clears menu; do
      script="$run_dir/scenarios/$scenario.script.txt"
      [ -f "$script" ] ||
        { status profile failed "no $scenario script; run the verify phase"
          continue; }
      mkdir -p "$p/$scenario/save"
      echo "t026: callgrind $scenario ($profile_frames frames)"
      timeout --foreground 600 \
        env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        valgrind --tool=callgrind --separate-threads=no \
        --callgrind-out-file="$p/$scenario/callgrind.out" \
        "$game" --seed "$seed" --tick-per-frame --frames "$profile_frames" \
        --save-dir "$p/$scenario/save" --input-script "$script" \
        --fixture-dir "$repo/tests/fixtures" \
        > "$p/$scenario/run.log" 2>&1
      code=$?
      echo "$code" > "$p/$scenario/exit"
      if [ "$code" -ne 0 ]; then
        echo "t026: callgrind run of $scenario exited $code" >&2
        profile_ok=0
        continue
      fi
      callgrind_annotate --auto=no --threshold=99.9 \
        "$p/$scenario/callgrind.out" > "$p/$scenario/annotate.txt" 2>/dev/null
      # The inclusive (cycle-consuming callers) view names the paths, the
      # exclusive view names the hot leaves.
      callgrind_annotate --tree=caller --auto=no --threshold=99 \
        "$p/$scenario/callgrind.out" \
        > "$p/$scenario/annotate-callers.txt" 2>/dev/null
      head -30 "$p/$scenario/annotate.txt"
    done
  fi
  if [ "$profile_ok" -eq 1 ]; then
    status profile passed "callgrind evidence in $p"
  fi
fi

# ---------------------------------------------------------------------------
if want_phase summarize; then
  echo
  echo "=== phase summarize ==="
  python3 "$summarizer" --run-dir "$run_dir" --write-tsv \
    "$run_dir/summary.tsv" --write-text "$run_dir/summary.txt"
fi

echo
if [ "$fail_count" -eq 0 ]; then
  echo "t026: PASS (run $run_id)"
  exit 0
fi
echo "t026: FAIL ($fail_count phase(s) not passed; see $run_dir/failures.txt)"
exit 1
