#!/usr/bin/env python3
"""Check the acceptance criteria of the accepted design-review findings.

Usage:
  check_fixes.py --packet PACKET_DIR [--json OUT.json]

Every check reads the packet images with the Python Imaging Library and
decides from pixels alone, so a fixed frame proves its own fix
(docs/DESIGN_REVIEW.md, T028). The checks encode the acceptance criteria
of the findings accepted in T027 and the approved composition of T037:

  focused-hud-stats-left   the statistics group (HOLD, score, lines,
                           level) is the only text of the left column -
                           seven label/value rows at the 14 px glyph
                           height - with no permanent control legend
                           below it, and the quiet gap between the
                           column and the board frame holds nothing
  focused-hud-next-right   the right column carries the NEXT heading and
                           the five preview pieces alone - no score text -
                           with its gap to the board frame quiet as well
  gameover-panel-nests     the game-over panel's solid face stays inside
                           the gap between the two columns, so neither
                           column holds a panel pixel and the dimmed
                           statistics and previews read whole beside it
                           (task T043)
  ghost-outline-contrast   the ghost outline holds at least a 3:1
                           luminance contrast against the board well
  letterbox-scene-centered the letterboxed frame splits its slack bars
                           evenly, within two pixels, and both bars
                           exist

The superseded control-hint-glyph-scale criterion (T030) measured the
permanent gameplay legend that task T037 removed; the two focused-hud
checks above replace it. The hold dimming is reported for information;
T027 classed the LOCKED label as optional cosmetic work, which does not
gate T028.
"""

import argparse
import json
import math
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    print("check_fixes: the Python Imaging Library is required",
          file=sys.stderr)
    raise

# Canvas geometry from include/stackfall/renderer_sdl.hpp. The board is 27 px
# cells from (345, 2) to (614, 541) with its frame at x=344..615 and
# y=1..542, so every region below is stated in those coordinates. The focused
# HUD columns of task T037 sit at x=192..319 (statistics) and x=640..767
# (previews), each 24 px of quiet gap from the frame.
BOARD_ORIGIN = (345, 2)
BOARD_CELL = 27
FRAME_LEFT = 344
FRAME_RIGHT = 615
HUD_GAP = 24
LEFT_COLUMN = (192, 319)
RIGHT_COLUMN = (640, 767)
# The statistics group ends at the LEVEL value (y = 228 + 14); everything
# below it in the column is the space the removed control legend occupied.
STATS_BOTTOM_Y = 250
# The board rows of the five preview slots (nextSlotBox of the renderer).
FIRST_SLOT_Y = (35, 90)
WELL = (10, 12, 19)  # kBoardWell
TEXT = (232, 236, 246)        # kText
TEXT_DIM = (138, 148, 172)    # kTextDim
BACKGROUND = (16, 18, 26)     # kBackground
PANEL_FILL = (40, 46, 62)     # kPanelFill, mixToward(BACKGROUND, kFrame, 2, 5)
# The S ghost of seed 31337 at board columns 3..6 and rows 18..19, in canvas
# pixels: x = 345 + 3*27 .. 345 + 7*27 and y = 2 + 18*27 .. 2 + 20*27.
GHOST_REGION = (BOARD_ORIGIN[0] + 3 * BOARD_CELL,
                BOARD_ORIGIN[1] + 18 * BOARD_CELL,
                BOARD_ORIGIN[0] + 7 * BOARD_CELL,
                BOARD_ORIGIN[1] + 20 * BOARD_CELL)


def luminance(color):
    def channel(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    return (0.2126 * channel(color[0]) + 0.7152 * channel(color[1]) +
            0.0722 * channel(color[2]))


def contrast(a, b):
    first, second = sorted((luminance(a), luminance(b)), reverse=True)
    return (first + 0.05) / (second + 0.05)


def text_rows(image, box):
    """Rows in `box` holding a bright or dim text pixel."""
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0, min(y1, image.height)):
        for x in range(x0, min(x1, image.width)):
            if image.getpixel((x, y)) in (TEXT, TEXT_DIM):
                rows.append(y)
                break
    return rows


