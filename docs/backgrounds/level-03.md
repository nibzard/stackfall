# Distant stars (level 3)

Exactly 28 tiny square stars concentrated toward the outer margins.

| Field | Value |
| --- | --- |
| Level | 3 of 20 |
| Chapter | Still (levels 1-4) |
| Base field | `#0E121C` |
| Secondary shades | `#1B2230` |
| Accent shades | `#64718C` |
| Motion | static |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 1.8 of 10 |
| Budget | at most 300 small elements |
| Verified counts | star x28 |
| Measured marks | 25 per 1000 samples at one-eighth scale |
| Measured elements | 28 |
| Measured speed | 0 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #0E121C.
2. Lay a 24 px cell grid inside each loud margin. Take the 28 cells with the lowest hash rank h32(0x5355, margin_x, cx, cy); both orientations draw exactly 28 stars.
3. Each star is 1 x 1 px in 55 percent of the cells and 2 x 2 px in 45, at a hashed jitter inside its cell. Stars are secondary #1B2230 or accent #64718C in equal share. A star that would leave its margin is skipped and the next-ranked cell takes its place. Empty near the well; no bright dots under the HUD.

## Approved reference

Owner-approved concept plate: [reference/level-03.png](reference/level-03.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Starfield"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-03-landscape.png](images/mockup-03-landscape.png)
- Portrait 544x960: [mockup-03-portrait.png](images/mockup-03-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
