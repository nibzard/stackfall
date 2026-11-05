#!/bin/bash
# T015 M3 desktop gate driver (docs/AGENT_TESTING.md, T015).
#
# One command runs the complete round-M3 verification pass:
#
#   tests/harness/t015.sh
#
# Phases (pick a subset with T015_PHASES="builds hashes ..."):
#
#   builds      clean rebuild and full ctest in Debug and Release, with
#               the compile log kept so the checker can hold the
#               zero-warning gate
#   hashes      the rules suite at a fixed custom seed in both
#               configurations; the checker requires byte-identical
#               output and the pinned scenario hashes (section 11,
#               item 18, desktop half)
#   sessions    the seven M3 sessions through the real SDL application:
#               DAS/ARR tuning, lock-delay edges, three seven-bag seeds,
#               restart paths, and four high-score process runs over one
#               save directory. Every deterministic session runs through
#               the headless reference, repeats inside the run, and
#               replays in the Release build
#   regress     the retained full-game suites at this source identity:
#               the T023 harness (run.sh) and the T024 play sessions
#               (t024.sh) in Debug and in Release
#   stability   the T025 stability phases that carry the restart and
#               failure evidence (failures, restarts, san)
#   performance the T026 scenario verification phase in Release
#   check       the independent gate checker (tests/harness/check_t015.py)
#
# The gate needs no human and no physical device. Everything it writes
# stays under build/agent-checks/<run-id>/.
#
# Options through environment variables:
#   T015_BUILD_DIR     Debug build directory (default: build)
#   T015_RELEASE_DIR   Release build directory (default: build-rel)
#   T015_RUN_ID        evidence directory name (default: t015-<date>); a
#                      reused name is removed first, so a rerun starts
#                      from an empty run directory
#   T015_TIMEOUT_S     per-process timeout in seconds (default: 180)
#   T015_PHASES        space-separated phase list (default: all above)
#   T015_SKIP_X11      set to 1 to skip the X11-dependent retained checks
#                      (recorded unavailable, never silently dropped)
#   T015_T025_PHASES   the T025 phase list (default: "failures restarts
#                      san")

set -u

repo=$(cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T015_BUILD_DIR:-$repo/build}
release_dir=${T015_RELEASE_DIR:-$repo/build-rel}
run_id=${T015_RUN_ID:-t015-$(date +%Y-%m-%d-%H%M%S)}
timeout_s=${T015_TIMEOUT_S:-180}
phases=${T015_PHASES:-"builds hashes sessions regress stability performance check"}
t025_phases=${T015_T025_PHASES:-"failures restarts san"}
run_dir="$repo/build/agent-checks/$run_id"
scripts="$repo/tests/harness/scripts"
fixtures="$repo/tests/fixtures"
checker="$repo/tests/harness/check_t015.py"

fail() {
  echo "t015: $*" >&2
  exit 1
}

