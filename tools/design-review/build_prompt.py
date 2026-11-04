#!/usr/bin/env python3
"""Write the reviewer prompt for one bounded codex call.

Usage:
  build_prompt.py --packet PACKET_DIR --round initial \
      --attached id1,id2,... --out prompt.md [--prior-findings FILE]

The prompt states the product goal, the approved scope, the attached
images with their manifest entries, the assessment dimensions, the
evidence rules, and the prohibitions of docs/DESIGN_REVIEW.md. The
`--prior-findings` option adds the open findings of an earlier round; the
verification round (T028) uses it.
"""

import argparse
import json
from pathlib import Path

SCOPE = """\
## Approved scope

The following decisions are approved. Do not report them as defects.

- Canvas 960x544. Board cells are 27x27 pixels. The board occupies
  x=345..614 and y=2..541, centered on the canvas, inside a one-pixel
  frame at x=344..615 and y=1..542. The focused HUD of the approved
  composition (2026-11-03, task T037) holds two narrow columns beside
  the board, each about 24 pixels from the frame: the left column
  (x=192..319) carries Hold, score, lines, and level; the right column
  (x=640..767) carries the five next previews, the first at full color
  and the rest quieter. Ordinary gameplay shows no permanent control
  legend; the complete mappings live on the controls screen, reached
  from the title and from the pause overlay, and the outer regions of
  the canvas stay quiet. Do not report the missing gameplay legend or
  the quiet previews as defects.
- All graphics are procedural: filled rectangles and lines only, drawn
  from a source-defined 5x7 pixel glyph table. No sprites, no texture
  files, no image assets, no external fonts.
- Rules: Super Rotation System with wall kicks, seven-piece bag
  randomizer, hold locked until the next piece locks, level up every ten
  cleared lines, scoring 100/300/500/800 times level for one to four
  cleared lines.
- Controls: D-pad moves and soft drops, A rotates clockwise, B rotates
  counter-clockwise, X hard drops, Y or L1 holds, Start pauses. The
  keyboard mirrors them.
- Pieces spawn with their topmost cells at visible row 0 (approved
  2026-11-03, task T029), so every piece is on screen the tick it
  appears. Cells above row 0 exist only for rotations and are never
  drawn. Do not report immediate spawn visibility as a defect.
"""

ASSESS = """\
## What to assess

Judge gameplay clarity first and cosmetic preference last:

- Visual hierarchy, spacing, alignment, text size, contrast, clipping.
- Active piece visibility. Ghost distinct from the settled stack and
  from the active piece.
- Hold state clarity: available against locked.
- Preview order and readability: which piece comes next.
- Score, lines, and level readability at a glance.
- Menu selection clarity on the title, pause, and game-over screens.
- The controls screen and the pause overlay's route to it.
- Landing and clear feedback, when a frame pair can support it.
- The letterboxed 640x480 frame: readability at two-thirds size.
"""

EVIDENCE = """\
## Evidence rules

- Open and inspect every attached image before you write anything.
- Every finding cites exact image ids and names the screen region.
- Separate what the pixels show from judgment built on it. Use the
  evidence_type field: direct or inferred.
- A static image cannot establish input latency, audio quality, or
  enjoyment. A before-and-after pair can support transition findings.
- Do not report an approved scope decision as a defect.
- Do not propose new assets, fonts, or textures. Classify any idea that
  needs one as a product requirement change.
- An empty findings list is a valid answer when the images support one.
"""

PROHIBITIONS = """\
## Prohibitions

You are a reviewer only. Do not edit any file. Do not commit. Do not
create, change, or close tasks. Do not start other agents. Do not
publish anything. Do not ask for human or device testing. Judge from the
attached images and the manifest in this prompt.
"""

OUTPUT = """\
## Output

Reply with one JSON object that matches the schema exactly.

- inspected_images: every attached image id you opened, and nothing else.
- positive_observations: what already works, each naming its image id.
- findings: a list, possibly empty. Each finding has a stable
  kebab-case key, a priority from 1 to 5 (1 blocks play or clarity,
  2 significant confusion, 3 polish, 4 minor, 5 nit), exact image ids,
  the region, the observed problem, the effect on the player, the
  confidence (high, medium, low), the evidence type (direct, inferred),
  a concrete improvement, the scope (approved_fix, optional_cosmetic,
  product_requirement_change), reproduction steps from the manifest
  sessions, acceptance criteria a fixed frame shows, and the affected
  files.
- limitations: what this review cannot establish from these images.
"""


