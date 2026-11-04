#!/bin/sh
# T035 release-version check (docs/AGENT_TESTING.md, task T035).
#
# One command proves the shipped version is one value everywhere a player,
# a package browser, or a test can read it:
#
#   tests/harness/t035.sh
#
# Phases, each writing into build/agent-checks/<run-id>/:
#
#   build    the desktop build and the headless ctest suites; the sanity
#            pin and the five re-pinned replay hashes fail loudly at any
#            version but the one the sources declare
#   banner   a bounded headless run of the real binary under the dummy
#            video and audio drivers, with an explicit save directory, so
#            the startup banner lands on stdout and in the session log.txt
#   check    tests/harness/check_t035.py, the independent gate checker
#
# Environment variables: T035_BUILD_DIR (default build),
# T035_RUN_ID (default t035-version-<date>), T035_TIMEOUT_S (default 120),
# T035_SKIP_BUILD=1 to reuse the desktop build.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T035_BUILD_DIR:-$repo/build}
run_id=${T035_RUN_ID:-t035-version-$(date +%Y-%m-%d)}
timeout_s=${T035_TIMEOUT_S:-120}
run_dir="$repo/build/agent-checks/$run_id"
checker="$repo/tests/harness/check_t035.py"

fail() {
  echo "t035: $*" >&2
  exit 1
}

# The run id names one directory below build/agent-checks. The rerun
# cleanup below deletes that directory, so the id may not reach outside
# the evidence root.
case "$run_id" in
  */*|""|.|..) fail "T035_RUN_ID must name one directory, not '$run_id'" ;;
esac

[ -f "$checker" ] || fail "missing checker $checker"

rm -rf "$run_dir"
mkdir -p "$run_dir" || fail "cannot create $run_dir"

echo "t035: repository $repo"
echo "t035: evidence   $run_dir"

# ---------------------------------------------------------------------------
# Phase: source identity. Every file the checker reads, so a run ties to
# exact sources without a commit.
# ---------------------------------------------------------------------------
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  cd "$repo" && sha256sum include/stackfall/version.hpp src/version.cpp \
    CMakeLists.txt tests/test_sanity.cpp tests/test_regression.cpp \
    tests/harness/check_t015.py portmaster/release-notes.md \
    portmaster/gameinfo.xml portmaster/port.json portmaster/README.md \
    portmaster/Stackfall.sh tests/harness/t035.sh tests/harness/check_t035.py
} > "$run_dir/source.sha256" || fail "cannot record source identity"
cat "$run_dir/source.sha256"

# ---------------------------------------------------------------------------
# Phase: the desktop build and the headless suites.
# ---------------------------------------------------------------------------
echo
echo "=== phase build ==="
if [ "${T035_SKIP_BUILD:-0}" != "1" ]; then
  cmake -S "$repo" -B "$build_dir" -G Ninja >/dev/null ||
    fail "cmake configure failed in $build_dir"
  cmake --build "$build_dir" || fail "build failed in $build_dir"
fi
[ -x "$build_dir/stackfall" ] || fail "missing desktop game $build_dir/stackfall"

if ctest --test-dir "$build_dir" --output-on-failure >"$run_dir/ctest.log" 2>&1
then
  tail -3 "$run_dir/ctest.log"
else
  cat "$run_dir/ctest.log"
  fail "the headless suites failed; see $run_dir/ctest.log"
fi

# ---------------------------------------------------------------------------
# Phase: the startup banner of the real binary.
# ---------------------------------------------------------------------------
echo
echo "=== phase banner ==="
timeout --foreground "$timeout_s" \
  env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$build_dir/stackfall" --seed 20261103 --frames 120 \
  --save-dir "$run_dir/session" >"$run_dir/run.log" 2>&1
echo "$?" >"$run_dir/exit.txt"
grep -m1 "^STACKFALL " "$run_dir/run.log" || fail "no banner in $run_dir/run.log"

# ---------------------------------------------------------------------------
# Phase: the independent checker.
# ---------------------------------------------------------------------------
echo
echo "=== phase check ==="
python3 "$checker" "$run_dir" "$repo" || fail "the version check failed"
echo "t035: pass"
echo "evidence : $run_dir"
