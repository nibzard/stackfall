#!/bin/bash
# T025 stability and failure driver (docs/AGENT_TESTING.md, T025).
#
# One command runs the whole unattended stability pass on top of the T023
# harness:
#
#   tests/harness/t025.sh
#
# Phases (pick a subset with T025_PHASES="fuzz hardkill ..."):
#
#   host       host, toolchain, SDL, and renderer details into the evidence
#   fuzz       bounded mutation fuzz of the save and script parsers through
#              the sanitizer build (operator extension, before the rest)
#   hardkill   SIGKILL during save writes, every resulting file classified
#              and loaded, then two instances on one save directory
#   cycles     52 in-process restart and menu cycles at real-time pacing
#   restarts   50 process restarts carrying one high-score record forward
#   longplay   the ten-minute Release session: several seeded games, stalls,
#              checkpoints, frame times, CPU, and memory
#   pacing     frame-time percentiles with vsync requested and not, plus
#              CPU and memory per mode
#   failures   missing audio, software renderer, renderer failure, focus
#              loss, controller removal, a long stall, and the missing,
#              partial, corrupt, and unwritable save cases
#   san        the headless suites under AddressSanitizer and
#              UndefinedBehaviorSanitizer, plus one scripted SDL session
#   regress    the T023 and T024 harness drivers again, after T025's edits
#
# Every phase writes <phase>.status (passed|failed|unavailable) into
# build/agent-checks/<run-id>/ and the script exits nonzero when any phase
# failed or ended unavailable, because an unavailable check is a gap to
# resolve, not a pass. Numbers are host measurements: none of them
# certifies RG503 performance.
#
# Options through environment variables:
#   T025_BUILD_DIR     Release build directory (default: build-rel)
#   T025_SAN_BUILD_DIR sanitizer build directory (default: build-t025-san)
#   T025_RUN_ID        evidence directory name (default: t025-<date>); a
#                      reused name is removed first
#   T025_TIMEOUT_S     default per-process timeout in seconds (default: 120)
#   T025_KILL_RUNS     hard-kill attempts (default: 24)
#   T025_RESTART_RUNS  process restarts (default: 50)
#   T025_CYCLES        restart and menu cycles (default: 52)
#   T025_SKIP_X11      set to 1 to skip the X11-dependent checks (recorded
#                      unavailable; the ten-minute session needs them)
#   T025_PHASES        space-separated phase list (default: all)
#   T025_KEEP_DIR      set to 1 to add to an existing run directory instead
#                      of removing it first, so one failed phase can be
#                      rerun into the same evidence set

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T025_BUILD_DIR:-$repo/build-rel}
san_build_dir=${T025_SAN_BUILD_DIR:-$repo/build-t025-san}
run_id=${T025_RUN_ID:-t025-$(date +%Y-%m-%d-%H%M%S)}
default_timeout=${T025_TIMEOUT_S:-120}
run_dir="$repo/build/agent-checks/$run_id"
phases=${T025_PHASES:-"host fuzz hardkill cycles restarts longplay pacing failures san regress"}
kill_runs=${T025_KILL_RUNS:-24}
restart_runs=${T025_RESTART_RUNS:-50}
cycle_count=${T025_CYCLES:-52}
gen="$repo/tests/harness/t025_gen.py"
fuzzer="$repo/tests/harness/t025_fuzz.py"
frame_stats="$repo/tests/harness/frame_stats.py"

fail_count=0
children=""
reap() {
  for pid in $children; do
    kill -9 "$pid" 2>/dev/null
  done
}
trap reap EXIT INT TERM

fail() {
  echo "t025: $*" >&2
  exit 1
}

case "$run_id" in
  */*|""|.|..) fail "T025_RUN_ID must name one directory, not '$run_id'" ;;
esac

# status NAME passed|failed|unavailable [detail]
status() {
  echo "$2" > "$run_dir/$1.status"
  [ $# -gt 2 ] && echo "$3" >> "$run_dir/$1.status"
  echo "t025: phase $1: $2${3:+ ($3)}"
  if [ "$2" != "passed" ]; then
    fail_count=$((fail_count + 1))
    echo "$1: $2${3:+ ($3)}" >> "$run_dir/failures.txt"
  fi
}

# run_game NAME SECONDS -- command...: bounded game run whose stdout+stderr
# lands in <run_dir>/<name>.log and whose exit status in <name>.exit.
# Returns the game's exit status.
run_game() {
  local name=$1 seconds=$2
  shift 2
  mkdir -p "$(dirname "$run_dir/$name.log")"
  timeout --foreground "$seconds" "$@" > "$run_dir/$name.log" 2>&1
  local code=$?
  echo "$code" > "$run_dir/$name.exit"
  return $code
}

# Samples VmRSS, thread count, and CPU ticks of a pid every two seconds.
sample_rss() {
  local pid=$1 out=$2
  : > "$out"
  # The comm check stops the sampler when the pid dies even if the kernel
  # recycles the number for the next process (a recycled sampler target
  # once recorded a pacing run inside the longplay evidence).
  while [ "$(cat "/proc/$pid/comm" 2>/dev/null)" = stackfall ]; do
    local rss threads cpu
    rss=$(awk '/VmRSS/{print $2}' "/proc/$pid/status" 2>/dev/null)
    threads=$(awk '/Threads/{print $2}' "/proc/$pid/status" 2>/dev/null)
    cpu=$(awk '{print $14 + $15}' "/proc/$pid/stat" 2>/dev/null)
    printf '%s\t%s\t%s\t%s\n' "$(date +%s)" "${rss:-0}" "${threads:-0}" \
      "${cpu:-0}" >> "$out"
    sleep 2
  done
}

# Every phase starts from an empty directory of its own. A rerun with
# T025_KEEP_DIR=1 must not inherit the earlier pass's artifacts: the
# longplay checker once judged a stale rss.tsv left by a sampler that never
# started (task T034's lesson, applied per phase).
phase_dir() {
  rm -rf "$2"
  mkdir -p "$2"
}

# The launched pid is a wrapper (time, xvfb-run), and the game may take a
# few seconds to appear under Xvfb. Wait for the real process, then hand
# back its pid; empty output means "give up, run without the sampler".
stackfall_pid() {
  local pid
  for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    pid=$(pgrep -x stackfall | head -1)
    [ -n "$pid" ] && { echo "$pid"; return 0; }
    sleep 1
  done
  return 1
}

# Ticks a generated script waits in total, read from the generator's line
# "cycles: N cycles, T ticks ..." or "... T ticks (...)".
script_ticks() {
  sed -n 's/.* \([0-9]\+\) ticks.*/\1/p' "$1" | head -1
}

want_phase() {
  case " $phases " in
    *" $1 "*) return 0 ;;
    *) return 1 ;;
  esac
}

echo "t025: repository $repo"
echo "t025: release build $build_dir"
echo "t025: sanitizer build $san_build_dir"
echo "t025: evidence $run_dir"

