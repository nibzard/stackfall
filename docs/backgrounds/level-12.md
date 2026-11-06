# Long trails (level 12)

Seventy-two dim stars and twelve staircase meteor trails drifting as one layer.

| Field | Value |
| --- | --- |
| Level | 12 of 20 |
| Chapter | Build (levels 9-12) |
| Base field | `#111620` |
| Secondary shades | `#293144` |
| Accent shades | `#76849B` |
| Motion | drift down-left, 4 px/s, 90.7 s cycle |
| Level change | cut |
| Intensity | 5.4 of 10 |
| Budget | at most 300 small elements |
| Verified counts | star x72, trail head x12, trail step x72 |
| Measured marks | 35 per 1000 samples at one-eighth scale |
| Measured elements | 156 |
| Measured speed | 4 px/s |

## Pattern grammar

One grammar draws both canvases; only the canvas size and the layout zones change. Protected regions: nothing draws inside the opaque well (x 345..614, y 2..541 landscape; x 72..471, y 120..919 portrait) or over the one-pixel frame, and nothing brighter than `#2A3146` draws inside the quiet zones (the statistics and next columns, the portrait HUD strip, and the guard gaps). Accents draw only inside the loud margins. The full mask: [mask-landscape.png](images/mask-landscape.png), [mask-portrait.png](images/mask-portrait.png).

1. Fill the whole canvas with base #111620.
2. Lay a 20 px cell grid inside each loud margin and take the 72 cells with the lowest hash rank h32(0xC12, ...). Each star is 1 x 1 px in 55 percent of them and 2 x 2 px in 45, colored secondary #293144 or accent #76849B in equal share.
3. Draw 12 trails, alternating between the loud margins, each at a hashed position between 0 and 79 percent across and 0 and 89 percent down its margin. A trail is a staircase of six steps of secondary #293144, each 3 x 1 px, stepping 4 px left and 2 px down per segment, so the trail points to the upper right. Its head is one 2 x 2 px accent square of #76849B above the first step, dropped when it would leave the margin.
4. Motion: the whole star-and-trail layer drifts 2.4 px/s left and 3.2 px/s down (4.0 px/s total) and wraps inside its margin.

## Motion

Stars and trails share one velocity: 2.4 px/s left and 3.2 px/s down, 4.0 px/s along the path. Elements wrap at their margin spans; every element is 3 px or less in each axis, so edge crossings are sub-perceptual. The narrower portrait spans wrap first: the 39 px bottom strip height every 12.2 s and the 59 px side margin width every 24.6 s; the landscape height takes 170 s.

## Approved reference

Owner-approved concept plate: [reference/level-12.png](reference/level-12.png). The plate is visual direction, not an exact capture: the generated image is anti-aliased with off-palette noise, and this record's rectangle geometry is authoritative wherever the two differ.

## Reconciliation

Reconciled in T077 against the owner-approved set (approval 2026-11-05). The T075 design of this slot was "Meteors"; its grammar machinery draws on, with the approved name, palette, counts, motion, and transition adopted here. The T075 mockups and records were regenerated from the same machinery, so no completed design work was discarded.

## Mockups

The design under the real HUD and board, taken from the capture session `tools/backgrounds/mockup-hud.script.txt` and its portrait twin:

- Landscape 960x544: [mockup-12-landscape.png](images/mockup-12-landscape.png)
- Portrait 544x960: [mockup-12-portrait.png](images/mockup-12-portrait.png)

## Review status

Passed the mechanical acceptance check of `python3 tools/backgrounds/check.py` (run of 2026-11-05). Visually reviewed on the thumbnail sheet: no letters, numerals, arrows, or tetromino clusters, and distinct from both neighbors at one-eighth scale. An on-device look on the handheld remains open; the playtest pass of task T076 stays blocked until the playable implementation of T078 through T085 lands.
