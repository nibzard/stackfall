# Collapse (level 20)

Two hundred eighty tiny debris fragments falling past stepped slabs at the far edges.

| Field | Value |
| --- | --- |
| Level | 20 of 20 |
| Chapter | Peak (levels 17-20) |
| Base field | `#191E2B` |
| Secondary shades | `#2A3146` |
| Accent shades | `#8A94AC` |
| Motion | drift down, 10 px/s, 80.2 s cycle |
| Level change | fade, 0.6 s in 18 visible steps |
| Intensity | 10 of 10 |
| Budget | at most 300 small elements |
| Verified counts | debris x280 |
| Measured marks | 81 per 1000 samples at one-eighth scale |
| Measured elements | 292 |
| Measured speed | 10 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #191E2B.
2. Lay a 16 px cell grid inside each loud margin. Rank the cells by outer-edge closeness first and hash second, and take the first 280: the debris piles against the far canvas edges and thins toward a clear dark corridor beside the board.
3. Each fragment is 1 x 3 px in 35 percent of the cells, 2 x 5 px in 35 and 3 x 7 px in 30, at a hashed jitter inside its cell. Two thirds draw in secondary #2A3146 and one third in accent #8A94AC. A fragment that would leave its margin is skipped and the next candidate takes its place, so the count is exact.
4. In each loud margin draw three stepped slabs of secondary #2A3146, 32 to 71 px wide and 10 to 19 px tall, at hashed positions in the upper three quarters, each carrying a half-width 5 px step on its top.
5. Motion: the fragment layer falls 10 px/s and wraps over margin span plus fragment height; the slabs hold still. No tetromino clusters, no noise texture, no bright flare.

## Motion

Every fragment falls 10 px/s and wraps over margin span plus its own height, so fragments leave and enter fully off-canvas; the portrait side margin wraps after 802 px plus extent (80.2 s or more). The slabs hold still.

## Approved reference

Owner-approved concept plate: [reference/level-20.png](reference/level-20.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Collapse"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-20-landscape.png](images/mockup-20-landscape.png)
- Portrait 544x960: [mockup-20-portrait.png](images/mockup-20-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