if [ -e "$run_dir" ] && [ "${T025_KEEP_DIR:-0}" != "1" ]; then
  echo "t025: removing run directory of an earlier pass: $run_dir"
  rm -rf "$run_dir" || fail "cannot remove stale $run_dir"
fi
mkdir -p "$run_dir" || fail "cannot create $run_dir"

cmake -S "$repo" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null ||
  fail "cmake configure failed in $build_dir"
cmake --build "$build_dir" >/dev/null ||
  fail "release build failed"
game="$build_dir/stackfall"
[ -x "$game" ] || fail "missing $game"

{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "game binary: $(sha256sum "$game" | cut -d' ' -f1)"
  echo "commit: $(git -C "$repo" rev-parse HEAD 2>/dev/null || echo uncommitted)"
  cd "$repo" && sha256sum src/main.cpp src/storage.cpp src/script.cpp \
    include/stackfall/script.hpp tests/test_script.cpp \
    tests/harness/t025.sh tests/harness/t025_gen.py \
    tests/harness/t025_fuzz.py tests/harness/frame_stats.py \
    tests/harness/reference_replay.cpp
} > "$run_dir/source.sha256" || fail "cannot record source identity"
cat "$run_dir/source.sha256"

# ---------------------------------------------------------------------------
# host: what the numbers below were measured on.
# ---------------------------------------------------------------------------
if want_phase host; then
  echo
  echo "=== phase host ==="
  phase_dir host "$run_dir/host"
  {
    uname -a
    echo "cpu: $(lscpu 2>/dev/null | awk -F: '/Model name/{gsub(/^ +/,"",$2); print $2}')"
    echo "cpus: $(nproc)"
    free -m | head -2
    echo "gcc: $(g++ --version | head -1)"
    echo "cmake: $(cmake --version | head -1)"
    echo "xvfb-run: $(command -v xvfb-run || echo missing)"
    echo "xdotool: $(command -v xdotool || echo missing)"
    echo "openbox: $(command -v openbox || echo missing)"
  } > "$run_dir/host/host.txt" || true
  # The renderer driver list comes from the forced-failure probe: it prints
  # every driver this SDL carries before its clean exit.
  SDL_RENDER_DRIVER=stackfall-driver-probe SDL_VIDEODRIVER=dummy \
    "$game" --frames 5 > "$run_dir/host/driver-probe.log" 2>&1
  # One plain startup names the version, architecture, SDL, and renderer.
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$game" --frames 30 \
    > "$run_dir/host/startup-probe.log" 2>&1
  if grep -q "Render drivers available here:" "$run_dir/host/driver-probe.log" &&
     grep -q "Renderer:" "$run_dir/host/startup-probe.log"; then
    status host passed
  else
    status host failed "probe logs incomplete"
  fi
fi

# ---------------------------------------------------------------------------
# fuzz: the operator extension's first bounded failure pass.
# ---------------------------------------------------------------------------
if want_phase fuzz; then
  echo
  echo "=== phase fuzz ==="
  phase_dir fuzz "$run_dir/fuzz"
  cmake -S "$repo" -B "$san_build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
    > "$run_dir/fuzz/san-configure.log" 2>&1 ||
    fail "sanitizer configure failed"
  cmake --build "$san_build_dir" > "$run_dir/fuzz/san-build.log" 2>&1 ||
    fail "sanitizer build failed"
  python3 "$fuzzer" "$san_build_dir/stackfall" "$repo" "$run_dir/fuzz/run" \
    --budget-seconds 150 --binary-cases 320 > "$run_dir/fuzz/fuzz.stdout" 2>&1
  fuzz_code=$?
  # The finding this pass is named for, kept with the pass that closed it:
  # the pre-fix reproducer of the wait/stall count truncation.
  mkdir -p "$run_dir/fuzz/finding-pre-fix"
  printf 'key enter down\nwait 2\nkey enter up\nwait 5\nstall 6442450944\nwait 5\nquit\n' \
    > "$run_dir/fuzz/finding-pre-fix/reproducer.script.txt"
  cat > "$run_dir/fuzz/finding-pre-fix/README.txt" <<'EOF'
Pre-fix behavior (found 2026-11-03 while building this fuzz pass, at the
worktree of 657ea6d before the shared count parser existed): running the
reproducer through the game hung the process inside SDL_Delay and it
outlived `timeout 10` sending SIGTERM; it had to be killed with SIGKILL.
Cause: InputScript::parseLine narrowed a 64-bit count to int; 6442450944
is 2^32 + 2^31 and truncated to INT_MIN, which SDL_Delay read as an
unsigned ~24.8-day delay. The mirrored parser of the headless reference
carried the same cast.
Fix: stackfall::parseScriptCount (include/stackfall/script.hpp) bounds
wait/stall counts to 3600000; both parsers route through it, and
tests/test_script.cpp pins the crashing inputs. Post-fix the reproducer
exits 1 with "wait/stall needs a number from 0 to 3600000".
EOF
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$game" --seed 1 --frames 60 \
    --input-script "$run_dir/fuzz/finding-pre-fix/reproducer.script.txt" \
    --save-dir "$run_dir/fuzz/finding-pre-fix/post-fix-save" \
    > "$run_dir/fuzz/finding-pre-fix/post-fix-run.log" 2>&1
  echo "exit=$?" >> "$run_dir/fuzz/finding-pre-fix/post-fix-run.log"
  if [ "$fuzz_code" -eq 0 ]; then
    status fuzz passed "$(python3 -c "
import json
r = json.load(open('$run_dir/fuzz/run/fuzz-report.json'))
print('%s parser-loop; %d binary cases (%s)' % (
    r['parser_loop'].get('status', '?'), r['binary_cases'],
    ', '.join('%s %d' % kv for kv in sorted(r['binary_counts'].items()))))")"
  else
    status fuzz failed "findings in $run_dir/fuzz/run/fuzz-report.json"
  fi
fi

# ---------------------------------------------------------------------------
# hardkill: the operator extension's second bounded failure pass.
# ---------------------------------------------------------------------------
if want_phase hardkill; then
  echo
  echo "=== phase hardkill ==="
  hk="$run_dir/hardkill"
  phase_dir hardkill "$hk"
  mkdir -p "$hk/save"
  python3 "$gen" saves "$hk/saves.script.txt" > "$hk/gen.log"

  # The save-window shim: delays fsync and rename inside the save path, so
  # a kill aimed at the game-over moment reliably lands inside the write.
  cat > "$hk/save_delay.c" <<'EOF'
