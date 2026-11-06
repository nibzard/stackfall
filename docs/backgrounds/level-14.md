# Fine snow (level 14)

Exactly 160 tiny square particles falling over dark bands.

| Field | Value |
| --- | --- |
| Level | 14 of 20 |
| Chapter | Pressure (levels 13-16) |
| Base field | `#131923` |
| Secondary shades | `#293145` |
| Accent shades | `#7A879E` |
| Motion | drift down, 6 px/s, 90.7 s cycle |
| Level change | cut |
| Intensity | 6.2 of 10 |
| Budget | at most 300 small elements |
| Verified counts | flake x160, snow band x3 |
| Measured marks | 129 per 1000 samples at one-eighth scale |
| Measured elements | 163 |
| Measured speed | 6 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #131923.
2. Lay a 20 px cell grid inside each loud margin and take the 160 cells with the lowest hash rank h32(0xE14, ...). Each particle is 1 x 1 px in 55 percent of them and 2 x 2 px in 45, colored secondary #293145 or accent #7A879E in equal share.
3. Draw three full-width bands of secondary #293145 behind the field, 3.5 percent of the canvas height tall, at 30, 55 and 80 percent height.
4. Motion: the particle layer falls 6 px/s and wraps at its margin span.

## Motion

Every particle falls 6 px/s and wraps at its margin span (544 px landscape, 90.7 s; 802 px portrait, 133.7 s). Particles are 2 px or less, so edge crossings are sub-perceptual. The bands hold still.

## Approved reference

Owner-approved concept plate: [reference/level-14.png](reference/level-14.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Snow"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-14-landscape.png](images/mockup-14-landscape.png)
- Portrait 544x960: [mockup-14-portrait.png](images/mockup-14-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
