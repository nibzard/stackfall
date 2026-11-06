# Rising ember (level 17)

Exactly 190 warm-gray embers rising past stepped block columns.

| Field | Value |
| --- | --- |
| Level | 17 of 20 |
| Chapter | Peak (levels 17-20) |
| Base field | `#17191F` |
| Secondary shades | `#2A3040` |
| Accent shades | `#897A72` |
| Motion | drift up, 7 px/s, 77.7 s cycle |
| Level change | cut |
| Intensity | 7.7 of 10 |
| Budget | at most 300 small elements |
| Verified counts | ember x190 |
| Measured marks | 106 per 1000 samples at one-eighth scale |
| Measured elements | 268 |
| Measured speed | 7 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #17191F.
2. Lay an 18 px cell grid inside each loud margin and take the 190 cells with the lowest hash rank h32(0xE17, ...). Each ember is 1 x 2 px in half of them and 2 x 2 px in the other half, at a hashed jitter, all in the warm-gray accent #897A72: muted warmth lives in the accent pixels only, never in a wash.
3. Along the lower fifth of each side loud margin, draw stepped block columns of secondary #2A3040: blocks 12 to 31 px wide and 10 px tall, end to end with 8 px gaps, stacked every 34 px up from the bottom edge, each column carrying a half-width 6 px step 6 px above each block. In the portrait bottom margin, one row of the same blocks lies flat.
4. Motion: the embers rise 7 px/s and wrap at their margin span; the columns hold still.

## Motion

Every ember rises 7 px/s and wraps at its margin span (544 px landscape, 77.7 s; 802 px portrait, 114.6 s). Embers are 2 px or less, so edge crossings are sub-perceptual. The columns hold still.

## Approved reference

Owner-approved concept plate: [reference/level-17.png](reference/level-17.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Ember"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-17-landscape.png](images/mockup-17-landscape.png)
- Portrait 544x960: [mockup-17-portrait.png](images/mockup-17-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
