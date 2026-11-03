#!/bin/sh
# T024 agent play and inspection driver (docs/AGENT_TESTING.md, T024).
#
# One command replays the T024 play sessions through the real SDL
# application on top of the T023 harness:
#
#   tests/harness/t024.sh
#
# Four deterministic sessions run under the dummy video driver with
# --tick-per-frame, each twice - once through the headless reference and
# once through the application - so the checker can compare the two
# record streams field by field:
#
#   t024-shapes-keyboard  seed 31337  every shape on the keyboard, with
#                                        spawn frames, kicks, hold, pause,
#                                        restart-while-held, mute, and the
#                                        full scene cycle
#   t024-shapes-pad       seed 20261104 the same coverage on the virtual
#                                        controller plus a mid-game detach
#   t024-i-spawn          seed 777      the hidden I-piece spawn delay
#   t024-clears-23        seed 31337    the double and triple clears from
#                                        labeled fixtures
#
# A fifth session runs under a real X server when the tools exist, at
# real-time pacing, and checks focus loss plus the letterboxed smaller
# window:
#
#   xvfb-run -a -s "-screen 0 960x544x24" tests/harness/t024.sh
#
# (the driver starts it itself when DISPLAY is already served by Xvfb, or
# wraps it in xvfb-run when that exists)
#
# Options through environment variables:
#   T024_BUILD_DIR   build directory (default: build)
#   T024_RUN_ID      evidence directory name (default: t024-<date>); a reused
#                    name is removed first, so a rerun starts from an empty
#                    run directory and no saved high score leaks in from the
#                    earlier pass
#   T024_TIMEOUT_S   per-process timeout in seconds (default: 120)
#   T024_SKIP_X11    set to 1 to skip the X11 session

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T024_BUILD_DIR:-$repo/build}
run_id=${T024_RUN_ID:-t024-$(date +%Y-%m-%d-%H%M%S)}
timeout_s=${T024_TIMEOUT_S:-120}
run_dir="$repo/build/agent-checks/$run_id"

fail() {
  echo "t024: $*" >&2
  exit 1
}

# The run id names one directory below build/agent-checks. The rerun
# cleanup below deletes that directory, so the id may not reach outside
# the evidence root.
case "$run_id" in
  */*|""|.|..) fail "T024_RUN_ID must name one directory, not '$run_id'" ;;
esac

children=""
reap() {
  for pid in $children; do
    kill "$pid" 2>/dev/null
  done
}
trap reap EXIT INT TERM

echo "t024: repository $repo"
echo "t024: build $build_dir"
echo "t024: evidence $run_dir"

cmake -S "$repo" -B "$build_dir" -G Ninja >/dev/null ||
  fail "cmake configure failed in $build_dir"
cmake --build "$build_dir" || fail "build failed"
game="$build_dir/stackfall"
reference="$build_dir/stackfall_reference"
checker="$repo/tests/harness/check_t024.py"
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
  echo "t024: removing run directory of an earlier pass: $run_dir"
  rm -rf "$run_dir" || fail "cannot remove stale $run_dir"
fi
mkdir -p "$run_dir" || fail "cannot create $run_dir"

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "game binary: $(sha256sum "$game" | cut -d' ' -f1)"
  echo "reference binary: $(sha256sum "$reference" | cut -d' ' -f1)"
  cd "$repo" && sha256sum tests/harness/t024.sh tests/harness/t024-x11.sh \
    tests/harness/check_t024.py tests/harness/reference_replay.cpp \
    tests/fixtures/*.board.txt tests/harness/scripts/t024-*.script.txt
} > "$run_dir/source.sha256" || fail "cannot record source identity"
cat "$run_dir/source.sha256"

echo
echo "t024: headless rules suites (ctest)"
ctest --test-dir "$build_dir" --output-on-failure ||
  fail "headless rules suites failed; fix those first"

# name seed
sessions="t024-shapes-keyboard:31337 t024-shapes-pad:20261104 t024-i-spawn:777 t024-clears-23:31337"
: > "$run_dir/sessions.txt"

for entry in $sessions; do
  session=${entry%%:*}
  seed=${entry##*:}
  echo "$session" >> "$run_dir/sessions.txt"
  echo "$seed" > "$run_dir/$session.seed"
  echo
  echo "=== session $session (seed $seed) ==="
  dir="$run_dir/$session"
  mkdir -p "$dir/shots" "$dir/save"
  script="$scripts/$session.script.txt"
  [ -f "$script" ] || fail "missing script $script"

  echo "reference: $session"
  "$reference" --seed "$seed" --script "$script" \
    --fixture-dir "$fixtures" --out "$run_dir/$session.ref.obs" \
    > "$run_dir/$session.ref.log" 2>&1 ||
    fail "reference replay failed for $session (see $run_dir/$session.ref.log)"

  echo "game:      $session"
  timeout --foreground "$timeout_s" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
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
  [ "$status" -eq 0 ] || fail "session $session exited $status; see $run_dir/$session.run.log"
  echo "exit=0" > "$run_dir/$session.exit"

  if [ "$session" = "t024-shapes-keyboard" ]; then
    echo "repeat:    $session (determinism)"
    timeout --foreground "$timeout_s" \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$game" --seed "$seed" --tick-per-frame \
      --input-script "$script" --fixture-dir "$fixtures" \
      --save-dir "$dir/save-repeat" --screenshot-dir "$dir/shots-repeat" \
      --observations "$run_dir/$session.repeat.obs" \
      --frames 4000 > "$run_dir/$session.repeat.log" 2>&1
    sed 's/ shot=[^ ]*//' "$run_dir/$session.run.obs" > "$run_dir/$session.run.stripped"
    sed 's/ shot=[^ ]*//' "$run_dir/$session.repeat.obs" > "$run_dir/$session.repeat.stripped"
    cmp -s "$run_dir/$session.run.stripped" "$run_dir/$session.repeat.stripped" ||
      fail "$session did not repeat identically"
    echo "t024: $session repeated byte-identical"
  fi
