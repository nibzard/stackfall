#!/bin/sh
# T024 X11 session driver: focus loss and the letterboxed window
# (docs/AGENT_TESTING.md, T024 item 5 and item 3).
#
# Run it inside a virtual X server:
#
#   xvfb-run -a -s "-screen 0 960x544x24" tests/harness/t024-x11.sh
#
# It starts openbox, launches the real SDL application at real-time pacing
# with the t024-focus-letterbox script, then drives the window manager the
# way a user would: it minimizes the window while the script holds Left,
# restores it, and resizes it to 640x480 so the 960x544 logical canvas is
# letterboxed. The application's checkpoint records, screenshots, and input
# log land in the run directory; check_t024.py asserts the behavior.
#
# Options through environment variables:
#   T024_BUILD_DIR   build directory (default: build)
#   T024_RUN_DIR     evidence directory (default: build/agent-checks/t024-x11)
#   T024_TIMEOUT_S   whole-session timeout in seconds (default: 90)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T024_BUILD_DIR:-$repo/build}
run_dir=${T024_RUN_DIR:-$repo/build/agent-checks/t024-x11}
timeout_s=${T024_TIMEOUT_S:-90}
seed=31337

fail() {
  echo "t024-x11: $*" >&2
  exit 1
}

for tool in xdotool openbox; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "$tool is not installed; the focus-loss check stays unavailable"
done
game="$build_dir/stackfall"
[ -x "$game" ] || fail "missing $game (build it first)"

mkdir -p "$run_dir/shots" "$run_dir/save" || fail "cannot create $run_dir"
rm -f "$run_dir/observations.obs" "$run_dir/input.log" "$run_dir/run.log"

openbox >/dev/null 2>&1 &
wm_pid=$!
sleep 1

echo "t024-x11: starting the application (seed $seed, real-time pacing)"
"$game" --seed "$seed" \
  --input-script "$repo/tests/harness/scripts/t024-focus-letterbox.script.txt" \
  --save-dir "$run_dir/save" --screenshot-dir "$run_dir/shots" \
  --observations "$run_dir/observations.obs" \
  --input-log "$run_dir/input.log" \
  --frames 4000 > "$run_dir/run.log" 2>&1 &
game_pid=$!

cleanup() {
  kill "$game_pid" 2>/dev/null
  kill "$wm_pid" 2>/dev/null
}
trap cleanup EXIT INT TERM

# The window appears once the title screen renders.
win=""
try=0
while [ -z "$win" ] && [ "$try" -lt 100 ]; do
  win=$(xdotool search --onlyvisible --name '^STACKFALL$' 2>/dev/null | head -1)
  try=$((try + 1))
  sleep 0.1
done
[ -n "$win" ] || fail "no STACKFALL window appeared"
echo "t024-x11: window $win"

# Wait for one checkpoint record by name, then act. The application
# flushes every record the moment it writes it.
await_record() {
  want=$1
  try=0
  while [ "$try" -lt 400 ]; do
    if [ -f "$run_dir/observations.obs" ] &&
       grep -q "check=$want " "$run_dir/observations.obs"; then
      return 0
    fi
    if ! kill -0 "$game_pid" 2>/dev/null; then
      return 1
    fi
    try=$((try + 1))
    sleep 0.05
  done
  return 1
}

await_record before-minimize || fail "session ended before before-minimize"
sleep 0.2
echo "t024-x11: minimizing while Left is held"
xdotool windowminimize "$win" || fail "windowminimize failed"

await_record focus-lost-frozen || fail "session ended before focus-lost-frozen"
echo "t024-x11: restoring the window"
xdotool windowactivate --sync "$win" || fail "windowactivate failed"

await_record refocus-repress-moves ||
  fail "session ended before refocus-repress-moves"
echo "t024-x11: resizing the window to 640x480 (letterbox)"
xdotool windowsize "$win" 640 480 || fail "windowsize failed"

wait "$game_pid"
status=$?
game_pid=""
trap - EXIT INT TERM
kill "$wm_pid" 2>/dev/null

echo "t024-x11: application exited $status"
[ "$status" -eq 0 ] || fail "the application exited $status (see $run_dir/run.log)"
grep -q "Clean shutdown" "$run_dir/run.log" ||
  fail "no clean shutdown in $run_dir/run.log"
grep -q "Window focus lost; every held button cleared" "$run_dir/run.log" ||
  fail "the log lacks the focus-loss line"
echo "t024-x11: PASS (evidence in $run_dir)"
