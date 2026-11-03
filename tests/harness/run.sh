#!/bin/sh
# Unattended SDL test harness for STACKFALL (docs/AGENT_TESTING.md, T023).
#
# This is the one command that runs the scene cycle unattended:
#
#   tests/harness/run.sh
#
# It builds the game and the headless reference replay, then drives six
# sessions through the real SDL application under the dummy video driver:
# the full scene cycle on the keyboard and on the developer virtual
# controller, a normal planned game from an empty board, and three labeled
# board fixtures (four-line clear, lock-out, block-out). Every session also
# runs through the headless reference; check_harness.py compares the two
# checkpoint streams field by field and adds expectations pinned from the
# specification alone. On any failure the run directory keeps the log, the
# records, and every screenshot as evidence and the script exits nonzero.
# Every game process runs under `timeout` with a frame bound, so a hung
# session cannot outlive the harness.
#
# Options through environment variables:
#   HARNESS_BUILD_DIR  build directory (default: build)
#   HARNESS_RUN_ID     evidence directory name (default: harness-<date>); a
#                      reused name is removed first, so a rerun starts from
#                      an empty run directory and no saved high score leaks
#                      in from the earlier pass
#   HARNESS_VIDEO      SDL_VIDEODRIVER for the game runs (default: dummy)
#   HARNESS_TIMEOUT_S   per-process timeout in seconds (default: 120)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${HARNESS_BUILD_DIR:-$repo/build}
run_id=${HARNESS_RUN_ID:-harness-$(date +%Y-%m-%d-%H%M%S)}
video=${HARNESS_VIDEO:-dummy}
timeout_s=${HARNESS_TIMEOUT_S:-120}
run_dir="$repo/build/agent-checks/$run_id"

fail() {
  echo "harness: $*" >&2
  exit 1
}

