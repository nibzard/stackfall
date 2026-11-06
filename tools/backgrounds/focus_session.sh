#!/bin/sh
# T085 focus-loss session driver: the wallpaper clock under a real window
# manager (docs/AGENT_TESTING.md, task T085).
#
# Run it inside a virtual X server:
#
#   xvfb-run -a -s "-screen 0 960x544x24" tools/backgrounds/focus_session.sh
#
# It starts openbox, stages Fixed design 20 in the save directory, launches
# the real SDL application at real-time pacing with the generated focus
# probe script, and drives the window the way a user would: it minimizes
# the window after the first checkpoint and restores it after the second.
# focus_check.py then proves the wallpaper froze while the window held no
# focus and resumed without a jump.
#
# Options through environment variables:
#   T085_BUILD_DIR   build directory (default: build)
#   T085_RUN_DIR     evidence directory (default: build/backgrounds/t085)
#   T085_TIMEOUT_S   whole-session timeout in seconds (default: 90)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T085_BUILD_DIR:-$repo/build}
run_dir=${T085_RUN_DIR:-$repo/build/backgrounds/t085}
timeout_s=${T085_TIMEOUT_S:-90}
seed=31337

fail() {
  echo "focus: $*" >&2
  exit 1
}

for tool in xdotool openbox; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "$tool is not installed; the focus-loss check stays unavailable"
done
game="$build_dir/stackfall"
[ -x "$game" ] || fail "missing $game (build it first)"
script="$run_dir/focus.script.txt"
[ -f "$script" ] ||
  fail "missing $script (python3 tools/backgrounds/catalog_gen.py focus $script)"

session="$run_dir/focus"
rm -rf "$session"
mkdir -p "$session/shots" "$session/save" || fail "cannot create $session"

# Fixed design 20: the fastest drift in the catalog, so a clock that kept
# running through the focus loss cannot hide.
printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\neffectsvolume=8\nbackground=fixed\nbackgroundid=20\n' \
  > "$session/save/settings.dat"

openbox >/dev/null 2>&1 &
wm_pid=$!
sleep 1

echo "focus: starting the application (seed $seed, real-time pacing)"
"$game" --seed "$seed" \
  --input-script "$script" \
  --save-dir "$session/save" --screenshot-dir "$session/shots" \
  --observations "$session/observations.obs" \
  --input-log "$session/input.log" \
  --frames 4000 > "$session/run.log" 2>&1 &
game_pid=$!

cleanup() {
  kill "$game_pid" 2>/dev/null
  kill "$wm_pid" 2>/dev/null
}
trap cleanup EXIT INT TERM

win=""
try=0
while [ -z "$win" ] && [ "$try" -lt 100 ]; do
  win=$(xdotool search --onlyvisible --name '^STACKFALL$' 2>/dev/null | head -1)
  try=$((try + 1))
  sleep 0.1
done
[ -n "$win" ] || fail "no STACKFALL window appeared"
echo "focus: window $win"

await_record() {
  want=$1
  try=0
  while [ "$try" -lt 400 ]; do
    if grep -q "check=$want " "$session/observations.obs" 2>/dev/null; then
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

await_record foc-a || fail "session ended before foc-a"
sleep 0.2
echo "focus: minimizing the window (120 scripted frames unfocused)"
xdotool windowminimize "$win" || fail "windowminimize failed"

await_record foc-b || fail "session ended before foc-b"
echo "focus: restoring the window"
xdotool windowactivate --sync "$win" || fail "windowactivate failed"

await_record foc-c || fail "session ended before foc-c"
wait "$game_pid"
status=$?
game_pid=""
trap - EXIT INT TERM
kill "$wm_pid" 2>/dev/null

echo "focus: application exited $status"
[ "$status" -eq 0 ] || fail "the application exited $status (see $session/run.log)"
grep -q "Clean shutdown" "$session/run.log" ||
  fail "no clean shutdown in $session/run.log"
grep -q "Window focus lost; every held button cleared" "$session/run.log" ||
  fail "the log lacks the focus-loss line"
echo "focus: session PASS (evidence in $session)"
