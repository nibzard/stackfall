# Slow pulse (level 16)

Nine broken rectangular contours breathing outward around the scene.

| Field | Value |
| --- | --- |
| Level | 16 of 20 |
| Chapter | Pressure (levels 13-16) |
| Base field | `#141A26` |
| Secondary shades | `#2A3146` |
| Accent shades | `#7C88A0` |
| Motion | pulse out, 6 px/s, 8.0 s cycle |
| Level change | fade, 0.6 s in 18 visible steps |
| Intensity | 7 of 10 |
| Budget | at most 300 small elements |
| Verified counts | contour bar x72 |
| Measured marks | 245 per 1000 samples at one-eighth scale |
| Measured elements | 84 |
| Measured speed | 6 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #141A26.
2. Draw nine nested rectangular contours of secondary #2A3146, 2 px thick, inset 10, 36, 62, 88, 114, 140, 166, 192 and 218 px from the canvas edges.
3. Each contour is broken: each of its four bars is split once by a 24 px opening at a hashed offset at least 8 px inside the bar, so no contour is a closed ring. Nine contours of eight bars draw 72 bars in all.
4. On the outermost three contours, draw one 2 x 2 px accent tip of #7C88A0 at each corner that lies inside a loud margin.
5. Motion: every contour swells 24 px outward and returns over an 8 s cycle. Rectangular pressure only; never circular, never neon.

## Motion

Every contour swells 24 px outward from the canvas edges and returns over an 8 s cycle: 48 px of travel in 8 s, 6 px/s at the fastest edge, exactly the approved pulse.

## Approved reference

Owner-approved concept plate: [reference/level-16.png](reference/level-16.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Pulse"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-16-landscape.png](images/mockup-16-landscape.png)
- Portrait 544x960: [mockup-16-portrait.png](images/mockup-16-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
