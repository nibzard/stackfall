#!/bin/sh
# The one command of the integrated background validation (task T085,
# docs/AGENT_TESTING.md): it validates every design of the catalog, the
# saved options, and the rendering cost, end to end.
#
#   tools/backgrounds/t085.sh [phase ...]
#
# Phases, in order; name any subset to rerun just those:
#
#   generate   every scripted session of catalog_gen.py, into the run dir
#   capture    the ladder walks, comparison pairs, orientation and rapid
#              probes through the real SDL application, plus the
#              integrated catalog check with its two thumbnail sheets
#   focus      the X11 focus-loss probe under its own virtual display
#   determinism  one scripted round, four wallpaper modes, byte-identical
#              observation streams
#   legacy     Background Off against the pre-backgrounds revision cd54ba2,
#              frame for frame
#   perf       the draw counts, work-mode timings, and paced frame-time
#              percentiles against Off (a Release build in build-rel)
#   gates      the CTest suites of the Debug and Release builds and the
#              full unattended harness
#
# Environment:
#   T085_BUILD_DIR   Debug build directory (default: build)
#   T085_RUN_DIR     evidence directory (default: build/backgrounds/t085)
#   SKIP_CAPTURE=1   reuse the captures already in the run dir
#   xvfb-run, openbox, and xdotool must be installed for the focus phase.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
here="$repo/tools/backgrounds"
build_dir=${T085_BUILD_DIR:-$repo/build}
run_dir=${T085_RUN_DIR:-$repo/build/backgrounds/t085}
seed=31337

fail() {
  echo "t085: $*" >&2
  exit 1
}

cd "$repo"
mkdir -p "$run_dir"

# ---------------------------------------------------------------------------
phase_generate() {
  echo "=== generate ==="
  for name in catalog-landscape catalog-portrait pair orientation \
              rapid-modes stress-landscape stress-portrait det-game focus; do
    python3 "$here/catalog_gen.py" "$name" "$run_dir/$name.script.txt" ||
      fail "catalog_gen failed for $name"
  done
}

# One capture session: the staged settings decide everything the script
# leaves alone - the mode, the design, grayscale, the orientation. The
# `background` staging is one of:
#   none        no settings file at all: the fresh defaults, Auto design 1
#   legacy      the legacy form without background fields: Auto as well
#   auto|off|fixed  the saved options of task T084
capture() {
  name=$1 script=$2 orientation=$3 background=$4 design=$5 grayscale=$6
  dir=${7:-$name}
  [ "${SKIP_CAPTURE:-0}" = 1 ] && [ -d "$run_dir/$dir" ] && {
    echo "capture: reusing $name"
    return 0
  }
  session="$run_dir/$dir"
  rm -rf "$session"
  mkdir -p "$session/save" "$session/shots"
  case "$background" in
    none) fields="" ;;
    legacy) fields="" ;;
    *) fields="background=$background
backgroundid=$design
" ;;
  esac
  if [ "$background" != none ]; then
    printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=%s\norientation=%s\nmusic=auto\nmusicvolume=6\neffectsvolume=8\n%s' \
      "$grayscale" "$orientation" "$fields" > "$session/save/settings.dat"
  fi
  timeout --foreground 600 \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$build_dir/stackfall" --seed "$seed" --tick-per-frame \
    --input-script "$run_dir/$script.script.txt" \
    --fixture-dir "$repo/tests/fixtures" \
    --save-dir "$session/save" --screenshot-dir "$session/shots" \
    --observations "$session/observations.txt" \
    --frames 12000 > "$session/session.log" 2>&1 ||
    fail "the capture session $name failed (see $session/session.log)"
  echo "capture: $name -> $dir ($(ls "$session/shots" | wc -l) frames)"
}

phase_capture() {
  echo "=== capture ==="
  cmake --build "$build_dir" --target stackfall >/dev/null ||
    fail "build failed in $build_dir"
  mkdir -p "$run_dir/catalog"
  # The two ladder walks: landscape from a fresh save (Auto default),
  # portrait from the legacy settings form without background fields,
  # which must land on Auto as well. They land under catalog/<half>
  # because catalog_check.py reads them there beside the pair, orientation,
  # and rapid sessions.
  capture catalog-landscape catalog-landscape landscape none 1 off \
    catalog/landscape
  capture catalog-portrait catalog-portrait portrait legacy 1 off \
    catalog/portrait
  # The comparison pairs: Fixed 17 and 20, each with grayscale on and off,
  # and Off itself.
  capture pair-fixed17 pair landscape fixed 17 off
  capture pair-fixed17-gray pair landscape fixed 17 on
  capture pair-fixed20 pair landscape fixed 20 off
  capture pair-fixed20-gray pair landscape fixed 20 on
  capture pair-off pair landscape off 20 off
  # The orientation continuity probe starts landscape and turns portrait
  # three times; the rapid probe drives the rings from a fresh save.
  capture orientation orientation landscape fixed 20 off
  capture rapid-modes rapid-modes landscape none 1 off
  echo "t085: the integrated catalog check"
  python3 "$here/catalog_check.py" "$run_dir" ||
    fail "the catalog check failed"
}

phase_focus() {
  echo "=== focus ==="
  python3 "$here/catalog_gen.py" focus "$run_dir/focus.script.txt" ||
    fail "catalog_gen failed for focus"
  T085_RUN_DIR="$run_dir" xvfb-run -a -s "-screen 0 960x544x24" \
    "$here/focus_session.sh" ||
    fail "the focus session failed"
  python3 "$here/focus_check.py" "$run_dir/focus" ||
    fail "the focus check failed"
}

phase_determinism() {
  echo "=== determinism ==="
  T085_BUILD_DIR="$build_dir" T085_RUN_DIR="$run_dir" \
    "$here/determinism_runs.sh" ||
    fail "the determinism comparison failed"
}

phase_legacy() {
  echo "=== legacy ==="
  "$here/legacy_off_compare.sh" || fail "the legacy comparison failed"
}

phase_perf() {
  echo "=== perf ==="
  "$here/perf_runs.sh" || fail "the performance runs failed"
}

phase_gates() {
  echo "=== gates ==="
  cmake --build "$build_dir" >/dev/null || fail "build failed in $build_dir"
  ctest --test-dir "$build_dir" --output-on-failure ||
    fail "the Debug CTest suite failed"
  cmake -S "$repo" -B "$repo/build-rel" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release >/dev/null ||
    fail "cmake configure failed in build-rel"
  cmake --build "$repo/build-rel" >/dev/null ||
    fail "build failed in build-rel"
  ctest --test-dir "$repo/build-rel" --output-on-failure ||
    fail "the Release CTest suite failed"
  HARNESS_BUILD_DIR="$build_dir" tests/harness/run.sh ||
    fail "the unattended harness failed"
}

phases="${*:-generate capture focus determinism legacy perf gates}"
for phase in $phases; do
  case "$phase" in
    generate|capture|focus|determinism|legacy|perf|gates) ;;
    *) fail "unknown phase '$phase'" ;;
  esac
done
for phase in $phases; do
  "phase_$phase" || exit 1
done
echo "t085: all phases passed"