def run_lengths(values):
    """Contiguous run lengths of a sorted row list."""
    runs = []
    if not values:
        return runs
    start = previous = values[0]
    for value in values[1:]:
        if value == previous + 1:
            previous = value
            continue
        runs.append(previous - start + 1)
        start = previous = value
    runs.append(previous - start + 1)
    return runs


def window_box(image, box):
    """Map a canvas box into a window that letterboxes the canvas.

    The renderer scales the 960x544 canvas by the smaller axis ratio and
    centers the result, so the black bars stay outside a mapped box: the
    top-left corner rounds up and the bottom-right corner rounds down.
    On the native 960x544 window the mapping is the identity.
    """
    ratio = min(image.width / 960.0, image.height / 544.0)
    offset_x = (image.width - 960 * ratio) / 2.0
    offset_y = (image.height - 544 * ratio) / 2.0
    x0, y0, x1, y1 = box
    return (math.ceil(offset_x + x0 * ratio), math.ceil(offset_y + y0 * ratio),
            int(offset_x + x1 * ratio), int(offset_y + y1 * ratio))


def band_is_background(image, box):
    """True when every pixel of `box` is the canvas background."""
    x0, y0, x1, y1 = box
    for y in range(y0, min(y1, image.height)):
        for x in range(x0, min(x1, image.width)):
            if image.getpixel((x, y)) != BACKGROUND:
                return False
    return True


def count_not_background(image, box):
    x0, y0, x1, y1 = box
    count = 0
    for y in range(y0, min(y1, image.height)):
        for x in range(x0, min(x1, image.width)):
            if image.getpixel((x, y)) != BACKGROUND:
                count += 1
    return count


def count_color(image, box, color):
    x0, y0, x1, y1 = box
    count = 0
    for y in range(y0, min(y1, image.height)):
        for x in range(x0, min(x1, image.width)):
            if image.getpixel((x, y)) == color:
                count += 1
    return count


def check_stats_left(image, scale_with_window=True, required=True):
    """The statistics column of the focused HUD, from pixels.

    The letterboxed frame scales the whole canvas, so window_box shrinks
    and re-centers its boxes before the measurement; the reported heights
    are then logical pixels again.
    """
    ratio = min(image.width / 960.0, image.height / 544.0) \
        if scale_with_window else 1.0
    name = "focused-hud-stats-left"
    column = window_box(image, (LEFT_COLUMN[0], 0, LEFT_COLUMN[1] + 1, 544))
    rows = text_rows(image, column)
    runs = run_lengths(sorted(set(rows)))
    logical = [round(run / ratio) if ratio else run for run in runs]
    gaps = {
        "left_gap_quiet": band_is_background(
            image, window_box(image, (LEFT_COLUMN[1] + 1, 0,
                                      FRAME_LEFT, 544))),
        "outer_left_quiet": band_is_background(
            image, window_box(image, (0, 0, LEFT_COLUMN[0], 544))),
        "no_legend_below_stats": not text_rows(
            image, window_box(image, (LEFT_COLUMN[0], STATS_BOTTOM_Y,
                                      LEFT_COLUMN[1] + 1, 544))),
    }
    passed = (logical == [14] * 7 and all(gaps.values()))
    result = {
        "name": name,
        "measured": {"text_row_runs_logical_px": logical,
                     "window_ratio": round(ratio, 3), **gaps},
        "criterion": "seven 14 px statistics rows (HOLD, SCORE, LINES, "
                     "LEVEL labels and values), nothing else in the "
                     "column, the gap to the board frame quiet, and no "
                     "permanent control legend below the group",
        "passed": passed if required else None,
    }
    return result


