# Anbernic device screens and the resolution strategy

Research date: 2026-11-04.
Task: T056. Owner requested this research on 2026-11-04.
This note changes no production code.

The game renders one fixed 960×544 canvas and scales it to any panel.
`include/stackfall/renderer_sdl.hpp:35` defines the canvas and
`src/renderer_sdl.cpp` (`applyCanvas` and `beginFrame`) applies the letterbox.
The task request states that the RG503 port scales the canvas to a 640×480
panel. The sources below place the RG503 panel at 960×544, so the canvas is
native there. The 640×480 scaling case is the 4:3 device class of this note.
See the open questions for the discrepancy.

## Method

Web research on 2026-11-04. Sources are the official Anbernic product pages,
the ROCKNIX device wiki, and reviews from Retro Dodo, Retro Handhelds,
Liliputing, Flatfoot Fox, DroiX, and Notebookcheck. Anbernic product pages do
not state refresh rates. Reviews report 60 Hz for the RG353 line, the RG556,
and the RG 55G1. Treat 60 Hz as the planning value for every device below.
The fixed 60 Hz simulation and the vsync frame limiter already adapt to a
panel that runs at another rate.

## Device table

| Device (year) | Panel | Native resolution | Aspect ratio | Refresh rate | Class |
| --- | --- | --- | --- | --- | --- |
| RG503 (2022) | 4.95 in organic light-emitting diode (OLED) | 960×544 | 30:17 (near 16:9) | 60 Hz typical, not stated | B |
| RG505 (2023) | 4.95 in OLED, touch | 960×544 | 30:17 | not stated | B |
| RG351P / RG351M (2020) | 3.5 in in-plane switching (IPS) | 480×320 | 3:2 | not stated | E |
| RG351V (2021) | 3.5 in IPS | 640×480 | 4:3 | not stated | A |
| RG353P / M / PS / V / VS (2022) | 3.5 in IPS, touch | 640×480 | 4:3 | 60 Hz | A |
| RG35XX (2022) | 3.5 in IPS | 640×480 | 4:3 | not stated | A |
| RG35XX Plus / H / SP / 2024 (2024) | 3.5 in IPS | 640×480 | 4:3 | not stated | A |
| RG35XX Pro (2025) | 3.5 in IPS | 640×480 | 4:3 | not stated | A |
| RG405M / RG405V (2023) | 4.0 in IPS, touch | 640×480 | 4:3 | not stated | A |
| RG40XX H / V (2024) | 4.0 in IPS | 640×480 | 4:3 | not stated | A |
| RG28XX (2024) | 2.8 in | 640×480 | 4:3 | not stated | A |
| RG34XX (2025) | 3.4 in IPS | 720×480 | 3:2 | not stated | E |
| RG Cube / RG CubeXX (2024 to 2025) | 3.95 in IPS, touch | 720×720 | 1:1 | not stated | D |
| RG552 (2021) | 5.36 in IPS, touch | 1920×1152 | 5:3 | not stated | C |
| RG556 (2024) | 5.48 in active-matrix OLED (AMOLED), touch | 1920×1080 | 16:9 | 60 Hz | C |
| RG557 (2025) | 5.48 in AMOLED, touch | 1920×1080 | 16:9 | not stated | C |
| RG 55G1 (2026) | 5.5 in IPS, touch | 1920×1080 | 16:9 | 60 Hz | C |

Devices with a Linux distribution that runs PortMaster: the ROCKNIX device
list covers the RG351 line, the RG353 line, the RG503, the RG552, the RG 55G1,
the RG Arc, the RG35XX Plus, Pro, 2024, H, and SP, the RG28XX, the RG40XX H
and V, the RG CubeXX, and the RG34XX. The RG405 line, the RG505, the RG556,
and the RG557 ship Android, where PortMaster runs through its Android client.
The RG Arc appears in the ROCKNIX device list; this research did not verify
its panel, and reviews report the same 3.5-inch 640×480 panel as the RG353.