def image_entry(entry):
    obs = entry["observation"]
    return {
        "id": entry["id"],
        "file": entry["file"],
        "covers": entry["covers"],
        "session": entry["session"],
        "seed": entry["seed"],
        "scene": entry["scene"],
        "tick": obs["tick"],
        "window": entry["window"],
        "observation": {key: obs[key] for key in obs},
        "expected": entry["expected"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packet", required=True, type=Path)
    parser.add_argument("--round", default="initial")
    parser.add_argument("--attached", required=True,
                        help="comma-separated packet image ids")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--prior-findings", type=Path, default=None)
    args = parser.parse_args()

    manifest = json.loads((args.packet / "manifest.json").read_text())
    ids = [part for part in args.attached.split(",") if part]
    known = {entry["id"]: entry for entry in manifest["images"]}
    for image_id in ids:
        if image_id not in known:
            raise SystemExit(f"build_prompt: unknown image id {image_id}")
    others = [entry["id"] for entry in manifest["images"]
              if entry["id"] not in ids]

    lines = [
        f"# Independent design review of STACKFALL screenshots "
        f"(round: {args.round})",
        "",
        "You review captured screenshots of STACKFALL, a single-player, "
        "offline 2D block-stacking game for a handheld with a 5-inch "
        "960x544 panel. Comment on design, user interface, user "
        "experience, clarity, controls presentation, and feedback, "
        "inside the approved scope below.",
        "",
        "## Product goal",
        "",
        "Precise, readable, satisfying handheld block stacking, with fast "
        "entry into another game after a round ends.",
        "",
        SCOPE,
    ]

    if args.prior_findings and args.prior_findings.exists():
        lines += [
            "## Findings to verify",
            "",
            "An earlier round produced these findings. The attached images "
            "are fresh captures taken after fixes. For each finding below, "
            "judge whether the attached images now satisfy its acceptance "
            "criteria. Keep the original key, prefix the title with "
            "'verified:' or 'unresolved:', and set evidence_type direct.",
            "",
            "```json",
            args.prior_findings.read_text().strip(),
            "```",
            "",
        ]

    lines += [
        "## Your evidence",
        "",
        f"{len(ids)} images are attached to this message. Open and inspect "
        "every one. They are the only images you may report on.",
        "",
        "Attached in this call, in attachment order:",
        ", ".join(f"`{image_id}`" for image_id in ids) + ".",
        "The first attachment is the first id in that list, the second is "
        "the second, and so on.",
        "",
    ]
    if others:
        lines += [
            "Other images exist in this round but are not attached here. "
            "Do not report on them:",
            ", ".join(f"`{image_id}`" for image_id in others) + ".",
            "",
        ]
    lines += [
        "The manifest entry of every attached image follows. It records "
        "the capture session, the seed, the tick, the structured "
        "observation record of that frame, how the frame was reached, and "
        "what the frame is expected to show. Fixture sessions are labeled "
        "as such; they load a deterministic board for one difficult state "
        "and change nothing about normal play.",
        "",
        "```json",
        json.dumps([image_entry(known[image_id]) for image_id in ids],
                   indent=2),
        "```",
        "",
        "Session labels:",
        "",
    ]
    for session in manifest["sessions"]:
        fixtures = ", ".join(session["fixtures"]) or "no fixture"
        lines.append(f"- {session['name']}: seed {session['seed']}, "
                     f"{session['video']} video, {session['window']} "
                     f"window, {session['label']} ({fixtures}).")
    lines += [
        "",
        "For deeper reference, the repository holds docs/SPEC.md (the "
        "approved specification) and docs/DESIGN_REVIEW.md (this review's "
        "contract). You may read them. You may not change them.",
        "",
        ASSESS,
        EVIDENCE,
        PROHIBITIONS,
        OUTPUT,
    ]
    args.out.write_text("\n".join(lines))
    print(f"build_prompt: {args.out} ({len(ids)} images, round {args.round})")


if __name__ == "__main__":
    main()