done

# ---------------------------------------------------------------------------
# The X11 session: focus loss and the letterboxed smaller window. It needs
# a display server with a window manager; xvfb-run supplies one when the
# host has it. Recorded unavailable otherwise.
# ---------------------------------------------------------------------------
echo
if [ "${T024_SKIP_X11:-0}" = "1" ]; then
  echo "=== x11 session skipped (T024_SKIP_X11=1) ==="
  echo "unavailable: skipped by T024_SKIP_X11" > "$run_dir/t024-focus-letterbox.x11.status"
elif command -v xvfb-run >/dev/null 2>&1 &&
     command -v xdotool >/dev/null 2>&1 &&
     command -v openbox >/dev/null 2>&1; then
  echo "=== session t024-focus-letterbox (X11) ==="
  mkdir -p "$run_dir/t024-focus-letterbox"
  if T024_BUILD_DIR="$build_dir" \
     T024_RUN_DIR="$run_dir/t024-focus-letterbox" \
     T024_TIMEOUT_S="$timeout_s" \
     xvfb-run -a -s "-screen 0 960x544x24" \
     "$repo/tests/harness/t024-x11.sh" > "$run_dir/t024-focus-letterbox.driver.log" 2>&1; then
    echo "passed" > "$run_dir/t024-focus-letterbox.x11.status"
  else
    status=$?
    echo "failed ($status)" > "$run_dir/t024-focus-letterbox.x11.status"
    fail "the X11 session failed; see $run_dir/t024-focus-letterbox.driver.log"
  fi
else
  echo "=== x11 session unavailable (no xvfb-run, xdotool, or openbox) ==="
  echo "unavailable: xvfb-run, xdotool, or openbox missing" \
    > "$run_dir/t024-focus-letterbox.x11.status"
fi

# ---------------------------------------------------------------------------
echo
echo "=== independent checks ==="
python3 "$checker" "$run_dir"
status=$?

echo
if [ "$status" -eq 0 ]; then
  echo "t024: PASS"
  echo "t024: evidence in $run_dir"
else
  echo "t024: FAIL"
  echo "t024: failure evidence in $run_dir"
fi
exit "$status"
