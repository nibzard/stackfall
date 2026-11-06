# Undertow (level 15)

Seven interlocking stepped wave bands sliding left as one layer.

| Field | Value |
| --- | --- |
| Level | 15 of 20 |
| Chapter | Pressure (levels 13-16) |
| Base field | `#131923` |
| Secondary shades | `#2A3146` |
| Accent shades | `#74839B` |
| Motion | scroll left, 6 px/s, 13.3 s cycle |
| Level change | fade, 0.6 s in 18 visible steps |
| Intensity | 6.6 of 10 |
| Budget | at most 40 band elements |
| Verified counts | wave band x7 |
| Measured marks | 231 per 1000 samples at one-eighth scale |
| Measured elements | 35 |
| Measured speed | 6 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #131923.
2. Draw seven full-width stepped bands of secondary #2A3146, round(height * 0.07) rows tall, at base rows round(height * (0.28 + 0.09 * i)) for i = 0..6.
3. Each band's top edge is a staircase of 20 px steps with a 6 px lift (period 80 px); alternate bands carry the mirrored phase and each sits 3 px lower per index.
4. On every band, scatter two foam ticks of accent #74839B, 2 x 1 px, at hashed positions inside each loud margin, 5 to 14 px above the band edge. The foam holds its margin slots while the band layer moves.
5. Motion: the whole band layer slides left 6 px/s, wrapping every 80 px.

## Motion

The whole band layer slides left 6 px/s. The 80 px staircase period repeats every 13.3 s on both canvases. Alternate bands carry mirrored phases, so the stepped edges read as two opposing currents while every rectangle shares one velocity. (The pre-T082 generator advanced its offset positively, sliding the staircase right against this note and the declared velocity; the corrected generator and the runtime of task T082 draw the leftward slide.)

## Approved reference

Owner-approved concept plate: [reference/level-15.png](reference/level-15.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Undertow"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-15-landscape.png](images/mockup-15-landscape.png)
- Portrait 544x960: [mockup-15-portrait.png](images/mockup-15-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