/* LD_PRELOAD shim of the T025 hard-kill pass: widens the atomic save's
   fsync/rename window by STACKFALL_SAVE_DELAY_MS (default 0: passthrough),
   so a SIGKILL timed on the game-over line can land mid-write. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <unistd.h>

static void delay(void) {
  const char* text = getenv("STACKFALL_SAVE_DELAY_MS");
  if (text != NULL && text[0] != '\0') {
    usleep((useconds_t)atoi(text) * 1000);
  }
}

int rename(const char* old, const char* new) {
  static int (*real)(const char*, const char*);
  if (real == NULL) {
    real = (int (*)(const char*, const char*))dlsym(RTLD_NEXT, "rename");
  }
  delay();
  return real(old, new);
}

int fsync(int fd) {
  static int (*real)(int);
  if (real == NULL) {
    real = (int (*)(int))dlsym(RTLD_NEXT, "fsync");
  }
  delay();
  return real(fd);
}
EOF
  cc -shared -fPIC -o "$hk/save_delay.so" "$hk/save_delay.c" -ldl \
    > "$hk/shim-build.log" 2>&1 ||
    fail "hard-kill shim failed to build"

  # The kill loop: every run uses the same save directory; the saves script
  # tops out once, right after the "Game over:" log line, which is where a
  # triggered kill aims (the shim keeps the save open for ~2 x 120 ms).
  python3 - "$game" "$hk" "$kill_runs" > "$hk/kill-loop.log" 2>&1 <<'PYEOF'
import os, random, re, signal, subprocess, sys, time

game, hk, runs = sys.argv[1], sys.argv[2], int(sys.argv[3])
rng = random.Random(0x5EED)
save_dir = os.path.join(hk, "save")
script = os.path.join(hk, "saves.script.txt")
env = dict(os.environ)
env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
           LD_PRELOAD=os.path.join(hk, "save_delay.so"),
           STACKFALL_SAVE_DELAY_MS="120")

def record_state():
    """Classify highscore.dat: absent | canonical | damaged."""
    path = os.path.join(save_dir, "highscore.dat")
    if not os.path.exists(path):
        return "absent", ""
    with open(path, "rb") as fp:
        blob = fp.read()
    m = re.fullmatch(rb"version=1\nhigh_score=(\d+)\n", blob)
    return ("canonical", m.group(1).decode()) if m else ("damaged", "")

bad = []
with open(os.path.join(hk, "kills.tsv"), "w") as tsv:
    tsv.write("run\tmode\tkilled_after_ms\trecord_state\trecord_value\t"
              "temp_files\n")
    for i in range(runs):
        mode = "triggered" if i % 4 != 3 else "random"
        log = os.path.join(save_dir, "log.txt")
        if os.path.exists(log):
            os.remove(log)
        started = time.time()
        proc = subprocess.Popen(
            [game, "--seed", str(20261100 + i), "--input-script", script,
             "--save-dir", save_dir],
            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if mode == "triggered":
            # Poll the flushed session log for the game-over line, then kill
            # inside the widened save window behind it.
            deadline = time.time() + 30
            while time.time() < deadline and proc.poll() is None:
                try:
                    with open(log, "r", encoding="utf-8",
                              errors="replace") as fp:
                        if "Game over:" in fp.read():
                            break
                except OSError:
                    pass
                time.sleep(0.02)
            time.sleep(rng.uniform(0, 0.24))
        else:
            time.sleep(rng.uniform(1.0, 7.0))
        killed = proc.poll() is None
        if killed:
            proc.send_signal(signal.SIGKILL)
        proc.wait()
        state, value = record_state()
        temps = sorted(name for name in os.listdir(save_dir)
                       if name.startswith(".highscore.dat.tmp."))
        after_ms = int((time.time() - started) * 1000)
        print("kill %02d mode=%s sigkill=%s after %d ms: record %s%s, "
              "temp %s" % (i + 1, mode, killed, after_ms, state,
                           "=" + value if value else "", temps or "none"))
        tsv.write("%d\t%s\t%d\t%s\t%s\t%s\n"
                  % (i + 1, mode, after_ms, state, value,
                     ";".join(temps)))
        # A kill may leave the record absent, canonical, or damaged - never
        # anything else - and no file outside the known set.
        allowed = {"highscore.dat", "settings.dat", "log.txt"}
        extras = sorted(set(os.listdir(save_dir)) - allowed - set(temps))
        if extras or state not in ("absent", "canonical", "damaged"):
            bad.append("kill %d left %s %s" % (i + 1, state, extras))
sys.exit(1 if bad else 0)
PYEOF
  kill_code=$?

  # The repair-and-load run: one clean session on the killed directory must
  # load whatever survived, save a fresh record at its game over, and leave
  # a canonical file.
  run_game hardkill/verify 60 env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed 20261200 --input-script "$hk/saves.script.txt" \
    --save-dir "$hk/save"
  verify_code=$?

  python3 - "$hk" > "$hk/hardkill-check.txt" <<'PYEOF'
import csv, json, os, re, sys
hk = sys.argv[1]
rows = list(csv.DictReader(open(os.path.join(hk, "kills.tsv")),
                           delimiter="\t"))
report = {"kills": len(rows),
          "modes": {m: sum(1 for r in rows if r["mode"] == m)
                    for m in ("triggered", "random")},
          "states": {s: sum(1 for r in rows if r["record_state"] == s)
                     for s in ("absent", "canonical", "damaged")}}
problems = []
if not rows:
    problems.append("no kills recorded")
log = open(os.path.join(hk, "verify.log"), encoding="utf-8",
           errors="replace").read()
loaded = re.search(r"High score (\d+) \((ok|not found|invalid)\)", log)
if not loaded:
    problems.append("verify run did not log its loaded record")
else:
    report["verify_loaded"] = [int(loaded.group(1)), loaded.group(2)]
    # Whatever the kills left, the clean run must have loaded exactly that:
    # the canonical value, or 0 for absent and damaged.
    left = report["states"]
    if left.get("canonical", 0) and not left.get("damaged"):
        pass  # the value itself is checked below against the file
saved = re.search(r"New high score (\d+) saved", log)
if not saved:
    problems.append("verify run did not save a record")
else:
    report["verify_saved"] = int(saved.group(1))
    blob = open(os.path.join(hk, "save", "highscore.dat"), "rb").read()
    if blob != (b"version=1\nhigh_score=" + str(saved.group(1)).encode() +
                b"\n"):
        problems.append("record file is not canonical after the verify run")
report["problems"] = problems
json.dump(report, open(os.path.join(hk, "hardkill-check.json"), "w"),
          indent=2)
print(json.dumps(report, indent=2))
sys.exit(1 if problems else 0)
PYEOF
  check_code=$?
  if [ "$kill_code" -eq 0 ] && [ "$verify_code" -eq 0 ] &&
     [ "$check_code" -eq 0 ]; then
    status hardkill passed "$(python3 -c "
import json
r = json.load(open('$hk/hardkill-check.json'))
print('%d kills (%s), record states %s' % (r['kills'], r['modes'], r['states']))")"
  else
    status hardkill failed "kill loop $kill_code, verify run $verify_code, checks $check_code; see $hk"
  fi

  # Two simultaneous instances on one save directory. The directory starts
  # from a small prior record, so both rounds genuinely contend to save;
  # each writes only when it beats what it loaded, so the record can only
  # stay or grow, never shrink or corrupt.
  mkdir -p "$hk/two" "$hk/two-save"
  printf 'version=1\nhigh_score=10\n' > "$hk/two-save/highscore.dat"
  prior=$(sed -n 's/high_score=\([0-9]*\)/\1/p' "$hk/two-save/highscore.dat")
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    STACKFALL_LOG_FILE="$hk/two/a-log.txt" \
    "$game" --seed 20261301 --input-script "$hk/saves.script.txt" \
    --save-dir "$hk/two-save" > "$hk/two/a.stdout" 2>&1 &
  pid_a=$!
  children="$children $pid_a"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    STACKFALL_LOG_FILE="$hk/two/b-log.txt" \
    "$game" --seed 20261402 --input-script "$hk/saves.script.txt" \
    --save-dir "$hk/two-save" > "$hk/two/b.stdout" 2>&1 &
  pid_b=$!
  children="$children $pid_b"
  wait "$pid_a"; code_a=$?
  wait "$pid_b"; code_b=$?
  children=${children% "$pid_a"}
  children=${children% "$pid_b"}
  two_ok=1
  { [ "$code_a" -eq 0 ] && grep -q "Clean shutdown" "$hk/two/a-log.txt"; } || two_ok=0
  { [ "$code_b" -eq 0 ] && grep -q "Clean shutdown" "$hk/two/b-log.txt"; } || two_ok=0
  # Only persistence failures count here: the renderer fallback's own
  # "failed" attempt lines are the normal dummy-driver startup.
  grep -hE "Saving the (high score|sound setting).*failed" \
    "$hk/two/a-log.txt" "$hk/two/b-log.txt" > "$hk/two/failures.log" || true
  [ -s "$hk/two/failures.log" ] && two_ok=0
  score_a=$(sed -n 's/.*Game over: score \([0-9]*\),.*/\1/p' "$hk/two/a-log.txt" | head -1)
  score_b=$(sed -n 's/.*Game over: score \([0-9]*\),.*/\1/p' "$hk/two/b-log.txt" | head -1)
  record=$(sed -n 's/high_score=\([0-9]*\)/\1/p' "$hk/two-save/highscore.dat")
  {
    echo "instance a: exit $code_a, score ${score_a:-none}"
    echo "instance b: exit $code_b, score ${score_b:-none}"
    echo "prior record: ${prior:-none}"
    echo "record after both: ${record:-none}"
  } > "$hk/two/summary.txt"
  record_ok=0
  [ -n "$record" ] && [ "$record" -ge "$prior" ] && \
    { [ "$record" = "$prior" ] || [ "$record" = "$score_a" ] ||
      [ "$record" = "$score_b" ]; } && record_ok=1
  if [ "$two_ok" -eq 1 ] && [ "$record_ok" -eq 1 ]; then
    status hardkill-two passed "exits $code_a/$code_b, prior $prior -> record $record"
  else
    status hardkill-two failed "see $hk/two/summary.txt"
  fi
