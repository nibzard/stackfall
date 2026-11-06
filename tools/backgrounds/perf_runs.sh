#!/bin/sh
# T085 background cost measurements: the same workload under Off, Fixed
# 17..20, and Auto, both orientations, against the pre-backgrounds Off
# baseline of the same revision (notes/performance.md).
#
# Phases:
#   counts    the instrumented draw counts of stackfall_bg_counts (Release)
#   work      the stress scripts at --tick-per-frame: wall time is pure
#             work, so the delta against Off is the background cost
#   paced     the same scripts at real-time pacing with --frame-times,
#             bounded to PERF_FRAMES frames: frame_stats.py percentiles
#             (a desktop pacing profile, never an RG503 claim)
#
# Environment:
#   PERF_FRAMES   paced frame bound (default 1800, thirty seconds)
#   PERF_STEPS    draw-count sweep step in ms (default 25)
#
# Results land under build/backgrounds/t085/perf, one text file per run.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
rel="$repo/build-rel"
out="$repo/build/backgrounds/t085/perf"
frames=${PERF_FRAMES:-1800}
step=${PERF_STEPS:-25}
seed=31337

fail() {
  echo "perf: $*" >&2
  exit 1
}

echo "perf: configuring the Release build in $rel"
cmake -S "$repo" -B "$rel" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null ||
  fail "cmake configure failed in $rel"
cmake --build "$rel" >/dev/null ||
  fail "cmake build failed in $rel"
game="$rel/stackfall"
[ -x "$game" ] || fail "missing $game"
mkdir -p "$out"
rm -f "$out"/*.txt "$out"/*.json

# ---------------------------------------------------------------------------
echo "perf: draw counts (Release, sweep step ${step} ms)"
"$rel/stackfall_bg_counts" "$step" > "$out/draw-counts.txt" ||
  fail "stackfall_bg_counts failed"
tail -4 "$out/draw-counts.txt"

# ---------------------------------------------------------------------------
# One staged settings file per mode word; `off` doubles as the baseline.
stage_save() {
  dir=$1 mode=$2 design=$3 orientation=$4
  rm -rf "$dir"
  mkdir -p "$dir/save" "$dir/shots"
  printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=%s\nmusic=auto\nmusicvolume=6\neffectsvolume=8\nbackground=%s\nbackgroundid=%s\n' \
    "$orientation" "$mode" "$design" > "$dir/save/settings.dat"
}

run_work() {
  label=$1 script=$2 mode=$3 design=$4 orientation=$5
  stage_save "$out/work-$label" "$mode" "$design" "$orientation"
  start=$(date +%s%N)
  /usr/bin/time -v -o "$out/work-$label.time" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" --tick-per-frame \
    --input-script "$script" --fixture-dir "$repo/tests/fixtures" \
    --save-dir "$out/work-$label/save" --screenshot-dir "$out/work-$label/shots" \
    --observations "$out/work-$label.obs" \
    --frames 12000 > "$out/work-$label.log" 2>&1 ||
    fail "work run $label failed (see $out/work-$label.log)"
  end=$(date +%s%N)
  ms=$(( (end - start) / 1000000 ))
  cpu=$(awk -F': *' '/Percent of CPU/{printf "%d", $2}' "$out/work-$label.time")
  rss=$(awk -F': *' '/Maximum resident/{print $2}' "$out/work-$label.time")
  printf 'work %-22s %8d ms wall  %3s%% cpu  %6s kb max rss\n' \
    "$label" "$ms" "$cpu" "$rss" | tee "$out/work-summary.txt" -a
}

run_paced() {
  label=$1 script=$2 mode=$3 design=$4 orientation=$5
  stage_save "$out/paced-$label" "$mode" "$design" "$orientation"
  /usr/bin/time -v -o "$out/paced-$label.time" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" \
    --input-script "$script" --fixture-dir "$repo/tests/fixtures" \
    --save-dir "$out/paced-$label/save" --screenshot-dir "$out/paced-$label/shots" \
    --observations "$out/paced-$label.obs" \
    --frame-times "$out/paced-$label.frames" \
    --frames "$frames" > "$out/paced-$label.log" 2>&1 ||
    fail "paced run $label failed (see $out/paced-$label.log)"
  python3 "$repo/tests/harness/frame_stats.py" \
    "$out/paced-$label.frames" "$out/paced-$label.log" \
    --json "$out/paced-$label.json" > "$out/paced-$label.stats" ||
    fail "frame_stats failed for $label"
  cpu=$(awk -F': *' '/Percent of CPU/{printf "%d", $2}' "$out/paced-$label.time")
  rss=$(awk -F': *' '/Maximum resident/{print $2}' "$out/paced-$label.time")
  median=$(python3 -c "import json;print(json.load(open('$out/paced-$label.json'))['median_frame_ms'])")
  p99=$(python3 -c "import json;print(json.load(open('$out/paced-$label.json'))['p99_frame_ms'])")
  printf 'paced %-21s median %6s ms  p99 %7s ms  %3s%% cpu  %6s kb max rss\n' \
    "$label" "$median" "$p99" "$cpu" "$rss" | tee "$out/paced-summary.txt" -a
}

# The scripted workload is the same for every mode: the ladder to level 20
# and the long dwell there, no checkpoints in the way.
for orientation in landscape portrait; do
  script="$repo/build/backgrounds/t085/stress-$orientation.script.txt"
  [ -f "$script" ] || fail "missing $script"
  run_work "$orientation-off" "$script" off 1 "$orientation"
  for design in 17 18 19 20; do
    run_work "$orientation-fixed$design" "$script" fixed "$design" "$orientation"
  done
  run_work "$orientation-auto" "$script" auto 1 "$orientation"
done

for orientation in landscape portrait; do
  script="$repo/build/backgrounds/t085/stress-$orientation.script.txt"
  run_paced "$orientation-off" "$script" off 1 "$orientation"
  for design in 17 18 19 20; do
    run_paced "$orientation-fixed$design" "$script" fixed "$design" "$orientation"
  done
  run_paced "$orientation-auto" "$script" auto 1 "$orientation"
done

echo "perf: PASS (results in $out)"
