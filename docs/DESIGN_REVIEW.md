# Screenshot design review

T027 runs after the first agent gameplay review, T024. It captures the real
application and asks `codex exec` for an independent review of design, user
interface (UI), and user experience (UX). T028 verifies the resulting fixes.
Complete both tasks before the T025 stability baseline and T015 desktop gate.
Human and physical device tests remain deferred to final acceptance.

## Review packet

Save each packet under `build/agent-checks/design-review/<run-id>/`. Include
PNG screenshots, a manifest, a prompt, the output schema, the complete runner
transcript, and the final structured report. Keep the summary and finding
dispositions in `notes/design-review.md`.

The manifest records the date, source identity, seed, window dimensions,
scene, input steps, and expected behavior for each image. Preserve the
original screenshots. Contact sheets can supplement the original images.

Capture title, controls, early gameplay, a tall stack, a ghost landing, hold
available and unavailable, five previews, pause, game over, and restart.
Include spawn and line-clear sequences, plus a smaller letterboxed window.
Use deterministic fixtures where needed and label them. Capture current
screenshots again after a fix; do not reuse old images as new evidence.

## Review runner

Use the existing Codex configuration. Inspect `codex exec --help` in the
execution environment. The 2026-11-02 local command supports `--image`,
`--sandbox read-only`, `--output-schema`, `--json`, and
`--output-last-message`. A prompt can be read from standard input.

An illustrative invocation is:

```bash
codex exec --sandbox read-only --json \
  --output-schema "$packet_dir/review.schema.json" \
  --output-last-message "$packet_dir/review.json" \
  --image "$packet_dir/title.png" \
  --image "$packet_dir/gameplay.png" \
  - < "$packet_dir/prompt.md" > "$packet_dir/events.jsonl"
```

The driver supplies the packet directory and builds argument arrays for all
images. Do not interpolate filenames into shell code. Batch at most six
images per call and twelve images per round. Make at most two review rounds:
the initial review and the verification review after fixes. Bound each call
to ten minutes. Save standard error and exit status. Terminate child
processes on timeout. A failed or unavailable reviewer cannot count as a
successful independent review.

Explicitly ask the reviewer to open and inspect every attached image. Have
it list inspected image identifiers. Prohibit source edits, backlog edits,
commits, recursive delegation, publication, and requests for intermediate
human or device testing. Use the existing unattended permissions. Do not
override the configured model, provider, or credentials.

## Review prompt and output

Give the reviewer the specification, testing policy, manifest, screenshots,
and relevant tick observations. State the product goal: precise, readable,
satisfying handheld block stacking with fast entry into another game.

Ask it to assess hierarchy, spacing, text size, contrast, clipping, active
piece visibility, ghost distinction, hold state, preview order, score
readability, menu selection, control hints, and landing and clear feedback.
Evaluate gameplay clarity before cosmetic preferences. Preserve the approved
scope, procedural graphics, board dimensions, rules, and controls.

Require a structured result with inspected images, positive observations,
findings, and limitations. Each finding includes:

- A stable finding key, concise title, and priority from 1 to 5.
- Exact screenshot identifiers and the affected screen region.
- An observed problem, its effect on the player, and confidence.
- A concrete proposed improvement and scope classification: approved fix,
  optional cosmetic change, or a change to the product requirements.
- Reproduction steps, acceptance criteria, and suggested affected files.

Reject unsupported claims. A static image cannot establish input latency,
sound quality, or enjoyment. A sequence and tick observations can support
transition findings. Distinguish direct observations from inferences.

## T027: Capture, review, and route findings

1. Implement a reusable bounded capture and review command using T023.
   Build a fresh screenshot packet and record its source identity.
2. Run the initial independent review. Validate its output schema, image
   references, and inspected-image list. Retain the prompt and full output.
3. Reproduce each finding against the current application. Compare it with
   the specification and existing tasks. Reject unsupported findings and
   record the reason. Do not treat aesthetic preference as a rule defect.
4. Return actionable findings through the active Looper task-result contract.
   Looper owns task creation and execution status. Do not let the reviewer or
   a task worker write `init` directly. If the result contract cannot route
   findings, report that integration gap; saving JSON alone is insufficient.
5. Have the Looper planning phase create one task per accepted root cause,
   or attach evidence to an existing task. Use stable finding keys to prevent
   duplicates. Include image paths, reproduction steps, source identity,
   affected files, priority, and observable acceptance criteria.
6. Schedule approved UI fixes without intermediate human testing. Record
   optional cosmetic suggestions separately. Defer changes to requirements
   for the final owner review. Preserve unrelated tasks and statuses.
7. Verify that accepted findings have actual task identifiers. Link those
   tasks from the report. Make T028 depend on required fix tasks through
   Looper planning. Fix tasks depend on T027, never T028, to avoid cycles.

The routing integration must demonstrate duplicate suppression and valid task
creation using a sample finding. The real screenshot review must also run.
Do not mark T027 complete after only implementing its script.

## T028: Verify fixes and close the review

Resolve required findings through the created tasks. Capture the affected
screens again and run the second independent review against the old findings
and new packet. Rerun affected gameplay and deterministic tests. Record each
finding as fixed, rejected with evidence, optional and deferred, or unresolved.

New required defects found in this final round become follow-up tasks through
the same Looper route. Do not start unlimited review rounds. Verify those
fixes with targeted captures and checks. Keep T028 open while required
findings remain unresolved. Do not block it on optional cosmetic work or
explicitly deferred changes to the product requirements.

Acceptance requires real task identifiers, fresh before-and-after images,
independent review output, checked fixes, and recorded dispositions. A review
transcript without task routing or verified fixes does not satisfy this gate.
