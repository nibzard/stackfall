# Low signal (level 8)

Low-contrast scanline groups in the margins with solid edge bars, scrolling down.

| Field | Value |
| --- | --- |
| Level | 8 of 20 |
| Chapter | Stir (levels 5-8) |
| Base field | `#11151F` |
| Secondary shades | `#242C3B` |
| Accent shades | `#54617A` |
| Motion | scroll down, 3 px/s, 3.3 s cycle |
| Level change | cut |
| Intensity | 3.8 of 10 |
| Budget | at most 300 small elements |
| Verified counts | signal bar x4 |
| Measured marks | 81 per 1000 samples at one-eighth scale |
| Measured elements | 8 |
| Measured speed | 3 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #11151F.
2. Inside each loud margin, draw 1 px rules of secondary #242C3B every 10 rows, in two signal bursts per margin: each burst spans an uneven x run of 44 to 95 px at a hashed position, over an uneven band 80 to 149 px tall at a hashed height, so the runs read as broken transmissions rather than a full field.
3. Draw two solid bars of accent #54617A, 3 px tall, inside the first two loud margins: one 14 px below the margin top and one 40 px above its bottom, spanning the margin width minus 12 px.
4. Motion: the rule set scrolls down 3 px/s, wrapping at the margin height, so the pattern repeats every 3.3 s.

## Motion

The rule set slides down 3 px/s; the 10 px spacing repeats every 3.3 s. The scroll is a rigid texture shift, not a pulse, so the brief's four-second pulse floor does not apply; the speed stays far under the 10 px/s drift ceiling.

## Approved reference

Owner-approved concept plate: [reference/level-08.png](reference/level-08.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Scanlines"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-08-landscape.png](images/mockup-08-landscape.png)
- Portrait 544x960: [mockup-08-portrait.png](images/mockup-08-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
