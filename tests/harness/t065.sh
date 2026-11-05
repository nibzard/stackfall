#!/bin/bash
# T065 profiling driver: the integrated Release build under real SDL input
# (task T065, the post-v1.0 device-feedback round).
#
#   tests/harness/t065.sh
#
# The owner's device report says the game sometimes stutters after initially
# smooth play. This driver measures the current integrated build - the
# T052 mid-round record saves, the T055 portrait layout, and the T062
# soundtrack included - with deterministic, repeated, real SDL input, and
# decides from measurements which of the stated hypotheses costs frame time.
# docs/PERFORMANCE.md still governs the method; this file adds the round's
# own requirements: keep every scenario playing, cover keyboard and virtual
# controller, run a sustained case and one bounded CPU and storage-pressure
# case, and record audio callback work, command traffic, write counts, and
# resident memory over time.
#
# Phases (T065_PHASES selects a subset):
#
#   host        host, toolchain, renderer, and source identity on record
#   scenarios   generate the deterministic input scripts of this round
#   verify      one run per scenario proving what it measures: play share,
#               restarts, and the input coverage the task names
#   bench       the samples: work mode (pacing off, work per frame) and
#               paced mode (frame-time percentiles) per scenario, renderer,
#               and save mode
#   sustained   one long paced session with memory and write sampling
#   pressure    one bounded CPU and storage-pressure case
#   hypotheses  the stated causes, each measured on its own: save cost,
#               music and audio callback work, logging, pacing
#   profile     bounded instrumentation under callgrind, never inside a
#               timed sample
#   summarize   every sample of the evidence set into comparison tables
#
# Samples land in build/agent-checks/<run-id>/bench/<tag>/; rerun after a
# candidate fix with T065_TAG=fix1 and the same seed, frames, and scenarios,
# so before and after numbers replay exactly.
#
# Options through environment variables:
#   T065_RUN_ID         evidence directory (default: t065-<date>)
#   T065_TAG            sample label, baseline / fixN / final (default:
#                       baseline; a reused tag's samples are replaced)
#   T065_BUILD_DIR      Release build (default: build-rel)
#   T065_PHASES         space-separated phase list (default: all)
#   T065_SAMPLES        samples per scenario, renderer, and save mode
#                       (default: 3)
#   T065_WORK_TICKS     ticks per work-mode sample (default: 6000)
#   T065_PACED_SECONDS  seconds per paced sample (default: 60)
#   T065_SEED           the fixed round seed (default: 31, the T025/T026
#                       seed, so the numbers compare with those baselines)
#   T065_SCENARIOS      scenario list (default: rotate-move drops clears
#                       pause pad-mixed)
#   T065_RENDERERS      work-mode renderer list (default: x11-opengl
#                       x11-software dummy-software)
#   T065_PACED_RENDERERS  paced renderer list (default: dummy-software)
#   T065_SAVE_MODES     fresh and/or banked (default: both). fresh starts a
#                       save directory from nothing, so the T052 mid-round
#                       record saves fire; banked preloads an unreachable
#                       record, so none fire. The pair isolates the write
#                       cost inside the same workload.
#   T065_SUSTAINED_SECONDS  the sustained session (default: 180)
#   T065_PRESSURE_SECONDS  the pressured session (default: 60)
#   T065_X11_WORK_FRAMES  frames per x11 work-mode sample (default: 1200;
#                       the x11 rows are supporting evidence, see bench)
#   T065_PRESSURE_RENDERERS  renderers of the pressure case (default:
#                       dummy-software x11-opengl: the second one holds a
#                       full frame slot of work under Xvfb, so a save that
#                       lands in a busy frame shows in the tail)
#   T065_SKIP_X11       set to 1 when xvfb-run is missing
#   T065_SKIP_PACED     set to 1 to skip the paced percentile section
#   T065_PROFILE_FRAMES frames per callgrind run (default: 900)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T065_BUILD_DIR:-$repo/build-rel}
run_id=${T065_RUN_ID:-t065-$(date +%Y-%m-%d)}
tag=${T065_TAG:-baseline}
phases=${T065_PHASES:-"host scenarios verify bench sustained pressure hypotheses profile summarize"}
samples=${T065_SAMPLES:-3}
work_ticks=${T065_WORK_TICKS:-6000}
paced_seconds=${T065_PACED_SECONDS:-60}
seed=${T065_SEED:-31}
scenarios=${T065_SCENARIOS:-"rotate-move drops clears pause pad-mixed"}
renderers=${T065_RENDERERS:-"x11-opengl x11-software dummy-software"}
paced_renderers=${T065_PACED_RENDERERS:-"dummy-software"}
save_modes=${T065_SAVE_MODES:-"fresh banked"}
sustained_seconds=${T065_SUSTAINED_SECONDS:-180}
pressure_seconds=${T065_PRESSURE_SECONDS:-60}
pressure_renderers=${T065_PRESSURE_RENDERERS:-"dummy-software x11-opengl"}
x11_frames=${T065_X11_WORK_FRAMES:-1200}
profile_frames=${T065_PROFILE_FRAMES:-900}
run_dir="$repo/build/agent-checks/$run_id"
harness="$repo/tests/harness"
gen="$harness/t065_gen.py"
monitor="$harness/t065_monitor.py"
pressure_worker="$harness/t065_pressure.py"
frame_stats="$harness/frame_stats.py"
summarizer="$harness/t065_summarize.py"
game="$build_dir/stackfall"
probe="$build_dir/stackfall_save_probe"

