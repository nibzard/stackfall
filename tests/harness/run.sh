#!/bin/sh
# Unattended SDL test harness for STACKFALL (docs/AGENT_TESTING.md, T023).
#
# This is the one command that runs the scene cycle unattended:
#
#   tests/harness/run.sh
#
# It builds the game and the headless reference replay, then drives
# thirty-three sessions through the real SDL application under the dummy
# video driver:
# the full scene cycle on the keyboard and on the developer virtual
# controller, the landscape default-hard-drop session of task T067 (D-pad up
# hard drops, X carries nothing, and no press that returns to play drops a
# piece), the two thumbstick sessions of task T86 (a landscape round that
# moves, soft drops, and hard drops on the stick with the deadzone pinned
# from both sides and the D-pad and the keyboard beside it, and a portrait
# session that turns with the stick deflected across the select and then
# plays the turned round on the rotated stick table with the face buttons
# beside it), the staged hard-drop rebind session of task T87 (a landscape
# stage that pins the stick's edge rule on the T067 defaults, rebinds HARD
# DROP to the face button X, the right shoulder R1, and key D, and plays the
# rebound tables; a portrait stage that relaunches on the saved file, turns
# through the ORIENTATION row, and rebinds through the turned menu roles; a
# relaunch stage that proves the persisted portrait rebind and ends on
# RESTORE DEFAULTS), a normal planned game from an empty board, the options screen
# round trip that turns the ghost preview off, the ten-row options screen
# on both inputs, the music settings page sessions on both inputs of the
# soundtrack (T062), the backgrounds page sessions of the saved Auto, Off,
# and Fixed wallpaper options on both inputs plus the staged relaunch,
# corrupted-file, and legacy-file stages (T084), the hold-off round trip,
# the grayscale round trip, the three per-piece in-cell design sessions of
# the tenth options row on the keyboard, on the pad, and under grayscale in
# both orientations over the labeled all-pieces fixture (T095), the
# staged relaunch, legacy-settings, and next-count sessions of the options
# expansion (T045), the abandoned-round record session of the mid-round
# persistence (T052), the two staged portrait sessions of tasks T055 and
# T068 - a landscape stage that selects the ORIENTATION row with a direction
# held across the press, turns portrait at once, plays a round on the rotated
# keyboard or pad table, turns back to landscape and to portrait again, and
# a portrait relaunch that proves the saved preference and plays a full
# round - and three labeled board fixtures (four-line clear, lock-out,
# block-out). Every
# session also runs through the
# headless reference - a
# staged session once per stage, over the shared save directory - and
# check_harness.py compares the two checkpoint streams field by field and
# adds expectations pinned from the specification alone. On any failure the
# run directory keeps the log, the records, and every screenshot as evidence
# and the script exits nonzero. Every game process runs under `timeout` with
# a frame bound, so a hung session cannot outlive the harness.
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
    tests/fixtures/*.board.txt tests/harness/scripts/*.script.txt \
    tests/harness/scripts/*.stages.txt tests/harness/settings/*.dat \
    tests/harness/controls/*.dat
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
# A session with a `<session>.stages.txt` manifest runs one game process per
# listed stage over one shared save directory, so a session can relaunch the
# game on the settings an earlier stage wrote or start from a legacy file.
# ---------------------------------------------------------------------------

# One reference-and-game pair over one script. `$1` names the run for the
# logs, `$2` the script, `$3` the observation stem (`<stem>.ref.obs`,
# `<stem>.run.obs`, `<stem>.run.log`, `<stem>.exit`), `$4` the save directory
# both halves read the settings file from, and `$5` the evidence directory
# that takes the stage's screenshots and input log.
run_pair() {
  pair_name=$1
  pair_script=$2
  pair_stem=$3
  pair_save=$4
  pair_dir=$5

  echo "reference: $pair_name"
  "$reference" --seed "$seed" --script "$pair_script" \
    --fixture-dir "$fixtures" --settings "$pair_save/settings.dat" \
    --controls "$pair_save/controls.dat" \
    --high-score "$pair_save/highscore.dat" \
    --out "$run_dir/$pair_stem.ref.obs" \
    > "$run_dir/$pair_stem.ref.log" 2>&1 ||
    fail "reference replay failed for $pair_name (see $run_dir/$pair_stem.ref.log)"

  echo "game:      $pair_name (video driver $video)"
  timeout --foreground "$timeout_s" \
    env SDL_VIDEODRIVER="$video" SDL_AUDIODRIVER=dummy \
    "$game" --seed "$seed" --tick-per-frame \
    --input-script "$pair_script" --fixture-dir "$fixtures" \
    --save-dir "$pair_save" --screenshot-dir "$pair_dir/shots" \
    --observations "$run_dir/$pair_stem.run.obs" \
    --input-log "$pair_dir/input.log" \
    --frames 4000 > "$run_dir/$pair_stem.run.log" 2>&1 &
  game_pid=$!
  children="$children $game_pid"
  wait "$game_pid"
  status=$?
  children=${children% "$game_pid"}
  if [ "$status" -ne 0 ]; then
    echo "harness: $pair_name exited $status; evidence in $run_dir" >&2
    echo "exit=$status" > "$run_dir/$pair_stem.exit"
    fail "session $pair_name failed"
  fi
  echo "exit=0" > "$run_dir/$pair_stem.exit"
}

# One staged session: every line of the manifest names one script and may add
# `settings=<name>`, a file under tests/harness/settings/ the driver copies
# into the shared save directory before the stage runs, so a stage may start
# from a legacy settings file, `controls=<name>`, the same staging for a file
# under tests/harness/controls/ (task T066), or `settings=none`, which
# removes the settings file first and requires the stage to leave none - the
# marker for a stage that exercises no settings write. The settings and
# controls files are snapshotted before and after every stage, so the checker
# reads the exact bytes each stage started from and left behind.
run_stages() {
  stage_session=$1
  stage_dir=$2
  stage_manifest=$3
  stage_save="$stage_dir/save"
  stage_no=0
  while IFS= read -r stage_line || [ -n "$stage_line" ]; do
    stage_line=${stage_line%%#*}
    [ -n "$stage_line" ] || continue
    # The first word names the script; every later word is a `field=value`.
    # Word splitting over the line, not `set --`, because that would clobber
    # this function's own positional parameters.
    stage_script=""
    stage_template=""
    stage_controls=""
    stage_no_settings=0
    for stage_field in $stage_line; do
      case "$stage_field" in
        script=*)
          if [ -n "$stage_script" ]; then
            fail "two scripts on one line of $stage_manifest"
          fi
          stage_script=${stage_field#script=}
          ;;
        settings=none) stage_no_settings=1 ;;
        settings=*) stage_template=${stage_field#settings=} ;;
        controls=*) stage_controls=${stage_field#controls=} ;;
        *) fail "unknown field \"$stage_field\" in $stage_manifest" ;;
      esac
    done
    [ -n "$stage_script" ] || fail "a line of $stage_manifest names no script"
    stage_no=$((stage_no + 1))
    [ -f "$scripts/$stage_script" ] ||
      fail "missing script $scripts/$stage_script (named in $stage_manifest)"
    echo
    echo "--- stage $stage_no: $stage_script"
    if [ -n "$stage_template" ]; then
      [ -f "$settings_dir/$stage_template" ] ||
        fail "missing settings template $settings_dir/$stage_template"
      cp "$settings_dir/$stage_template" "$stage_save/settings.dat" ||
        fail "cannot stage $stage_template for $stage_session"
    elif [ "$stage_no_settings" -eq 1 ]; then
      rm -f "$stage_save/settings.dat"
    fi
    # `controls=<name>` stages a controls.dat the same way (task T066): a
    # stage may start from a custom or a damaged binding file, and the
    # snapshots below carry the exact bytes each stage started from and left
    # behind.
    if [ -n "$stage_controls" ]; then
      [ -f "$controls_dir/$stage_controls" ] ||
        fail "missing controls template $controls_dir/$stage_controls"
      cp "$controls_dir/$stage_controls" "$stage_save/controls.dat" ||
        fail "cannot stage $stage_controls for $stage_session"
    fi
    if [ -f "$stage_save/settings.dat" ]; then
      cp "$stage_save/settings.dat" \
        "$stage_dir/settings-before-stage$stage_no.dat"
    fi
    if [ -f "$stage_save/controls.dat" ]; then
      cp "$stage_save/controls.dat" \
        "$stage_dir/controls-before-stage$stage_no.dat"
    fi
    mkdir -p "$stage_dir/stage$stage_no/shots"
    run_pair "$stage_session stage $stage_no" "$scripts/$stage_script" \
      "$stage_session.stage$stage_no" "$stage_save" "$stage_dir/stage$stage_no"
    if [ "$stage_no_settings" -eq 1 ]; then
      # A stage marked `settings=none` changes no preference, so the run must
      # not grow a settings file either: an accidental write would surface
      # here instead of hiding behind the staged sessions that do write one.
      [ -f "$stage_save/settings.dat" ] &&
        fail "stage $stage_no of $stage_session wrote settings.dat without a settings change"
    else
      [ -f "$stage_save/settings.dat" ] ||
        fail "stage $stage_no of $stage_session left no settings.dat"
      cp "$stage_save/settings.dat" "$stage_dir/settings-after-stage$stage_no.dat"
    fi
    if [ -f "$stage_save/controls.dat" ]; then
      cp "$stage_save/controls.dat" \
        "$stage_dir/controls-after-stage$stage_no.dat"
    else
      : > "$stage_dir/controls-after-stage$stage_no.dat"
    fi
  done < "$stage_manifest"
  [ "$stage_no" -gt 0 ] || fail "$stage_manifest lists no stage"
}

seed=20261103
settings_dir="$repo/tests/harness/settings"
controls_dir="$repo/tests/harness/controls"
sessions="scene-cycle-keyboard scene-cycle-pad harddrop-pad stick-pad stick-portrait harddrop-rebind normal-game-clears options-ghost options-six-keyboard options-six-pad music-settings music-settings-pad backgrounds-keyboard backgrounds-pad backgrounds-relaunch hold-off grayscale piece-designs-keyboard piece-designs-pad piece-designs-gray options-relaunch options-legacy next-count portrait-keyboard portrait-pad record-abandon-save controls-keyboard controls-pad controls-relaunch controls-invalid fixture-four-line fixture-lockout fixture-blockout"
: > "$run_dir/sessions.txt"
for session in $sessions; do
  echo "$session" >> "$run_dir/sessions.txt"
done

for session in $sessions; do
  echo
  echo "=== session $session ==="
  dir="$run_dir/$session"
  mkdir -p "$dir/save"
  manifest="$scripts/$session.stages.txt"
  if [ -f "$manifest" ]; then
    run_stages "$session" "$dir" "$manifest"
  else
    script="$scripts/$session.script.txt"
    [ -f "$script" ] || fail "missing script $script"
    mkdir -p "$dir/shots"
    run_pair "$session" "$script" "$session" "$dir/save" "$dir"
  fi

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
