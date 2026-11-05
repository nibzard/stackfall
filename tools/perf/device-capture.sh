#!/bin/bash
# Device capture procedure of the T065 performance round (task T065).
#
#   tools/perf/device-capture.sh [GAME] [OUTDIR]
#
# The T026/T065 desktop measurements cannot certify the handheld: under
# Xvfb the dummy and llvmpipe paths never block on a real panel, the host's
# storage is an SSD, and the frame budget the device lives in is a sixth of
# the desktop's per-frame work. This script is the repeatable procedure that
# closes that gap on a real RG503 (or any ROCKNIX host with the game
# installed). It records, in one bounded run each:
#
#   frame times   the per-frame microsecond record of a real-time paced
#                 session (the T025 instrument), per scenario
#   memory/writes /proc accounting sampled from outside the process, the
#                 same monitor the desktop harness uses
#   audio         the callback work line (STACKFALL_PROFILE_AUDIO_CALLBACK)
#                 and the music command traffic line
#   save cost     the durable-save probe through the same store the game
#                 uses, on the device's own storage
#
# Everything lands in OUTDIR (default: stackfall-device-capture-<date>),
# together with a copy of this procedure's inputs, so the directory alone
# re-derives the report. Nothing here runs longer than a few minutes, fills
# no disk (the probe writes one small file repeatedly), and needs no
# network. Task T036 owns the resulting tail measurements.
#
# What to copy to the device: the game binary (or use the installed one),
# tools/perf/save_probe's binary of the same architecture, tests/harness/
# t065_monitor.py, and the scenario scripts named below (generate them with
# tests/harness/t065_gen.py at 6000 ticks). Run from the directory that
# holds those files:
#
#   ./device-capture.sh ./stackfall.aarch64 ./capture
#
# Then copy the whole capture directory back and read it with the T065
# summarizer's frame statistics.

set -u

game=${1:-./stackfall}
out=${2:-stackfall-device-capture-$(date +%Y%m%d-%H%M%S)}
seed=${DEVICE_SEED:-31}
seconds=${DEVICE_SECONDS:-60}
monitor=${DEVICE_MONITOR:-./t065_monitor.py}
probe=${DEVICE_SAVE_PROBE:-./stackfall_save_probe}
scripts=${DEVICE_SCRIPTS:-./scripts}

fail() {
  echo "device-capture: $*" >&2
  exit 1
}

[ -x "$game" ] || fail "no game binary at '$game' (pass it as the first \
argument; on the handheld that is stackfall.aarch64 of the installed port)"
mkdir -p "$out" || fail "cannot create $out"

# One scenario per capture. The same five the desktop round measures; a
# shorter list runs faster on the device.
scenarios=${DEVICE_SCENARIOS:-"rotate-move drops clears pause pad-mixed"}

record() {
  local name=$1 script=$2 extra=${3:-}
  local dir="$out/$name"
  mkdir -p "$dir/save"
  echo "device-capture: $name ($seconds s at real-time pacing)"
  if [ -x "$monitor" ]; then
    python3 "$monitor" --out "$dir/mon.csv" --interval-ms 250 -- \
      env STACKFALL_PROFILE_AUDIO_CALLBACK=1 SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy} \
      "$game" --seed "$seed" --frames $((seconds * 60)) \
      --save-dir "$dir/save" --input-script "$script" \
      --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
      $extra > "$dir/run.log" 2>&1
  else
    env STACKFALL_PROFILE_AUDIO_CALLBACK=1 \
      "$game" --seed "$seed" --frames $((seconds * 60)) \
      --save-dir "$dir/save" --input-script "$script" \
      --input-log "$dir/il.log" --frame-times "$dir/frame-times.txt" \
      $extra > "$dir/run.log" 2>&1
  fi
  echo "$?" > "$dir/exit"
}

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host: $(uname -srm)"
  echo "game: $game ($("$game" --help 2>&1 | head -1 >/dev/null; echo ok))"
  echo "seed: $seed, seconds per scenario: $seconds"
  echo "kernel storage: $(mount | grep ' / ' | head -1)"
} > "$out/device.txt"

for scenario in $scenarios; do
  script="$scripts/$scenario.script.txt"
  [ -f "$script" ] || { echo "device-capture: no script for $scenario at \
$script; generate it with tests/harness/t065_gen.py" >&2; continue; }
  extra=""
  [ "$scenario" = pad-mixed ] && extra="--virtual-controller"
  record "$scenario" "$script" "$extra"
done

# The save probe on the device's own storage: the number the desktop round
# measured at about 2.7 ms median, which the fix moved off the gameplay
# thread. On the handheld it is the number that explains any remaining tail.
if [ -x "$probe" ]; then
  mkdir -p "$out/save-probe"
  "$probe" "$out/save-probe" 100 > "$out/save-probe.txt" 2>&1
  cat "$out/save-probe.txt"
fi

echo
echo "device-capture: evidence in $out"
echo "device-capture: percentiles per scenario:"
for scenario in $scenarios; do
  file="$out/$scenario/frame-times.txt"
  [ -f "$file" ] || continue
  python3 - "$file" "$scenario" <<'PYEOF'
import sys
values = sorted(int(line) for line in open(sys.argv[1]) if line.strip())
if not values:
    print("%s: no frames" % sys.argv[2])
    sys.exit(0)
def pct(fraction):
    return values[int(round(fraction * (len(values) - 1)))] / 1e3
print("%s: n=%d median %.3f ms p95 %.3f p99 %.3f max %.3f" % (
    sys.argv[2], len(values), pct(0.5), pct(0.95), pct(0.99),
    values[-1] / 1e3))
PYEOF
done