fail_count=0
fail() {
  echo "t065: $*" >&2
  exit 1
}

case "$run_id" in
  */*|""|.|..) fail "T065_RUN_ID must name one directory, not '$run_id'" ;;
esac
case "$run_id" in *..*) fail "T065_RUN_ID must not traverse" ;; esac
[ -x "$game" ] || fail "no Release game binary at $game; build it first"
mkdir -p "$run_dir" || fail "cannot create $run_dir"

want_phase() {
  case " $phases " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

status() {
  echo "t065: phase $1: $2${3:+ ($3)}"
  if [ "$2" != passed ] && [ "$2" != unavailable ]; then
    fail_count=$((fail_count + 1))
    echo "$1: $2${3:+ ($3)}" >> "$run_dir/failures.txt"
  fi
}

# renderer variables for one named renderer mode.
renderer_setup() {
  case "$1" in
    x11-opengl) video=x11; driver="" ;;
    x11-software) video=x11; driver=software ;;
    dummy-software) video=dummy; driver="" ;;
    *) fail "unknown renderer '$1'" ;;
  esac
}

# prepare_save_dir DIR MODE seeds one save directory for a sample. fresh
# starts empty, so the record the round beats is 0 and the T052 mid-round
# saves fire; banked preloads a record no scripted round can reach, so the
# same session runs without one save.
prepare_save_dir() {
  local dir=$1 mode=$2
  rm -rf "$dir"
  mkdir -p "$dir"
  if [ "$mode" = banked ]; then
    printf 'version=1\nhigh_score=999999999999\n' > "$dir/highscore.dat"
  fi
}

# run_sample DIR VIDEO DRIVER [GAME ARG ...]: one monitored game run. The
# monitor samples /proc accounting from outside the process, /usr/bin/time
# records CPU and peak memory, and the game writes its own run log.
run_sample() {
  local dir=$1 video=$2 driver=$3
  shift 3
  local render_env=""
  [ -n "$driver" ] && render_env="SDL_RENDER_DRIVER=$driver"
  # /usr/bin/time sits outermost because it accounts for the whole tree it
  # waits on; the monitor sits directly around the game, whose /proc entries
  # are the ones the samples must read (env execs the game, so one pid).
  if [ "$video" = x11 ]; then
    xvfb-run -a -s "-screen 0 960x544x24" \
      timeout --foreground 300 \
      /usr/bin/time -v -o "$dir/time.txt" \
      python3 "$monitor" --out "$dir/mon.csv" --interval-ms 250 -- \
      env SDL_AUDIODRIVER=dummy $render_env "$game" "$@" \
        > "$dir/run.log" 2>&1
  else
    timeout --foreground 300 \
      /usr/bin/time -v -o "$dir/time.txt" \
      python3 "$monitor" --out "$dir/mon.csv" --interval-ms 250 -- \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy $render_env \
      "$game" "$@" > "$dir/run.log" 2>&1
  fi
  return $?
}

scenario_args() {
  # Extra game arguments a scenario needs.
  [ "$1" = pad-mixed ] && echo "--virtual-controller"
  return 0
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
    echo "filesystem: $(df -T "$repo" | awk 'NR==2{print $2, $7}')"
    echo "game binary: $game"
    echo "build type: $(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' \
      "$build_dir/CMakeCache.txt")"
    echo "build flags (CXX_FLAGS_RELEASE): $(sed -n \
      's/^CMAKE_CXX_FLAGS_RELEASE:STRING=//p' "$build_dir/CMakeCache.txt")"
    echo "xvfb-run: $(command -v xvfb-run || echo missing)"
    echo "valgrind: $(valgrind --version 2>/dev/null || echo missing)"
    echo "perf_event_paranoid: $(cat /proc/sys/kernel/perf_event_paranoid \
      2>/dev/null || echo unknown)"
  } > "$h/host.txt"
  if git -C "$repo" rev-parse HEAD >/dev/null 2>&1; then
    {
      echo "commit: $(git -C "$repo" rev-parse HEAD)"
      echo "dirty: $(git -C "$repo" status --porcelain -- src include tests \
        CMakeLists.txt tools | wc -l) changed files"
    } > "$h/source.txt"
    git -C "$repo" ls-files -- src include tests CMakeLists.txt tools |
      (cd "$repo" && xargs sha256sum) > "$h/source.sha256" 2>/dev/null
  else
    echo "commit: none (isolated working copy)" > "$h/source.txt"
    (cd "$repo" && find src include tests CMakeLists.txt tools -type f |
      sort | xargs sha256sum) > "$h/source.sha256" 2>/dev/null
  fi
  timeout --foreground 30 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --frames 10 --save-dir "$h/save" > "$h/probe.log" 2>&1
  echo "probe exit: $?" >> "$h/host.txt"
  grep -E 'STACKFALL|Architecture|SDL compiled|Renderer:|video driver' \
    "$h/probe.log" >> "$h/host.txt" || true
  cat "$h/host.txt"
  status host passed
fi

# ---------------------------------------------------------------------------
if want_phase scenarios; then
  echo
  echo "=== phase scenarios ==="
  s="$run_dir/scenarios"
  mkdir -p "$s"
  scenarios_ok=1
  for scenario in $scenarios; do
    out=$(python3 "$gen" "$scenario" "$work_ticks" \
      "$s/$scenario.script.txt" 2>&1) ||
      { status scenarios failed "$scenario generator: $out"; scenarios_ok=0;
        continue; }
    sed -n 's/^TICKS //p' <<< "$out" > "$s/$scenario.ticks"
    echo "$scenario: $out"
  done
  # The sustained session needs a script at least as long as its wall time.
  out=$(python3 "$gen" sustained $((sustained_seconds * 60 + 120)) \
    "$s/sustained.script.txt" 2>&1) ||
    { status scenarios failed "sustained generator: $out"; scenarios_ok=0; }
  sed -n 's/^TICKS //p' <<< "$out" > "$s/sustained.ticks"
  if [ "$scenarios_ok" = 1 ]; then
    status scenarios passed "scripts and tick budgets recorded"
  fi
fi

# ---------------------------------------------------------------------------
if want_phase verify; then
  echo
  echo "=== phase verify ==="
  v="$run_dir/verify"
  mkdir -p "$v"
  verify_ok=1
  : > "$run_dir/verify.txt"
  for scenario in $scenarios; do
    dir="$v/$scenario"
    mkdir -p "$dir/save"
    ticks=$(cat "$run_dir/scenarios/$scenario.ticks")
    timeout --foreground 200 \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$game" --seed "$seed" --tick-per-frame --frames $((ticks + 60)) \
      --save-dir "$dir/save" \
      --input-script "$run_dir/scenarios/$scenario.script.txt" \
      --input-log "$dir/il.log" --fixture-dir "$repo/tests/fixtures" \
      $(scenario_args "$scenario") > "$dir/run.log" 2>&1
    code=$?
    echo "$code" > "$dir/exit"
    python3 "$harness/t065_verify.py" "$dir" "$scenario" \
      >> "$run_dir/verify.txt"
    [ $? = 0 ] || verify_ok=0
  done
  cat "$run_dir/verify.txt"
  if [ "$verify_ok" = 1 ]; then
    status verify passed "every scenario measures its claim while playing"
  else
    status verify failed "a scenario claim failed; tune t065_gen.py"
  fi
fi

# ---------------------------------------------------------------------------
# bench: work mode (pacing off; the wall clock is work alone) and paced mode
# (the whole 16.67 ms slot, deliberate wait included) per scenario, renderer,
# and save mode.
# ---------------------------------------------------------------------------
if want_phase bench; then
  echo
  echo "=== phase bench (tag $tag) ==="
  b="$run_dir/bench/$tag"
  mkdir -p "$b"
  bench_ok=1
  : > "$b/failures.txt"
  for scenario in $scenarios; do
    ticks=$(cat "$run_dir/scenarios/$scenario.ticks")
    for renderer in $renderers; do
      renderer_setup "$renderer"
      # The x11 renderers present at about the frame slot even with pacing
      # off (T025/T026), so their work rows are supporting evidence: the
      # same scripts, truncated to a shorter prefix, keep the phase inside
      # a bounded wall time. The dummy rows carry the work decision.
      frames=$((ticks + 60))
      [ "$video" = x11 ] && frames=$x11_frames
      for mode in $save_modes; do
        for sample in $(seq 1 "$samples"); do
          dir="$b/work/$renderer/$mode/$scenario/$sample"
          mkdir -p "$dir"
          prepare_save_dir "$dir/save" "$mode"
          run_sample "$dir" "$video" "$driver" \
            --seed "$seed" --tick-per-frame --frames "$frames" \
            --save-dir "$dir/save" \
            --input-script "$run_dir/scenarios/$scenario.script.txt" \
            --input-log "$dir/il.log" \
            --fixture-dir "$repo/tests/fixtures" \
            $(scenario_args "$scenario")
          code=$?
          echo "$code" > "$dir/exit"
          [ "$code" = 0 ] || {
            echo "work $scenario/$renderer/$mode/$sample exited $code" \
              >> "$b/failures.txt"
            bench_ok=0
          }
        done
      done
    done
  done

  if [ "${T065_SKIP_PACED:-0}" != 1 ]; then
    for scenario in $scenarios; do
      for renderer in $paced_renderers; do
        renderer_setup "$renderer"
        for mode in $save_modes; do
          for sample in $(seq 1 "$samples"); do
            dir="$b/paced/$renderer/$mode/$scenario/$sample"
            mkdir -p "$dir"
            prepare_save_dir "$dir/save" "$mode"
            run_sample "$dir" "$video" "$driver" \
              --seed "$seed" --frames $((paced_seconds * 60)) \
              --save-dir "$dir/save" \
              --input-script "$run_dir/scenarios/$scenario.script.txt" \
              --input-log "$dir/il.log" \
              --frame-times "$dir/frame-times.txt" \
              --fixture-dir "$repo/tests/fixtures" \
              $(scenario_args "$scenario")
            code=$?
            echo "$code" > "$dir/exit"
            if [ "$code" = 0 ]; then
              python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
                --json "$dir/frame-stats.json" > /dev/null 2>&1 ||
                { echo "paced $scenario/$renderer/$mode/$sample stats" \
                    >> "$b/failures.txt"; bench_ok=0; }
            else
              echo "paced $scenario/$renderer/$mode/$sample exited $code" \
                >> "$b/failures.txt"
              bench_ok=0
            fi
          done
        done
      done
    done
  fi
  if [ "$bench_ok" = 1 ]; then
    status bench passed "$(find "$b" -name exit -exec cat {} \; | \
      grep -c 0) samples recorded under bench/$tag"
  else
    status bench failed "see bench/$tag/failures.txt"
  fi
fi

# ---------------------------------------------------------------------------
# sustained: one long paced session, memory and writes sampled over time.
# ---------------------------------------------------------------------------
if want_phase sustained; then
  echo
  echo "=== phase sustained (tag $tag) ==="
  dir="$run_dir/sustained/$tag"
  mkdir -p "$dir"
  renderer_setup dummy-software
  prepare_save_dir "$dir/save" fresh
  run_sample "$dir" "$video" "$driver" \
    --seed "$seed" --frames $((sustained_seconds * 60)) \
    --save-dir "$dir/save" \
    --input-script "$run_dir/scenarios/sustained.script.txt" \
    --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
    --fixture-dir "$repo/tests/fixtures"
  code=$?
  echo "$code" > "$dir/exit"
  if [ "$code" = 0 ]; then
    python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
      --json "$dir/frame-stats.json" > /dev/null
    python3 "$harness/t065_sustained.py" "$dir" > "$dir/summary.txt"
    cat "$dir/summary.txt"
    status sustained passed "$(head -1 "$dir/summary.txt")"
  else
    status sustained failed "session exited $code"
  fi
fi

# ---------------------------------------------------------------------------
# pressure: one bounded CPU and storage-pressure case. The worker holds a
# hard time cap and byte cap, writes only inside this run directory, and is
# registered in the Looper cleanup file before it starts, so an interrupted
# phase cannot leave a spinner behind.
# ---------------------------------------------------------------------------
if want_phase pressure; then
  echo
  echo "=== phase pressure (tag $tag) ==="
  p="$run_dir/pressure/$tag"
  mkdir -p "$p"
  scenario=drops
  pressure_ok=1
  : > "$p/summary.tsv"
  for kind in idle cpu storage both; do
   for renderer in $pressure_renderers; do
    renderer_setup "$renderer"
    dir="$p/$kind-$renderer"
    mkdir -p "$dir"
    worker_pid=""
    if [ "$kind" != idle ]; then
      mkdir -p "$dir/pressure-files"
      nice -n 19 python3 "$pressure_worker" --kind "$kind" \
        --seconds $((pressure_seconds + 30)) --dir "$dir/pressure-files" \
        --max-mib 256 --out "$dir/worker.txt" > "$dir/worker.log" 2>&1 &
      worker_pid=$!
      if [ -n "${LOOPER_RESOURCE_CLEANUP_FILE:-}" ]; then
        {
          echo "# t065 pressure worker ($run_id, kind $kind)"
          echo "if [ -d '$dir/pressure-files' ]; then"
          echo "  kill $worker_pid 2>/dev/null || true"
          echo "  pkill -P $worker_pid 2>/dev/null || true"
          echo "  rm -rf '$dir/pressure-files'"
          echo "fi"
        } >> "$LOOPER_RESOURCE_CLEANUP_FILE"
      fi
      sleep 2
    fi
    prepare_save_dir "$dir/save" fresh
    run_sample "$dir" "$video" "$driver" \
      --seed "$seed" --frames $((pressure_seconds * 60)) \
      --save-dir "$dir/save" \
      --input-script "$run_dir/scenarios/$scenario.script.txt" \
      --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
      --fixture-dir "$repo/tests/fixtures"
    code=$?
    echo "$code" > "$dir/exit"
    if [ -n "$worker_pid" ]; then
      kill "$worker_pid" 2>/dev/null
      wait "$worker_pid" 2>/dev/null
      rm -rf "$dir/pressure-files"
    fi
    if [ "$code" != 0 ]; then
      status pressure failed "$kind exited $code"
      pressure_ok=0
      continue
    fi
    python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
      --json "$dir/frame-stats.json" > /dev/null
    # The save probe inside the same window: what one durable save costs
    # while the storage queue is busy.
    mkdir -p "$dir/probe-dir"
    "$probe" "$dir/probe-dir" 100 > "$dir/save-probe.txt" 2>&1
    rm -rf "$dir/probe-dir"
    python3 "$harness/t065_pressure_row.py" "$dir" "$kind-$renderer" \
      >> "$p/summary.tsv"
   done
  done
  {
    echo -e "kind_renderer\tmedian_ms\tp95_ms\tp99_ms\tmax_ms\trecord_saves\tsave_p50_us"
    cat "$p/summary.tsv"
  } > "$p/summary.txt"
  column -t -s $'\t' "$p/summary.txt" 2>/dev/null || cat "$p/summary.txt"
  [ "$pressure_ok" = 1 ] && status pressure passed \
    "bounded pressure recorded per kind" || true
fi

# ---------------------------------------------------------------------------
# hypotheses: each stated cause, measured on its own.
# ---------------------------------------------------------------------------
if want_phase hypotheses; then
  echo
  echo "=== phase hypotheses (tag $tag) ==="
  hh="$run_dir/hypotheses/$tag"
  mkdir -p "$hh"
  renderer_setup dummy-software
  hyp_ok=1

  # 1. Synchronous score writes: the store's own save cost, and the same
  #    paced workload with the saves on and off (fresh against banked).
  mkdir -p "$hh/save-cost/probe"
  "$probe" "$hh/save-cost/probe" 300 > "$hh/save-cost/probe.txt" 2>&1
  rm -rf "$hh/save-cost/probe"
  grep "save-probe" "$hh/save-cost/probe.txt" ||
    { status hypotheses failed "save probe produced no output"; hyp_ok=0; }

  # 2. Music and the audio callback: the callback-timing run (its own
  #    session, so the clock pair stays out of every timed sample), the
  #    soundtrack off against a playing pool at the same workload, and the
  #    audio suite's own budget print.
  dir="$hh/music/callback-timing"
  mkdir -p "$dir"
  prepare_save_dir "$dir/save" banked
  timeout --foreground 200 \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    STACKFALL_PROFILE_AUDIO_CALLBACK=1 \
    "$game" --seed "$seed" --frames $((paced_seconds * 60)) \
    --save-dir "$dir/save" \
    --input-script "$run_dir/scenarios/drops.script.txt" \
    --input-log "$dir/il.log" > "$dir/run.log" 2>&1
  grep -E "^Audio:|^Music commands:" "$dir/run.log" > "$hh/music/callback.txt"
  cat "$hh/music/callback.txt"
  for music in off arcade; do
    for sample in 1 2 3; do
      dir="$hh/music/$music/$sample"
      mkdir -p "$dir"
      prepare_save_dir "$dir/save" banked
      printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=landscape\nmusic=%s\nmusicvolume=6\neffectsvolume=8\n' \
        "$music" > "$dir/save/settings.dat"
      run_sample "$dir" "$video" "$driver" \
        --seed "$seed" --frames $((paced_seconds * 60)) \
        --save-dir "$dir/save" \
        --input-script "$run_dir/scenarios/drops.script.txt" \
        --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
        --fixture-dir "$repo/tests/fixtures"
      code=$?
      echo "$code" > "$dir/exit"
      if [ "$code" = 0 ]; then
        python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
          --json "$dir/frame-stats.json" > /dev/null 2>&1 ||
          { status hypotheses failed "music $music sample $sample stats";
            hyp_ok=0; }
      else
        status hypotheses failed "music $music sample $sample exited $code"
        hyp_ok=0
      fi
    done
  done
  (cmake --build "$build_dir" --target stackfall_audio_tests > /dev/null \
    2>&1 && "$build_dir/stackfall_audio_tests" 2>&1 |
    grep -i "callback") > "$hh/music/audio-suite.txt" 2>&1 ||
    echo "audio suite unavailable" >> "$hh/music/audio-suite.txt"

  # 3. Diagnostic logging: the same paced workload with the session log on
  #    a real file and on /dev/null, so the per-line write cost separates
  #    from the game's own work. The run is paced and a full sample long,
  #    so the monitor's write counters cover the whole session and the
  #    percentiles land beside them; the line counts come from the log.
  for variant in logfile devnull; do
    dir="$hh/logging/$variant"
    mkdir -p "$dir"
    prepare_save_dir "$dir/save" banked
    if [ "$variant" = logfile ]; then
      log_target="$dir/session-log.txt"
    else
      log_target=/dev/null
    fi
    timeout --foreground 200 \
      /usr/bin/time -v -o "$dir/time.txt" \
      python3 "$monitor" --out "$dir/mon.csv" --interval-ms 250 -- \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      STACKFALL_LOG_FILE="$log_target" \
      "$game" --seed "$seed" --frames $((paced_seconds * 60)) \
      --save-dir "$dir/save" \
      --input-script "$run_dir/scenarios/drops.script.txt" \
      --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
      > "$dir/run.log" 2>&1
    code=$?
    echo "$code" > "$dir/exit"
    if [ "$code" = 0 ]; then
      python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
        --json "$dir/frame-stats.json" > /dev/null 2>&1
    else
      status hypotheses failed "logging $variant exited $code"
      hyp_ok=0
    fi
  done

  # 4. The portrait layout of task T055: the same paced workload with the
  #    stored orientation set to portrait, so the rotated frame's own cost
  #    has a row (the build under test includes the feature; nothing here
  #    infers the handheld, it only separates the two layouts' work).
  for sample in 1 2 3; do
    dir="$hh/portrait/$sample"
    mkdir -p "$dir"
    prepare_save_dir "$dir/save" banked
    printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=portrait\nmusic=auto\nmusicvolume=6\neffectsvolume=8\n' \
      > "$dir/save/settings.dat"
    run_sample "$dir" "$video" "$driver" \
      --seed "$seed" --frames $((paced_seconds * 60)) \
      --save-dir "$dir/save" \
      --input-script "$run_dir/scenarios/drops.script.txt" \
      --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
      --fixture-dir "$repo/tests/fixtures"
    code=$?
    echo "$code" > "$dir/exit"
    if [ "$code" = 0 ]; then
      python3 "$frame_stats" "$dir/frame-times.txt" \
        "$dir/run.log" --json "$dir/frame-stats.json" > /dev/null 2>&1
    fi
    grep -m1 "orientation start" "$dir/il.log" > "$dir/orientation.txt" \
      2>/dev/null || true
  done

  # 5. Pacing: the limiter and stall lines of this evidence set, so
  #    rendering versus waiting stays explicit in the tables.
  {
    echo "limiter and stall lines across this evidence set:"
    # The patterns spell the logged sentences of src/main.cpp exactly; an
    # approximation here matched nothing and made the file read as 'no
    # limiter engaged', which the paced logs themselves contradict.
    grep -H -E "Fallback frame limiter engaged|Unusual frame stall|Frame stall of" \
      "$run_dir"/bench/"$tag"/paced/*/*/*/*/run.log \
      "$run_dir"/sustained/"$tag"/run.log 2>/dev/null | sort | uniq -c
    echo "(end)"
  } > "$hh/pacing.txt"
  head -5 "$hh/pacing.txt"
  [ "$hyp_ok" = 1 ] && status hypotheses passed \
    "save cost, music, logging, pacing measured" || true
