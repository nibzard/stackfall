#!/bin/sh
# Design-review X11 session driver: the smaller letterboxed window
# (docs/DESIGN_REVIEW.md, T027).
#
# Run it inside a virtual X server:
#
#   xvfb-run -a -s "-screen 0 960x544x24" tools/design-review/letterbox-x11.sh
#
# It starts openbox, launches the real application at real-time pacing
# with the design-review-letterbox script, waits for the
# dr-letterbox-ready checkpoint, and resizes the window to 640x480 with
# xdotool, so the 960x544 logical canvas letterboxes. The reviewed frame
# lands in the session's shots directory next to its record.
#
# Options through environment variables:
#   DR_BUILD_DIR    build directory (default: build)
#   DR_RUN_DIR      session directory (default: build/agent-checks/design-review/letterbox)
#   DR_TIMEOUT_S    whole-session timeout in seconds (default: 90)
#   DR_LETTERBOX_WxH  window size the driver resizes to (default: 640x480)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${DR_BUILD_DIR:-$repo/build}
run_dir=${DR_RUN_DIR:-$repo/build/agent-checks/design-review/letterbox}
timeout_s=${DR_TIMEOUT_S:-90}
size=${DR_LETTERBOX_WxH:-640x480}
size_w=${size%%x*}
size_h=${size##*x}
seed=31337

fail() {
  echo "dr-letterbox: $*" >&2
  exit 1
}

for tool in xdotool openbox; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "$tool is not installed; the letterbox capture stays unavailable"
done
game="$build_dir/stackfall"
[ -x "$game" ] || fail "missing $game (build it first)"

mkdir -p "$run_dir/shots" "$run_dir/save" || fail "cannot create $run_dir"
rm -f "$run_dir/letterbox.obs" "$run_dir/input.log" "$run_dir/run.log"

openbox >/dev/null 2>&1 &
wm_pid=$!
sleep 1

echo "dr-letterbox: starting the application (seed $seed, real-time pacing)"
"$game" --seed "$seed" \
  --input-script "$repo/tests/harness/scripts/design-review-letterbox.script.txt" \
  --save-dir "$run_dir/save" --screenshot-dir "$run_dir/shots" \
  --observations "$run_dir/letterbox.obs" \
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
echo "dr-letterbox: window $win"

# Wait for the checkpoint record that says the round is running, then act.
await_record() {
  want=$1
  try=0
  while [ "$try" -lt 400 ]; do
    if [ -f "$run_dir/letterbox.obs" ] &&
       grep -q "check=$want " "$run_dir/letterbox.obs"; then
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

await_record dr-letterbox-ready ||
  fail "session ended before dr-letterbox-ready"
echo "dr-letterbox: resizing the window to ${size_w}x${size_h} (letterbox)"
xdotool windowsize "$win" "$size_w" "$size_h" || fail "windowsize failed"

wait "$game_pid"
status=$?
game_pid=""
trap - EXIT INT TERM
kill "$wm_pid" 2>/dev/null

echo "dr-letterbox: application exited $status"
[ "$status" -eq 0 ] || fail "the application exited $status (see $run_dir/run.log)"
grep -q "Clean shutdown" "$run_dir/run.log" ||
  fail "no clean shutdown in $run_dir/run.log"
[ -f "$run_dir/shots/dr-letterboxed-640x480.bmp" ] ||
  fail "the letterboxed screenshot is missing"
echo "dr-letterbox: PASS (evidence in $run_dir)"
