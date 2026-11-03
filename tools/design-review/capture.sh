#!/bin/sh
# Design-review capture driver (docs/DESIGN_REVIEW.md, T027).
#
# One command captures the review packet from the real SDL application:
#
#   tools/design-review/capture.sh
#
# It builds the game, records the source identity, runs the headless
# rules suites, then drives four sessions on top of the T023 harness:
# the scene and HUD session, the line-clear sequence, the tall stack,
# and the smaller letterboxed window under Xvfb. Checkpoints convert to
# PNG under build/agent-checks/design-review/<run-id>/packet/images/
# with a manifest that pairs every image with its observation record.
# The packet is the input of tools/design-review/review.sh.
#
# Options through environment variables:
#   DR_BUILD_DIR   build directory (default: build)
#   DR_RUN_ID      packet run identifier (default: <date>)
#   DR_TIMEOUT_S   per-process timeout in seconds (default: 120)
#   DR_SKIP_X11    set to 1 to skip the letterbox session (the packet
#                  then lacks its letterbox image and cannot be built)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${DR_BUILD_DIR:-$repo/build}
run_id=${DR_RUN_ID:-$(date +%Y-%m-%d-%H%M%S)}
timeout_s=${DR_TIMEOUT_S:-120}
run_dir="$repo/build/agent-checks/design-review/$run_id"
tools="$repo/tools/design-review"

fail() {
  echo "dr-capture: $*" >&2
  exit 1
}

# Every child of this script dies with it, including a game process that
# ignored its own frame limit.
children=""
reap() {
  for pid in $children; do
    kill "$pid" 2>/dev/null
  done
}
trap reap EXIT INT TERM

echo "dr-capture: repository $repo"
echo "dr-capture: build $build_dir"
echo "dr-capture: packet $run_dir"

cmake -S "$repo" -B "$build_dir" -G Ninja >/dev/null ||
  fail "cmake configure failed in $build_dir"
cmake --build "$build_dir" || fail "build failed"
game="$build_dir/stackfall"
reference="$build_dir/stackfall_reference"
for path in "$game" "$reference"; do
  [ -x "$path" ] || fail "missing $path"
done

mkdir -p "$run_dir" || fail "cannot create $run_dir"

# Source identity: the reviewed images must tie back to exact sources.
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "commit: $(git -C "$repo" rev-parse HEAD 2>/dev/null || echo none)"
  echo "game binary: $(sha256sum "$game" | cut -d' ' -f1)"
  echo "reference binary: $(sha256sum "$reference" | cut -d' ' -f1)"
  cd "$repo" && sha256sum docs/SPEC.md docs/DESIGN_REVIEW.md \
    tools/design-review/capture.sh tools/design-review/letterbox-x11.sh \
    tools/design-review/make_packet.py tools/design-review/packet.tsv \
    tests/harness/scripts/design-review-*.script.txt \
    tests/fixtures/*.board.txt src/main.cpp src/renderer_sdl.cpp \
    include/stackfall/renderer_sdl.hpp
} > "$run_dir/source.sha256" || fail "cannot record source identity"
cat "$run_dir/source.sha256"

echo
echo "dr-capture: headless rules suites (ctest)"
ctest --test-dir "$build_dir" --output-on-failure ||
  fail "headless rules suites failed; fix those first"

# ---------------------------------------------------------------------------
# The three dummy-driver sessions from the packet table. The letterbox
# session runs last, under a real X server.
# ---------------------------------------------------------------------------
sessions=$(grep -v '^#' "$tools/packet.tsv" | grep '^session|' |
  grep -v '|x11|' | cut -d'|' -f2)
[ -n "$sessions" ] || fail "no dummy sessions in $tools/packet.tsv"

for session in $sessions; do
  seed=$(grep "^session|$session|" "$tools/packet.tsv" | cut -d'|' -f3)
  script=$(grep "^session|$session|" "$tools/packet.tsv" | cut -d'|' -f6)
  echo
  echo "=== session $session (seed $seed) ==="
  dir="$run_dir/$session"
  # A reused run id must not carry a stale save: the high score of an
  # earlier pass would change this pass's records and title frame.
  rm -rf "$dir"
  mkdir -p "$dir/shots" "$dir/save"
  [ -f "$repo/$script" ] || fail "missing script $script"
  timeout --foreground "$timeout_s" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" --tick-per-frame \
    --input-script "$repo/$script" --fixture-dir "$repo/tests/fixtures" \
    --save-dir "$dir/save" --screenshot-dir "$dir/shots" \
    --observations "$run_dir/$session.obs" \
    --input-log "$dir/input.log" \
    --frames 2000 > "$run_dir/$session.log" 2>&1 &
  game_pid=$!
  children="$children $game_pid"
  wait "$game_pid"
  status=$?
  children=${children% "$game_pid"}
  [ "$status" -eq 0 ] || fail "session $session exited $status; see $run_dir/$session.log"
  grep -q "Clean shutdown" "$run_dir/$session.log" ||
    fail "session $session has no clean shutdown"
  echo "exit=0" > "$run_dir/$session.exit"
done

# ---------------------------------------------------------------------------
echo
session=letterbox
if [ "${DR_SKIP_X11:-0}" = "1" ]; then
  echo "=== session letterbox skipped (DR_SKIP_X11=1) ==="
  echo "unavailable: skipped by DR_SKIP_X11" > "$run_dir/$session.status"
elif command -v xvfb-run >/dev/null 2>&1 &&
     command -v xdotool >/dev/null 2>&1 &&
     command -v openbox >/dev/null 2>&1; then
  echo "=== session letterbox (X11, 640x480) ==="
  rm -rf "$run_dir/$session"
  mkdir -p "$run_dir/$session"
  if DR_BUILD_DIR="$build_dir" DR_RUN_DIR="$run_dir/$session" \
     DR_TIMEOUT_S="$timeout_s" \
     xvfb-run -a -s "-screen 0 960x544x24" \
     "$tools/letterbox-x11.sh" > "$run_dir/$session.driver.log" 2>&1; then
    echo "passed" > "$run_dir/$session.status"
    # The observation file and shots land in the session directory itself.
    mv "$run_dir/$session/letterbox.obs" "$run_dir/$session.obs" 2>/dev/null
  else
    status=$?
    echo "failed ($status)" > "$run_dir/$session.status"
    fail "the letterbox session failed; see $run_dir/$session.driver.log"
  fi
else
  echo "=== session letterbox unavailable (no xvfb-run, xdotool, or openbox) ==="
  echo "unavailable: xvfb-run, xdotool, or openbox missing" > "$run_dir/$session.status"
  fail "the letterbox image is required; install xvfb-run, xdotool, and openbox"
fi

# ---------------------------------------------------------------------------
echo
echo "=== packet ==="
commit=$(git -C "$repo" rev-parse HEAD 2>/dev/null || echo "")
python3 "$tools/make_packet.py" --repo "$repo" --run-dir "$run_dir" \
  --round initial --commit "$commit" ||
  fail "the packet could not be built"

echo
echo "dr-capture: PASS"
echo "dr-capture: packet in $run_dir/packet"
echo "dr-capture: review it with tools/design-review/review.sh"
exit 0
