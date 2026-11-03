# Screenshot design review tools

These commands implement `docs/DESIGN_REVIEW.md`. Task T027 captures the
real application and asks `codex exec` for an independent review. Task
T028 repeats the review after fixes.

## Capture the packet

```bash
tools/design-review/capture.sh
```

The command builds the game, records the source identity, runs the
headless rules suites, and drives four sessions on the T023 harness:
the scene and HUD session, the line-clear sequence, the tall stack, and
the smaller letterboxed window under Xvfb. The reviewed frames convert
to PNG under `build/agent-checks/design-review/<run-id>/packet/images/`
with `manifest.json`, `manifest.md`, and the observation record of every
image. The packet definition lives in `packet.tsv`, which names the
sessions and the checkpoints each reviewed image comes from.

Environment options: `DR_BUILD_DIR` (default `build`), `DR_RUN_ID`
(default date and time), `DR_TIMEOUT_S` (default 120), `DR_SKIP_X11`
(set to 1 to skip the letterbox session; the packet then cannot be
built, because the letterbox image is required).

## Run the bounded review

```bash
tools/design-review/review.sh
```

The command sends the newest packet to `codex exec` in batches. Every
call uses the existing model configuration, a read-only sandbox,
explicit image attachments, a prompt read from standard input, the
strict output schema `review.schema.json`, the JSONL transcript, saved
standard error, the exit status, and a hard timeout that kills the
child. `check_review.py` then validates each report against the schema
and the packet and merges the findings by key into `merged.json`. A
report that fails validation fails the run.

Bounds: `DR_BATCH` images per call (default 6), `DR_MAX_IMAGES` per
round (default 12), `DR_MAX_CALLS` per round (default 2), and
`DR_TIMEOUT_S` per call (default 600). Two rounds exist in total: the
initial review (T027) and the verification review (T028).

For the verification round, select the affected images and pass the
open findings:

```bash
DR_ROUND=verify \
DR_IMAGES=hold-locked,gameplay-early \
DR_PRIOR_FINDINGS=build/agent-checks/design-review/<run-id>/packet/review-initial/open-findings.json \
tools/design-review/review.sh
```

The packets are untracked build output, so `prior-findings.json` in this
directory keeps a durable copy of the required findings and their
acceptance criteria. Pass it through `DR_PRIOR_FINDINGS` when the
initial packet is not on disk.

`DR_SKIP_CODEX=1` revalidates existing call outputs without calling the
reviewer. `DR_PACKET_DIR` selects a packet directory explicitly.

## Evidence layout

```text
build/agent-checks/design-review/<run-id>/
  source.sha256            source identity of the capture
  <session>.obs            observation record stream per session
  <session>/shots/*.bmp    original checkpoint screenshots
  packet/
    images/*.png           reviewed images (copies of the BMPs)
    manifest.json          structured manifest with observations
    manifest.md            the same manifest for reading
    review-<round>/
      runner.json          reviewer identity, sandbox, bounds
      call-01.prompt.md    the exact prompt of the call
      call-01.events.jsonl the complete transcript
      call-01.stderr.log   saved standard error
      call-01.exit         exit status, images, duration
      call-01.review.json  the structured report
      validation.json      per-call validation and duplicate keys
      merged.json          merged findings, positives, limitations
```

The findings dispositions, reproduction evidence, and task routing live
in `notes/design-review.md`.