def check_next_right(image, scale_with_window=True, required=True):
    """The preview column of the focused HUD, from pixels."""
    ratio = min(image.width / 960.0, image.height / 544.0) \
        if scale_with_window else 1.0
    column = window_box(image, (RIGHT_COLUMN[0], 0, RIGHT_COLUMN[1] + 1, 544))
    rows = text_rows(image, column)
    runs = run_lengths(sorted(set(rows)))
    logical = [round(run / ratio) if ratio else run for run in runs]
    slot_pixels = []
    for index in range(5):
        top = FIRST_SLOT_Y[0] + index * 56
        box = window_box(image, (RIGHT_COLUMN[0], top, RIGHT_COLUMN[1] + 1,
                                 top + 56))
        slot_pixels.append(count_not_background(image, box))
    quiet = {
        "right_gap_quiet": band_is_background(
            image, window_box(image, (FRAME_RIGHT + 1, 0,
                                      RIGHT_COLUMN[0], 544))),
        "outer_right_quiet": band_is_background(
            image, window_box(image, (RIGHT_COLUMN[1] + 1, 0, 960, 544))),
    }
    passed = (logical == [14] and all(count > 50 for count in slot_pixels)
              and all(quiet.values()))
    return {
        "name": "focused-hud-next-right",
        "measured": {"text_row_runs_logical_px": logical,
                     "window_ratio": round(ratio, 3),
                     "slot_piece_pixels": slot_pixels, **quiet},
        "criterion": "the NEXT heading is the only text of the right column, "
                     "every one of the five slots holds its preview piece, "
                     "and the gap to the board frame stays quiet",
        "passed": passed if required else None,
    }


def check_gameover_nests(image):
    """The game-over panel nests between the HUD columns, from pixels.

    The overlay draws one solid panel face over the veiled round. Nesting
    (task T043, owner decision 2026-11-04) means that face stays inside the
    gap between the two columns: neither the statistics column nor the
    preview column holds a panel pixel, so the dimmed labels of the frozen
    round read whole beside the modal instead of fragments peeking out at
    the panel's edge, exactly as the pause overlay has nested since T037.
    """
    left = count_color(image, (LEFT_COLUMN[0], 0, LEFT_COLUMN[1] + 1, 544),
                       PANEL_FILL)
    right = count_color(image, (RIGHT_COLUMN[0], 0, RIGHT_COLUMN[1] + 1, 544),
                        PANEL_FILL)
    # The gap between the columns, frame-edge to frame-edge: the panel's
    # face covers most of it on the game-over frame.
    gap = count_color(image, (LEFT_COLUMN[1] + 1, 0, RIGHT_COLUMN[0], 544),
                      PANEL_FILL)
    passed = left == 0 and right == 0 and gap > 10000
    return {
        "name": "gameover-panel-nests",
        "measured": {"panel_pixels_left_column": left,
                     "panel_pixels_right_column": right,
                     "panel_pixels_between_columns": gap},
        "criterion": "no panel pixel in either HUD column, the panel face "
                     "inside the gap between them (x=320..639)",
        "passed": passed,
    }


def check_ghost_contrast(image):
    """Contrast of the ghost outline against the board well."""
    x0, y0, x1, y1 = GHOST_REGION
    counts = {}
    for y in range(y0, min(y1, image.height)):
        for x in range(x0, min(x1, image.width)):
            pixel = image.getpixel((x, y))
            if pixel in (WELL, (26, 30, 42)):  # well and grid lines
                continue
            counts[pixel] = counts.get(pixel, 0) + 1
    if not counts:
        return {"name": "ghost-outline-contrast", "passed": False,
                "measured": {"error": "no ghost pixels found in the region"},
                "criterion": "ghost outline contrast >= 3.0 against the well"}
    outline = max(counts, key=counts.get)
    ratio = contrast(outline, WELL)
    return {
        "name": "ghost-outline-contrast",
        "measured": {"outline_color": outline,
                     "outline_pixels": counts[outline],
                     "contrast_ratio": round(ratio, 2)},
        "criterion": "ghost outline contrast >= 3.0 against the well",
        "passed": ratio >= 3.0,
    }


