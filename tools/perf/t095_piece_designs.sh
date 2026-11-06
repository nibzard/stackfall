#!/bin/sh
# T095 per-piece design cost measurements: the same scripted workload under
# PIECE DESIGNS Off and On, both orientations, plus the pre-task baseline
# build of HEAD for the default path (notes/performance.md).
#
# Phases:
#   work      the stress script at --tick-per-frame: wall time is pure
#             work, so the On delta against Off is the mark cost; every
#             variant repeats PERF_REPEAT times and the report keeps the
#             minimum and the median of each
#   baseline  the same Off runs on a build of HEAD (the commit the
#             worktree started from), the default-path no-regression
#             number
#   paced     the same scripts at real-time pacing with --frame-times,
#             bounded to PERF_FRAMES frames: frame_stats.py percentiles
#             (a desktop pacing profile, never an RG503 claim)
#
# The workload tops the board out with the near-full perf-stack fixture and
# then draws the frozen 110-cell board to the last frame, so the settled
# cells - the cells the marks draw on - dominate every drawn frame. No
# rotations: a rotation that lifts a resting piece clears the lock streak
# without spending the reset budget (SPEC section 4.5), so a rotate-heavy
# script spins one piece at the floor forever and settles nothing.
#
# Environment:
#   PERF_FRAMES   paced frame bound (default 1800, thirty seconds)
#   PERF_REPEAT   work repetitions per variant (default 5)
#
# Results land under build/perf/t095, one text file per run.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
rel="$repo/build-rel"
out="$repo/build/perf/t095"
frames=${PERF_FRAMES:-1800}
repeat=${PERF_REPEAT:-5}
seed=31337
base="$out/baseline-src"

fail() {
  echo "perf: $*" >&2
  exit 1
}

echo "perf: configuring the Release build in $rel"
cmake -S "$repo" -B "$rel" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null ||
  fail "cmake configure failed in $rel"
cmake --build "$rel" >/dev/null ||
  fail "cmake build failed"
