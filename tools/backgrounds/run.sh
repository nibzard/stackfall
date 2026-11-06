#!/usr/bin/env bash
# Build every artifact of the background design package (task T075):
# the two HUD captures, the plates and manifests, the mockups and
# sheets, the acceptance check, and the design records under
# docs/backgrounds.
#
# Environment:
#   HARNESS_BUILD_DIR  build tree of the game binary (default build)
#   SKIP_CAPTURE=1     reuse the captures already under build/backgrounds
set -euo pipefail
cd "$(dirname "$0")/../.."

OUT=build/backgrounds
SEED=31337
BUILD=${HARNESS_BUILD_DIR:-build}
HARNESS=$BUILD/stackfall

if [ "${SKIP_CAPTURE:-0}" != 1 ]; then
  cmake --build "$BUILD" --target stackfall >/dev/null

  capture() {
    local orientation="$1"
    local dir="$OUT/capture-$orientation"
    rm -rf "$dir"
    mkdir -p "$dir/save" "$dir/shots"
    if [ "$orientation" = portrait ]; then
      # The portrait half of the fixed quarter turn (task T055): the
      # save file selects the orientation before the session starts.
      cat > "$dir/save/settings.dat" <<'EOF'
version=1
sound=on
ghost=on
grid=on
hold=on
next=5
grayscale=off
orientation=portrait
music=auto
musicvolume=6
effectsvolume=8
EOF
    fi
    local script="tools/backgrounds/mockup-hud.script.txt"
    if [ "$orientation" = portrait ]; then
      script="tools/backgrounds/mockup-hud-portrait.script.txt"
    fi
    # One tick per frame keeps the checkpoint ticks deterministic, so
    # the same seed and script always land on the same frame.
    "$HARNESS" --seed "$SEED" --tick-per-frame \
      --input-script "$script" \
      --input-log "$dir/input.log" \
      --fixture-dir tests/fixtures \
      --save-dir "$dir/save" \
      --observations "$dir/observations.txt" \
      --screenshot-dir "$dir/shots" > "$dir/session.log"
    echo "captured $orientation: $(grep -c '^check=' "$dir/observations.txt") checkpoints"
  }

  capture landscape
  capture portrait
else
  echo "SKIP_CAPTURE=1: reusing $OUT/capture-*"
fi

python3 tools/backgrounds/render.py
python3 tools/backgrounds/check.py
python3 tools/backgrounds/records.py
echo "background package complete: docs/backgrounds"
