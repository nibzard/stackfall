# Circuit hush (level 10)

Sixteen right-angle circuit paths with square nodes whose cores breathe.

| Field | Value |
| --- | --- |
| Level | 10 of 20 |
| Chapter | Build (levels 9-12) |
| Base field | `#111721` |
| Secondary shades | `#283144` |
| Accent shades | `#6B8498` |
| Motion | pulse swell, 0.29 px/s, 7.0 s cycle |
| Level change | fade, 0.5 s in 15 visible steps |
| Intensity | 4.6 of 10 |
| Budget | at most 300 small elements |
| Verified counts | node x16, trace x64 |
| Measured marks | 71 per 1000 samples at one-eighth scale |
| Measured elements | 86 |
| Measured speed | 0.29 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #111721.
2. Lay out 16 paths, eight per side loud margin, one per band of margin height / 8 starting 8 px into the band. Each path has two right-angle bends: a horizontal run of secondary #283144, 2 px thick and 24 to 71 px long at a hashed x; a vertical drop 16 to 39 px tall at the run's right end; a second run 8 to 71 px back toward the margin edge; and a closing drop 14 to 35 px tall. Sixteen paths draw 64 trace rectangles.
3. Close every path with a 4 x 4 px square node of #283144 at the second drop's foot. Every third node carries a lit core of accent #6B8498, 2 x 2 px swelling to 3 x 3 px.
4. Motion: the lit cores swell over a 7 s cycle. No traces in the well, no letters, no arrowheads; broad clear zones behind the HUD because paths live in the margins only.

## Motion

Every third node core swells 2 to 3 px and returns over a 7 s cycle, 0.29 px/s at the fastest edge: the approved node pulse as a slow size change. The traces hold still.

## Approved reference

Owner-approved concept plate: [reference/level-10.png](reference/level-10.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Circuit"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-10-landscape.png](images/mockup-10-landscape.png)
- Portrait 544x960: [mockup-10-portrait.png](images/mockup-10-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
