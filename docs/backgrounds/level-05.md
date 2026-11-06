# Slow drift (level 5)

Fifty-six square stars sinking slowly, with static dust beneath.

| Field | Value |
| --- | --- |
| Level | 5 of 20 |
| Chapter | Stir (levels 5-8) |
| Base field | `#0F1420` |
| Secondary shades | `#20283A` |
| Accent shades | `#687892` |
| Motion | drift down, 2 px/s, 272.0 s cycle |
| Level change | cut |
| Intensity | 2.6 of 10 |
| Budget | at most 300 small elements |
| Verified counts | dust x10, star x56 |
| Measured marks | 26 per 1000 samples at one-eighth scale |
| Measured elements | 66 |
| Measured speed | 2 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #0F1420.
2. Lay a 20 px cell grid inside each loud margin. Take the 56 cells with the lowest hash rank h32(0x5356, margin_x, cx, cy). Each star is 1 x 1 px in 60 percent of them and 2 x 2 px in 40, at a hashed jitter, colored secondary #20283A or accent #687892 in equal share.
3. Add 10 static dust flecks of secondary #20283A, 3 x 1 px, by hash rank h32(0x5357, ...) on a 28 px grid.
4. Motion: the star layer drifts down at 2 px/s. A star that reaches the bottom of its margin re-enters at its top; a 2 px star crossing the edge is sub-perceptual, so no visible jump.

## Motion

Every star sinks 2 px/s and wraps at its margin span. The landscape margin is 544 px tall, so one star completes one pass in 272 s; the portrait side margin takes 401 s. The dust flecks hold still, so the layer beneath is still.

## Approved reference

Owner-approved concept plate: [reference/level-05.png](reference/level-05.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Drift"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-05-landscape.png](images/mockup-05-landscape.png)
- Portrait 544x960: [mockup-05-portrait.png](images/mockup-05-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