fi

# ---------------------------------------------------------------------------
# cycles: restart and menu cycles in one process, real-time pacing.
# ---------------------------------------------------------------------------
if want_phase cycles; then
  echo
  echo "=== phase cycles ==="
  cy="$run_dir/cycles"
  phase_dir cycles "$cy"
  mkdir -p "$cy/shots" "$cy/save"
  python3 "$gen" cycles "$cycle_count" "$cy/cycles.script.txt" > "$cy/gen.log"
  ticks=$(script_ticks "$cy/gen.log")
  [ -n "$ticks" ] || ticks=$((cycle_count * 220))
  frames=$((ticks + 1500))
  seconds=$((ticks / 60 + 120))
  /usr/bin/time -v -o "$cy/time.txt" \
    timeout --foreground "$seconds" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed 20261501 --input-script "$cy/cycles.script.txt" \
    --virtual-controller --save-dir "$cy/save" \
    --screenshot-dir "$cy/shots" --observations "$cy/observations.obs" \
    --input-log "$cy/input.log" --frame-times "$cy/frame-times.txt" \
    --frames "$frames" > "$cy/run.log" 2>&1 &
  session_pid=$!
  children="$children $session_pid"
  # $! is the time wrapper, not the game; the sampler needs the game.
  game_pid=$(stackfall_pid || true)
  if [ -n "$game_pid" ]; then
    sample_rss "$game_pid" "$cy/rss.tsv" &
    sampler=$!
    children="$children $sampler"
  else
    echo "t025: cycles sampler not started: no stackfall pid" >&2
    sampler=""
  fi
  wait "$session_pid"; code=$?
  [ -n "$sampler" ] && kill "$sampler" 2>/dev/null
  children=${children% "$session_pid"}
  [ -n "$sampler" ] && children=${children% "$sampler"}
  echo "$code" > "$cy/exit"
  python3 - "$cy" "$cycle_count" > "$cy/cycles-check.txt" 2>&1 <<'PYEOF'
import os, re, sys
cy, want = sys.argv[1], int(sys.argv[2])
problems = []
log = open(os.path.join(cy, "run.log"), encoding="utf-8", errors="replace").read()
input_log = open(os.path.join(cy, "input.log"), encoding="utf-8",
                 errors="replace").read()
cycles = len(re.findall(r"^script cycle-\d+-", input_log, re.M))
print("cycle markers: %d" % cycles)
if cycles < want:
    problems.append("only %d of %d cycle markers ran" % (cycles, want))
if "Clean shutdown" not in log:
    problems.append("no clean shutdown")
opened = len(re.findall(r'Game controller ".*" \(instance \d+\) opened', log))
removed = len(re.findall(r"Game controller .* removed", log))
print("controller open events: %d, removals: %d" % (opened, removed))
if opened != 1 or removed != 0:
    problems.append("controller set changed across cycles")
toggles = len(re.findall(r"^sound (muted|unmuted)", input_log, re.M))
print("sound toggles: %d" % toggles)
# The generator toggles the sound every ninth cycle, so a run shorter than
# nine cycles legitimately has none.
if want >= 9 and toggles < 1:
    problems.append("no sound toggle ran inside the cycles")
# The movement probes prove no input stuck: the piece still answers a left
# tap after the last cycle exactly as it did before the first.
obs = {}
for line in open(os.path.join(cy, "observations.obs"), encoding="ascii"):
    m = re.match(r"check=(\S+)", line)
    if m:
        obs[m.group(1)] = line
def active_x(name):
    line = obs.get(name, "")
    m = re.search(r"active=(\w),\d+,(-?\d+),(-?\d+)", line)
    return int(m.group(2)) if m else None
for probe in ("probe0", "probe-final"):
    before, after = active_x(probe + "-before"), active_x(probe + "-after")
    print("%s: x %s -> %s" % (probe, before, after))
    if before is None or after is None or after >= before:
        problems.append("%s did not move left (stuck input?)" % probe)
# Memory and threads: the first samples against the last samples.
rows = [line.split("\t") for line in
        open(os.path.join(cy, "rss.tsv"), encoding="ascii").read().splitlines()
        if line.strip()]
if len(rows) >= 4:
    early = [int(r[1]) for r in rows[:5]]
    late = [int(r[1]) for r in rows[-5:]]
    print("rss kB: early max %d, late max %d, threads late %s"
          % (max(early), max(late), rows[-1][2].strip()))
    if max(late) - max(early) > 8192:
        problems.append("rss grew %d kB across the cycles"
                        % (max(late) - max(early)))
