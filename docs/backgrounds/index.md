# The STACKFALL level backgrounds

Twenty procedural level backgrounds in five chapters of four. Every design is one pattern grammar in `tools/backgrounds/designs.py` that draws axis-aligned rectangles onto either canvas; the tools render the plates and mockups, and the acceptance check verifies every criterion of the brief mechanically.

T075 designed the set from the operator brief. T077 reconciled it with the owner-approved imagegen set (approval 2026-11-05): the approved names, palettes, concept lines, element counts, motion, and transitions are the contract, drawn with the T075 grammar machinery. The renderer integration follows as tasks T078 through T085 under [BACKGROUND_IMPLEMENTATION.md](../BACKGROUND_IMPLEMENTATION.md).

## Source and date

- Design date: 2026-11-05; reconciliation date: 2026-11-05.
- Brief: operator background brief, retained at docs/backgrounds/reference/operator-background-brief.md.
- Approved set: owner-approved imagegen set of 2026-11-05, retained at docs/backgrounds/reference/ (concepts.json, prompts.md, and the 20 concept plates).
- Layout mask: derived from the implemented geometry of `include/stackfall/renderer_sdl.hpp` (`kBoardOrigin`, `kBoardFrame`, the HUD zone constants, `kPortraitBoard`). The brief promised a mask image that never arrived; the approved prompts request the same geometry this mask encodes, so the derived mask is the authoritative one.

## The approved reference set

The owner approved the imagegen gallery on 2026-11-05 and requested implementation. The references this approval rests on are retained inside the repository, so no worktree, hosted gallery, or `/tmp` file is needed:

- [reference/operator-background-brief.md](reference/operator-background-brief.md) - the design brief the set was built against.
- [reference/concepts.json](reference/concepts.json) - the approved names, palettes, concept lines, motion, and intensities, one record per level.
- [reference/prompts.md](reference/prompts.md) - the exact imagegen prompts of the 20 concept plates.
- `reference/level-01.png` through `reference/level-20.png` - the original concept plates, 1536 x 1024 presentation rasters.
- [BACKGROUND_IMPLEMENTATION.md](../BACKGROUND_IMPLEMENTATION.md) - the owner's implementation plan: renderer contract, player options, validation, and the task structure T077 through T085.

The plates are direction, not captures. Each was requested as a 1536 x 1024 presentation raster holding two screenshots (a 960 x 544 landscape view and a 544 x 960 portrait view) with a level label above the left view; the generator placed them approximately and painted with anti-aliasing and heavy noise (over 100,000 distinct colors per plate), and several plates ignore the requested `#080A10` surround or drift past the brightness caps. Wherever a plate and this package disagree, the rectangle geometry, palette, and counts of the records here are authoritative, per the implementation plan.

## The layout mask

Landscape, 960x544:

| Zone | Rectangle | Rule |
| --- | --- | --- |
| Board well | x 345..614, y 2..541 | opaque `#0A0C13`; no design shows |
| Board frame | x 344..615, y 1..542 | one-pixel ring `#4E5874`; stays visible |
| Statistics zone | x 192..319, full height | quiet: nothing brighter than `#2A3146` |
| Next zone | x 640..767, full height | quiet |
| Gaps | x 320..343 and x 616..639 | quiet, 24 px each |
| Loud margins | x 0..191 and x 768..959 | accents may draw here |

Portrait, 544x960:

| Zone | Rectangle | Rule |
| --- | --- | --- |
| Board well | x 72..471, y 120..919 | opaque |
| Board frame | x 71..472, y 119..920 | one-pixel ring |
| HUD strip | y 0..118, full width | quiet |
| Gaps | x 59..70 and x 473..484 | quiet, 12 px each |
| Loud margins | x 0..58, x 485..543, y 921..959 | accents may draw here |

Mask sheets: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

## The design contract

Every cap comparison is componentwise: a color is within a cap when each of its red, green, and blue channels is at or below the cap's channel.

- Base fields stay within `#0A0C13` through `#1C2130`, componentwise in and componentwise out.
- Large secondary shapes stay at or below `#2A3146`.
- Small accents stay at or below `#8A94AC` and cover at most 2 percent of the canvas. Accent coverage is accounted over the complete logical canvas: accent-colored pixels painted in the final plate, divided by the full canvas area (960 x 544 or 544 x 960), with the opaque well and frame counting against the budget wherever they cover accent shapes.
- Nothing brighter than `#2A3146` draws inside the quiet zones or the guard gaps of either orientation.
- Accent shades may echo a piece hue; no large area ever takes a piece hue. Every base field and secondary shade stays at least 2 gray steps from all seven piece grays and below every piece gray, so the grayscale play mode keeps the pieces readable against every large area.
- Motion: levels 1 through 4 are static. From level 5, at most one moving layer per design, drift or scroll at or below 10 px/s, pulse cycles of at least 4 s. No blinking, no strobe. Wrapping is seamless: an element over 4 px in its motion axis wraps over a span that includes its own extent, so it leaves the canvas fully before re-entering; elements of 4 px or less may wrap at the margin span, where the crossing is sub-perceptual.
- Level changes use a cut or a fade of at most 1 s in visible steps; fade colors respect the same caps.
- Budget: at most 300 small elements or 40 band elements per design. Grouped elements (bands, rule sets, dither fields) count as one element but expand into primitives; the expanded counts live in each record's verified counts and in the manifests under `build/backgrounds/`.

