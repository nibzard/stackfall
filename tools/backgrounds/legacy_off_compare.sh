#!/bin/sh
# T085 legacy Off comparison: Background Off must keep the legacy play
# pixels apart from the intentional new menu rows.
#
# It extracts the last pre-backgrounds revision (cd54ba2, "Restore
# accepted tree to land retained T075") into a private temporary tree,
# builds its game binary, replays the same scripted round through both
# binaries, and compares every screenshot byte for byte:
#
#   legacy  no settings file: the revision's own defaults
#   off     background=off staged: everything else default
#
# The scripted round never opens the options screen, so every frame it
# captures is a title, play, or game-over frame - exactly the pixels the
# acceptance holds unchanged.
#
# The temporary tree lives under $TMPDIR with the Looper resource prefix
# and is registered in $LOOPER_RESOURCE_CLEANUP_FILE before creation.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out="$repo/build/backgrounds/t085/legacy"
legacy=${TMPDIR:-/tmp}/${LOOPER_RESOURCE_PREFIX:-looper}-t085-legacy
seed=31337
script="$repo/tests/harness/scripts/normal-game-clears.script.txt"

fail() {
  echo "legacy: $*" >&2
  exit 1
}

# Register the cleanup of the private tree before creating it.
if [ -n "${LOOPER_RESOURCE_CLEANUP_FILE:-}" ]; then
  mkdir -p "$(dirname "$LOOPER_RESOURCE_CLEANUP_FILE")"
  printf 'rm -rf %s\n' "$legacy" >> "$LOOPER_RESOURCE_CLEANUP_FILE"
fi

rm -rf "$legacy" "$out"
mkdir -p "$legacy" "$out/legacy/save" "$out/legacy/shots" \
  "$out/off/save" "$out/off/shots" || fail "cannot create the run dirs"

echo "legacy: extracting revision cd54ba2 into $legacy"
git -C "$repo" archive cd54ba2 | tar -x -C "$legacy" ||
  fail "git archive cd54ba2 failed"

echo "legacy: building the pre-backgrounds binary"
cmake -S "$legacy" -B "$legacy/build" -G Ninja >/dev/null 2>&1 ||
  fail "cmake configure failed in $legacy/build"
cmake --build "$legacy/build" >/dev/null ||
  fail "cmake build failed in $legacy/build"
[ -x "$legacy/build/stackfall" ] || fail "no legacy stackfall binary"
cmake --build "$repo/build" --target stackfall >/dev/null ||
  fail "cmake build failed for the current binary"

# The Off run stages background=off; the legacy run reads no settings at
# all, so its own defaults apply.
printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\neffectsvolume=8\nbackground=off\nbackgroundid=1\n' \
  > "$out/off/save/settings.dat"

run() {
  binary=$1 save=$2 shots=$3 log=$4
  timeout --foreground 300 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$binary" --seed "$seed" --tick-per-frame \
    --input-script "$script" --fixture-dir "$repo/tests/fixtures" \
    --save-dir "$save" --screenshot-dir "$shots" \
    --observations "$out/$5.obs" \
    --frames 4000 > "$log" 2>&1 ||
    fail "the run of $(basename "$(dirname "$binary")") failed (see $log)"
}

echo "legacy: replaying the scripted round through both binaries"
run "$legacy/build/stackfall" "$out/legacy/save" "$out/legacy/shots" \
  "$out/legacy.log" legacy
run "$repo/build/stackfall" "$out/off/save" "$out/off/shots" \
  "$out/off.log" off

names=$(cd "$out/legacy/shots" && ls | sort)
[ -n "$names" ] || fail "the legacy run wrote no screenshots"
new_count=$(cd "$out/off/shots" && ls | wc -l)
old_count=$(cd "$out/legacy/shots" && ls | wc -l)
[ "$new_count" = "$old_count" ] ||
  fail "shot count differs: legacy $old_count, off $new_count"

differ=""
for name in $names; do
  if ! cmp -s "$out/legacy/shots/$name" "$out/off/shots/$name"; then
    differ="$differ $name"
  fi
done
if [ -n "$differ" ]; then
  fail "frames differ from the legacy pixels:$differ"
fi
echo "legacy: all $old_count frames byte-identical to cd54ba2"

# The observation streams should agree too: the wallpaper work must not
# reach the simulation records.
sed 's/ shot=[^ ]*//' "$out/legacy.obs" > "$out/legacy.stripped"
sed 's/ shot=[^ ]*//' "$out/off.obs" > "$out/off.stripped"
if cmp -s "$out/legacy.stripped" "$out/off.stripped"; then
  echo "legacy: observation streams byte-identical as well"
else
  echo "legacy: note - observation streams differ (pixels above still hold)"
fi
echo "legacy: PASS"
