# Deep grid (level 13)

Twenty-two staircase grid lines receding toward a mid-height horizon, the horizontal set scrolling.

| Field | Value |
| --- | --- |
| Level | 13 of 20 |
| Chapter | Pressure (levels 13-16) |
| Base field | `#121722` |
| Secondary shades | `#293145` |
| Accent shades | `#79869B` |
| Motion | scroll down, 5 px/s, 108.8 s cycle |
| Level change | fade, 0.6 s in 18 visible steps |
| Intensity | 5.8 of 10 |
| Budget | at most 300 small elements |
| Verified counts | grid guide x40, grid node x10, grid rule x24 |
| Measured marks | 114 per 1000 samples at one-eighth scale |
| Measured elements | 74 |
| Measured speed | 5 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #121722.
2. Draw 12 horizontal 1 px rules of secondary #293145, six above and six below the canvas middle row, at offsets 6, 11, 18, 27, 38 and 51 px: the spacing tightens toward the horizon at mid-height and widens toward the edges. Each rule draws once inside every tall loud margin (both side strips), so the plate carries 24 rule bars for the 12 line positions.
3. Draw 10 vertical guides of the same color, 2 px wide, five evenly spaced inside each of the first two loud margins, each in four segments of one quarter of the margin height (40 bars for the 10 guides), each segment stepping 1 px toward the canvas center, so the guides converge in staircases.
4. Close each guide with one 2 x 2 px accent node of #79869B at its foot. The 22 staircase lines of the concept are these 12 horizontal rules plus these 10 vertical guides.
5. Motion: the horizontal rules scroll down 5 px/s and wrap at the margin span; the guides and nodes hold still.

## Motion

The horizontal rules scroll down 5 px/s as one rigid set and wrap at their margin span (544 px landscape, 108.8 s; 802 px portrait, 160.4 s). Because every rule shares one velocity, the spacing pattern translates without deforming. The vertical guides and nodes hold still.

## Approved reference

Owner-approved concept plate: [reference/level-13.png](reference/level-13.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Deep Grid"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-13-landscape.png](images/mockup-13-landscape.png)
- Portrait 544x960: [mockup-13-portrait.png](images/mockup-13-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
