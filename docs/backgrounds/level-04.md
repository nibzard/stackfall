# Quiet strata (level 4)

Eight broad uneven sediment bands with stepped ends near the outer edges.

| Field | Value |
| --- | --- |
| Level | 4 of 20 |
| Chapter | Still (levels 1-4) |
| Base field | `#11141D` |
| Secondary shades | `#222A39` |
| Accent shades | `#2A3146` |
| Motion | static |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 2.2 of 10 |
| Budget | at most 40 band elements |
| Verified counts | stratum x8 |
| Measured marks | 208 per 1000 samples at one-eighth scale |
| Measured elements | 40 |
| Measured speed | 0 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #11141D.
2. Stack eight full-width bands of secondary #222A39 from the bottom edge upward, brightest reading first. Band heights are round(height * share) for shares 5.5, 4.5, 6.0, 4.0, 5.0, 3.5, 4.5 and 3.0 percent, separated by 9 px gaps of base, so the stack fills the bottom third of the canvas.
3. At each band end that reaches a loud margin, draw one step of the same secondary: a block 6 px wide and 6 px taller than the band, on the band's bottom edge at the margin's outer edge, only when it fits inside the margin.
4. The bands run under the quiet zones at their secondary shade; the well and frame cover them behind the board.

## Approved reference

Owner-approved concept plate: [reference/level-04.png](reference/level-04.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Strata"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-04-landscape.png](images/mockup-04-landscape.png)
- Portrait 544x960: [mockup-04-portrait.png](images/mockup-04-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
