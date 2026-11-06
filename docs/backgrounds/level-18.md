# Storm lines (level 18)

Two hundred rain dashes and four stepped trails falling as one layer.

| Field | Value |
| --- | --- |
| Level | 18 of 20 |
| Chapter | Peak (levels 17-20) |
| Base field | `#131925` |
| Secondary shades | `#2A3146` |
| Accent shades | `#8290A7` |
| Motion | drift down, 8 px/s, 68.0 s cycle |
| Level change | cut |
| Intensity | 8.4 of 10 |
| Budget | at most 300 small elements |
| Verified counts | dash x200 |
| Measured marks | 80 per 1000 samples at one-eighth scale |
| Measured elements | 220 |
| Measured speed | 8 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #131925.
2. Lay a 16 px cell grid inside each loud margin and take the 200 cells with the lowest hash rank h32(0x518, ...). Each dash is 2 px wide and 4 px tall in 65 percent of them and 2 x 6 px in 35, at a hashed jitter, colored secondary #2A3146 or accent #8290A7 in equal share.
3. Draw four longer stepped trails, cycling through the loud margins, each at 50 percent across and 8, 38, 62 and 86 percent down its margin: a staircase of five segments of secondary #2A3146, each 4 x 2 px, stepping 4 px left and 2 px down per segment.
4. Motion: every dash and trail falls 8 px/s and wraps as above. Dim slate blue; no lightning, no glow.

## Motion

Every dash and trail falls 8 px/s. Dashes and trail steps alike wrap over margin height plus 6 px - the tallest dash's extent, which keeps every element over 4 px on its fall axis leaving fully before it re-enters - while the 2 px dash width keeps the horizontal edge sub-perceptual. (The pre-T083 note named the bare margin span for the dashes and the extent for the trails alone; the generator always wrapped both over the span plus 6, and the runtime of task T083 draws that wrap.)

## Approved reference

Owner-approved concept plate: [reference/level-18.png](reference/level-18.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Storm"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-18-landscape.png](images/mockup-18-landscape.png)
- Portrait 544x960: [mockup-18-portrait.png](images/mockup-18-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
