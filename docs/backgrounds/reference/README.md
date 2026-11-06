# The approved background reference set

Date: 2026-11-05. Owner approval: the imagegen gallery of 20 concept
plates, plus the request to implement every background with saved Auto,
Off, and Fixed options. This directory retains the references that
approval rests on, so later worktrees need no `/tmp` file, primary
workspace path, or hosted link. The implementation plan retained at
[docs/BACKGROUND_IMPLEMENTATION.md](../../BACKGROUND_IMPLEMENTATION.md)
directs the whole round.

## Files

| File | Role |
| --- | --- |
| `operator-background-brief.md` | The design brief: medium, stage, scene colors, design rules, the 20-slot arc, deliverables, and acceptance checks. |
| `concepts.json` | The approved concept metadata: name, chapter, base, secondary, accent, concept line, motion, intensity, and plate path, one record per level 1 through 20. |
| `prompts.md` | The exact imagegen prompt of each of the 20 plates, including the two-view plate layout and the requested geometry. |
| `level-01.png` .. `level-20.png` | The original generated concept plates, 1536 x 1024, one per level. |

Not retained here: AXINBOX records, drop credentials, the hosted gallery
files, and the build scripts of the mock run. The hosted gallery link
inside the implementation plan expired on 2026-11-08 unless claimed; the
local set above replaces it.

## How to read the plates

Each plate was requested as a 1536 x 1024 presentation raster on
`#080A10` holding exactly two complete game screenshots - a 960 x 544
landscape view at x 0..959, y 240..783 and a 544 x 960 portrait view at
x 992..1535, y 32..991 - with the level number and name set above the
left view. The generator followed this only approximately:

- placements vary from plate to plate, and several plates paint their
  motif over the whole raster instead of leaving the `#080A10` surround;
- the rendering is anti-aliased with heavy noise - over 100,000
  distinct colors per plate - not the requested hard-edged rectangle
  medium;
- some plate colors drift past the brief's brightness caps.

The plates are visual direction, never exact captures. Wherever a plate
and the reconciled records under [../](../index.md) disagree, the record
wins: its rectangle geometry, role palette, counts, motion, and
transition are the contract that tasks T078 through T085 implement, and
`tools/backgrounds/check.py` verifies every one of them mechanically
against the caps of the brief.

## Provenance

- Intake sources named by task T077: the brief, the implementation plan,
  `gallery/concepts.json`, `gallery/prompts.md`, and
  `originals/level-01.png` through `level-20.png` of the background
  mock set.
- Copies are byte-identical to the originals; nothing was re-encoded or
  edited.
- The reconciled level mapping (approved names, level 1 through 20) is
  the table of [../index.md](../index.md), generated from
  `concepts.json` and the grammars of `tools/backgrounds/designs.py`.