def check_letterbox(image):
    """Top and bottom letterbox bars, from the black rows."""
    black = []
    for y in range(image.height):
        if all(image.getpixel((x, y)) == (0, 0, 0)
               for x in range(0, image.width, 4)):
            black.append(y)
    if not black:
        return {"name": "letterbox-scene-centered", "passed": False,
                "measured": {"error": "no letterbox bar found"},
                "criterion": "both bars exist and differ by at most 2 px"}
    top = black[0] + 1 if black[0] == 0 else 0
    # Leading run of black rows is the top bar; trailing run the bottom bar.
    upper = 0
    for y in black:
        if y == upper:
            upper += 1
        else:
            break
    lower = 0
    for y in reversed(black):
        if y == image.height - 1 - lower:
            lower += 1
        else:
            break
    return {
        "name": "letterbox-scene-centered",
        "measured": {"window": f"{image.width}x{image.height}",
                     "top_bar_px": upper, "bottom_bar_px": lower,
                     "first_black_row": black[0], "last_black_row": black[-1]},
        "criterion": "both bars exist and differ by at most 2 px",
        "passed": upper > 0 and lower > 0 and abs(upper - lower) <= 2,
    }


def check_hold_dim(image_locked, image_free):
    """Brightness ratio of the stashed hold piece against its full color."""
    def brightest(box):
        x0, y0, x1, y1 = box
        best = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                best = max(best, sum(image_locked.getpixel((x, y))))
        return best

    # The hold box (196, 30, 120, 60) and the first next slot (644, 35,
    # 120, 56) of the focused HUD columns.
    locked = brightest((200, 34, 312, 86))
    queue = brightest((648, 39, 760, 88))
    return {
        "name": "hold-lock-dim (informational)",
        "measured": {"locked_peak": locked, "preview_peak": queue,
                     "ratio": round(locked / queue, 2) if queue else None},
        "criterion": "informational: the dim signal stays below 0.8 of the "
                     "full color",
        "passed": None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packet", required=True, type=Path)
    parser.add_argument("--json", type=Path, default=None)
    args = parser.parse_args()

    images = args.packet / "images"
    results = []
    gameplay = Image.open(images / "gameplay-early.png").convert("RGB")
    locked = Image.open(images / "hold-locked.png").convert("RGB")
    letterbox = Image.open(images / "letterbox-640x480.png").convert("RGB")
    gameover = Image.open(images / "gameover.png").convert("RGB")
    results.append(check_stats_left(gameplay))
    results.append(check_next_right(gameplay))
    results.append(check_gameover_nests(gameover))
    for image, label in ((letterbox, "letterbox"),):
        stats = check_stats_left(image, required=False)
        stats["name"] = "focused-hud-stats-left (%s)" % label
        stats["criterion"] = ("informational: the same statistics column "
                              "measured at the letterbox window scale")
        results.append(stats)
        previews = check_next_right(image, required=False)
        previews["name"] = "focused-hud-next-right (%s)" % label
        previews["criterion"] = ("informational: the same preview column "
                                 "measured at the letterbox window scale")
        results.append(previews)
    results.append(check_ghost_contrast(gameplay))
    results.append(check_letterbox(letterbox))
    results.append(check_hold_dim(locked, gameplay))

    report = {"packet": str(args.packet), "checks": results}
    required = [result for result in results if result["passed"] is not None]
    failed = [result for result in required if not result["passed"]]

    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    for result in results:
        state = ("PASS" if result["passed"] else
                 "INFO" if result["passed"] is None else "FAIL")
        print(f"check_fixes: [{state}] {result['name']}")
        for key, value in result["measured"].items():
            print(f"    {key}: {value}")
        if not result["passed"] and result["passed"] is not None:
            print(f"    criterion: {result['criterion']}")
    print(f"check_fixes: {len(required) - len(failed)} of {len(required)} "
          f"required criteria pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
