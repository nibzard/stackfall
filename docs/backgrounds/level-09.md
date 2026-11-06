# Fracture (level 9)

Two stepped diagonal fractures in the far margins with sinking edge ticks.

| Field | Value |
| --- | --- |
| Level | 9 of 20 |
| Chapter | Build (levels 9-12) |
| Base field | `#10151F` |
| Secondary shades | `#283044` |
| Accent shades | `#707D96` |
| Motion | drift down, 3 px/s, 181.3 s cycle |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 4.2 of 10 |
| Budget | at most 300 small elements |
| Verified counts | ledge x24, seam x24 |
| Measured marks | 93 per 1000 samples at one-eighth scale |
| Measured elements | 92 |
| Measured speed | 3 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #10151F.
2. In the left loud margin, draw one stepped diagonal of secondary #283044 from the margin's top-left toward its bottom-right corner in 12 equal steps: each step is a rectangle 5 px thick and one step-width wide (margin width / 12 + 1: 17 px landscape, 5 px portrait), each step one twelfth of the margin height below the one before.
3. In the right loud margin, draw the mirrored staircase from its top-right toward its bottom-left corner, same sizes.
4. Each fracture carries a dark offset ledge: a 22 px tall staircase of the same secondary, shifted 12 px toward the canvas center.
5. Beside every step, on both sides of the seam, place one accent tick of #707D96, 2 x 2 px, 6 to 15 px below the step, only inside the margin. Motion: the ticks sink 3 px/s and wrap at the margin span; both staircases hold still.

## Motion

The tick layer sinks 3 px/s and wraps at its margin span (544 px landscape, 181 s; 802 px portrait, 267 s). The 2 px ticks cross the edge sub-perceptually. Both fracture staircases hold still.

## Approved reference

Owner-approved concept plate: [reference/level-09.png](reference/level-09.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Fault"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-09-landscape.png](images/mockup-09-landscape.png)
- Portrait 544x960: [mockup-09-portrait.png](images/mockup-09-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
