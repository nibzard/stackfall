#!/bin/sh
# Capture the PortMaster release screenshot from the desktop build
# (docs/SPEC.md section 10.4, task T019).
#
#   tools/portmaster/capture-screenshot.sh [output.png]
#
# PortMaster requires a 4:3 gameplay screenshot of at least 640x480 that
# shows the game as it appears on a 4:3 handheld, letterboxing included
# (https://portmaster.games/packaging.html). The desktop window opens at
# the 960x544 canvas size, so this tool runs the real application under a
# virtual X server with the t019 screenshot session, resizes the window to
# 640x480 through xdotool while the session waits at its screenshot-ready
# checkpoint, and takes the game's own checkpoint capture of the following
# frame: a real 640x480 letterboxed render, not a scaled copy of the
# 960x544 canvas. The BMP the checkpoint writer produces is converted to
# PNG with tools/portmaster/bmp2png.py, which needs no image library.
#
# The session is the rehearsed opening of the normal-game-clears harness
# session at seed 20261103, so the board in the picture is the board the
# automated tests pin. Because the run is paced in real time, the exact
# fall row of the piece in flight can differ by a row between captures;
# the board, the HUD, and the letterbox layout do not.
#
# Options through environment variables or command-line flags:
#   STACKFALL_SHOT_GAME     the desktop game binary (default build/stackfall)
#   STACKFALL_SHOT_OUT      the PNG to write (default
#                          build/portmaster-package/screenshot.png)
#   STACKFALL_SHOT_RUN_DIR  evidence directory (default
#                          build/agent-checks/t019-screenshot-<date>)
#   --game PATH, --out PATH, --run-dir DIR   same, as flags

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
game=${STACKFALL_SHOT_GAME:-$repo/build/stackfall}
out=${STACKFALL_SHOT_OUT:-$repo/build/portmaster-package/screenshot.png}
run_dir=${STACKFALL_SHOT_RUN_DIR:-$repo/build/agent-checks/t019-screenshot-$(date +%Y-%m-%d)}
seed=20261103

fail() {
  echo "capture-screenshot: $*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case $1 in
    --game) [ $# -ge 2 ] || fail "--game needs a path"; game=$2; shift 2 ;;
    --out) [ $# -ge 2 ] || fail "--out needs a path"; out=$2; shift 2 ;;
    --run-dir) [ $# -ge 2 ] || fail "--run-dir needs a path"; run_dir=$2; shift 2 ;;
    *) fail "unknown argument: $1" ;;
  esac
done

for tool in Xvfb xdotool openbox; do
  command -v "$tool" >/dev/null 2>&1 ||
    fail "$tool is not installed; the release screenshot cannot be captured"
done
[ -x "$game" ] || fail "missing desktop game $game (build it first)"

case "$out" in
  /*) ;;
  *) out="$PWD/$out" ;;
esac
mkdir -p "$(dirname "$out")" || fail "cannot create $(dirname "$out")"

rm -rf "$run_dir"
mkdir -p "$run_dir/shots" "$run_dir/save" || fail "cannot create $run_dir"

# The display this run owns. The X server, the window manager, and the
# game die with this script.
display=""
xvfb_pid=""
wm_pid=""
game_pid=""
cleanup() {
  [ -n "$game_pid" ] && kill "$game_pid" 2>/dev/null
  [ -n "$wm_pid" ] && kill "$wm_pid" 2>/dev/null
  [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null
  [ -n "$display" ] && rm -f "/tmp/.X${display}-lock" 2>/dev/null
}
trap cleanup EXIT INT TERM

# A free display number, so two captures never share an X server.
display=$(python3 - <<'EOF'
import os
used = set()
for entry in os.listdir('/tmp'):
    if entry.startswith('.X') and entry.endswith('-lock'):
        used.add(entry[2:-5])
for number in range(20, 120):
    if str(number) not in used:
        print(number)
        break
EOF
) || fail "cannot pick a display number"
Xvfb ":$display" -screen 0 1280x800x24 >/dev/null 2>&1 &
xvfb_pid=$!
sleep 1
DISPLAY=":$display" openbox >/dev/null 2>&1 &
wm_pid=$!
sleep 1
export DISPLAY=":$display"

echo "capture-screenshot: starting the application (seed $seed, display :$display)"
"$game" --seed "$seed" \
  --input-script "$repo/tests/harness/scripts/t019-screenshot.script.txt" \
  --save-dir "$run_dir/save" --screenshot-dir "$run_dir/shots" \
  --observations "$run_dir/observations.obs" \
  --frames 6000 > "$run_dir/run.log" 2>&1 &
game_pid=$!

# The window appears once the title screen renders.
win=""
try=0
while [ -z "$win" ] && [ "$try" -lt 100 ]; do
  win=$(xdotool search --onlyvisible --name '^STACKFALL$' 2>/dev/null | head -1)
  try=$((try + 1))
  sleep 0.1
done
[ -n "$win" ] || fail "no STACKFALL window appeared"

await_record() {
  want=$1
  try=0
  while [ "$try" -lt 400 ]; do
    if grep -q "check=$want " "$run_dir/observations.obs" 2>/dev/null; then
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

await_record screenshot-ready || fail "the session ended before screenshot-ready"
echo "capture-screenshot: resizing the window to 640x480 (the 4:3 letterbox)"
xdotool windowsize "$win" 640 480 || fail "windowsize failed"

await_record screenshot || fail "the session ended before the screenshot checkpoint"
sleep 1

wait "$game_pid"
status=$?
game_pid=""
trap - EXIT INT TERM
kill "$wm_pid" 2>/dev/null
kill "$xvfb_pid" 2>/dev/null
rm -f "/tmp/.X${display}-lock" 2>/dev/null

[ "$status" -eq 0 ] || fail "the application exited $status (see $run_dir/run.log)"
grep -q "Window resized to 640x480" "$run_dir/run.log" ||
  fail "the log lacks the 640x480 resize line"
[ -f "$run_dir/shots/screenshot.bmp" ] ||
  fail "no screenshot.bmp in $run_dir/shots"

python3 "$repo/tools/portmaster/bmp2png.py" \
  "$run_dir/shots/screenshot.bmp" "$out" || fail "the BMP to PNG conversion failed"

python3 - "$out" <<'EOF' || fail "the PNG check failed"
import struct
import sys
with open(sys.argv[1], "rb") as handle:
    data = handle.read()
if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 33:
    sys.exit("not a PNG")
width, height = struct.unpack(">II", data[16:24])
if (width, height) != (640, 480):
    sys.exit(f"the capture is {width}x{height}, not 640x480")
if width * 3 != height * 4:
    sys.exit(f"the capture {width}x{height} is not 4:3")
EOF

cp "$run_dir/shots/screenshot.bmp" "$run_dir/screenshot-source.bmp"
echo "capture-screenshot: $out"
echo "evidence          : $run_dir"
