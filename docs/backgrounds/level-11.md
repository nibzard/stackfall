# Band aurora (level 11)

Three broad stepped ribbons rising together, edges cascading.

| Field | Value |
| --- | --- |
| Level | 11 of 20 |
| Chapter | Build (levels 9-12) |
| Base field | `#131725` |
| Secondary shades | `#283044` |
| Accent shades | `#67758D` |
| Motion | drift up, 4 px/s, 161.2 s cycle |
| Level change | fade, 0.6 s in 18 visible steps |
| Intensity | 5 of 10 |
| Budget | at most 40 band elements |
| Verified counts | ribbon x9 |
| Measured marks | 159 per 1000 samples at one-eighth scale |
| Measured elements | 17 |
| Measured speed | 4 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #131725.
2. Draw three full-width ribbons of secondary #283044 with base rows at round(height * 0.16), round(height * 0.38) and round(height * 0.60). Each ribbon is three overlapping staircase bands, each round(height * 0.035) rows tall and offset 6 px per layer, so the edge cascades and the ribbon reads as one broad layered curtain (9 bands in all).
3. Each band's top edge is a staircase of 40 px steps with an 8 px lift (period 160 px); neighboring bands and ribbons alternate the mirrored phase and sit 2 to 5 px lower, so the edges cascade in opposing staircase contours.
4. Add 8 accent ticks of #67758D, 2 x 2 px, at hashed positions inside the loud margins, rising with the ribbons.
5. Motion: the ribbon layer rises 4 px/s, wrapping seamlessly as above.

## Motion

All three ribbons rise 4 px/s as one layer. Each wraps over canvas height plus its own extent - the three layered bands, their 6 px offsets, and the tallest 16 px lift, 101 px landscape and 146 px portrait - so a ribbon leaves the top edge fully before re-entering below the bottom edge: 161.25 s per pass landscape, 276.5 s portrait. The concept's 96 s loop would wrap a visible ribbon; this contract keeps the rise seamless instead.

## Approved reference

Owner-approved concept plate: [reference/level-11.png](reference/level-11.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Aurora"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-11-landscape.png](images/mockup-11-landscape.png)
- Portrait 544x960: [mockup-11-portrait.png](images/mockup-11-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
