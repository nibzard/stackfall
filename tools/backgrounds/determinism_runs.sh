#!/bin/sh
# T085 determinism across wallpaper modes: the same seed and the same
# scripted round must reproduce the same observation stream under every
# mode. Four fresh save directories, four settings stages:
#   fresh  no settings file (defaults: Auto, design 1)
#   off    background=off
#   auto   background=auto
#   fixed  background=fixed backgroundid=17
#
# The scripted round is the det-game ladder of catalog_gen.py: a checkpoint
# after every four-line cycle, so the stream carries scores, levels,
# boards, and piece queues up to level 20 - across every design change
# Auto makes on the way.
#
# Options through environment variables:
#   T085_BUILD_DIR   build directory (default: build)
#   T085_RUN_DIR     evidence directory (default: build/backgrounds/t085)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T085_BUILD_DIR:-$repo/build}
out=${T085_RUN_DIR:-$repo/build/backgrounds/t085}/det
game="$build_dir/stackfall"
seed=20261103

fail() {
  echo "determinism: $*" >&2
  exit 1
}

[ -x "$game" ] || fail "missing $game (build it first)"
script="$repo/build/backgrounds/t085/det-game.script.txt"
[ -f "$script" ] ||
  fail "missing $script (generate it with catalog_gen.py det-game)"

rm -rf "$out"
mkdir -p "$out"

for mode in fresh off auto fixed; do
  save="$out/$mode/save"
  mkdir -p "$save" "$out/$mode/shots"
  if [ "$mode" != fresh ]; then
    printf 'version=1\nsound=on\nghost=on\ngrid=on\nhold=on\nnext=5\ngrayscale=off\norientation=landscape\nmusic=auto\nmusicvolume=6\neffectsvolume=8\nbackground=%s\nbackgroundid=17\n' \
      "$mode" > "$save/settings.dat"
  fi
  timeout --foreground 300 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" --tick-per-frame \
    --input-script "$script" --fixture-dir "$repo/tests/fixtures" \
    --save-dir "$save" --screenshot-dir "$out/$mode/shots" \
    --observations "$out/$mode.obs" \
    --frames 4000 > "$out/$mode.log" 2>&1 ||
    fail "$mode run failed (see $out/$mode.log)"
  sed 's/ shot=[^ ]*//' "$out/$mode.obs" > "$out/$mode.stripped"
  echo "determinism: $mode -> $(wc -l < "$out/$mode.stripped") records"
done
for mode in off auto fixed; do
  cmp -s "$out/fresh.stripped" "$out/$mode.stripped" ||
    fail "$mode diverged from fresh (compare $out/fresh.stripped and $out/$mode.stripped)"
  echo "determinism: $mode byte-identical to fresh"
done

levels=$(grep -o 'level=[0-9]*' "$out/fresh.stripped" |
  sort -t= -k2 -n -u | wc -l)
[ "$levels" = 20 ] ||
  fail "the ladder reached $levels levels, not 20 - the comparison would prove nothing"
echo "determinism: PASS (levels 1-20, four modes, byte-identical)"
