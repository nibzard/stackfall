"""The Still chapter acceptance check of task T079 (docs/backgrounds).

Reads the real game frames of tools/backgrounds/still-capture.script.txt and
its portrait twin - the checkpoint BMPs the sessions write for levels 1
through 4 - and checks them against the retained records:

  * the exact wallpaper pixels of each design, at the coordinates the
    records state, on both orientations;
  * the palette caps of the brief, componentwise: nothing above the accent
    cap #8A94AC anywhere the wallpaper draws, nothing above the secondary
    cap #2A3146 in the quiet strips the HUD text never reaches, and no
    wallpaper color inside the opaque well;
  * the accent share of the complete canvas, at most two percent;
  * the static proof: the loud margins of each level's two frames, taken
    about one second apart on the presentation clock, hold identical bytes
    while the pieces and the counters move on;
  * distinctness at the one-eighth review scale between adjacent levels,
    over the whole captured frame.

Both sessions capture through the game's own screenshot path, which writes
the device window: the landscape frames are the 960x544 canvas itself, and
the portrait frames are the turned scene inside the same window, so every
portrait coordinate below maps through the renderer's rotation
(device x = portrait y, device y = 543 - portrait x).

Run: python3 tools/backgrounds/still_check.py <capture-root>
where <capture-root> holds landscape/ and portrait/, each with shots/ and
observations.txt. Exits nonzero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from imaging import read_bmp, parse_hex

SECONDARY_CAP = parse_hex("#2A3146")
ACCENT_CAP = parse_hex("#8A94AC")
WELL = parse_hex("#0A0C13")

BASES = {1: "#10121A", 2: "#10131C", 3: "#0E121C", 4: "#11141D"}
# The accent shades each record names, which the coverage share accounts
# over the complete canvas whether or not the grammar draws them.
ACCENTS = {1: [parse_hex("#283044")],
           2: [parse_hex("#2A3146")],
           3: [parse_hex("#64718C")],
           4: [parse_hex("#2A3146")]}
ALL_ACCENTS = [parse_hex(hex_text) for hex_text in
                ["#283044", "#2A3146", "#64718C"]]
INK1 = parse_hex("#0C0E16")
RULE2 = parse_hex("#2A3146")
UNDER2 = parse_hex("#1C2433")
STAR_DIM = parse_hex("#1B2230")
STAR_BRIGHT = parse_hex("#64718C")
INK4 = parse_hex("#222A39")
# Every shade the four designs can paint: the well must hold none of them.
WALLPAPER_COLORS = [parse_hex(hex_text) for hex_text in
                    ["#10121A", "#10131C", "#0E121C", "#11141D", "#0C0E16",
                     "#2A3146", "#1C2433", "#1B2230", "#64718C", "#222A39"]]


def device_point(orientation, x, y):
    """One logical point of the orientation in capture space."""
    if orientation == "landscape":
        return (x, y)
    return (y, 543 - x)


def device_rect(orientation, rect):
    """One logical rectangle of the orientation in capture space."""
    if orientation == "landscape":
        return rect
    x, y, w, h = rect
    return (y, 543 - (x + w - 1), h, w)


# The zones of docs/backgrounds/index.md, in logical coordinates.
ZONES = {
    "landscape": {
        "size": (960, 544),
        "frame": (344, 1, 272, 542),
        "well": (345, 2, 270, 540),
        "quiet": [(320, 0, 24, 544), (616, 0, 24, 544)],
        "loud": [(0, 0, 192, 544), (768, 0, 192, 544)],
        # The portrait HUD strip draws its own previews and text, so no
        # brightness scan reads it: the wallpaper check there is the exact
        # accent shades of the chapter, which the scene never paints.
        "hud": None,
    },
    "portrait": {
        "size": (544, 960),
        "frame": (71, 119, 402, 802),
        "well": (72, 120, 400, 800),
        "quiet": [(59, 119, 12, 802), (473, 119, 12, 802)],
        "loud": [(0, 119, 59, 802), (485, 119, 59, 802), (0, 921, 544, 39)],
        "hud": (0, 0, 544, 119),
    },
}

# The pinned pixels of each design, restated from the level records in
# logical coordinates: what the frame must hold at that point. The quiet
# pins sit in the guard strips, where the HUD never draws.
PINS = {
    ("landscape", 1): [
        (5, 5, INK1), (159, 120, INK1), (160, 120, parse_hex(BASES[1])),
        (90, 100, INK1), (100, 300, parse_hex(BASES[1])),
        (900, 100, INK1), (900, 300, parse_hex(BASES[1])),
        (100, 500, INK1), (900, 500, INK1), (940, 300, parse_hex(BASES[1])),
        (330, 300, parse_hex(BASES[1]))],
    ("landscape", 2): [
        (5, 440, RULE2), (343, 440, RULE2), (616, 440, RULE2),
        (5, 439, parse_hex(BASES[2])), (5, 441, parse_hex(BASES[2])),
        (5, 449, UNDER2), (5, 458, UNDER2), (5, 460, parse_hex(BASES[2])),
        (955, 440, RULE2), (955, 448, UNDER2), (330, 440, RULE2)],
    ("landscape", 3): [
        (779, 371, STAR_DIM), (954, 395, STAR_DIM), (831, 86, STAR_BRIGHT),
        (2, 203, STAR_DIM), (147, 16, STAR_DIM), (126, 103, STAR_DIM),
        (930, 179, STAR_DIM), (878, 495, STAR_DIM), (118, 463, STAR_DIM),
        (768, 405, STAR_BRIGHT), (947, 377, STAR_BRIGHT),
        (45, 14, STAR_BRIGHT), (330, 300, parse_hex(BASES[3]))],
    ("landscape", 4): [
        (5, 505, INK4), (10, 504, parse_hex(BASES[4])), (5, 534, INK4),
        (5, 535, parse_hex(BASES[4])), (5, 277, INK4),
        (10, 276, parse_hex(BASES[4])), (300, 505, INK4), (343, 505, INK4),
        (0, 499, INK4), (6, 499, parse_hex(BASES[4])),
        (0, 498, parse_hex(BASES[4])), (186, 499, INK4),
        (192, 499, parse_hex(BASES[4])), (768, 499, INK4), (954, 499, INK4),
        (940, 300, parse_hex(BASES[4]))],
    ("portrait", 1): [
        (5, 5, INK1), (55, 131, INK1), (30, 500, parse_hex(BASES[1])),
        (70, 212, INK1), (30, 220, parse_hex(BASES[1])),
        (5, 900, INK1), (530, 500, parse_hex(BASES[1])),
        (500, 700, parse_hex(BASES[1])), (272, 60, parse_hex(BASES[1])),
        (30, 300, parse_hex(BASES[1]))],
    ("portrait", 2): [
        (5, 784, RULE2), (70, 784, RULE2), (473, 784, RULE2),
        (538, 784, RULE2), (5, 783, parse_hex(BASES[2])),
        (5, 785, parse_hex(BASES[2])), (5, 793, UNDER2), (5, 802, UNDER2),
        (5, 804, parse_hex(BASES[2]))],
    ("portrait", 3): [
        (2, 322, STAR_DIM), (34, 882, STAR_DIM), (513, 796, STAR_DIM),
        (147, 937, STAR_DIM), (522, 203, STAR_BRIGHT),
        (45, 133, STAR_BRIGHT), (369, 921, STAR_BRIGHT),
        (504, 617, STAR_BRIGHT), (272, 60, parse_hex(BASES[3])),
        (30, 500, parse_hex(BASES[3]))],
    ("portrait", 4): [
        (5, 898, INK4), (5, 950, INK4), (10, 845, parse_hex(BASES[4])),
        (5, 542, INK4), (10, 535, parse_hex(BASES[4])), (0, 840, INK4),
        (6, 840, parse_hex(BASES[4])), (53, 840, INK4), (538, 840, INK4),
        (541, 773, INK4), (485, 773, INK4), (272, 60, parse_hex(BASES[4])),
        (30, 500, parse_hex(BASES[4]))],
}

failures = []
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        failures.append(message)
        print("FAIL %s" % message)


def in_rect(x, y, rect):
    return rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]


def at_most(color, cap):
    return all(color[i] <= cap[i] for i in range(3))


def eighth(canvas):
    """The one-eighth review scale: 2x2 averages per 8x8 block."""
    w, h = canvas.w // 8, canvas.h // 8
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            base_x, base_y = x * 8, y * 8
            total = 0
            for dy, dx in ((0, 0), (0, 4), (4, 0), (4, 4)):
                i = ((base_y + dy) * canvas.w + base_x + dx) * 3
                total += (canvas.buf[i] + canvas.buf[i + 1] +
                          canvas.buf[i + 2]) // 3
            out[y * w + x] = total // 4
    return w, h, out


def main(root):
    frames = {}
    for orientation in ("landscape", "portrait"):
        zones = ZONES[orientation]
        shots = os.path.join(root, orientation, "shots")
        observations = os.path.join(root, orientation, "observations.txt")
        levels = {}
        with open(observations) as handle:
            for line in handle:
                if not line.startswith("check=still-"):
                    continue
                fields = dict(part.split("=", 1) for part in line.split()
                              if "=" in part)
                name = fields["check"]
                late = name.endswith("b")
                core = name[:-1] if late else name
                digits = core.split("-")[1]
                if not digits.isdigit():
                    continue  # still-title
                levels.setdefault(int(digits), {})["b" if late else "a"] = fields
        for level in (1, 2, 3, 4):
            pair = levels.get(level, {})
            check("a" in pair and "b" in pair,
                  "%s level %d: the session wrote both frames" %
                  (orientation, level))
            if "a" not in pair or "b" not in pair:
                continue
            check(int(pair["a"]["level"]) == level,
                  "%s level %d frame a records level %s" %
                  (orientation, level, pair["a"]["level"]))
            check(pair["a"]["settled"] == pair["b"]["settled"],
                  "%s level %d: the two frames hold different boards" %
                  (orientation, level))

            frame_a = read_bmp(os.path.join(shots, "still-%d.bmp" % level))
            frame_b = read_bmp(os.path.join(shots, "still-%db.bmp" % level))
            frames[(orientation, level)] = frame_a
            check((frame_a.w, frame_a.h) == (960, 544),
                  "%s level %d frame is %dx%d, expected the 960x544 window" %
                  (orientation, level, frame_a.w, frame_a.h))
            check((frame_b.w, frame_b.h) == (960, 544),
                  "%s level %d late frame is %dx%d" %
                  (orientation, level, frame_b.w, frame_b.h))
            tick_a = int(pair["a"]["tick"])
            tick_b = int(pair["b"]["tick"])
            check(tick_b > tick_a + 30,
                  "%s level %d frames are %d and %d ticks in, too close for "
                  "the static proof" % (orientation, level, tick_a, tick_b))

            # The pinned pixels of the record, through the rotation.
            for x, y, color in PINS[(orientation, level)]:
                dx, dy = device_point(orientation, x, y)
                got = frame_a.pixel(dx, dy)
                check(got == color,
                      "%s level %d pin (%d,%d) is #%02X%02X%02X, expected "
                      "#%02X%02X%02X" % (orientation, level, x, y, *got,
                                         *color))

            # The static proof: identical wallpaper bytes in the loud
            # margins - which carry no HUD text - later on the clock, while
            # the round itself moved on.
            for index, rect in enumerate(zones["loud"]):
                dx, dy, dw, dh = device_rect(orientation, rect)
                same = True
                first = None
                for y in range(dy, dy + dh):
                    for x in range(dx, dx + dw):
                        if frame_a.pixel(x, y) != frame_b.pixel(x, y):
                            same = False
                            first = (x, y, frame_a.pixel(x, y),
                                     frame_b.pixel(x, y))
                            break
                    if not same:
                        break
                check(same,
                      "%s level %d loud margin %d changed at %s: %s -> %s" %
                      (orientation, level, index, first and first[:2],
                       first and tuple(first[2]), first and tuple(first[3])))

            # The caps over the regions the wallpaper owns alone: the loud
            # margins, where the accent cap holds, and the guard strips,
            # where the secondary cap holds. The HUD panels and their text
            # are scene colors, not wallpaper, so the scan leaves them out.
            loud_dev = [device_rect(orientation, rect)
                        for rect in zones["loud"]]
            for rect in loud_dev:
                peak = (0, 0, 0)
                for y in range(rect[1], rect[1] + rect[3]):
                    for x in range(rect[0], rect[0] + rect[2]):
                        color = frame_a.pixel(x, y)
                        peak = tuple(max(a, b) for a, b in zip(peak, color))
                check(at_most(peak, ACCENT_CAP),
                      "%s level %d: loud margin %s peaks at #%02X%02X%02X, "
                      "above the accent cap" %
                      (orientation, level, rect[:2], *peak))
            for rect in zones["quiet"]:
                dx, dy, dw, dh = device_rect(orientation, rect)
                peak = (0, 0, 0)
                for y in range(dy, dy + dh):
                    for x in range(dx, dx + dw):
                        color = frame_a.pixel(x, y)
                        peak = tuple(max(a, b) for a, b in zip(peak, color))
                check(at_most(peak, SECONDARY_CAP),
                      "%s level %d: quiet strip %s peaks at #%02X%02X%02X, "
                      "above the secondary cap" %
                      (orientation, level, rect[:2], *peak))

            # The portrait HUD strip carries the scene's own previews and
            # text, so its wallpaper rule reads the exact accent shades of
            # the chapter: none of them may appear there.
            if zones["hud"] is not None:
                dx, dy, dw, dh = device_rect(orientation, zones["hud"])
                leaked = [color for color in ALL_ACCENTS if any(
                    frame_a.pixel(x, y) == color
                    for y in range(dy, dy + dh)
                    for x in range(dx, dx + dw))]
                check(not leaked,
                      "%s level %d: the HUD strip holds the accent shades %s"
                      % (orientation, level,
                         ["#%02X%02X%02X" % color for color in leaked]))

            # The accent share of the complete canvas: the pixels painted in
            # the design's accent shades, as the design package accounts
            # them. The scene's own colors never match a wallpaper shade.
            accents = ACCENTS[level]
            accent_pixels = 0
            canvas_pixels = frame_a.w * frame_a.h
            for y in range(frame_a.h):
                for x in range(frame_a.w):
                    if frame_a.pixel(x, y) in accents:
                        accent_pixels += 1
            check(accent_pixels * 100 <= canvas_pixels * 2,
                  "%s level %d: accent pixels cover %.3f%% of the canvas" %
                  (orientation, level, 100.0 * accent_pixels / canvas_pixels))

            # The well holds no wallpaper color at all: the opaque well and
            # the pieces it carries are the only things inside it.
            wx, wy, ww, wh = device_rect(orientation, zones["well"])
            leaked = None
            for y in range(wy, wy + wh):
                for x in range(wx, wx + ww):
                    if frame_a.pixel(x, y) in WALLPAPER_COLORS:
                        leaked = (x, y, frame_a.pixel(x, y))
                        break
                if leaked:
                    break
            check(leaked is None,
                  "%s level %d: the well holds wallpaper color %s at %s" %
                  (orientation, level, leaked and tuple(leaked[2]),
                   leaked and leaked[:2]))

    # Distinctness at the one-eighth review scale, over the whole frame.
    for orientation in ("landscape", "portrait"):
        thumbs = {}
        for level in (1, 2, 3, 4):
            thumbs[level] = eighth(frames[(orientation, level)])
        for level in (1, 2, 3):
            w, h, a = thumbs[level]
            _, _, b = thumbs[level + 1]
            differ = sum(1 for i in range(len(a)) if abs(a[i] - b[i]) >= 6)
            share = 100.0 * differ / len(a)
            floor = 1.0 if orientation == "landscape" else 0.5
            check(share >= floor,
                  "%s levels %d and %d differ in only %.2f%% of the "
                  "one-eighth samples" % (orientation, level, level + 1,
                                          share))
            print("  %s %d vs %d: %.2f%% of %d samples differ" %
                  (orientation, level, level + 1, share, len(a)))

    print("%d checks, %d failures" % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