# The run id names one directory below build/agent-checks; the rerun
# cleanup below deletes that directory, so the id may not reach outside
# the evidence root (the T034 rule).
case "$run_id" in
  */*|""|.|..) fail "T015_RUN_ID must name one directory, not '$run_id'" ;;
esac

children=""
reap() {
  for pid in $children; do
    kill "$pid" 2>/dev/null
  done
}
trap reap EXIT INT TERM

want_phase() {
  case " $phases " in *" $1 "*) return 0 ;; *) return 1 ;; esac
}

echo "t015: repository $repo"
echo "t015: debug build $build_dir"
echo "t015: release build $release_dir"
echo "t015: evidence $run_dir"

if [ -e "$run_dir" ]; then
  echo "t015: removing run directory of an earlier pass: $run_dir"
  rm -rf "$run_dir" || fail "cannot remove stale $run_dir"
fi
mkdir -p "$run_dir" || fail "cannot create $run_dir"

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "repository head: $(git -C "$repo" rev-parse HEAD 2>/dev/null || echo uncommitted-only)"
  echo "game binary (debug): $(sha256sum "$build_dir/stackfall" 2>/dev/null | cut -d' ' -f1)"
  echo "game binary (release): $(sha256sum "$release_dir/stackfall" 2>/dev/null | cut -d' ' -f1)"
  cd "$repo" && sha256sum tests/harness/t015.sh tests/harness/check_t015.py \
    tests/harness/scripts/m3-*.script.txt docs/AGENT_TESTING.md \
    notes/agent-testing.md notes/performance.md notes/design-review.md
} > "$run_dir/source.sha256" 2>/dev/null || fail "cannot record source identity"

# ---------------------------------------------------------------------------
if want_phase builds; then
  echo
  echo "=== phase builds ==="
  mkdir -p "$run_dir/builds"
  for pair in "debug:$build_dir:Debug" "release:$release_dir:Release"; do
    name=${pair%%:*}
    rest=${pair#*:}
    dir=${rest%%:*}
    type=${rest##*:}
    echo "t015: clean $type build in $dir"
    cmake -S "$repo" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$type" \
      >/dev/null 2>&1 || fail "cmake configure failed for $dir"
    cmake --build "$dir" --clean-first > "$run_dir/builds/build-$name.log" 2>&1 ||
      fail "build failed for $dir (see $run_dir/builds/build-$name.log)"
    # The zero-warning gate belongs to the build itself, not only to the
    # check phase, so a round that runs just 'builds hashes' still fails on
    # a compiler warning. check_t015.py re-derives the same verdict from the
    # kept log when the full gate runs. This is the gate the Scene::Rebind
    # warning of T066 slipped past, because the later rounds ran the subset.
    warnings=$(grep -c "warning:" "$run_dir/builds/build-$name.log" || true)
    [ "$warnings" = 0 ] ||
      fail "$type build has $warnings compiler warnings (see $run_dir/builds/build-$name.log)"
    ctest --test-dir "$dir" --output-on-failure \
      > "$run_dir/builds/ctest-$name.txt" 2>&1 ||
      fail "ctest failed in $dir (see $run_dir/builds/ctest-$name.txt)"
    echo "t015: $name ctest: $(grep -c 'Passed' "$run_dir/builds/ctest-$name.txt") suites green"
  done
fi

game="$build_dir/stackfall"
game_rel="$release_dir/stackfall"
reference="$build_dir/stackfall_reference"
for path in "$game" "$game_rel" "$reference"; do
  [ -x "$path" ] || fail "missing binary $path; run the builds phase"
done

# ---------------------------------------------------------------------------
if want_phase hashes; then
  echo
  echo "=== phase hashes ==="
  mkdir -p "$run_dir/hashes"
  for pair in "debug:$build_dir" "release:$release_dir"; do
    name=${pair%%:*}
    dir=${pair#*:}
    "$dir/stackfall_tests" --seed 987654321 > "$run_dir/hashes/$name.txt" 2>&1 ||
      fail "rules suite failed in $dir"
  done
  if cmp -s "$run_dir/hashes/debug.txt" "$run_dir/hashes/release.txt"; then
    echo "t015: rules-suite output identical in Debug and Release"
  else
    fail "rules-suite output differs between Debug and Release"
  fi
fi

# ---------------------------------------------------------------------------
# The M3 sessions. Every deterministic session runs through the headless
# reference (which prescribes the records), through the application, once
# more inside the run (byte-identical records), and again in the Release
# build (the same reference stream).
m3_sessions="m3-das-arr:20261103 m3-lock-delay:20261103 m3-bag-20261103:20261103 m3-bag-31337:31337 m3-bag-777:777 m3-restart:31337"

# strip_shot PATH: one observation stream without the shot= field, so a
# run without screenshots compares byte for byte against one with them.
strip_shot() { sed 's/ shot=[^ ]*//' "$1"; }

run_session() {
  # run_session BINARY SCRIPT SEED OUT_LOG OBS [SAVEDIR] [SHOTS] [INPUTLOG]
  binary=$1 script=$2 seed=$3 out_log=$4 obs=$5
  savedir=${6:-}
  shots=${7:-}
  inputlog=${8:-}
  mkdir -p "$(dirname "$out_log")"
  [ -n "$savedir" ] && mkdir -p "$savedir"
  [ -n "$shots" ] && mkdir -p "$shots"
  timeout --foreground "$timeout_s" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$binary" --seed "$seed" --tick-per-frame \
    --input-script "$script" --fixture-dir "$fixtures" \
    ${savedir:+--save-dir "$savedir"} \
    ${shots:+--screenshot-dir "$shots"} \
    --observations "$obs" \
    ${inputlog:+--input-log "$inputlog"} \
    --frames 6000 > "$out_log" 2>&1
}