fi

# ---------------------------------------------------------------------------
if want_phase profile; then
  echo
  echo "=== phase profile (tag $tag) ==="
  prof="$run_dir/profile/$tag"
  mkdir -p "$prof"
  if ! command -v valgrind > /dev/null 2>&1; then
    status profile unavailable "valgrind missing; callgrind skipped"
  else
    profile_ok=1
    for scenario in drops rotate-move; do
      dir="$prof/$scenario"
      mkdir -p "$dir/save"
      prepare_save_dir "$dir/save" fresh
      # The environment sits outside valgrind on purpose. Valgrind does not
      # follow an exec by default, so 'valgrind env VAR=... game' profiles
      # env alone and writes an empty callgrind file that still exits 0 -
      # exactly what the first T070 profile run produced before this order
      # was pinned. The T026 driver already ordered it this way.
      timeout --foreground 900 \
        env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        valgrind --tool=callgrind \
        --callgrind-out-file="$dir/callgrind.out" \
        "$game" --seed "$seed" --tick-per-frame --frames "$profile_frames" \
        --save-dir "$dir/save" \
        --input-script "$run_dir/scenarios/$scenario.script.txt" \
        --input-log "$dir/il.log" --fixture-dir "$repo/tests/fixtures" \
        > "$dir/run.log" 2>&1
      code=$?
      echo "$code" > "$dir/exit"
      # An empty callgrind file is a failed profile, not a quiet one: the
      # annotate output must carry the program totals before the phase may
      # claim the instrumentation ran.
      if [ -s "$dir/callgrind.out" ]; then
        callgrind_annotate "$dir/callgrind.out" > "$dir/annotate.txt" 2>&1
        echo "$scenario: exit $code, $(wc -l < "$dir/annotate.txt") \
annotated lines"
      else
        echo "$scenario: exit $code, callgrind output empty" >&2
        profile_ok=0
      fi
    done
    if [ "$profile_ok" = 1 ]; then
      status profile passed "callgrind runs kept out of the timed samples"
    else
      status profile failed "a callgrind run produced no profile"
    fi
  fi
fi

# ---------------------------------------------------------------------------
if want_phase summarize; then
  echo
  echo "=== phase summarize ==="
  set -o pipefail
  if python3 "$summarizer" --run-dir "$run_dir" --tag "$tag" |
      tee "$run_dir/summary-$tag.txt"; then
    status summarize passed "tables written to summary-$tag.txt"
  else
    status summarize failed "summarizer failed"
  fi
  set +o pipefail
  [ -f "$run_dir/failures.txt" ] && cat "$run_dir/failures.txt"
fi

echo
if [ "$fail_count" = 0 ]; then
  echo "t065: all requested phases passed (run id $run_id, tag $tag)"
  exit 0
fi
echo "t065: $fail_count phase step(s) failed; see $run_dir/failures.txt"
exit 1
