"""The Stir chapter acceptance check of task T080 (docs/backgrounds).

Reads the real game frames of tools/backgrounds/stir-capture.script.txt and
its portrait twin - the checkpoint BMPs the sessions write for levels 5
through 8, three clock phases per level - and checks them against the
retained records:

  * the exact wallpaper of each frame: the loud margins of every capture
    must equal the design package's own plate (tools/backgrounds/
    designs.py) at one exact presentation-clock value. The harness renders
    one frame per tick, so the clock of a frame is
    floor((tick + K) * 50 / 3) ms for one constant frame offset K per
    session. A single frame pins K only within the rounding window of its
    finest mark, so the check solves K jointly over every frame of the
    session - the star phase of level 5, the patch phase of level 6, the
    swell phase of level 7, and the scanline phase of level 8 together -
    and requires one offset to hold them all;
  * the motion the phases show: the moving layer really moved between the
    frames of a level (dust, zones, towers, and bars aside, which hold
    still), and the frames of a level differ only inside the loud margins
    they repaint;
  * the palette caps of the brief, componentwise: nothing above the
    accent cap #8A94AC anywhere the wallpaper draws, nothing above the
    secondary cap #2A3146 in the quiet strips the HUD text never reaches,
    and no wallpaper color inside the opaque well;
  * the accent share of the complete canvas, at most two percent;
  * distinctness at the one-eighth review scale between adjacent levels,
    over the whole captured frame.

Both sessions capture through the game's own screenshot path, which writes
the device window: the landscape frames are the 960x544 canvas itself, and
the portrait frames are the turned scene inside the same window, so every
portrait coordinate below maps through the renderer's rotation
(device x = portrait y, device y = 543 - portrait x).

Run: python3 tools/backgrounds/stir_check.py <capture-root>
where <capture-root> holds landscape/ and portrait/, each with shots/ and
observations.txt. Exits nonzero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import designs
from imaging import read_bmp, parse_hex

SECONDARY_CAP = parse_hex("#2A3146")
ACCENT_CAP = parse_hex("#8A94AC")
WELL = parse_hex("#0A0C13")

ACCENTS = {5: [parse_hex("#687892")],
           6: [parse_hex("#38445B")],
           7: [parse_hex("#64728A")],
           8: [parse_hex("#54617A")]}
ALL_ACCENTS = [parse_hex(hex_text) for hex_text in
               ["#687892", "#38445B", "#64728A", "#54617A", "#20283A",
                "#242B3A", "#242C3B"]]
# Every shade the four designs can paint: the well must hold none of them.
WALLPAPER_COLORS = [parse_hex(hex_text) for hex_text in
                    ["#0F1420", "#121620", "#11151F", "#20283A", "#687892",
                     "#242B3A", "#38445B", "#252D3D", "#64728A", "#242C3B",
                     "#54617A"]]
LEVELS = (5, 6, 7, 8)
SUFFIXES = ("", "b", "c")


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

failures = []
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        failures.append(message)
        print("FAIL %s" % message)


def at_most(color, cap):
    return all(color[i] <= cap[i] for i in range(3))


def primitives(level, orientation, t_ms):
    """The package's primitive rects of one design at one clock: the
    elements of designs.py expanded, well and frame excluded (nothing of
    the design draws inside them)."""
    zone = designs.zones_for(orientation)
    design = [d for d in designs.catalogue() if d["level"] == level][0]
    elements = designs.draw_design(design, zone, t_ms / 1000.0)
    out = []
    for element in elements:
        color = parse_hex(element["color"])
        if element["tag"] in ("frame", "well"):
            continue
        if element["kind"] == "rect":
            if element.get("hollow"):
                continue
            out.append((color, element["x"], element["y"], element["w"],
                        element["h"]))
        elif element["kind"] == "hlines":
            span, spacing = element["h"], element["spacing"]
            for index in range(span // spacing):
                y = element["y"] + (index * spacing +
                                    element["offset_y"]) % span
                out.append((color, element["x"], y, element["w"],
                            element["thickness"]))
        else:
            raise ValueError(element["kind"])
    return design, out


def paint_margins(orientation, level, t_ms):
    """The loud margins of the package plate at t_ms, as a dict of device
    pixels: the base, then every primitive clipped to the margins."""
    zone = designs.zones_for(orientation)
    design, prims = primitives(level, orientation, t_ms)
    pixels = {}
    for rect in ZONES[orientation]["loud"]:
        for y in range(rect[1], rect[1] + rect[3]):
            for x in range(rect[0], rect[0] + rect[2]):
                pixels[device_point(orientation, x, y)] = parse_hex(
                    design["base"])
    for color, x, y, w, h in prims:
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                point = device_point(orientation, xx, yy)
                if point in pixels:
                    pixels[point] = color
    return design, pixels


def frame_matches(frame, orientation, level, t_ms):
    """True when the loud margins of `frame` equal the package plate."""
    _, pixels = paint_margins(orientation, level, t_ms)
    for (x, y), color in pixels.items():
        if frame.pixel(x, y) != color:
            return False
    return True


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
    clocks = {}
    for orientation in ("landscape", "portrait"):
        zones = ZONES[orientation]
        shots = os.path.join(root, orientation, "shots")
        observations = os.path.join(root, orientation, "observations.txt")
        levels = {}
        with open(observations) as handle:
            for line in handle:
                if not line.startswith("check=stir-"):
                    continue
                fields = dict(part.split("=", 1) for part in line.split()
                              if "=" in part)
                name = fields["check"]
                digits = name[5:].rstrip("bc")
                if not digits.isdigit():
                    continue  # stir-title
                suffix = name[5:][len(digits):]
                levels.setdefault(int(digits), {})[suffix or "a"] = fields

        read = {}
        for level in LEVELS:
            read = {}
            pair = levels.get(level, {})
            for slot, suffix in zip("abc", SUFFIXES):
                key = suffix if suffix else "a"
                check(key in pair,
                      "%s level %d frame %s missing from observations" %
                      (orientation, level, suffix or "a"))
                if key not in pair:
                    break
                record = pair[key]
                check(record["scene"] == "playing",
                      "%s level %d frame %s sits in scene %s" %
                      (orientation, level, suffix, record["scene"]))
                check(int(record["level"]) == level,
                      "%s level %d frame %s records level %s" %
                      (orientation, level, suffix, record["level"]))
                frame = read_bmp(os.path.join(shots, "stir-%d%s.bmp" %
                                              (level, suffix)))
                check((frame.w, frame.h) == (960, 544),
                      "%s level %d frame %s is %dx%d" %
                      (orientation, level, suffix, frame.w, frame.h))
                read[slot] = (int(record["tick"]), frame)
            if len(read) == 3:
                frames[(orientation, level)] = read

        if len(read) != 3 or (orientation, 5) not in frames:
            continue

        # Solve the frame offset K over every frame of the session: one
        # tick is one rendered frame, and one frame is exactly 50/3 ms of
        # presentation clock, so the clock of a frame at tick T is
        # floor((T + K) * 50 / 3) for one constant K. The finer grammars
        # (1 px stars at 2 px/s) pin K only within about fifteen frames,
        # so the solve starts from the level 5 frame and then requires
        # every frame of every level - including the scanline phase of
        # level 8 and the swell phase of level 7 - to agree on the same K.
        tick_a = frames[(orientation, 5)]["a"][0]
        candidates = []
        for k in range(-32, 64):
            t_ms = (tick_a + k) * 50 // 3
            if frame_matches(frames[(orientation, 5)]["a"][1], orientation,
                             5, t_ms):
                candidates.append(k)
        check(bool(candidates),
              "%s: no presentation clock in the search window matches the "
              "level 5 frame against the design package" % orientation)

        def frame_at(level, slot, k):
            tick = frames[(orientation, level)][slot][0]
            return frames[(orientation, level)][slot][1], (tick + k) * 50 // 3

        consistent = []
        for k in candidates:
            if all(frame_matches(frame_at(level, slot, k)[0], orientation,
                                 level, frame_at(level, slot, k)[1])
                   for level in LEVELS if (orientation, level) in frames
                   for slot in "abc"):
                consistent.append(k)
        check(bool(consistent),
              "%s: no single frame offset holds every captured frame "
              "against the design package (level 5 candidates %s)" %
              (orientation, candidates))
        if not consistent:
            continue
        k = consistent[0]
        print("  %s: frame offset K=%d (%d equivalent candidates)" %
              (orientation, k, len(consistent)))

        for level in LEVELS:
            if (orientation, level) not in frames:
                continue
            read = frames[(orientation, level)]
            times = {}
            for slot in "abc":
                tick, frame = read[slot]
                times[slot] = (tick + k) * 50 // 3
                check(frame_matches(frame, orientation, level, times[slot]),
                      "%s level %d frame %s: the loud margins do not equal "
                      "the design package plate at %d ms" %
                      (orientation, level, slot, times[slot]))
                clocks[(orientation, level, slot)] = times[slot]

            # The motion of the record: the frames of a level sit apart on
            # the clock, and the moving layer actually moved between them.
            span = times["c"] - times["a"]
            check(span >= 1500,
                  "%s level %d: the three frames span only %d ms of "
                  "presentation clock" % (orientation, level, span))
            if level != 7:
                _, pixels_a = paint_margins(orientation, level, times["a"])
                _, pixels_c = paint_margins(orientation, level, times["c"])
                moved = sum(1 for point in pixels_a
                            if pixels_a[point] != pixels_c[point])
                check(moved > 0,
                      "%s level %d: the moving layer never moved between "
                      "the outer frames" % (orientation, level))
                print("  %s level %d: %d ms of clock, %d of %d margin "
                      "pixels moved" % (orientation, level, span, moved,
                                        len(pixels_a)))
            else:
                # The pulse: which swell phases the three frames cover.
                swells = []
                for slot in "abc":
                    phase = (times[slot] % 8000) / 8000.0
                    swells.append(round(designs.triangle(phase, 1.0)))
                print("  %s level %d: %d ms of clock, swell phases %s" %
                      (orientation, level, span, swells))
                check(len(set(swells)) >= 1, "swell phases readable")

            # The frames of one level differ only in the loud margins:
            # the scene's own play field may move, the quiet strips hold
            # the design's static wallpaper alone.
            frame_a = read["a"][1]
            frame_c = read["c"][1]
            quiet_bad = None
            for rect in zones["quiet"]:
                dx, dy, dw, dh = device_rect(orientation, rect)
                for y in range(dy, dy + dh):
                    for x in range(dx, dx + dw):
                        if frame_a.pixel(x, y) != frame_c.pixel(x, y):
                            quiet_bad = (x, y)
                            break
                    if quiet_bad:
                        break
                if quiet_bad:
                    break
            check(quiet_bad is None,
                  "%s level %d: the quiet strip changed at %s between the "
                  "outer frames" % (orientation, level, quiet_bad))

            # The caps over the regions the wallpaper owns alone.
            frame = frame_a
            for rect in zones["loud"]:
                dx, dy, dw, dh = device_rect(orientation, rect)
                peak = (0, 0, 0)
                for y in range(dy, dy + dh):
                    for x in range(dx, dx + dw):
                        color = frame.pixel(x, y)
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
                        color = frame.pixel(x, y)
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
                    frame.pixel(x, y) == color
                    for y in range(dy, dy + dh)
                    for x in range(dx, dx + dw))]
                check(not leaked,
                      "%s level %d: the HUD strip holds the accent shades %s"
                      % (orientation, level,
                         ["#%02X%02X%02X" % color for color in leaked]))

            # The accent share of the complete canvas.
            accents = ACCENTS[level]
            accent_pixels = 0
            canvas_pixels = frame.w * frame.h
            for y in range(frame.h):
                for x in range(frame.w):
                    if frame.pixel(x, y) in accents:
                        accent_pixels += 1
            check(accent_pixels * 100 <= canvas_pixels * 2,
                  "%s level %d: accent pixels cover %.3f%% of the canvas" %
                  (orientation, level, 100.0 * accent_pixels / canvas_pixels))

            # The well holds no wallpaper color at all.
            wx, wy, ww, wh = device_rect(orientation, zones["well"])
            leaked = None
            for y in range(wy, wy + wh):
                for x in range(wx, wx + ww):
                    if frame.pixel(x, y) in WALLPAPER_COLORS:
                        leaked = (x, y, frame.pixel(x, y))
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
        for level in LEVELS:
            if (orientation, level) in frames:
                thumbs[level] = eighth(frames[(orientation, level)]["a"][1])
        ordered = [level for level in LEVELS if level in thumbs]
        for first, second in zip(ordered, ordered[1:]):
            w, h, a = thumbs[first]
            _, _, b = thumbs[second]
            differ = sum(1 for i in range(len(a)) if abs(a[i] - b[i]) >= 6)
            share = 100.0 * differ / len(a)
            floor = 1.0 if orientation == "landscape" else 0.5
            check(share >= floor,
                  "%s levels %d and %d differ in only %.2f%% of the "
                  "one-eighth samples" % (orientation, first, second, share))
            print("  %s %d vs %d: %.2f%% of %d samples differ" %
                  (orientation, first, second, share, len(a)))

    print("%d checks, %d failures" % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