game="$rel/stackfall"
[ -x "$game" ] || fail "missing $game"
mkdir -p "$out"
rm -f "$out"/*.txt "$out"/*.json "$out"/*.obs "$out"/*.log
rm -rf "$out/work-"* "$out/paced-"*

# The pre-task baseline: an export of HEAD, the commit the worktree started
# from, built beside the candidate. Nothing of the working tree leaks in, and
# no ref moves.
baseline_game="$out/baseline-build/stackfall"
if [ ! -x "$baseline_game" ]; then
  echo "perf: building the HEAD baseline in $out/baseline-build"
  rm -rf "$base" "$out/baseline-build"
  mkdir -p "$base" "$out/baseline-build" ||
    fail "cannot create the baseline directories"
  git -C "$repo" archive HEAD | tar -x -C "$base" ||
    fail "cannot export HEAD for the baseline"
  cmake -S "$base" -B "$out/baseline-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release >/dev/null ||
    fail "cmake configure failed for the baseline"
  cmake --build "$out/baseline-build" >/dev/null ||
    fail "cmake build failed for the baseline"
fi
[ -x "$baseline_game" ] || fail "missing $baseline_game"

# ---------------------------------------------------------------------------
# The near-full workload board: ninety settled cells of every piece type in
# ten rows, gaps staggered so no row is full, spawn rows free. The round
# that loads it tops out within two hundred ticks and then draws the frozen
# board - plus the active piece, the ghost, and the six previews while the
# round lives - to the last frame.
{
  echo "# stackfall board fixture v1"
  echo "# A near-full stack for the T095 performance workload: ninety"
  echo "# settled cells of every piece type, no full row, free spawn rows."
  echo "name: perf-stack"
  echo "description: ten rows of nine cells each, gaps staggered, all seven piece types"
  echo "provenance: hand-built for the T095 perf driver (tools/perf/t095_piece_designs.sh); the round that loads it tops out in a few pieces, so the frozen board draws to the end of the run"
  for y in 1 2 3 4 5 6 7 8 9 10 11 12; do
    echo ".........."
  done
  for y in 13 14 15 16 17 18 19 20 21 22; do
    if [ $((y % 2)) -eq 1 ]; then
      echo "IOTSZJLIO."
    else
      echo ".IOTSZJLIO"
    fi
  done
} > "$out/perf-stack.board.txt"

# The stress scripts: Enter starts the round, the fixture stacks the board,
# and soft-drop presses finish it. Portrait names its own soft-drop key -
# Right in the turned default table, where Down moves left (bindings.cpp) -
# so both orientations drive the same falling piece. A checkpoint every 100
# cycles lands in the observation stream, so the evidence names the scene
# and the settled count the run drew.
write_stress() {
  path=$1 soft=$2
  {
    echo "# T095 stress workload, seed $seed: top-out over perf-stack, then"
    echo "# the frozen board draws to the last frame."
    echo "key enter down"
    echo "wait 2"
    echo "key enter up"
    echo "wait 2"
    echo "fixture perf-stack"
    echo "wait 2"
    cycle=0
    while [ "$cycle" -lt 600 ]; do
      printf 'key %s down\nwait 1\nkey %s up\nwait 1\n' "$soft" "$soft"
      if [ $((cycle % 100)) -eq 99 ]; then
        printf 'check c%d\n' "$cycle"
      fi
      cycle=$((cycle + 1))
    done
  } > "$path"
}
write_stress "$out/stress-landscape.script.txt" down
write_stress "$out/stress-portrait.script.txt" right

# One staged settings file per run. The baseline tree reads the thirteen-line
# form of its own era; the candidate reads the same lines plus piecedesigns.
stage_save() {
  dir=$1 orientation=$2 designs=$3
  rm -rf "$dir"
  mkdir -p "$dir/save" "$dir/shots"
  printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=%s\nmusic=auto\nmusicvolume=6\neffectsvolume=8\nbackground=auto\nbackgroundid=1\n' \
    "$orientation" > "$dir/save/settings.dat"
  if [ "$designs" = on ]; then
    printf 'piecedesigns=on\n' >> "$dir/save/settings.dat"
  fi
}

run_work_once() {
  label=$1 binary=$2 orientation=$3 designs=$4 pass=$5
  stage_save "$out/work-$label" "$orientation" "$designs"
  start=$(date +%s%N)
  /usr/bin/time -v -o "$out/work-$label-$pass.time" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$binary" --seed "$seed" --tick-per-frame \
    --input-script "$out/stress-$orientation.script.txt" --fixture-dir "$out" \
    --save-dir "$out/work-$label/save" --screenshot-dir "$out/work-$label/shots" \
    --observations "$out/work-$label-$pass.obs" \
    --frames 1300 > "$out/work-$label-$pass.log" 2>&1 ||
    fail "work run $label pass $pass failed (see $out/work-$label-$pass.log)"
  end=$(date +%s%N)
  ms=$(( (end - start) / 1000000 ))
  echo "$ms"
}

run_work() {
  label=$1 binary=$2 orientation=$3 designs=$4
  times=""
  pass=1
  while [ "$pass" -le "$repeat" ]; do
    times="$times $(run_work_once "$label" "$binary" "$orientation" \
      "$designs" "$pass")"
    pass=$((pass + 1))
  done
  best=$(printf '%s\n' $times | sort -n | head -1)
  median=$(printf '%s\n' $times | sort -n |
    awk '{a[NR]=$1} END {print (NR%2==1)?a[(NR+1)/2]:int((a[NR/2]+a[NR/2+1])/2)}')
  last=$(tail -1 "$out/work-$label-$repeat.obs")
  last_scene=$(printf '%s\n' "$last" | sed -n 's/.*check=[a-z0-9]* scene=\([a-z]*\).*/\1/p')
  last_settled=$(printf '%s\n' "$last" | sed -n 's/.* settled=\([0-9]*\).*/\1/p')
  over=$(grep -c 'Scene playing -> gameover' "$out/work-$label-$repeat.log" ||
    true)
  printf 'work %-17s min %4d ms  median %4d ms  (%s runs)  gameover=%s last=%s/%s\n' \
    "$label" "$best" "$median" "$repeat" "$over" "$last_scene" \
    "$last_settled" | tee -a "$out/work-summary.txt"
}

run_paced() {
  label=$1 orientation=$2 designs=$3
  stage_save "$out/paced-$label" "$orientation" "$designs"
  /usr/bin/time -v -o "$out/paced-$label.time" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" \
    --input-script "$out/stress-$orientation.script.txt" --fixture-dir "$out" \
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
  printf 'paced %-16s median %6s ms  p99 %7s ms  %3s%% cpu  %6s kb max rss\n' \
    "$label" "$median" "$p99" "$cpu" "$rss" | tee -a "$out/paced-summary.txt"
}

for orientation in landscape portrait; do
  run_work "$orientation-off" "$game" "$orientation" off
  run_work "$orientation-on" "$game" "$orientation" on
  run_work "$orientation-head" "$baseline_game" "$orientation" off
done

for orientation in landscape portrait; do
  run_paced "$orientation-off" "$orientation" off
  run_paced "$orientation-on" "$orientation" on
done

echo "perf: PASS (results in $out)"
