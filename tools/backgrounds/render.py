"""Render the background design package (docs/backgrounds, task T075).

One command builds every artifact of the package from `designs.py` and the
two captured HUD frames:

  plates      the raw design layer, both orientations, at t=0 s and at
              t=1 s, plus one manifest per plate
  mockups     the plates with the real HUD and board of the capture
              composited on top, one per level and orientation
  sheets      the layout mask for both orientations, the 20-design
              thumbnail sheet, and the name-and-swatch summary sheet
  records     one Markdown record per level, plus the index

Everything lands under `--out` (ignored build output) except the mockups
and the sheets, which are the reviewed deliverables and land under
`docs/backgrounds/images/`.
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import designs
from imaging import Canvas, parse_hex, read_bmp, write_png

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

# The plain canvas ground the game clears every scene with. Where the
# capture holds this color, the design shows through; everywhere else the
# capture is the real HUD and board.
SCENE_GROUND = (16, 18, 26)

# Every color the capture frame's own wallpaper paints. The mockup session
# plays at level 1, so its frame carries the Still ground design: the base
# #10121A and its corner ink #0C0E16. Those pixels belong to design 1's
# plate, never to the mockup being composited, so the overlay skips them
# exactly as it skips the flat ground of the years before the designs
# (task T079 landed the level 1 grammar and made this set necessary).
SCENE_WALLPAPER = ((16, 18, 26), (12, 14, 22))

CAPTURE_FRAME = "bg-mockup.bmp"


# ---------------------------------------------------------------------------
# The game's own 5x7 glyph table, read from the source
# ---------------------------------------------------------------------------


def load_glyphs():
    """The 95 printable glyphs of include/stackfall/font.hpp."""
    path = os.path.join(REPO, "include", "stackfall", "font.hpp")
    with open(path) as handle:
        text = handle.read()
    pattern = re.compile(
        r"\{\s*((?:0b[01]{5},\s*)+0b[01]{5})\s*\}\s*,\s*//\s*(\d+)")
    glyphs = {}
    for body, code in pattern.findall(text):
        rows = [int(row, 2) for row in re.findall(r"0b([01]{5})", body)]
        glyphs[int(code)] = rows
    if len(glyphs) != 95:
        raise ValueError("read %d glyphs from font.hpp, expected 95" %
                         len(glyphs))
    return glyphs


GLYPHS = None


def draw_text(canvas, x, y, scale, color, text):
    """One line of text, one filled rectangle per font pixel."""
    for index, char in enumerate(text):
        rows = GLYPHS.get(ord(char), GLYPHS[32])
        left = x + index * 6 * scale
        for row_index, row in enumerate(rows):
            for column in range(5):
                if row & (1 << (4 - column)):
                    canvas.rect(left + column * scale,
                                y + row_index * scale, scale, scale, color)
    return x + len(text) * 6 * scale


def text_width(scale, text):
    return len(text) * 6 * scale - scale


# ---------------------------------------------------------------------------
# Plates, manifests, and mockups
# ---------------------------------------------------------------------------


def render_plate(design, zones, t):
    elements = designs.draw_design(design, zones, t)
    plate = Canvas(zones.w, zones.h, parse_hex(design["base"]))
    designs.expand(elements, plate)
    return elements, plate


def manifest_of(design, zones, elements):
    return {
        "level": design["level"],
        "name": design["name"],
        "orientation": zones.name,
        "canvas": [zones.w, zones.h],
        "zones": {
            "well": list(zones.well), "frame": list(zones.frame),
            "quiet": [list(rect) for rect in zones.quiet],
            "guard": [list(rect) for rect in zones.guard],
            "loud": [list(rect) for rect in zones.loud],
        },
        "palette": {
            "base": design["base"],
            "secondary": list(design["secondary"]),
            "accent": list(design["accent"]),
            "roles": {color: element["role"] for element in elements
                      for color in ([element["color"]] if "color" in element
                                    else element["colors"])},
        },
        "motion": design["motion"],
        "transition": list(design["transition"]),
        "intensity": design["intensity"],
        "budget": design["budget"],
        "time": 0.0,
        "elements": elements,
    }


def read_overlay(orientation, captures):
    """The captured HUD and board in orientation space.

    The landscape capture is already in canvas space. The portrait capture
    arrives in device space: one portrait pixel (x, y) is device pixel
    (y, x), the fixed quarter turn of task T055.
    """
    device = read_bmp(os.path.join(captures, "capture-" + orientation,
                                   "shots", CAPTURE_FRAME))
    if orientation == "landscape":
        return device
    overlay = Canvas(544, 960, (0, 0, 0))
    for y in range(960):
        for x in range(544):
            color = device.pixel(y, x)
            base = (y * 544 + x) * 3
            overlay.buf[base:base + 3] = bytes(color)
    return overlay


def composite(plate, overlay):
    """The plate with every scene pixel of the capture on top: the well,
    the frame, the HUD text and panels, the previews - anything the round
    itself draws. The capture's own wallpaper stays out, so every mockup
    shows its own design alone."""
    out = bytearray(plate.buf)
    hidden = tuple(bytes(color) for color in SCENE_WALLPAPER)
    source = overlay.buf
    for i in range(0, len(out), 3):
        if source[i:i + 3] not in hidden:
            out[i:i + 3] = source[i:i + 3]
    canvas = Canvas(plate.w, plate.h, (0, 0, 0))
    canvas.buf = out
    return canvas


# ---------------------------------------------------------------------------
# Sheets
# ---------------------------------------------------------------------------

MASK_COLORS = [
    ("WELL (OPAQUE)", parse_hex("#0A0C13")),
    ("FRAME", parse_hex("#4E5874")),
    ("QUIET HUD ZONE", parse_hex("#1B2740")),
    ("QUIET GAP", parse_hex("#243352")),
    ("LOUD MARGIN (ACCENTS)", parse_hex("#3A5078")),
    ("BASE FIELD", parse_hex("#101728")),
]


def mask_sheet(zones, path):
    """One layout mask sheet: every zone in one flat color plus a legend."""
    label_height = 18 * len(MASK_COLORS) + 24
    sheet = Canvas(zones.w, zones.h + label_height, (8, 10, 16))
    # The zone layer
    layer = Canvas(zones.w, zones.h, MASK_COLORS[5][1])
    for rect in zones.loud:
        layer.rect(*rect, MASK_COLORS[4][1])
    for rect in zones.quiet:
        layer.rect(*rect, MASK_COLORS[2][1])
    for rect in zones.guard:
        layer.rect(*rect, MASK_COLORS[3][1])
    layer.rect(*zones.frame, MASK_COLORS[1][1])
    layer.rect(*zones.well, MASK_COLORS[0][1])
    sheet.buf[0:zones.w * zones.h * 3] = layer.buf
    # The legend
    title = "%s %dx%d LAYOUT MASK" % (zones.name.upper(), zones.w, zones.h)
    draw_text(sheet, 12, zones.h + 6, 2, (232, 236, 246), title)
    y = zones.h + 28
    for label, color in MASK_COLORS:
        sheet.rect(12, y + 2, 14, 8, color)
        draw_text(sheet, 34, y, 1, (138, 148, 172), label)
        y += 18
    write_png(path, sheet)


def thumbnail_sheet(designs_plates, path):
    """All 20 designs at one-eighth scale, one row per chapter."""
    tile_w, tile_h = 120, 68
    gap = 14
    label_h = 16
    header_h = 30
    rows = 5
    sheet = Canvas(4 * tile_w + 5 * gap, header_h + rows *
                   (tile_h + label_h + gap), (8, 10, 16))
    draw_text(sheet, gap, 8, 2, (232, 236, 246),
              "STACKFALL BACKGROUNDS - ONE EIGHTH SCALE")
    for index, (design, plate) in enumerate(designs_plates):
        chapter_row = (design["level"] - 1) // 4
        column = (design["level"] - 1) % 4
        x = gap + column * (tile_w + gap)
        y = header_h + chapter_row * (tile_h + label_h + gap)
        # Nearest-neighbor eighth scale of the landscape plate.
        for ty in range(tile_h):
            source_row = (ty * 8) * plate.w * 3
            target_row = ((y + ty) * sheet.w + x) * 3
            for tx in range(tile_w):
                source = source_row + (tx * 8) * 3
                sheet.buf[target_row + tx * 3:target_row + tx * 3 + 3] = \
                    plate.buf[source:source + 3]
        label = "%02d  %s" % (design["level"], design["name"].upper())
        draw_text(sheet, x, y + tile_h + 3, 1,
                  (138, 148, 172) if design["intensity"] < 7 else
                  (200, 210, 230), label)
    write_png(path, sheet)


def summary_sheet(catalogue_list, path):
    """All 20 names in level order with their base colors as swatches."""
    row_h = 30
    width = 560
    header_h = 40
    sheet = Canvas(width, header_h + 20 * row_h + 44, (8, 10, 16))
    draw_text(sheet, 16, 10, 2, (232, 236, 246),
              "STACKFALL BACKGROUNDS - SUMMARY")
    chapter_marks = {1: "STILL", 5: "STIR", 9: "BUILD", 13: "PRESSURE",
                     17: "PEAK"}
    for index, design in enumerate(catalogue_list):
        y = header_h + index * row_h
        if design["level"] in chapter_marks:
            draw_text(sheet, 16, y + 10, 1, (36, 204, 216),
                      chapter_marks[design["level"]])
        draw_text(sheet, 96, y + 4, 2, (232, 236, 246),
                  "%02d" % design["level"])
        draw_text(sheet, 140, y + 6, 2, (232, 236, 246),
                  design["name"].upper())
        sheet.rect(420, y + 4, 70, 16, parse_hex(design["base"]))
        sheet.rect(420, y + 4, 70, 1, (78, 88, 116))
        sheet.rect(420, y + 19, 70, 1, (78, 88, 116))
        sheet.rect(420, y + 4, 1, 16, (78, 88, 116))
        sheet.rect(489, y + 4, 1, 16, (78, 88, 116))
        draw_text(sheet, 506, y + 9, 1, (138, 148, 172), design["base"])
    write_png(path, sheet)


# ---------------------------------------------------------------------------
# The build
# ---------------------------------------------------------------------------


def level_tag(level):
    return "%02d" % level


def main():
    global GLYPHS
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=os.path.join(REPO, "build",
                                                      "backgrounds"))
    parser.add_argument("--images", default=os.path.join(
        REPO, "docs", "backgrounds", "images"))
    parser.add_argument("--captures", default=None,
                        help="the two capture directories (default: "
                             "<out>/capture-landscape and capture-portrait)")
    parser.add_argument("--skip-sheets", action="store_true")
    args = parser.parse_args()
    GLYPHS = load_glyphs()

    catalogue_list = designs.catalogue()
    os.makedirs(os.path.join(args.out, "plates"), exist_ok=True)
    os.makedirs(os.path.join(args.out, "phases"), exist_ok=True)
    os.makedirs(os.path.join(args.out, "manifests"), exist_ok=True)
    os.makedirs(args.images, exist_ok=True)

    overlays = {}
    for orientation in ("landscape", "portrait"):
        captures = args.captures or args.out
        overlays[orientation] = read_overlay(orientation, captures)

    plates = []
    for design in catalogue_list:
        tag = level_tag(design["level"])
        for orientation in ("landscape", "portrait"):
            zones = designs.zones_for(orientation)
            elements, plate = render_plate(design, zones, 0.0)
            name = "%s-%s" % (tag, orientation)
            write_png(os.path.join(args.out, "plates", name + ".png"), plate)
            with open(os.path.join(args.out, "manifests", name + ".json"),
                      "w") as handle:
                json.dump(manifest_of(design, zones, elements), handle,
                          indent=1, sort_keys=True)
            # The one-second phase plate, the motion evidence.
            _, phase = render_plate(design, zones, 1.0)
            write_png(os.path.join(args.out, "phases", name + "-t1.png"),
                      phase)
            mockup = composite(plate, overlays[orientation])
            write_png(os.path.join(args.images,
                                   "mockup-%s-%s.png" % (tag, orientation)),
                      mockup)
            if orientation == "landscape":
                plates.append((design, plate))
        print("rendered %s %s" % (tag, design["name"]))

    if args.skip_sheets:
        return
    mask_sheet(designs.landscape_zones(),
               os.path.join(args.images, "mask-landscape.png"))
    mask_sheet(designs.portrait_zones(),
               os.path.join(args.images, "mask-portrait.png"))
    thumbnail_sheet(plates, os.path.join(args.images, "thumbnails.png"))
    summary_sheet(catalogue_list, os.path.join(args.images, "summary.png"))
    print("sheets written to %s" % args.images)


if __name__ == "__main__":
    main()