if want_phase sessions; then
  echo
  echo "=== phase sessions ==="
  : > "$run_dir/sessions.txt"
  release_ok=1
  for entry in $m3_sessions; do
    session=${entry%%:*}
    seed=${entry##*:}
    echo "$session" >> "$run_dir/sessions.txt"
    echo "$seed" > "$run_dir/$session.seed"
    echo
    echo "--- session $session (seed $seed)"
    script="$scripts/$session.script.txt"
    [ -f "$script" ] || fail "missing script $script"
    dir="$run_dir/$session"
    mkdir -p "$dir/save" "$dir/shots"

    echo "reference: $session"
    "$reference" --seed "$seed" --script "$script" \
      --fixture-dir "$fixtures" --out "$run_dir/$session.ref.obs" \
      > "$run_dir/$session.ref.log" 2>&1 ||
      fail "reference replay failed for $session"

    echo "game:      $session (debug, with screenshots)"
    run_session "$game" "$script" "$seed" "$run_dir/$session.run.log" \
      "$run_dir/$session.run.obs" "$dir/save" "$dir/shots" "$dir/input.log" ||
      fail "session $session exited nonzero; see $run_dir/$session.run.log"

    echo "repeat:    $session (determinism)"
    run_session "$game" "$script" "$seed" "$run_dir/$session.repeat.log" \
      "$run_dir/$session.repeat.obs" "$dir/save-repeat" ||
      fail "the repeat of $session exited nonzero"
    if ! cmp -s <(strip_shot "$run_dir/$session.run.obs") \
                <(strip_shot "$run_dir/$session.repeat.obs"); then
      fail "$session did not repeat identically"
    fi

    echo "release:   $session"
    run_session "$game_rel" "$script" "$seed" "$run_dir/$session.release.log" \
      "$run_dir/$session.release.obs" "$dir/save-release" ||
      { release_ok=0; echo "t015: the Release replay of $session exited nonzero"; }
    if ! cmp -s <(strip_shot "$run_dir/$session.run.obs") \
                <(strip_shot "$run_dir/$session.release.obs"); then
      release_ok=0
      echo "t015: the Release replay of $session differs from the Debug run"
    fi
  done
  if [ "$release_ok" -eq 1 ]; then
    echo "passed: all sessions replay identically in the Release build" \
      > "$run_dir/m3-release.status"
  else
    echo "failed: a Release replay differs" > "$run_dir/m3-release.status"
  fi

  # The high-score process runs: one script, one shared save directory,
  # four processes (fresh, same seed again, better seed, damaged file).
  echo
  echo "--- session m3-highscore (four processes, one save directory)"
  echo "m3-highscore" >> "$run_dir/sessions.txt"
  hs="$run_dir/m3-highscore"
  mkdir -p "$hs/save"
  script="$scripts/m3-highscore.script.txt"
  for entry in run1:999 run2:999 run3:5150 run4:20261103; do
    name=${entry%%:*}
    seed=${entry##*:}
    mkdir -p "$hs/$name"
    echo "$seed" > "$hs/$name/seed"
    if [ "$name" = run4 ]; then
      # A damaged record: binary garbage, not a valid file (section 9).
      printf 'this file is not a record\x00\x01\x02high_score=9999999' \
        > "$hs/save/highscore.dat"
      cp "$hs/save/highscore.dat" "$hs/corrupt-highscore.dat"
    fi
    echo "process:   $name (seed $seed)"
    timeout --foreground "$timeout_s" \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$game" --seed "$seed" --tick-per-frame \
      --input-script "$script" --fixture-dir "$fixtures" \
      --save-dir "$hs/save" \
      --observations "$hs/$name/run.obs" \
      --input-log "$hs/$name/input.log" \
      --frames 6000 > "$hs/$name/run.log" 2>&1 ||
      fail "high-score $name exited nonzero; see $hs/$name/run.log"
    # Snapshot the shared record after every process: later runs keep
    # writing the same file, and each run's saved state is evidence.
    [ -f "$hs/save/highscore.dat" ] &&
      cp "$hs/save/highscore.dat" "$hs/$name/highscore.after"
  done
  "$reference" --seed 999 --script "$script" \
    --fixture-dir "$fixtures" --out "$hs/run1/ref.obs" \
    > "$hs/run1/ref.log" 2>&1 ||
    fail "reference replay failed for the high-score script"
fi

# ---------------------------------------------------------------------------
if want_phase regress; then
  echo
  echo "=== phase regress (retained full-game suites) ==="
  mkdir -p "$run_dir/regress"
  echo "t015: T023 harness (run.sh)"
  if HARNESS_RUN_ID="$run_id-harness" "$repo/tests/harness/run.sh" \
      > "$run_dir/regress/harness.log" 2>&1; then
    echo "passed: $(tail -1 "$repo/build/agent-checks/$run_id-harness/check-passed.txt" 2>/dev/null || echo run.sh green)" \
      > "$run_dir/regress/harness.status"
  else
    echo "failed: see $run_dir/regress/harness.log" \
      > "$run_dir/regress/harness.status"
  fi
  cat "$run_dir/regress/harness.status"

  for pair in "t024-debug:$build_dir:" "t024-release:$release_dir:T024_BUILD_DIR=$release_dir"; do
    name=${pair%%:*}
    rest=${pair#*:}
    dir=${rest%%:*}
    export_env=${rest#*:}
    echo "t015: T024 play sessions ($name)"
    extra=""
    [ -n "${T015_SKIP_X11:-}" ] && extra="T024_SKIP_X11=1"
    if env $export_env T024_RUN_ID="$run_id-$name" $extra \
        xvfb-run -a -s "-screen 0 960x544x24" \
        "$repo/tests/harness/t024.sh" \
        > "$run_dir/regress/$name.log" 2>&1; then
      echo "passed: t024.sh green in $dir" > "$run_dir/regress/$name.status"
    else
      status=$?
      if [ "$status" -eq 127 ]; then
        echo "unavailable: xvfb-run missing for $name" \
          > "$run_dir/regress/$name.status"
      else
        echo "failed: see $run_dir/regress/$name.log" \
          > "$run_dir/regress/$name.status"
      fi
    fi
    cat "$run_dir/regress/$name.status"
  done
fi

# ---------------------------------------------------------------------------
if want_phase stability; then
  echo
  echo "=== phase stability (T025: $t025_phases) ==="
  mkdir -p "$run_dir/stability"
  T025_PHASES="$t025_phases" T025_RUN_ID="$run_id-t025" \
    "$repo/tests/harness/t025.sh" > "$run_dir/stability/t025.log" 2>&1
  t025_code=$?
  t025_dir="$repo/build/agent-checks/$run_id-t025"
  for phase in $t025_phases; do
    if [ -f "$t025_dir/$phase.status" ]; then
      cp "$t025_dir/$phase.status" "$run_dir/stability/$phase.status"
    else
      echo "failed: T025 wrote no $phase.status" \
        > "$run_dir/stability/$phase.status"
    fi
    echo "t015: T025 $phase: $(cat "$run_dir/stability/$phase.status" | head -1)"
  done
  [ "$t025_code" -eq 0 ] || echo "t015: T025 driver exited $t025_code (see $run_dir/stability/t025.log)"
fi

# ---------------------------------------------------------------------------
if want_phase performance; then
  echo
  echo "=== phase performance (T026 verify) ==="
  mkdir -p "$run_dir/performance"
  T026_PHASES=verify T026_TAG=gate T026_RUN_ID="$run_id-t026" \
    "$repo/tests/harness/t026.sh" > "$run_dir/performance/t026.log" 2>&1
  t026_code=$?
  t026_dir="$repo/build/agent-checks/$run_id-t026"
  if [ "$t026_code" -eq 0 ] && [ ! -s "$t026_dir/failures.txt" ]; then
    echo "passed: every benchmark scenario proved its claim" \
      > "$run_dir/performance/verify.status"
  else
    echo "failed: T026 verify exited $t026_code (see $run_dir/performance/t026.log)" \
      > "$run_dir/performance/verify.status"
  fi
  cat "$run_dir/performance/verify.status"
fi

# ---------------------------------------------------------------------------
echo
if want_phase check; then
  echo "=== phase check (independent gate checker) ==="
  python3 "$checker" "$run_dir"
  status=$?
else
  status=0
fi

echo
if [ "$status" -eq 0 ]; then
  echo "t015: M3 GATE PASS"
  echo "t015: evidence in $run_dir"
else
  echo "t015: M3 GATE FAIL"
  echo "t015: failure evidence in $run_dir"
fi
exit "$status"