# The run id names one directory below build/agent-checks. The rerun
# cleanup below deletes that directory, so the id may not reach outside
# the evidence root.
case "$run_id" in
  */*|""|.|..) fail "HARNESS_RUN_ID must name one directory, not '$run_id'" ;;
esac

# Every child of this script dies with it, including a game process that
# ignored its own frame limit.
children=""
reap() {
  for pid in $children; do
    kill "$pid" 2>/dev/null
  done
}
trap reap EXIT INT TERM

echo "harness: repository $repo"
echo "harness: build $build_dir"
echo "harness: evidence $run_dir"

# ---------------------------------------------------------------------------
# Build. The same directory the desktop build uses; nothing outside build/.
# ---------------------------------------------------------------------------
cmake -S "$repo" -B "$build_dir" -G Ninja >/dev/null ||
  fail "cmake configure failed in $build_dir"
cmake --build "$build_dir" ||
  fail "build failed"
game="$build_dir/stackfall"
reference="$build_dir/stackfall_reference"
checker="$repo/tests/harness/check_harness.py"
scripts="$repo/tests/harness/scripts"
fixtures="$repo/tests/fixtures"
for path in "$game" "$reference" "$checker"; do
  [ -x "$path" ] || [ -f "$path" ] || fail "missing $path"
done

# A reused run id must not inherit the earlier pass's evidence. Its save
# directory still holds that pass's high score, which changes this run's
# title and game-over records and fails the record checks, and its stale
# logs and screenshots would mix two passes. Clear it the way
# tools/design-review/capture.sh clears each session directory.
if [ -e "$run_dir" ]; then
  echo "harness: removing run directory of an earlier pass: $run_dir"
  rm -rf "$run_dir" || fail "cannot remove stale $run_dir"
fi
mkdir -p "$run_dir" || fail "cannot create $run_dir"

# Source identity: hashes of everything the harness exercises, so a run can
# be tied to exact sources without a commit.
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "video driver: $video"
  echo "game binary: $(sha256sum "$game" | cut -d' ' -f1)"
  echo "reference binary: $(sha256sum "$reference" | cut -d' ' -f1)"
  cd "$repo" && sha256sum src/main.cpp src/game.cpp src/fixture.cpp \
    src/observe.cpp include/stackfall/game.hpp include/stackfall/fixture.hpp \
    include/stackfall/observe.hpp tests/harness/run.sh \
    tests/harness/check_harness.py tests/harness/reference_replay.cpp \
    tests/fixtures/*.board.txt tests/harness/scripts/*.script.txt
} > "$run_dir/source.sha256" || fail "cannot record source identity"
cat "$run_dir/source.sha256"

# The headless rules suite stays separate from these platform sessions and
# runs first: a rules failure is not a harness concern.
echo
echo "harness: headless rules suites (ctest)"
if ctest --test-dir "$build_dir" --output-on-failure; then
  echo "harness: rules suites passed"
else
  fail "headless rules suites failed; fix those first"
fi

# ---------------------------------------------------------------------------
# Sessions. Each runs twice: the headless reference computes what the rules
# core must do, then the real SDL application does it through the event path.
# ---------------------------------------------------------------------------
seed=20261103
sessions="scene-cycle-keyboard scene-cycle-pad normal-game-clears fixture-four-line fixture-lockout fixture-blockout"
: > "$run_dir/sessions.txt"
for session in $sessions; do
  echo "$session" >> "$run_dir/sessions.txt"
done

for session in $sessions; do
  echo
  echo "=== session $session ==="
  dir="$run_dir/$session"
  mkdir -p "$dir/shots" "$dir/save"
  script="$scripts/$session.script.txt"
  [ -f "$script" ] || fail "missing script $script"

  echo "reference: $session"
  "$reference" --seed "$seed" --script "$script" \
    --fixture-dir "$fixtures" --out "$run_dir/$session.ref.obs" \
    > "$run_dir/$session.ref.log" 2>&1 ||
    fail "reference replay failed for $session (see $run_dir/$session.ref.log)"

  echo "game:      $session (video driver $video)"
  timeout --foreground "$timeout_s" \
    env SDL_VIDEODRIVER="$video" SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" --tick-per-frame \
    --input-script "$script" --fixture-dir "$fixtures" \
    --save-dir "$dir/save" --screenshot-dir "$dir/shots" \
    --observations "$run_dir/$session.run.obs" \
    --input-log "$dir/input.log" \
    --frames 4000 > "$run_dir/$session.run.log" 2>&1 &
  game_pid=$!
  children="$children $game_pid"
  wait "$game_pid"
  status=$?
  children=${children% "$game_pid"}
  if [ "$status" -ne 0 ]; then
    echo "harness: $session exited $status; evidence in $run_dir" >&2
    echo "exit=$status" > "$run_dir/$session.exit"
    fail "session $session failed"
  fi
  echo "exit=0" > "$run_dir/$session.exit"

  # Repeat the planned normal game once more: the same seed and script must
  # reproduce the same checkpoint records byte for byte.
  if [ "$session" = "normal-game-clears" ]; then
    echo "repeat:    $session (determinism)"
    timeout --foreground "$timeout_s" \
      env SDL_VIDEODRIVER="$video" SDL_AUDIODRIVER=dummy \
      "$game" --seed "$seed" --tick-per-frame \
      --input-script "$script" --fixture-dir "$fixtures" \
      --save-dir "$dir/save-repeat" --screenshot-dir "$dir/shots-repeat" \
      --observations "$run_dir/$session.repeat.obs" \
      --frames 4000 > "$run_dir/$session.repeat.log" 2>&1
    sed 's/ shot=[^ ]*//' "$run_dir/$session.run.obs" > "$run_dir/$session.run.stripped"
    sed 's/ shot=[^ ]*//' "$run_dir/$session.repeat.obs" > "$run_dir/$session.repeat.stripped"
    if cmp -s "$run_dir/$session.run.stripped" "$run_dir/$session.repeat.stripped"; then
      echo "harness: $session repeated byte-identical"
    else
      fail "$session did not repeat identically (compare $session.run.stripped and $session.repeat.stripped in $run_dir)"
    fi
  fi
done

# ---------------------------------------------------------------------------
# Normal startup: no developer option may change it. A bounded plain run has
# no scripts, no observations, and no screenshots, and still shuts down.
# ---------------------------------------------------------------------------
echo
echo "=== normal startup smoke ==="
plain_dir="$run_dir/plain-startup"
mkdir -p "$plain_dir"
timeout --foreground "$timeout_s" \
  env SDL_VIDEODRIVER="$video" SDL_AUDIODRIVER=dummy \
  "$game" --frames 90 > "$plain_dir/run.log" 2>&1
status=$?
echo "exit=$status" > "$plain_dir/exit"
[ "$status" -eq 0 ] || fail "plain startup exited $status"
grep -q "Clean shutdown" "$plain_dir/run.log" ||
  fail "plain startup has no clean shutdown"
grep -q "Harness mode" "$plain_dir/run.log" &&
  fail "plain startup reports harness mode without options"
if [ -n "$(ls -A "$plain_dir" | grep -v 'run.log\|exit')" ]; then
  fail "plain startup left files behind: $(ls -A "$plain_dir")"
fi
echo "harness: plain startup unchanged"

# ---------------------------------------------------------------------------
# The independent checker decides pass or fail.
# ---------------------------------------------------------------------------
echo
echo "=== independent checks ==="
python3 "$checker" "$run_dir"
status=$?

echo
if [ "$status" -eq 0 ]; then
  echo "harness: PASS"
  echo "harness: evidence in $run_dir"
else
  echo "harness: FAIL"
  echo "harness: failure evidence in $run_dir (logs, records, screenshots)"
fi
exit "$status"
