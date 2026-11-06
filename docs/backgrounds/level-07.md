# Sleeping skyline (level 7)

Fourteen stepped tower silhouettes with subdued breathing windows.

| Field | Value |
| --- | --- |
| Level | 7 of 20 |
| Chapter | Stir (levels 5-8) |
| Base field | `#11151F` |
| Secondary shades | `#252D3D` |
| Accent shades | `#64728A` |
| Motion | pulse swell, 0.25 px/s, 8.0 s cycle |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 3.4 of 10 |
| Budget | at most 300 small elements |
| Verified counts | tower x14 |
| Measured marks | 132 per 1000 samples at one-eighth scale |
| Measured elements | 96 |
| Measured speed | 0.25 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #11151F.
2. Landscape: divide the canvas bottom into 14 equal slots and raise one tower per slot, 62 percent of the slot wide at a hashed offset. Portrait: raise 7 towers inside each outer side strip (14 total), 60 percent of a seventh of the strip wide, anchored on the canvas bottom edge.
3. Tower height is round(height * (0.10 + 0.24 * (1 - corner closeness) + hash / 1000)), so towers rise highest at the corners. Each tower carries two stepped roof blocks above its top: 60 percent of its width 8 px tall, then 30 percent 6 px taller still.
4. On towers taller than 30 percent of the canvas, draw windows of accent #64728A, 3 px wide and 4 to 5 px tall, two columns 4 px from the tower sides, one every 14 rows from 10 px below the top to its middle, only where they stay inside a loud margin.
5. Motion: the windows swell 1 px in height and return over an 8 s cycle. Nothing else moves.

## Motion

The window layer swells 1 px in height and returns over an 8 s cycle, 0.25 px/s at the fastest edge: the approved window pulse expressed as a slow size change, because a brightness pulse would blink. The towers hold still.

## Approved reference

Owner-approved concept plate: [reference/level-07.png](reference/level-07.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Skyline"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-07-landscape.png](images/mockup-07-landscape.png)
- Portrait 544x960: [mockup-07-portrait.png](images/mockup-07-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
