#!/bin/sh
# Bounded codex review driver (docs/DESIGN_REVIEW.md, T027 and T028).
#
#   tools/design-review/review.sh
#
# It sends the captured packet to `codex exec` in batches: the existing
# model configuration, a read-only sandbox, every image attached
# explicitly, a prompt from stdin, a strict output schema, the JSONL
# transcript saved, standard error and the exit status recorded, and a
# hard timeout per call that kills the child. After the calls,
# check_review.py validates every report against the schema and the
# packet, then merges the findings across calls.
#
# Bounds: at most DR_BATCH images per call (default 6), at most
# DR_MAX_IMAGES images in the round (default 12), at most DR_MAX_CALLS
# calls in the round (default 2), and DR_TIMEOUT_S per call (default
# 600, the ten-minute bound). A failed or unavailable reviewer exits
# nonzero; it cannot count as a successful independent review.
#
# Options through environment variables:
#   DR_PACKET_DIR  packet directory (default: newest packet under
#                  build/agent-checks/design-review)
#   DR_ROUND       round label, initial or verify (default: initial)
#   DR_IMAGES      comma-separated image ids to review (default: all)
#   DR_PRIOR_FINDINGS  JSON file of findings the verify round rechecks
#   DR_SKIP_CODEX  set to 1 to validate existing call outputs only

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tools="$repo/tools/design-review"
packet_dir=${DR_PACKET_DIR:-}
round=${DR_ROUND:-initial}
batch=${DR_BATCH:-6}
max_images=${DR_MAX_IMAGES:-12}
max_calls=${DR_MAX_CALLS:-2}
timeout_s=${DR_TIMEOUT_S:-600}

fail() {
  echo "dr-review: $*" >&2
  exit 1
}

# A missing reviewer fails the round: an unavailable reviewer cannot
# count as a successful independent review.
if [ "${DR_SKIP_CODEX:-0}" != "1" ]; then
  command -v codex >/dev/null 2>&1 ||
    fail "codex is not on PATH; the review cannot run"
fi

# ---------------------------------------------------------------------------
# Resolve the packet.
# ---------------------------------------------------------------------------
if [ -z "$packet_dir" ]; then
  root="$repo/build/agent-checks/design-review"
  [ -d "$root" ] || fail "no design-review packets yet; run capture.sh first"
  packet_dir=$(ls -1dt "$root"/*/packet 2>/dev/null | head -1)
  [ -n "$packet_dir" ] || fail "no packet found under $root"
fi
[ -f "$packet_dir/manifest.json" ] || fail "no manifest.json in $packet_dir"
echo "dr-review: packet $packet_dir"
echo "dr-review: round $round"

review_dir="$packet_dir/review-$round"
mkdir -p "$review_dir" || fail "cannot create $review_dir"

# The image list, in manifest order, optionally filtered by DR_IMAGES.
ids=$(python3 "$tools/review_ids.py" --packet "$packet_dir" \
  ${DR_IMAGES:+--only "$DR_IMAGES"} --max "$max_images") ||
  fail "cannot read the image list"
[ -n "$ids" ] || fail "no images to review"
count=$(printf '%s\n' "$ids" | wc -l | tr -d ' ')
echo "dr-review: $count image(s) within the bound of $max_images"

# The reviewer configuration on record: existing config, never overridden.
# A revalidation pass (DR_SKIP_CODEX=1) must not rewrite it, so the file
# keeps describing the calls that produced the reports.
if [ "${DR_SKIP_CODEX:-0}" != "1" ]; then
  {
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "round: $round"
    echo "codex version: $(codex --version 2>/dev/null || echo unavailable)"
    echo "configured model: $(sed -n 's/^model *= *"\(.*\)"/\1/p' \
      "$HOME/.codex/config.toml" 2>/dev/null | head -1)"
    echo "sandbox: read-only"
    echo "prompt schema: review.schema.json"
  } > "$review_dir/runner.json"
fi

# ---------------------------------------------------------------------------
# The calls. Arguments are built as an argv list through `set --`; no
# file name is ever interpolated into shell code.
# ---------------------------------------------------------------------------
call=0
remaining="$ids"
while [ -n "$remaining" ]; do
  call=$((call + 1))
  [ "$call" -le "$max_calls" ] ||
    fail "the round needs more than $max_calls calls; raise DR_BATCH"
  batch_ids=""
  picked=0
  for image_id in $remaining; do
    [ "$picked" -lt "$batch" ] || break
    if [ -z "$batch_ids" ]; then
      batch_ids="$image_id"
    else
      batch_ids="$batch_ids,$image_id"
    fi
    picked=$((picked + 1))
  done
  remaining=$(printf '%s\n' $remaining | tail -n +"$((picked + 1))" |
    tr '\n' ' ' | sed 's/ *$//')

  prefix="$review_dir/call-$(printf '%02d' "$call")"
  prompt="$prefix.prompt.md"

  echo
  echo "=== review call $call: $batch_ids ==="
  if [ "${DR_SKIP_CODEX:-0}" = "1" ]; then
    echo "dr-review: DR_SKIP_CODEX=1; skipping the codex call"
    [ -f "$prefix.review.json" ] ||
      fail "revalidation needs $prefix.review.json from a real call"
    continue
  fi
  python3 "$tools/build_prompt.py" --packet "$packet_dir" --round "$round" \
    --attached "$batch_ids" --out "$prompt" \
    ${DR_PRIOR_FINDINGS:+--prior-findings "$DR_PRIOR_FINDINGS"} ||
    fail "cannot build the prompt for call $call"

  set --
  for image_id in $(printf '%s' "$batch_ids" | tr ',' ' '); do
    set -- "$@" --image "$packet_dir/images/$image_id.png"
  done

  start=$(date +%s)
  # --skip-git-repo-check lets codex run from a worktree the codex trust
  # list does not name; it changes no model, provider, or credential.
  timeout --foreground -k 10 "$timeout_s" \
    codex exec --skip-git-repo-check --sandbox read-only --json \
    --output-schema "$tools/review.schema.json" \
    --output-last-message "$prefix.review.json" \
    "$@" - < "$prompt" \
    > "$prefix.events.jsonl" 2> "$prefix.stderr.log"
  status=$?
  end=$(date +%s)
  echo "exit=$status" > "$prefix.exit"
  echo "images=$batch_ids" >> "$prefix.exit"
  echo "seconds=$((end - start))" >> "$prefix.exit"
  cat "$prefix.exit"
  [ "$status" -eq 0 ] ||
    fail "call $call exited $status; see $prefix.stderr.log and $prefix.events.jsonl"
  [ -s "$prefix.review.json" ] ||
    fail "call $call wrote no report; see $prefix.events.jsonl"
done

# ---------------------------------------------------------------------------
echo
echo "=== independent validation and merge ==="
python3 "$tools/check_review.py" --packet "$packet_dir" --review-dir "$review_dir"
status_all=$?

echo
if [ "$status_all" -eq 0 ]; then
  echo "dr-review: PASS ($call call(s))"
  echo "dr-review: findings in $review_dir/merged.json"
else
  echo "dr-review: FAIL (validation rejected the review output)"
fi
exit "$status_all"