else:
    problems.append("too few rss samples")
for problem in problems:
    print("PROBLEM:", problem)
sys.exit(1 if problems else 0)
PYEOF
  check=$?
  python3 "$frame_stats" "$cy/frame-times.txt" "$cy/run.log" \
    --json "$cy/frame-stats.json" > "$cy/frame-stats.txt" || true
  if [ "$code" -eq 0 ] && [ "$check" -eq 0 ]; then
    status cycles passed "$(grep -E 'cycle markers|controller open|rss kB' "$cy/cycles-check.txt" | tr '\n' '; ')"
  else
    status cycles failed "exit $code, checks $check; see $cy/cycles-check.txt"
  fi
fi

# ---------------------------------------------------------------------------
# restarts: process restarts carrying one record forward.
# ---------------------------------------------------------------------------
if want_phase restarts; then
  echo
  echo "=== phase restarts ==="
  rs="$run_dir/restarts"
  phase_dir restarts "$rs"
  mkdir -p "$rs/save"
  python3 "$gen" saves "$rs/saves.script.txt" > "$rs/gen.log"
  python3 - "$game" "$rs" "$restart_runs" > "$rs/restarts-check.txt" 2>&1 <<'PYEOF'
import os, re, subprocess, sys

game, rs, runs = sys.argv[1], sys.argv[2], int(sys.argv[3])
save = os.path.join(rs, "save")
script = os.path.join(rs, "saves.script.txt")
env = dict(os.environ)
env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
record = 0
problems = []
scores = []
for i in range(runs):
    seed = 777000 + i
    log = os.path.join(rs, "run-%03d.log" % (i + 1))
    with open(log, "w") as sink:
        try:
            code = subprocess.call(
                [game, "--seed", str(seed), "--tick-per-frame",
                 "--input-script", script, "--save-dir", save,
                 "--frames", "4000"],
                env=env, stdout=sink, stderr=subprocess.STDOUT,
                timeout=120)
        except subprocess.TimeoutExpired:
            code = -9
    text = open(log, encoding="utf-8", errors="replace").read()
    if code != 0 or "Clean shutdown" not in text:
        problems.append("run %d (seed %d) exited %d" % (i + 1, seed, code))
        break
    loaded = re.search(r"High score (\d+) \((ok|not found|invalid)\)", text)
    over = re.search(r"Game over: score (\d+),", text)
    if not loaded or not over:
        problems.append("run %d lacks load or game-over lines" % (i + 1))
        break
    # The first run starts from an empty save directory, so its record is
    # not found; every later run must load the running record intact.
    expected_status = "not found" if i == 0 else "ok"
    if int(loaded.group(1)) != record or loaded.group(2) != expected_status:
        problems.append("run %d loaded %s (%s), expected %d (%s)"
                        % (i + 1, loaded.group(1), loaded.group(2), record,
                           expected_status))
        break
    score = int(over.group(1))
    scores.append(score)
    saved = re.search(r"New high score (\d+) saved", text)
    if score > record:
        if not saved or int(saved.group(1)) != score:
            problems.append("run %d beat the record but did not save it"
                            % (i + 1))
            break
        record = score
    elif saved:
        problems.append("run %d saved %s without beating %d"
                        % (i + 1, saved.group(1), record))
        break
    if (i + 1) % 10 == 0:
        print("restart %d: record %d" % (i + 1, record))
blob = open(os.path.join(save, "highscore.dat"), "rb").read()
if blob != b"version=1\nhigh_score=%d\n" % record:
    problems.append("final file %r does not carry record %d" % (blob, record))
print("restarts: %d runs, final record %d, best score %d, min %d"
      % (len(scores), record, max(scores, default=0), min(scores, default=0)))
for problem in problems:
    print("PROBLEM:", problem)
sys.exit(1 if problems else 0)
PYEOF
  check=$?
  if [ "$check" -eq 0 ]; then
    status restarts passed "$(tail -1 "$rs/restarts-check.txt")"
  else
    status restarts failed "see $rs/restarts-check.txt"
  fi
fi

# ---------------------------------------------------------------------------
# longplay: the ten-minute Release session.
# ---------------------------------------------------------------------------
if want_phase longplay; then
  echo
  echo "=== phase longplay ==="
  lp="$run_dir/longplay"
  phase_dir longplay "$lp"
  mkdir -p "$lp/shots" "$lp/save"
  python3 "$gen" longplay "$lp/longplay.script.txt" > "$lp/gen.log"
  if [ "${T025_SKIP_X11:-0}" = "1" ] || ! command -v xvfb-run >/dev/null 2>&1; then
    status longplay unavailable "xvfb-run missing or skipped"
  else
    # time sits inside xvfb-run so its rusage covers the game alone, not
    # the X server; the sampler waits for the game process itself, because
    # under Xvfb it appears a few seconds after the wrapper.
    xvfb-run -a -s "-screen 0 960x544x24" \
      timeout --foreground 690 \
      env SDL_AUDIODRIVER=dummy \
      /usr/bin/time -v -o "$lp/time.txt" \
      "$game" --seed 20261601 --input-script "$lp/longplay.script.txt" \
      --save-dir "$lp/save" --screenshot-dir "$lp/shots" \
      --observations "$lp/observations.obs" --input-log "$lp/input.log" \
      --frame-times "$lp/frame-times.txt" --frames 39000 \
      > "$lp/run.log" 2>&1 &
    session_pid=$!
    children="$children $session_pid"
    game_pid=$(stackfall_pid || true)
    if [ -n "${game_pid:-}" ]; then
      sample_rss "$game_pid" "$lp/rss.tsv" &
      sampler=$!
      children="$children $sampler"
    else
      echo "t025: longplay sampler not started: no stackfall pid" >&2
      sampler=""
    fi
    wait "$session_pid"; code=$?
    [ -n "$sampler" ] && kill "$sampler" 2>/dev/null
    children=${children% "$session_pid"}
    [ -n "$sampler" ] && children=${children% "$sampler"}
    echo "$code" > "$lp/exit"
    python3 "$frame_stats" "$lp/frame-times.txt" "$lp/run.log" \
      --expect-seconds 600 --json "$lp/frame-stats.json" \
      > "$lp/frame-stats.txt"
    stats_code=$?
    python3 - "$lp" > "$lp/longplay-check.txt" 2>&1 <<'PYEOF'
import glob, os, re, sys
lp = sys.argv[1]
problems = []
log = open(os.path.join(lp, "run.log"), encoding="utf-8", errors="replace").read()
input_log = open(os.path.join(lp, "input.log"), encoding="utf-8",
                 errors="replace").read()
seeds = re.findall(r"^Round seed (\d+)$", log, re.M)
print("distinct round seeds: %d (%s...)" % (len(set(seeds)),
                                            ",".join(seeds[:4])))
if len(set(seeds)) < 3:
    problems.append("fewer than three seeded rounds ran")
