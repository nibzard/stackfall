# First light (level 2)

One fine dim horizontal rule low on the canvas with two broader darker under-bands.

| Field | Value |
| --- | --- |
| Level | 2 of 20 |
| Chapter | Still (levels 1-4) |
| Base field | `#10131C` |
| Secondary shades | `#1C2433` |
| Accent shades | `#2A3146` |
| Motion | static |
| Level change | fade, 0.4 s in 12 visible steps |
| Intensity | 1.4 of 10 |
| Budget | at most 300 small elements |
| Verified counts | rule x1, under band x2 |
| Measured marks | 57 per 1000 samples at one-eighth scale |
| Measured elements | 3 |
| Measured speed | 0 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #10131C.
2. Draw one horizontal rule of accent #2A3146, 1 px tall, across the full canvas width at row (round(height * 0.82) rounded down to a multiple of 8): row 440 landscape, row 784 portrait. The opaque well and its frame cover it where they overlap.
3. Draw two broader under-bands of secondary #1C2433 below the rule: rows rule+8 .. rule+10 (3 px) and rule+16 .. rule+19 (4 px), both full width.
4. Static. On level change the design fades in over 0.4 s in 12 visible steps.

## Approved reference

Owner-approved concept plate: [reference/level-02.png](reference/level-02.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "First Light"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-02-landscape.png](images/mockup-02-landscape.png)
- Portrait 544x960: [mockup-02-portrait.png](images/mockup-02-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
