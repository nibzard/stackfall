# Still ground (level 1)

Almost empty ink-navy field with four exceptionally faint dark corner steps.

| Field | Value |
| --- | --- |
| Level | 1 of 20 |
| Chapter | Still (levels 1-4) |
| Base field | `#10121A` |
| Secondary shades | `#0C0E16` |
| Accent shades | `#283044` |
| Motion | static |
| Level change | cut |
| Intensity | 1 of 10 |
| Budget | at most 40 band elements |
| Verified counts | corner x4, corner step x4 |
| Measured marks | 25 per 1000 samples at one-eighth scale |
| Measured elements | 8 |
| Measured speed | 0 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #10121A.
2. Into each of the four corners draw two nested steps of secondary #0C0E16: an outer rectangle of round(width / 6) x round(height / 4.5) px (160 x 121 landscape, 91 x 213 portrait) touching the two canvas edges, then an inner rectangle of 62 percent of those sizes touching the same corner. Each corner reads as two faint steps.
3. Nothing else draws. The accent shade #283044 is reserved and unused: the brief asks for no bright marks at all.

## Approved reference

Owner-approved concept plate: [reference/level-01.png](reference/level-01.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Still Ground"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-01-landscape.png](images/mockup-01-landscape.png)
- Portrait 544x960: [mockup-01-portrait.png](images/mockup-01-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