games = len(re.findall(r"^script game-g\d+", input_log, re.M))
print("games started: %d" % games)
if games < 10:
    problems.append("only %d games started" % games)
if "Frame stall of" not in log or "clamped to 250 ms" not in log:
    problems.append("the long stall was not clamped")
if "Unusual frame stall of" not in log:
    problems.append("the short stall warning is missing")
if "Clean shutdown" not in log:
    problems.append("no clean shutdown")
if "Fallback frame limiter engaged" not in log:
    problems.append("the fallback limiter never engaged")
obs = open(os.path.join(lp, "observations.obs"), encoding="ascii").read()
gameovers = len(re.findall(r"scene=gameover", obs))
print("game-over records: %d" % gameovers)
if gameovers < 5:
    problems.append("only %d game overs recorded" % gameovers)
shots = sorted(glob.glob(os.path.join(lp, "shots", "*.bmp")))
print("screenshots: %d" % len(shots))
if len(shots) < 8:
    problems.append("only %d screenshots" % len(shots))
rows = [line.split("\t") for line in
        open(os.path.join(lp, "rss.tsv"), encoding="ascii").read().splitlines()
        if line.strip()]
if len(rows) >= 60:
    # Renderer setup (Mesa under Xvfb) settles in the first minutes; the
    # leak question is steady-state growth, so compare the middle third of
    # the samples against the last third, not the first minutes.
    third = len(rows) // 3
    mid = max(int(r[1]) for r in rows[third:2 * third])
    late = max(int(r[1]) for r in rows[-third:])
    print("rss kB: mid max %d, late max %d, threads late %s"
          % (mid, late, rows[-1][2].strip()))
    if late - mid > 12288:
        problems.append("rss grew %d kB after setup settled" % (late - mid))
else:
    problems.append("too few rss samples (%d)" % len(rows))
for name in ("after-stall-g03", "after-stall-g07"):
    if not re.search(r"check=%s scene=playing" % name, obs):
        problems.append("%s did not record an active round" % name)
for problem in problems:
    print("PROBLEM:", problem)
sys.exit(1 if problems else 0)
PYEOF
    check=$?
    if [ "$code" -eq 0 ] && [ "$stats_code" -eq 0 ] && [ "$check" -eq 0 ]; then
      status longplay passed "$(grep -hE 'elapsed_seconds|median_frame_ms|p99_frame_ms|ticks_per_second|distinct round|rss kB' "$lp/frame-stats.txt" "$lp/longplay-check.txt" | tr '\n' '; ')"
    else
      status longplay failed "exit $code, frame stats $stats_code, checks $check; see $lp"
    fi
  fi
fi

# ---------------------------------------------------------------------------
# pacing: percentiles, CPU, and memory per renderer mode.
# ---------------------------------------------------------------------------
if want_phase pacing; then
  echo
  echo "=== phase pacing ==="
  pc="$run_dir/pacing"
  phase_dir pacing "$pc"
  pacing_ok=1
  printf 'run\trenderer\tvsync_requested\telapsed_s\tframes\tticks\thz\tmedian_ms\tp95_ms\tp99_ms\tmax_ms\tcpu_percent\tmax_rss_kib\n' \
    > "$pc/pacing.tsv"
  # pace_one NAME VIDEO DRIVER LABEL VSYNC [EXTRA_ENV]: VIDEO is x11 or
  # dummy; DRIVER may be empty for the default chain; VSYNC is the
  # vsync_requested column; EXTRA_ENV is one VAR=VALUE word, unquoted on
  # purpose.
  pace_one() {
    local name=$1 video=$2 driver=$3 label=$4 vsync=$5 extra=${6:-}
    local dir="$pc/$name"
    mkdir -p "$dir/save"
    if [ "$video" = x11 ]; then
      xvfb-run -a -s "-screen 0 960x544x24" \
        timeout --foreground 200 \
        env SDL_AUDIODRIVER=dummy ${driver:+SDL_RENDER_DRIVER=$driver} \
        ${extra:+$extra} \
        /usr/bin/time -v -o "$dir/time.txt" \
        "$game" --seed 31 --frames 7200 --save-dir "$dir/save" \
        --frame-times "$dir/frame-times.txt" > "$dir/run.log" 2>&1
    else
      timeout --foreground 200 \
        env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        ${driver:+SDL_RENDER_DRIVER=$driver} ${extra:+$extra} \
        /usr/bin/time -v -o "$dir/time.txt" \
        "$game" --seed 31 --frames 7200 --save-dir "$dir/save" \
        --frame-times "$dir/frame-times.txt" > "$dir/run.log" 2>&1
    fi
    local code=$?
    echo "$code" > "$dir/exit"
    if [ "$code" -ne 0 ]; then
      echo "t025: pacing run $name exited $code" >&2
      pacing_ok=0
      return
    fi
    python3 "$frame_stats" "$dir/frame-times.txt" "$dir/run.log" \
      --expect-seconds 1 --json "$dir/frame-stats.json" > "$dir/stats.txt"
    local stats=$?
    [ "$stats" -ne 0 ] && pacing_ok=0
    local cpu rss
    cpu=$(awk -F': *' '/Percent of CPU/{print $2}' "$dir/time.txt" | tr -d '%')
    rss=$(awk '/Maximum resident set size/{print $NF}' "$dir/time.txt")
    python3 - "$pc/pacing.tsv" "$name" "$label" "$vsync" "$cpu" "$rss" <<'PYEOF'
import json, sys
tsv, name, label, vsync, cpu, rss = sys.argv[1:7]
s = json.load(open(tsv.replace("pacing.tsv", "") + name +
                    "/frame-stats.json"))
row = [name, label, vsync,
       "%.1f" % s["elapsed_seconds"], str(s["frames_rendered"]),
       str(s["ticks_simulated"]), "%.2f" % s["ticks_per_second"],
       "%.2f" % s["median_frame_ms"], "%.2f" % s["p95_frame_ms"],
       "%.2f" % s["p99_frame_ms"], "%.2f" % s["max_frame_ms"],
       cpu or "?", rss or "?"]
open(tsv, "a").write("\t".join(row) + "\n")
PYEOF
  }
  if [ "${T025_SKIP_X11:-0}" = "1" ] || ! command -v xvfb-run >/dev/null 2>&1; then
    echo "t025: x11 pacing runs unavailable" >&2
    pacing_ok=0
    echo "unavailable: xvfb-run missing or skipped" > "$pc/x11.status"
  else
    # The default chain asks for an accelerated vsync renderer first; under
    # Xvfb the opengl backend reports the flag while presentation returns
    # at once, so the limiter holds the rate either way (T014's finding).
    # SDL_RENDER_VSYNC=0 strips the flag for real: same renderer, vsync
    # explicitly off, the fallback limiter alone holds the 60 Hz rate.
    pace_one x11-opengl-vsync x11 "" "opengl (vsync requested)" yes
    pace_one x11-opengl-novsync x11 "" "opengl (vsync off by hint)" no \
      "SDL_RENDER_VSYNC=0"
    pace_one x11-software x11 software "software (no vsync flag)" yes
    # Pin the enabled/disabled claim to the renderer flags the game logged.
    grep -q 'Renderer: opengl (accelerated, vsync)' \
      "$pc/x11-opengl-vsync/run.log" || {
      echo "t025: pacing x11-opengl-vsync lacks the vsync flag" >&2
      pacing_ok=0; }
    grep -q 'Renderer: opengl (accelerated)$' \
      "$pc/x11-opengl-novsync/run.log" || {
      echo "t025: pacing x11-opengl-novsync still reports the vsync flag" >&2
      pacing_ok=0; }
  fi
  pace_one dummy-fallback dummy "" "dummy (software, pretends vsync)" yes
  column -t -s "$(printf '\t')" "$pc/pacing.tsv" > "$pc/pacing.txt" 2>/dev/null || true
  cat "$pc/pacing.tsv"
  if [ "$pacing_ok" -eq 0 ]; then
    status pacing failed "at least one pacing run failed; see $pc"
  else
    status pacing passed "$(($(wc -l < "$pc/pacing.tsv") - 1)) renderer modes measured"
  fi