## Sources per device

- RG503: [ROCKNIX device page](https://rocknix.org/devices/anbernic/rg503/)
  ("5-inch 960*544"), [DroiX review](https://droix.net/blogs/rg503-review/),
  [Liliputing](https://liliputing.com/anbernic-rg503-handheld-game-console-has-an-oled-screen-and-starting-price-under-150).
- RG505: [Anbernic product page](https://anbernic.com/products/rg505),
  [Retro Dodo review](https://retrododo.com/rg505).
- RG351 line: [ROCKNIX device page](https://rocknix.org/devices/anbernic/rg351pmv/)
  ("3.5-inch 480_320 (P/M), 640_480 (V)").
- RG353 line: [Anbernic RG353PS](https://anbernic.com/products/rg353ps),
  [Anbernic RG353V/VS](https://anbernic.com/products/rg353v-rg353vs),
  [Retro Dodo RG353P](https://retrododo.com/anbernic-rg353p).
- RG35XX: [Anbernic product page](https://anbernic.com/products/rg35xx)
  ("3.5-inch IPS full viewing angle, OCA full lamination/ 640*480").
- RG35XX Plus / H / SP: [Anbernic RG35XX H](https://anbernic.com/products/rg35xx-h),
  [Retro Handhelds RG35XX SP review](https://retrohandhelds.gg/anbernic-rg35xx-sp-review).
- RG35XX Pro: [Anbernic product page](https://anbernic.com/products/rg35xxpro),
  [Retro Dodo announcement](https://retrododo.com/anbernic-announce-new-rg35xx-pro-handheld-in-three-colours).
- RG405 line: [Anbernic RG405M](https://anbernic.com/products/rg405m),
  [Retro Dodo RG405V](https://retrododo.com/anbernic-rg405v-review).
- RG40XX H / V: [Anbernic RG40XX H](https://anbernic.com/products/rg40xx-h),
  [Anbernic RG40XX V](https://anbernic.com/products/rg-40xxv).
- RG28XX: [ROCKNIX device page](https://rocknix.org/devices/anbernic/rg28xx/)
  ("2.8-inch 640*480").
- RG34XX: [Anbernic product page](https://anbernic.com/products/rg34xx)
  ("3.4-inch IPS full view angle, OCA full lamination 720*480"),
  [ROCKNIX device page](https://rocknix.org/devices/anbernic/rg34xx/).
- RG Cube and RG CubeXX: [Anbernic RG Cube](https://anbernic.com/products/rg-cube),
  [Anbernic RG CubeXX](https://anbernic.com/products/rg-cubexx)
  (both "720*720").
- RG552: [Anbernic product page](https://anbernic.com/products/anbernic-rg552)
  ("5.36 inch OCA full lamination IPS 1920 * 1152 (5:3)"),
  [Flatfoot Fox review](https://flatfootfox.com/anbernic-rg552).
- RG556: [Anbernic product page](https://anbernic.com/products/rg556),
  [DroiX review](https://droix.net/blogs/anbernic-rg556-review-with-video/).
- RG557: [Anbernic product page](https://anbernic.com/products/rg557),
  [NotebookCheck](https://www.notebookcheck.net/New-Anbernic-RG-557-Android-gaming-handheld-with-5-5-inch-AMOLED-display-officially-unveiled.994856.0.html).
- RG 55G1: [Anbernic product page](https://anbernic.com/products/rg-55g1),
  [Retro Handhelds review](https://retrohandhelds.gg/anbernic-rg55g1-review),
  [ROCKNIX device page](https://rocknix.org/devices/anbernic/rg55g1/).
- ROCKNIX device index:
  [rocknix.org/devices](https://rocknix.org/devices/).
- PortMaster launcher environment:
  [portmaster.games/packaging.html](https://portmaster.games/packaging.html).

## Support classes

Five classes cover every device in the table.

- **Class A, 4:3 640×480.** The largest cluster: RG351V, the RG353 line,
  every RG35XX model, the RG405 line, the RG40XX H and V, and the RG28XX.
  Panel sizes run from 2.8 to 4.0 inches.
- **Class B, 960×544 OLED.** RG503 and RG505. The panel matches the game
  canvas exactly.
- **Class C, wide 1080p class.** RG552 (1920×1152, 5:3), RG556, RG557, and
  RG 55G1 (1920×1080, 16:9).
- **Class D, square 720×720.** RG Cube and RG CubeXX.
- **Class E, 3:2.** RG351P and RG351M (480×320) and RG34XX (720×480).

### What the fixed canvas does on each class today

The letterbox scales the 960×544 canvas to fit and centers it
(task T032). Device-pixel results:

| Panel | Scaled scene | Bars | Board cell size |
| --- | --- | --- | --- |
| 640×480 | 640×363 | about 58 px top and bottom | 18 px |
| 960×544 | native, no bars | none | 27 px |
| 1920×1080 | 1906×1080 | 7 px left and right | about 54 px |
| 1920×1152 | 1920×1088 | 32 px top and bottom | 54 px |
| 720×720 | 720×408 | 156 px top and bottom | about 20 px |
| 720×480 | 720×408 | 36 px top and bottom | about 20 px |
| 480×320 | 480×272 | 24 px top and bottom | about 13.5 px |

The 640×480 letterbox path already ships: the release pipeline captures the
PortMaster screenshot by resizing the window to 640×480, and
`tests/harness/t019.sh` verifies the centered bars
(`docs/SPEC.md` section 10.4). So every class works today. The strategy
question is readability: class A and class D lose the most.

## Recommendation per class

One strategy per class, chosen from the three the owner named: a fixed canvas
with letterbox scaling, a per-device canvas, or a rotated portrait layout.

### Class A, 4:3 640×480: rotated portrait layout

- **Rendering change.** Add one portrait canvas of 480×640 logical pixels
  and present it rotated 90 degrees, so the player holds the device
  sideways. A 30 px cell gives a 300×600 board with a 302×602 frame, 19 px
  margins top and bottom, and 90 px side columns for the heads-up display
  (HUD). Reuse the rotation and input remap that task T055 builds for the
  RG503. The portrait cell is about 3.3 mm on a 3.5-inch panel and 3.8 mm on
  a 4.0-inch panel, against 2.0 to 2.3 mm for today's letterboxed 18 px
  cell, and larger than the 3.1 mm native cell on the RG503. A per-device
  landscape canvas caps at a 23 px cell (460 px board plus frame in 480 px),
  so portrait is also the largest-cell landscape alternative. Keep the
  letterboxed landscape canvas as the fallback selection, at no extra cost.
- **Settings surface.** The T055 OPTIONS row that selects Landscape or
  Portrait, persisted in `settings.dat`. Default to Portrait when the
  detected panel belongs to this class. Detect the class at startup from
  the desktop display mode (`SDL_GetDesktopDisplayMode`); the launcher may
  override it for one device through a `$param_device` branch in
  `portmaster/Stackfall.sh`, because PortMaster's `control.txt` exports that
  variable. The in-game row stays authoritative for the player.
- **Test approach.** One new harness session that forces a 640×480 window
  and plays a round in portrait. Pin the rotated input map for the pad and
  the keyboard, symmetric letterbox bars within 2 px, the 30 px cell size,
  and a no-clipping scan of text and previews. Check that landscape
  sessions, pinned hashes, and timing stay byte-identical, because rotation
  must not touch the rules core. Keep the T019 letterbox screenshot scenario
  as the fallback proof.

### Class B, 960×544 OLED: fixed canvas with letterbox scaling

- **Rendering change.** None. The canvas equals the panel. T055 adds the
  optional portrait layout for players who want larger squares.
- **Settings surface.** The T055 Landscape/Portrait row, defaulting to
  Landscape on this class.
- **Test approach.** The existing 960×544 suites and the T055 portrait
  session. No new sessions.

### Class C, wide 1080p class: fixed canvas with letterbox scaling

- **Rendering change.** None. The scale factor is about 1.99 at 1920×1080
  (7 px side bars) and 2.0 at 1920×1152 (32 px top and bottom bars). The
  5.5-inch panels have about 400 pixels per inch, so the upscaled HUD stays
  sharp. A per-device canvas here would add a third layout for no
  readability gain.
- **Settings surface.** None. Letterboxing is automatic. The portrait row
  remains available if the owner wants it later.
- **Test approach.** Two short window sessions at 1920×1080 and 1920×1152
  that assert centered bars and unchanged observation streams. Desktop
  checks only; no device dependence.

### Class D, square 720×720: per-device canvas

- **Rendering change.** Add one square canvas of 720×720 logical pixels and
  draw the existing composition on it without rotation. A 34 px cell gives a
  340×680 board with a 682 px frame, 19 px top and bottom margins, and 189 px
  side columns. A square panel is wide enough for the board plus both HUD
  columns, so the player does not hold the device sideways. The fixed canvas
  letterboxes to 720×408 here and wastes about 43 percent of the panel
  height, so this class gains the most from its own canvas. The T037
  composition carries over: statistics left, Next previews right, board
  centered.
- **Settings surface.** None beyond detection. Select the canvas from the
  desktop display mode at startup. The T055 orientation row does not apply,
  because no rotation is involved.
- **Test approach.** One harness session at a 720×720 window. Pin the board
  geometry, the 34 px cell, the no-clipping scan, and unchanged rules hashes
  against the reference stream.

### Class E, 3:2 480×320 and 720×480: fixed canvas with letterbox scaling

- **Rendering change.** None. The bars are small: 24 px on 480×320 and
  36 px on 720×480, against 58 px on class A. The cell size, 13.5 to 20 px,
  matches what class A ships today. These are small or legacy panels; a
  dedicated canvas is not worth a fourth layout until a player asks.
- **Settings surface.** None. The T055 portrait row can stay available,
  because 720×480 rotated gives a 480×720 portrait panel that suits the
  class A portrait canvas.
- **Test approach.** Two short window sessions at 480×320 and 720×480 that
  assert centered bars. Revisit only if a readability review fails.

## Suggested order of work

1. Finish T055 on the RG503. Its rotation, input remap, and OPTIONS row are
   the pieces class A reuses.
2. Add the class A portrait canvas, then the class D square canvas. These
   two classes cover every ROCKNIX device the PortMaster package can reach
   besides the RG503 itself.
3. Classes C and E need no code. Their checks are two window sessions each.

## Open questions

1. **The RG503 premise.** The task request says the RG503 port scales the
   canvas to a 640×480 panel. Every source above, including the ROCKNIX
   page the project cites in `docs/SPEC.md`, places the RG503 panel at
   960×544, where the canvas is native. The owner should confirm which
   device shows the 640×480 panel. If a ROCKNIX install reports 640×480 on
   an RG503, that is a video-output setting, not the panel, and the fix
   belongs in the device configuration.
2. **Refresh rates.** Anbernic does not publish them. One social post
   claims 110 Hz for the RG34XX; no manufacturer or review source confirms
   it, so the table says "not stated". The frame limiter makes the game
   safe at any rate.
3. **RG Arc and RG34XX SP.** Both appear in the ROCKNIX device list. This
   research did not verify their panels. Reviews report the RG Arc shares
   the 3.5-inch 640×480 panel of the RG353. The RG34XX SP is new and needs
   its own check before support claims cover it.
4. **Physical validation.** Panel research cannot replace device tests.
   Each class the owner approves needs one device round like T018, on
   hardware that class covers.

## Follow-up work

Follow-up tasks come from the owner's choice of strategy. This note
recommends: portrait for class A, fixed canvas for classes B, C, and E, and
a per-device square canvas for class D.