## The twenty designs

| Level | Name | Chapter | Base | Motion | Peak speed | Intensity |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Still ground | Still | `#10121A` | static | 0 px/s | 1 |
| 2 | First light | Still | `#10131C` | static | 0 px/s | 1.4 |
| 3 | Distant stars | Still | `#0E121C` | static | 0 px/s | 1.8 |
| 4 | Quiet strata | Still | `#11141D` | static | 0 px/s | 2.2 |
| 5 | Slow drift | Stir | `#0F1420` | drift down | 2 px/s | 2.6 |
| 6 | Dither dusk | Stir | `#121620` | drift up | 2 px/s | 3 |
| 7 | Sleeping skyline | Stir | `#11151F` | pulse swell | 0.25 px/s | 3.4 |
| 8 | Low signal | Stir | `#11151F` | scroll down | 3 px/s | 3.8 |
| 9 | Fracture | Build | `#10151F` | drift down | 3 px/s | 4.2 |
| 10 | Circuit hush | Build | `#111721` | pulse swell | 0.29 px/s | 4.6 |
| 11 | Band aurora | Build | `#131725` | drift up | 4 px/s | 5 |
| 12 | Long trails | Build | `#111620` | drift down-left | 4 px/s | 5.4 |
| 13 | Deep grid | Pressure | `#121722` | scroll down | 5 px/s | 5.8 |
| 14 | Fine snow | Pressure | `#131923` | drift down | 6 px/s | 6.2 |
| 15 | Undertow | Pressure | `#131923` | scroll left | 6 px/s | 6.6 |
| 16 | Slow pulse | Pressure | `#141A26` | pulse out | 6 px/s | 7 |
| 17 | Rising ember | Peak | `#17191F` | drift up | 7 px/s | 7.7 |
| 18 | Storm lines | Peak | `#131925` | drift down | 8 px/s | 8.4 |
| 19 | Compression | Peak | `#171C28` | pulse in | 9 px/s | 9.2 |
| 20 | Collapse | Peak | `#191E2B` | drift down | 10 px/s | 10 |

One record per level sits beside this file:

- [Still ground](level-01.md) - level 1, Almost empty ink-navy field with four exceptionally faint dark corner steps
- [First light](level-02.md) - level 2, One fine dim horizontal rule low on the canvas with two broader darker under-bands
- [Distant stars](level-03.md) - level 3, Exactly 28 tiny square stars concentrated toward the outer margins
- [Quiet strata](level-04.md) - level 4, Eight broad uneven sediment bands with stepped ends near the outer edges
- [Slow drift](level-05.md) - level 5, Fifty-six square stars sinking slowly, with static dust beneath
- [Dither dusk](level-06.md) - level 6, Five stepped dusk zones with coarse checker patches rising at the outer edges
- [Sleeping skyline](level-07.md) - level 7, Fourteen stepped tower silhouettes with subdued breathing windows
- [Low signal](level-08.md) - level 8, Low-contrast scanline groups in the margins with solid edge bars, scrolling down
- [Fracture](level-09.md) - level 9, Two stepped diagonal fractures in the far margins with sinking edge ticks
- [Circuit hush](level-10.md) - level 10, Sixteen right-angle circuit paths with square nodes whose cores breathe
- [Band aurora](level-11.md) - level 11, Three broad stepped ribbons rising together, edges cascading
- [Long trails](level-12.md) - level 12, Seventy-two dim stars and twelve staircase meteor trails drifting as one layer
- [Deep grid](level-13.md) - level 13, Twenty-two staircase grid lines receding toward a mid-height horizon, the horizontal set scrolling
- [Fine snow](level-14.md) - level 14, Exactly 160 tiny square particles falling over dark bands
- [Undertow](level-15.md) - level 15, Seven interlocking stepped wave bands sliding left as one layer
- [Slow pulse](level-16.md) - level 16, Nine broken rectangular contours breathing outward around the scene
- [Rising ember](level-17.md) - level 17, Exactly 190 warm-gray embers rising past stepped block columns
- [Storm lines](level-18.md) - level 18, Two hundred rain dashes and four stepped trails falling as one layer
- [Compression](level-19.md) - level 19, Thirty broad broken bands pressing inward from all four edges
- [Collapse](level-20.md) - level 20, Two hundred eighty tiny debris fragments falling past stepped slabs at the far edges

## The chapter arc