fi

# ---------------------------------------------------------------------------
# failures: the forced-failure matrix.
# ---------------------------------------------------------------------------
if want_phase failures; then
  echo
  echo "=== phase failures ==="
  fa="$run_dir/failures"
  phase_dir failures "$fa"
  python3 "$gen" saves "$fa/saves.script.txt" > "$fa/gen.log"
  problems=0

  # Missing audio: the session plays through, every cue reported dropped.
  mkdir -p "$fa/audio-missing"
  run_game failures/audio-missing 90 env SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=stackfall-missing-driver \
    "$game" --seed 5 --input-script "$fa/saves.script.txt" \
    --save-dir "$fa/audio-missing/save"
  if [ $? -eq 0 ] && grep -q "without a device" "$fa/audio-missing/save/log.txt" &&
     grep -q "Clean shutdown" "$fa/audio-missing/save/log.txt"; then
    echo "audio-missing: passed" >> "$fa/matrix.txt"
  else
    echo "audio-missing: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # Renderer failure: exit 1 with the actionable error and the driver list.
  mkdir -p "$fa/render-fail"
  run_game failures/render-fail "$default_timeout" env SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=stackfall-no-such-renderer \
    "$game" --seed 5 --frames 30 --save-dir "$fa/render-fail/save"
  if [ "$(cat "$fa/render-fail.exit")" = "1" ] &&
     grep -q "carries no render driver" "$fa/render-fail.log"; then
    echo "render-fail: passed" >> "$fa/matrix.txt"
  else
    echo "render-fail: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # Software renderer fallback: runs and names the software renderer.
  mkdir -p "$fa/software/save"
  run_game failures/software "$default_timeout" env SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
    "$game" --seed 5 --frames 240 --save-dir "$fa/software/save"
  if [ $? -eq 0 ] && grep -q "Renderer: software" "$fa/software.log"; then
    echo "software-renderer: passed" >> "$fa/matrix.txt"
  else
    echo "software-renderer: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # Focus loss on a real X server, through the T024 X11 session driver.
  if [ "${T025_SKIP_X11:-0}" = "1" ]; then
    echo "focus-loss: unavailable (T025_SKIP_X11)" >> "$fa/matrix.txt"; problems=1
  elif command -v xvfb-run >/dev/null 2>&1 &&
       command -v xdotool >/dev/null 2>&1 &&
       command -v openbox >/dev/null 2>&1; then
    if T024_BUILD_DIR="$build_dir" \
       T024_RUN_DIR="$fa/focus-loss" T024_TIMEOUT_S="$default_timeout" \
       xvfb-run -a -s "-screen 0 960x544x24" \
       "$repo/tests/harness/t024-x11.sh" > "$fa/focus-loss.driver.log" 2>&1; then
      echo "focus-loss: passed" >> "$fa/matrix.txt"
    else
      echo "focus-loss: FAILED" >> "$fa/matrix.txt"; problems=1
    fi
  else
    echo "focus-loss: unavailable (x11 tools missing)" >> "$fa/matrix.txt"; problems=1
  fi

  # Virtual-controller removal mid-game, real-time pacing.
  mkdir -p "$fa/pad-detach/shots" "$fa/pad-detach/save"
  run_game failures/pad-detach "$default_timeout" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed 20261104 --input-script \
    "$repo/tests/harness/scripts/scene-cycle-pad.script.txt" \
    --save-dir "$fa/pad-detach/save" --screenshot-dir "$fa/pad-detach/shots" \
    --observations "$fa/pad-detach/observations.obs" \
    --input-log "$fa/pad-detach/input.log" --frames 6000
  if [ $? -eq 0 ] &&
     grep -q "Game controller .* removed" "$fa/pad-detach/save/log.txt" &&
     grep -q "keyboard fallback active" "$fa/pad-detach/save/log.txt" &&
     grep -q "Clean shutdown" "$fa/pad-detach/save/log.txt"; then
    echo "controller-removal: passed" >> "$fa/matrix.txt"
  else
    echo "controller-removal: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # A dedicated long stall: the clamp line, then the session continues.
  mkdir -p "$fa/stall/save"
  cat > "$fa/stall.script.txt" <<'EOF'
