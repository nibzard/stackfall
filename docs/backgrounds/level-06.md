# Dither dusk (level 6)

Five stepped dusk zones with coarse checker patches rising at the outer edges.

| Field | Value |
| --- | --- |
| Level | 6 of 20 |
| Chapter | Stir (levels 5-8) |
| Base field | `#121620` |
| Secondary shades | `#242B3A` |
| Accent shades | `#38445B` |
| Motion | drift up, 2 px/s, 278.0 s cycle |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 3 of 10 |
| Budget | at most 300 small elements |
| Verified counts | dither x120, dusk zone x18 |
| Measured marks | 112 per 1000 samples at one-eighth scale |
| Measured elements | 138 |
| Measured speed | 2 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #121620.
2. Inside each side loud margin, draw five stepped horizontal zones from row round(height * 0.30) downward, heights round(height * share) for shares 8, 10, 12, 14 and 16 percent, separated by 4 px gaps. The zones alternate the two dark tones: zones 1, 3 and 5 carry the secondary #242B3A, zones 2 and 4 stay base, so the dusk reads as five steps. Each ink zone is one band inset 6 px from the margin edges plus a 6 px wide stepped end cap at each margin edge, 5 px taller than the zone (18 zone rectangles in all). The zones stay inside the margins, keeping the center corridor clear: no continuous full-screen texture.
3. Inside each side loud margin, place two checker patches per zone: a 16 x 12 px block of 4 px squares of accent #38445B on the even cells of its own 4 px block grid, at hashed positions at least 10 px inside the margin and the zone.
4. Motion: the patch layer drifts up at 2 px/s and wraps over margin height plus 12 px, so patches enter and leave fully off-canvas. The zones hold still.

## Motion

The patch layer rises 2 px/s. Each patch wraps over margin height plus its own 12 px height, so it leaves the canvas fully before re-entering below the bottom edge: the rise never pops. Landscape wraps after 556 px (278 s); the zones hold still.

## Approved reference

Owner-approved concept plate: [reference/level-06.png](reference/level-06.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Dither Dusk"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-06-landscape.png](images/mockup-06-landscape.png)
- Portrait 544x960: [mockup-06-portrait.png](images/mockup-06-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