The arc the brief demands: speed ramps from level 1 to level 20, and the background follows it.

- Still (levels 1-4): Fully static fields. Nothing moves; the board reads alone.
- Stir (levels 5-8): First motion. One slow layer per design, 2 to 3 px/s.
- Build (levels 9-12): Structure appears: seams, traces, ribbons, streaks.
- Pressure (levels 13-16): The set tightens: faster drift, denser marks, pulses.
- Peak (levels 17-20): The climax: the busiest fields, all near the caps.

Measured on the rendered plates, at the one-eighth review scale of the thumbnail sheet:

| Chapter | Mean marks per 1000 | Mean elements | Peak speed |
| --- | --- | --- | --- |
| Still | 79 | 20 | 0 px/s |
| Stir | 88 | 77 | 3 px/s |
| Build | 89 | 88 | 4 px/s |
| Pressure | 180 | 89 | 6 px/s |
| Peak | 194 | 260 | 10 px/s |

All three measures rise chapter over chapter, and the Peak chapter carries the most marks per sample of the five.

## Sheets

- [thumbnails.png](images/thumbnails.png) - all 20 designs at one-eighth scale, one row per chapter.
- [summary.png](images/summary.png) - all 20 names with their base color swatches.
- [mask-landscape.png](images/mask-landscape.png) and [mask-portrait.png](images/mask-portrait.png) - the layout mask of both orientations.
- `images/mockup-XX-landscape.png` and `images/mockup-XX-portrait.png` - every design under the real HUD and board.

## Check results

`python3 tools/backgrounds/check.py` re-paints every plate from its manifest with an independent rectangle-only painter, then runs every criterion of the brief: componentwise palette caps, accent coverage over the complete canvas, quiet-zone brightness, well purity, frame visibility on the mockups, motion bounds and layer count with the seamless-wrap rule, verified element counts, budget, grayscale separation from the seven piece colors, one grammar per level, neighbor distinctness at one-eighth scale, and the chapter rise above.

Run of 2026-11-05: **907 checks, 0 failures**. The results also land in `build/backgrounds/check-results.json`.

Regenerate everything with `tools/backgrounds/run.sh`.

## Integrated validation

Task T085 validated the implemented catalog end to end through the real
game: `tools/backgrounds/t085.sh` drives the ladder walk of every level in
both orientations (the two captured twenty-level thumbnail sheets of
`build/backgrounds/t085/` are real frames, one per level), the entry
transitions, the pause, restart, and focus-loss clocks, the grayscale and
Off comparisons, the orientation continuity, the rapid saved-option rings,
the determinism across modes, the legacy Off frame comparison against
revision `cd54ba2`, and the rendering cost against Off
(`notes/performance.md`). Its integrated check, `catalog_check.py`, holds
every captured frame against the package plates at a solved presentation
clock: 968 checks, 0 failures. The design records above and the runtime
agree byte for byte; open item 3 below keeps only the on-device look.

## Open questions

1. The brief promised a layout mask image; none arrived. The mask above is derived from the renderer geometry, and the approved prompts request the same zones, so it stands as authoritative.
2. Accent share is accounted over the complete logical canvas, as the implementation plan directs. Confirmed by the owner's plan of 2026-11-05.
3. An on-device look on the handheld screen remains open, and the playtest revision pass of task T076 stays blocked until the implementation tasks T078 through T085 deliver a playable build.
4. The renderer integration itself is task T078 through T085; this package carries the contracts they implement. The Still chapter of levels 1 through 4 landed in the renderer with task T079, from these records and the same seeds; the correction of that task - the landscape rule row of First light, 440, which the formula and the retained mockups always drew and the record prose mistyped as 448 - is already in level-02.md above. The Stir chapter of levels 5 through 8 landed with task T080, again from these records and the same seeds; its correction - the tower-height share of Sleeping skyline, `0.10 + 0.24 * (1 - corner closeness)`, which the machinery, mockups, and runtime draw and the record prose mistyped as 0.30 - is in level-07.md above. The Build chapter of levels 9 through 12 landed with task T081; its correction - the wrap phase of Band aurora, anchored one extent below the base row so a ribbon that has left the top edge re-enters below the bottom edge instead of popping back at its base row, and the pass periods that follow, 161.25 s landscape and 276.5 s portrait - is in level-11.md above. The Pressure chapter of levels 13 through 16 landed with task T082; its correction - the slide direction of Undertow, whose generator offset advanced positively and slid the staircase right against its own declared velocity, its record, and the approved concept, all of which name the leftward 6 px/s slide the generator now draws - is in level-15.md above. The Peak chapter of levels 17 through 20 landed with task T083; its corrections - the cell pitch of Storm lines, whose prose named a 22 px grid while the generator, the retained mockups, and the runtime all draw the 16 px grid, and the wrap note of the same design, whose dashes wrap over margin height plus the tallest dash's 6 px exactly as its trails do, not at the bare margin span - are in level-18.md above.