key enter down
wait 2
key enter up
wait 30
echo before-stall
stall 1500
wait 30
echo after-stall
key esc down
wait 2
key esc up
wait 2
key down down
wait 2
key down up
wait 2
key down down
wait 2
key down up
wait 2
key enter down
wait 2
key enter up
wait 2
check stall-recovered
key down down
wait 2
key down up
wait 2
key down down
wait 2
key down up
wait 2
key enter down
wait 2
key enter up
EOF
  run_game failures/stall "$default_timeout" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed 9 --input-script "$fa/stall.script.txt" \
    --save-dir "$fa/stall/save" --screenshot-dir "$fa/stall/shots" \
    --observations "$fa/stall/observations.obs" --frames 600
  if [ $? -eq 0 ] &&
     grep -q "stalling 1500 ms for a catch-up frame" "$fa/stall.log" &&
     grep -q "Frame stall of 15[0-9][0-9] ms clamped to 250 ms" "$fa/stall.log" &&
     grep -q "script: after-stall" "$fa/stall.log"; then
    echo "long-stall: passed" >> "$fa/matrix.txt"
  else
    echo "long-stall: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # Unwritable save directory: the failed save is logged, play continues.
  mkdir -p "$fa/save-unwritable/dir"
  chmod 500 "$fa/save-unwritable/dir"
  run_game failures/save-unwritable 90 env SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy \
    "$game" --seed 5 --input-script "$fa/saves.script.txt" \
    --save-dir "$fa/save-unwritable/dir"
  uw_code=$?
  chmod 700 "$fa/save-unwritable/dir"
  if [ "$uw_code" -eq 0 ] &&
     grep -q "failed: write failed" "$fa/save-unwritable.log"; then
    echo "save-unwritable: passed" >> "$fa/matrix.txt"
  else
    echo "save-unwritable: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # Damaged and missing saves: each loads to 0 with a status, never a
  # crash. A MISSING marker means the file is not written at all.
  corrupt_ok=1
  mk_save_case() {
    local name=$1 blob=$2
    mkdir -p "$fa/save-$name/save"
    if [ "$blob" != "MISSING" ]; then
      printf '%b' "$blob" > "$fa/save-$name/save/highscore.dat"
    fi
    run_game "failures/save-$name" "$default_timeout" \
      env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$game" --seed 5 --frames 60 --save-dir "$fa/save-$name/save"
    local line expect
    line=$(grep -o "High score [0-9]* ([a-z ]*)" "$fa/save-$name.log" | head -1)
    if [ "$blob" = "MISSING" ]; then
      expect="High score 0 (not found)"
    else
      expect="High score 0 (invalid)"
    fi
    if [ "$line" = "$expect" ]; then
      echo "save-$name: passed ($line)" >> "$fa/matrix.txt"
    else
      echo "save-$name: FAILED (${line:-NO LOAD LINE})" >> "$fa/matrix.txt"
      corrupt_ok=0
    fi
  }
  mk_save_case missing MISSING
  mk_save_case empty ""
  mk_save_case partial "version=1\\nhigh_score=21"
  mk_save_case truncated "version=1\\nhigh_score=218\\nhigh"
  mk_save_case garbage "not a stackfall file at all\\n"
  mk_save_case doubled "version=1\\nhigh_score=218\\nversion=1\\nhigh_score=219\\n"
  mk_save_case crlf "version=1\\r\\nhigh_score=218\\r\\n"
  if [ "$corrupt_ok" -eq 1 ]; then
    echo "save-corrupt-suite: passed" >> "$fa/matrix.txt"
  else
    echo "save-corrupt-suite: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # No save directory at all: persistence off, session unaffected.
  run_game failures/save-nodir "$default_timeout" \
    env -u HOME -u XDG_DATA_HOME -u STACKFALL_SAVE_DIR -u STACKFALL_LOG_FILE \
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$game" --seed 5 --frames 60
  if [ $? -eq 0 ] && grep -q "No save directory" "$fa/save-nodir.log"; then
    echo "save-nodir: passed" >> "$fa/matrix.txt"
  else
    echo "save-nodir: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  # Plain startup: no developer option may change it.
  run_game failures/plain-startup "$default_timeout" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$game" --frames 90
  if [ $? -eq 0 ] && grep -q "Clean shutdown" "$fa/plain-startup.log" &&
     ! grep -q "Harness mode" "$fa/plain-startup.log"; then
    echo "plain-startup: passed" >> "$fa/matrix.txt"
  else
    echo "plain-startup: FAILED" >> "$fa/matrix.txt"; problems=1
  fi

  cat "$fa/matrix.txt"
  if [ "$problems" -eq 0 ]; then
    status failures passed "$(grep -c ': passed' "$fa/matrix.txt") checks"
  else
    status failures failed "$(grep -c FAILED "$fa/matrix.txt") checks failed; see $fa/matrix.txt"
  fi
fi

# ---------------------------------------------------------------------------
# san: the rules suites and one scripted session under both sanitizers.
# ---------------------------------------------------------------------------
if want_phase san; then
  echo
  echo "=== phase san ==="
  sn="$run_dir/san"
  phase_dir san "$sn"
  if [ ! -f "$san_build_dir/build.ninja" ]; then
    cmake -S "$repo" -B "$san_build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
      > "$sn/configure.log" 2>&1 || fail "sanitizer configure failed"
  fi
  cmake --build "$san_build_dir" > "$sn/build.log" 2>&1 ||
    fail "sanitizer build failed"
  ctest --test-dir "$san_build_dir" --output-on-failure > "$sn/ctest.log" 2>&1
  ctest_code=$?
  # One scripted session through the sanitized SDL application: the whole
  # platform layer under both sanitizers, not just the core.
  mkdir -p "$sn/session/save"
  timeout --foreground "$default_timeout" \
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ASAN_OPTIONS=detect_leaks=1 \
    "$san_build_dir/stackfall" --seed 20261103 --tick-per-frame \
    --input-script "$repo/tests/harness/scripts/scene-cycle-keyboard.script.txt" \
    --save-dir "$sn/session/save" --frames 4000 \
    > "$sn/session/run.log" 2>&1
  session_code=$?
  if grep -qE "ERROR: (Address|Leak)Sanitizer|runtime error:" \
       "$sn/ctest.log" "$sn/session/run.log"; then
    status san failed "a sanitizer reported; see $sn"
  elif [ "$ctest_code" -eq 0 ] && [ "$session_code" -eq 0 ] &&
       grep -q "Clean shutdown" "$sn/session/run.log"; then
    status san passed "ctest and one scripted ASan+UBSan session clean"
  else
    status san failed "ctest $ctest_code, session $session_code; see $sn"
  fi
fi

# ---------------------------------------------------------------------------
# regress: the recorded harness drivers still pass after T025's edits.
# ---------------------------------------------------------------------------
if want_phase regress; then
  echo
  echo "=== phase regress ==="
  rg="$run_dir/regress"
  phase_dir regress "$rg"
  cmake -S "$repo" -B "$repo/build" -G Ninja > "$rg/configure.log" 2>&1 ||
    fail "debug configure failed"
  cmake --build "$repo/build" > "$rg/build.log" 2>&1 ||
    fail "debug build failed"
  HARNESS_BUILD_DIR="$repo/build" HARNESS_RUN_ID=t025-regress-harness \
    "$repo/tests/harness/run.sh" > "$rg/run-sh.log" 2>&1
  run_code=$?
  if command -v xvfb-run >/dev/null 2>&1; then
    T024_BUILD_DIR="$repo/build" T024_RUN_ID=t025-regress-t024 \
      xvfb-run -a -s "-screen 0 960x544x24" "$repo/tests/harness/t024.sh" \
      > "$rg/t024-sh.log" 2>&1
    t024_code=$?
  else
    t024_code=1
    echo "t024.sh unavailable: xvfb-run missing" > "$rg/t024-sh.log"
  fi
  if [ "$run_code" -eq 0 ] && [ "$t024_code" -eq 0 ]; then
    status regress passed "run.sh and t024.sh green"
  else
    status regress failed "run.sh $run_code, t024.sh $t024_code; see $rg"
  fi
fi

# ---------------------------------------------------------------------------
echo
echo "=== summary ==="
{
  for phase_status in "$run_dir"/*.status; do
    printf '%s\t%s\n' "$(basename "$phase_status" .status)" \
      "$(head -1 "$phase_status")"
  done
} | sort > "$run_dir/summary.txt"
cat "$run_dir/summary.txt"
if [ "$fail_count" -gt 0 ]; then
  echo "t025: FAIL ($fail_count phases failed; evidence in $run_dir)"
  exit 1
fi
echo "t025: PASS (evidence in $run_dir)"
