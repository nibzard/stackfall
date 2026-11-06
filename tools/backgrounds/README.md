# Background design tools

The toolchain of the level background package (`docs/backgrounds`).
It is design tooling only: nothing here links into the game.

Task T075 built the package from the operator brief. Task T077
reconciled it with the owner-approved imagegen set (approval
2026-11-05): the catalogue now carries the approved names, palettes,
concept lines, element counts, motion, and transitions, drawn with the
same grammar machinery, and the approved references are retained under
`docs/backgrounds/reference/`.

One command builds the whole package, from the game's own checkpoints to
the reviewed deliverables:

```sh
tools/backgrounds/run.sh
```

`SKIP_CAPTURE=1` reuses the captures already under `build/backgrounds`.
Every step is deterministic: the same seed, scripts, and grammars always
produce the same bytes.

## Pipeline

1. `run.sh` runs the game harness twice with the scripted sessions
   `mockup-hud.script.txt` and `mockup-hud-portrait.script.txt` (seed
   31337, one tick per frame). Each session scores twice, loads the
   `tall-stack-ready` fixture, and holds, so the checkpoint frame
   `bg-mockup` carries the real HUD over a real board: a piece in
   flight, its ghost, a stashed Hold piece, five Next previews, and the
   statistics column. The portrait session is the same script through
   the rotated key table of task T055 (`key left` hard drops).
2. `render.py` turns the 20 grammars of `designs.py` into plates (the
   raw design layer at t = 0 s and t = 1 s, both orientations), one
   JSON manifest per plate, the mockups (plate plus the captured HUD
   and board, composited pixel by pixel), the two layout mask sheets,
   the one-eighth thumbnail sheet, and the name-and-swatch summary
   sheet. Generated files land under `build/backgrounds/`; the mockups
   and sheets land under `docs/backgrounds/images/` as the deliverables.
3. `check.py` runs every acceptance criterion of the brief (see below)
   and writes `build/backgrounds/check-results.json`.
4. `records.py` writes `docs/backgrounds/index.md` and the 20 level
   records from the catalogue and the check results, including each
   record's reconciliation note against the approved set.

## Files

| File | Role |
| --- | --- |
| `designs.py` | The 20 pattern grammars, the layout zones, the catalogue records |
| `imaging.py` | Dependency-free PNG writer and reader, and the harness BMP reader |
| `render.py` | Plates, manifests, mockups, and the sheets |
| `check.py` | The mechanical acceptance suite |
| `records.py` | The Markdown deliverables under `docs/backgrounds/` |
| `mockup-hud.script.txt` | The landscape capture session |
| `mockup-hud-portrait.script.txt` | The portrait capture session |
| `still-capture.script.txt` | The landscape real-game session of the
  implemented Still chapter (task T079): four-line clears walk the level
  ladder to capture levels 1 through 4 twice each |
| `still-capture-portrait.script.txt` | The portrait twin of the same
  session, through the rotated key table |
| `still_check.py` | The Still chapter acceptance check over those
  captures: record pixels, caps, accent share, well purity, the static
  proof, and one-eighth distinctness |
| `stir-capture.script.txt` | The landscape real-game session of the
  implemented Stir chapter (task T080): four-line clears walk the level
  ladder to levels 5 through 8 and capture three clock phases each |
| `stir-capture-portrait.script.txt` | The portrait twin of the same
  session, through the rotated key table |
| `stir_check.py` | The Stir chapter acceptance check over those
  captures: every frame's loud margins against the design package plate
  at its solved presentation clock, caps, accent share, well purity, the
  motion between the phases, and one-eighth distinctness |
| `build-capture.script.txt` | The landscape real-game session of the
  implemented Build chapter (task T081): the same ladder walk to levels
  9 through 12, three clock phases each |
| `build-capture-portrait.script.txt` | The portrait twin of the same
  session, through the rotated key table |
| `build_check.py` | The Build chapter acceptance check over those
  captures: the same solved-clock plate equality, caps, accent share,
  well purity, guard gaps, the motion between the phases, and
  one-eighth distinctness |
| `pressure-capture.script.txt` | The landscape real-game session of the
  implemented Pressure chapter (task T082): the same ladder walk to
  levels 13 through 16, three clock phases each |
| `pressure-capture-portrait.script.txt` | The portrait twin of the same
  session, through the rotated key table |
| `pressure_check.py` | The Pressure chapter acceptance check over those
  captures: the same solved-clock plate equality, caps, accent share,
  well purity, guard gaps, the swell phases of the pulse design, the
  motion between the phases, and one-eighth distinctness |
