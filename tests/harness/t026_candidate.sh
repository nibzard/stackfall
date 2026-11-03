#!/bin/bash
# Bounded nested-agent candidate driver (docs/PERFORMANCE.md, T026 step 4).
#
#   tests/harness/t026_candidate.sh CAND_DIR PROMPT_FILE
#
# CAND_DIR names the candidate's evidence directory. The driver:
#
#   1. copies the current sources into CAND_DIR/base (pristine) and
#      CAND_DIR/work (the nested agent's isolated working copy; no .git, no
#      build output, no backlog files, so a commit or a backlog edit is not
#      even possible from inside);
#   2. runs the configured nested runner (`claude -p`, T026_CLAUDE_BIN)
#      noninteractively with PROMPT_FILE on standard input, through setsid
#      so the whole child process group can be terminated at the timeout;
#      the transcript, standard error, exit status, and duration land in
#      CAND_DIR;
#   3. writes the candidate's whole change as CAND_DIR/candidate.patch
#      (unified diff of base against work).
#
# The runner configuration stays exactly as the owner set it: the model,
# provider, and credentials are never overridden, only -p and the prompt are
# passed. A missing runner exits 2 so the caller can fall back to running
# the same loop itself, which docs/PERFORMANCE.md allows.
#
# Options through environment variables:
#   T026_CLAUDE_BIN  runner executable (default: claude from PATH)
#   T026_TIMEOUT_S   hard per-call bound in seconds (default: 600)

set -u

candidate_dir=$1
prompt_file=$2
runner=${T026_CLAUDE_BIN:-claude}
timeout_s=${T026_TIMEOUT_S:-600}

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

fail() {
  echo "t026-candidate: $*" >&2
  exit 1
}

[ -f "$prompt_file" ] || fail "no prompt file at $prompt_file"
command -v "$runner" >/dev/null 2>&1 ||
  { echo "t026-candidate: runner '$runner' unavailable" >&2; exit 2; }
runner_path=$(command -v "$runner")
mkdir -p "$candidate_dir" || fail "cannot create $candidate_dir"

# One source copy without Git metadata, build output, or the Looper backlog
# files: the nested agent works on sources alone.
copy_tree() {  # copy_tree DST
  local dst=$1
  mkdir -p "$dst"
  (cd "$repo" && tar cf - \
    --exclude=./build \
    --exclude=./build-rel \
    --exclude='./build-*' \
    --exclude=./.git \
    --exclude=./to-do.json \
    --exclude=./to-do.json.context \
    --exclude=./to-do.json.bak-\* \
    --exclude=./to-do.schema.json \
    --exclude=./init.schema.json \
    .) | (cd "$dst" && tar xf -)
}

if [ ! -d "$candidate_dir/base" ]; then
  echo "t026-candidate: copying the sources"
  copy_tree "$candidate_dir/base" || fail "cannot create the base copy"
fi
rm -rf "$candidate_dir/work"
copy_tree "$candidate_dir/work" || fail "cannot create the working copy"

# The runner record: what ran, from where, under which bound. The model,
# provider, and credentials come from the owner's configuration; nothing
# here overrides them, and no credential material is copied into evidence.
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "runner: $runner_path"
  echo "runner version: $("$runner_path" --version 2>/dev/null || echo ?)"
  echo "mode: -p (noninteractive), prompt on stdin"
  echo "timeout_s: $timeout_s"
  echo "model/provider: existing owner configuration, not overridden" \
       "(docs/PERFORMANCE.md: the owner confirms claude uses GLM 5.3)"
  echo "working copy: $candidate_dir/work (no git metadata)"
} > "$candidate_dir/runner.json"

echo "t026-candidate: running $runner_path -p (bound ${timeout_s}s)"
start=$(date +%s)
# setsid gives the child its own process group, so the timeout can take down
# every descendant, not only the runner itself.
setsid "$runner_path" -p \
  < "$prompt_file" \
  > "$candidate_dir/transcript.md" \
  2> "$candidate_dir/stderr.log" &
pid=$!
( sleep "$timeout_s"
  if kill -0 "$pid" 2>/dev/null; then
    echo "t026-candidate: timeout after ${timeout_s}s; killing the group"
    kill -- -"$pid" 2>/dev/null
    sleep 5
    kill -9 -- -"$pid" 2>/dev/null
  fi ) &
watchdog=$!
wait "$pid"
status=$?
kill "$watchdog" 2>/dev/null
wait "$watchdog" 2>/dev/null
end=$(date +%s)
echo "$status" > "$candidate_dir/exit"
echo "seconds=$((end - start))" >> "$candidate_dir/exit"
echo "t026-candidate: exit=$status seconds=$((end - start))"

# The candidate's whole change as one patch, build output excluded.
diff -ruN --exclude=build --exclude='build-*' --exclude='log.txt' \
  "$candidate_dir/base" "$candidate_dir/work" \
  > "$candidate_dir/candidate.patch"
diff_status=$?
[ "$diff_status" -le 1 ] ||
  fail "diffing the working copy failed with $diff_status"
if [ -s "$candidate_dir/candidate.patch" ]; then
  echo "t026-candidate: patch in $candidate_dir/candidate.patch" \
    "($(grep -c '^diff ' "$candidate_dir/candidate.patch") file(s))"
else
  echo "t026-candidate: the working copy is unchanged"
fi
exit "$status"
