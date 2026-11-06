# Compression (level 19)

Thirty broad broken bands pressing inward from all four edges.

| Field | Value |
| --- | --- |
| Level | 19 of 20 |
| Chapter | Peak (levels 17-20) |
| Base field | `#171C28` |
| Secondary shades | `#2A3146` |
| Accent shades | `#8591A8` |
| Motion | pulse in, 9 px/s, 12.0 s cycle |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 9.2 of 10 |
| Budget | at most 300 small elements |
| Verified counts | press bar x238 |
| Measured marks | 508 per 1000 samples at one-eighth scale |
| Measured elements | 260 |
| Measured speed | 9 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #171C28.
2. Draw 30 nested broken rectangular bands of secondary #2A3146, 3 px thick, starting 8 px from the canvas edges and stepping inward by max(5, 13 - index / 3) px per band, so the spacing tightens from 13 px outside to 5 px near the board: insets 8, 21, 34, 47, 59, 71, 83, 94, 105, 116, 126, 136, 146, 155, 164, 173, 181, 189, 197, 204, 211, 218, 224, 230, 236, 241, 246, 251, 256 and 261 px.
3. Each band is broken: each of its four bars is split once by a 20 px opening at a hashed offset at least 8 px inside the bar. The innermost band's two short cross-axis bars (22 px) stay whole, so the nest draws 238 bars. Bars that would cross the canvas edges clip there.
4. On the two outermost bands, lay scarce accent ticks of #8591A8, 2 x 2 px every 32 px along the horizontal bar pieces that pass through loud margins.
5. Motion: every band presses 54 px inward and returns over a 12 s cycle. The board stays the clear center of attention.

## Motion

The whole nest contracts 54 px onto the board and returns over a 12 s seamless cycle: 108 px of travel in 12 s, 9 px/s at the fastest edge, exactly the approved pulse. A linear inward scroll would teleport each band back to its outer edge when it wrapped; the triangle pulse keeps the motion continuous.

## Approved reference

Owner-approved concept plate: [reference/level-19.png](reference/level-19.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Pressure"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-19-landscape.png](images/mockup-19-landscape.png)
- Portrait 544x960: [mockup-19-portrait.png](images/mockup-19-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