| `peak-capture.script.txt` | The landscape real-game session of the
  implemented Peak chapter (task T083): the same ladder walk to levels
  17 through 20, three clock phases each |
| `peak-capture-portrait.script.txt` | The portrait twin of the same
  session, through the rotated key table |
| `peak_check.py` | The Peak chapter acceptance check over those
  captures: the same solved-clock plate equality - the press rounding of
  the 54 px pulse included - caps, accent share, well purity, guard
  gaps, the press phases of the pulse design, the motion between the
  phases, and one-eighth distinctness |
| `run.sh` | The one-command pipeline |

## The layout mask

The operator brief promised a mask image that never arrived, so the
zones come from the implemented geometry of
`include/stackfall/renderer_sdl.hpp`. `designs.py` encodes them once:

- Landscape 960x544: well x 345..614 y 2..541, frame ring x 344..615
  y 1..542, quiet statistics zone x 192..319, quiet next zone
  x 640..767, quiet gaps x 320..343 and x 616..639, loud margins
  x 0..191 and x 768..959.
- Portrait 544x960: well x 72..471 y 120..919, frame x 71..472
  y 119..920, quiet HUD strip y 0..118, quiet gaps x 59..70 and
  x 473..484, loud margins x 0..58, x 485..543, y 921..959.

The approved imagegen prompts request the same zones, so the derived
mask is authoritative.

## What the check proves

`check.py` re-paints every plate from its manifest with its own
rectangle-only painter (`paint` reduces every element kind to
axis-aligned `fill_rect` calls), so plate and manifest can only agree
when the design truly is rectangles. On top of that it verifies:

- the palette caps, compared componentwise (each red, green, and blue
  channel at or below the cap's channel): base within `#0A0C13..#1C2130`,
  secondary at or below `#2A3146`, accents at or below `#8A94AC` and at
  most 2 percent of the complete logical canvas;
- the scene partners: nothing brighter than secondary in the quiet
  zones and gaps, a pure well, an intact frame ring and the real HUD
  on every mockup;
- motion: levels 1 to 4 byte-identical over time; from level 5 exactly
  one moving layer (one shared velocity for drift and scroll, one
  shared cycle for pulses, never both); drift and scroll at or below
  10 px/s; pulse cycles of at least 4 s, the floor the brief's rule 12
  states for pulses; seamless wrapping - an element over 4 px in its
  motion axis wraps over a span that includes its own extent, so it
  re-enters only after leaving fully, and texture scrolls (rule sets
  and full-width staircases shifting phase) are marked and skipped
  because their pattern is periodic by construction; fades at or below
  1 s in at least 10 visible steps; and moving plates really move
  within the displacement window the texture can show;
- the approved headline counts, by element tag, in both orientations
  (28, 56, 72, 160, 190, 200, and 280 dots; 14 towers; 16 circuit
  paths; 9 ribbon bands; 72 contour bars; 238 press bars; 190 embers;
  200 dashes; and the rest);
- budgets: at most 300 small elements or 40 band elements per design,
  counting grouped elements once, with the expanded primitive counts
  documented in the records and manifests;
- palette safety: no design color equals a piece color; base fields
  and secondary shades stay 2 or more gray steps from every piece gray
  and every piece gray sits above the base field, so the grayscale
  play mode keeps the pieces readable against every large area.
  Accents may echo a piece hue, as the brief's rule 6 allows - the
  approved accents `#79869B`, `#7A879E`, and `#7C88A0` sit within two
  gray steps of the blue piece in grayscale, which is why the distance
  rule covers large areas only;
- one grammar per level across both orientations;
- the arc: adjacent levels at least 1 percent distinct at one-eighth
  scale, and chapter means of mark density and element count plus
  chapter peak speed all rising through the five chapters.

The mark-density metric works at the review scale of the thumbnail
sheet: each sample is a 2x2 average of an 8x8 block, and a mark counts
when neighboring samples differ by 6 of 255 gray or more.

## Determinism

`h32` (a CRC32 over packed integers) drives every hashed placement, so
a grammar redraws identically on every run, in both orientations, and
`exact_dots` picks an exact count of cells by hash rank with
replacement of any candidate whose jitter would leave its margin. The
seeds are part of the contract: the records name them, and the
implementation tasks T078 onward must reproduce the same layouts from
the same formulas - the game draws them at runtime with its own
cosmetic generator, never from these Python tools and never from the
retained PNG plates.
