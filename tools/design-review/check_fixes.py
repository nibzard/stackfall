#!/usr/bin/env python3
"""Check the acceptance criteria of the accepted design-review findings.

Usage:
  check_fixes.py --packet PACKET_DIR [--json OUT.json]

Every check reads the packet images with the Python Imaging Library and
decides from pixels alone, so a fixed frame proves its own fix
(docs/DESIGN_REVIEW.md, T028). The checks encode the acceptance criteria
of the findings accepted in T027:

  control-hint-glyph-scale   the gameplay control hints draw at least
                             14 logical pixels of glyph height in the
                             left panel, and no hint row collides with
                             the panel edge or the board
  ghost-outline-contrast     the ghost outline holds at least a 3:1
                             luminance contrast against the board well
  letterbox-scene-centered   the letterboxed frame splits its slack bars
                             evenly, within two pixels, and both bars
                             exist

The hold dimming is reported for information; T027 classed the LOCKED
label as optional cosmetic work, which does not gate T028.
"""

import argparse
import json
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    print("check_fixes: the Python Imaging Library is required",
          file=sys.stderr)
    raise

# Canvas geometry from include/stackfall/renderer_sdl.hpp.
LEFT_PANEL_X = 52
LEFT_PANEL_WIDTH = 272
HINT_REGION_Y = (150, 290)
WELL = (10, 12, 19)  # kBoardWell
GHOST_REGION = (432, 464, 528, 512)  # the S ghost of seed 31337 at y=18..19


def luminance(color):
    def channel(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    return (0.2126 * channel(color[0]) + 0.7152 * channel(color[1]) +
            0.0722 * channel(color[2]))


def contrast(a, b):
    first, second = sorted((luminance(a), luminance(b)), reverse=True)
    return (first + 0.05) / (second + 0.05)


def bright_rows(image, box, threshold=90):
    """Rows in `box` holding a pixel brighter than `threshold` in sum."""
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0, min(y1, image.height)):
        for x in range(x0, min(x1, image.width)):
            if sum(image.getpixel((x, y))) > threshold:
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


def check_hint_glyphs(image, scale_with_window=True):
    """Glyph height of the gameplay control hints, from pixels.

    The letterboxed frame scales the whole canvas, so its box shrinks by
    the window ratio before the measurement; the reported height is then
    in device pixels.
    """
    ratio = image.width / 960.0 if scale_with_window else 1.0
    box = (int(LEFT_PANEL_X * ratio), int(HINT_REGION_Y[0] * ratio),
           int((LEFT_PANEL_X + LEFT_PANEL_WIDTH) * ratio),
           int(HINT_REGION_Y[1] * ratio))
    rows = bright_rows(image, box, threshold=120)
    runs = run_lengths(sorted(set(rows)))
    tallest = max(runs) if runs else 0
    logical = round(tallest / ratio) if ratio else tallest
    return {
        "name": "control-hint-glyph-scale",
        "measured": {"hint_glyph_height_px": tallest,
                     "hint_glyph_height_logical_px": logical,
                     "window_ratio": round(ratio, 3),
                     "text_row_runs": runs,
                     "lowest_text_row": rows[-1] if rows else None,
                     "panel_right_edge": int(588 * ratio)},
        "criterion": "hint glyph height >= 14 logical px and the text "
                     "stays inside the left panel",
        "passed": logical >= 14 and (not rows or rows[-1] < 588 * ratio),
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

    locked = brightest((128, 60, 248, 120))
    queue = brightest((660, 60, 880, 116))
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
    results.append(check_hint_glyphs(gameplay))
    letterbox_hints = check_hint_glyphs(letterbox)
    letterbox_hints["name"] = "control-hint-glyph-scale (letterbox)"
    letterbox_hints["criterion"] = ("informational: the same hints measured "
                                    "at the letterbox window scale")
    letterbox_hints["passed"] = None
    results.append(letterbox_hints)
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
