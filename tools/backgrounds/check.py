"""The mechanical acceptance check of the background package (task T075).

Every criterion of the design brief runs here, dependency-free, in the
pattern of the harness BMP readers of `tests/harness/check_harness.py`:
the plates and mockups are decoded pixel by pixel with `imaging.py`, the
manifests are re-expanded by the independent painter below - not by
`designs.expand` - so a plate and its manifest can only agree when the
design truly is axis-aligned rectangles.

Checks, one per criterion of the brief:

  plates    base field inside #0A0C13..#1C2130; secondary shapes at or
            below #2A3146; accents at or below #8A94AC and at most 2
            percent of the canvas; nothing brighter than #2A3146 inside
            the quiet zones and the gaps; the well pure #0A0C13; the
            manifest re-paints the plate exactly
  mockups   every well pixel is the capture, the frame ring intact at
            #4E5874, the real HUD present in the quiet zones
  motion    levels 1 to 4 byte-identical over time; from level 5 at most
            one moving layer, no speed above 10 px/s, pulse cycles of at
            least 4 s, seamless wrapping (an element over 4 px in its
            motion axis wraps over a span that includes its own extent),
            no fade longer than 1 s, moving plates really move
  counts    the headline element counts of the approved contract, by tag
  budget    at most 300 small elements or 40 band elements per design
  palette   no design color equals a piece color; base fields and
            secondary shades stay 2 or more gray steps from every piece
            gray and every piece gray sits above the base field, so the
            grayscale play mode keeps the pieces readable (accents may
            echo a piece hue, as the brief allows)
  grammar   one grammar per level: both orientations share palette,
            motion, and record fields
  arc       adjacent levels distinct at one-eighth scale; chapter means
            of mark density and element count and chapter peak speed all
            rise through the five chapters

Every cap comparison is componentwise: a color is within a cap when each
of its red, green, and blue channels is at or below the cap's channel.
The pulse-cycle floor follows the brief's rule 12, which bounds pulse
cycles only; drift and scroll are bounded by the 10 px/s speed ceiling
and by the seamless-wrap rule. Accent coverage is accounted over the
complete logical canvas: accent-role pixels painted in the final plate
over the full canvas area.

Run after `render.py`; exits nonzero on the first failing group.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import designs
from imaging import Canvas, gray_of, parse_hex, read_png

REPO = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))

BASE_FLOOR = parse_hex("#0A0C13")
BASE_CEIL = parse_hex("#1C2130")
SECONDARY_CAP = parse_hex("#2A3146")
ACCENT_CAP = parse_hex("#8A94AC")
FRAME_COLOR = parse_hex("#4E5874")
WELL_COLOR = parse_hex("#0A0C13")

BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
STEP_LIFT = (0, 1, 2, 1)

FAILURES = []
CHECKS = [0]


def check(ok, label):
    CHECKS[0] += 1
    if not ok:
        FAILURES.append(label)
        print("FAIL %s" % label)
    return ok


# ---------------------------------------------------------------------------
# The independent painter: manifest elements to pixels, its own code
# ---------------------------------------------------------------------------


def fill_rect(canvas, x, y, w, h, color):
    """One axis-aligned filled rectangle, the only shape this file paints."""
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(canvas.w, x + w), min(canvas.h, y + h)
    if x1 <= x0 or y1 <= y0:
        return
    run = bytes(color) * (x1 - x0)
    for row in range(y0, y1):
        start = (row * canvas.w + x0) * 3
        canvas.buf[start:start + (x1 - x0) * 3] = run


def paint(element, canvas):
    """Every element kind reduces to `fill_rect` calls."""
    kind = element["kind"]
    color = parse_hex(element["color"]) if "color" in element else None
    if kind == "rect":
        x, y, w, h = element["x"], element["y"], element["w"], element["h"]
        if element.get("hollow"):
            fill_rect(canvas, x, y, w, 1, color)
            fill_rect(canvas, x, y + h - 1, w, 1, color)
            fill_rect(canvas, x, y, 1, h, color)
            fill_rect(canvas, x + w - 1, y, 1, h, color)
        else:
            fill_rect(canvas, x, y, w, h, color)
    elif kind == "band":
        step_w, step_h = element["step_w"], element["step_h"]
        phase = element.get("phase", 0)
        end = element["x"] + element["w"]
        offset = element["offset"]
        first = (element["x"] - offset) // step_w - 1
        last = (end - offset) // step_w + 1
        for k in range(first, last + 1):
            left = (element["x"] if k == first else
                    max(element["x"], k * step_w + offset))
            right = (end if k == last else
                     min(end, (k + 1) * step_w + offset))
            if right <= left:
                continue
            lift = STEP_LIFT[(k + phase) % 4] * step_h
            fill_rect(canvas, left, element["y"] - lift + element["drop"],
                      right - left, element["h"] + lift, color)
    elif kind == "hlines":
        span = element["h"]
        for index in range(span // element["spacing"]):
            y = element["y"] + (index * element["spacing"] +
                                element["offset_y"]) % span
            fill_rect(canvas, element["x"], y, element["w"],
                      element["thickness"], color)
    elif kind == "checker":
        base = parse_hex(element["colors"][0])
        ink = parse_hex(element["colors"][1])
        cell = element["cell"]
        for row, density in enumerate(element["densities"]):
            y = element["y"] + row * cell
            for column in range(element["w"] // cell):
                x = element["x"] + column * cell
                index_x = (x + element["offset_x"]) // cell
                threshold = BAYER[(y // cell % 4) * 4 + (index_x % 4)]
                fill_rect(canvas, x, y, cell, cell,
                          ink if threshold < density else base)
    else:
        raise ValueError("unknown element kind %r" % kind)


def repaint(manifest):
    """One plate re-painted from its manifest alone."""
    canvas = Canvas(manifest["canvas"][0], manifest["canvas"][1],
                    parse_hex(manifest["palette"]["base"]))
    for element in manifest["elements"]:
        paint(element, canvas)
    return canvas


# ---------------------------------------------------------------------------
# Review-scale helpers
# ---------------------------------------------------------------------------


def eighth(canvas):
    """One one-eighth scale gray image, every sample a 2x2 average."""
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


def edge_density(canvas):
    """Mark density at review scale: gray transitions per 1000 samples."""
    w, h, gray = eighth(canvas)
    transitions = 0
    for y in range(h):
        for x in range(w):
            here = gray[y * w + x]
            if x + 1 < w and abs(here - gray[y * w + x + 1]) >= 6:
                transitions += 1
            if y + 1 < h and abs(here - gray[(y + 1) * w + x]) >= 6:
                transitions += 1
    return transitions * 1000.0 / (w * h)


def at_most(color, cap):
    """True when `color` is at or below `cap` componentwise: each of its
    red, green, and blue channels at or below the cap's channel. This is
    the documented comparison of the design contract."""
    return all(color[i] <= cap[i] for i in range(3))


# ---------------------------------------------------------------------------
# The suite
# ---------------------------------------------------------------------------


def zone_rects(manifest, key):
    return [tuple(rect) for rect in manifest["zones"][key]]


def check_plates(build):
    for level in range(1, 21):
        tag = "%02d" % level
        for orientation in ("landscape", "portrait"):
            name = "%s-%s" % (tag, orientation)
            with open(os.path.join(build, "manifests", name + ".json")) as fh:
                manifest = json.load(fh)
            plate = read_png(os.path.join(build, "plates", name + ".png"))
            label = "%s/plate" % name

            # The manifest re-paints the plate, byte for byte.
            fresh = repaint(manifest)
            check(fresh.key() == plate.key(),
                  "%s: manifest repaint diverges" % label)

            # Palette roles against the three caps of the brief.
            roles = manifest["palette"]["roles"]
            base = parse_hex(manifest["palette"]["base"])
            check(all(BASE_FLOOR[i] <= base[i] <= BASE_CEIL[i]
                      for i in range(3)),
                  "%s: base %s outside #0A0C13..#1C2130" %
                  (label, manifest["palette"]["base"]))
            for hexcolor, role in roles.items():
                color = parse_hex(hexcolor)
                if role == "secondary":
                    check(at_most(color, SECONDARY_CAP),
                          "%s: secondary %s above #2A3146" % (label, hexcolor))
                elif role == "accent":
                    check(at_most(color, ACCENT_CAP),
                          "%s: accent %s above #8A94AC" % (label, hexcolor))

            # Accents cover at most 2 percent of the canvas.
            accents = {bytes(parse_hex(hexcolor)) for hexcolor, role
                       in roles.items() if role == "accent"}
            area = plate.w * plate.h
            painted = 0
            for i in range(0, len(plate.buf), 3):
                if bytes(plate.buf[i:i + 3]) in accents:
                    painted += 1
            check(painted <= area * 0.02,
                  "%s: accent coverage %.2f%% above 2%%" %
                  (label, painted * 100.0 / area))

            # Quiet zones and gaps hold nothing brighter than secondary.
            quiet = zone_rects(manifest, "quiet") + zone_rects(manifest,
                                                               "guard")
            quiet_ok = True
            for x0, y0, w, h in quiet:
                for y in range(y0, y0 + h):
                    for x in range(x0, x0 + w):
                        i = (y * plate.w + x) * 3
                        color = (plate.buf[i], plate.buf[i + 1],
                                 plate.buf[i + 2])
                        if not at_most(color, SECONDARY_CAP):
                            quiet_ok = False
                            break
                    if not quiet_ok:
                        break
                if not quiet_ok:
                    break
            check(quiet_ok, "%s: quiet zone brighter than #2A3146" % label)

            # The well stays pure: no design shows inside the board.
            wx, wy, ww, wh = tuple(manifest["zones"]["well"])
            well_ok = True
            for y in range(wy, wy + wh):
                base_i = (y * plate.w + wx) * 3
                if bytes(plate.buf[base_i:base_i + ww * 3]) != \
                        bytes(WELL_COLOR) * ww:
                    well_ok = False
                    break
            check(well_ok, "%s: design inside the well" % label)


def check_mockups(build, images):
    from render import read_overlay, SCENE_GROUND
    for orientation in ("landscape", "portrait"):
        overlay = read_overlay(orientation, build)
        for level in range(1, 21):
            tag = "%02d" % level
            name = "%s-%s" % (tag, orientation)
            with open(os.path.join(build, "manifests", name + ".json")) as fh:
                manifest = json.load(fh)
            mockup = read_png(os.path.join(images,
                                           "mockup-%s.png" % name))
            label = "%s/mockup" % name

            # The board and its HUD own their zones: inside the well the
            # mockup is exactly the capture, no design pixel shows.
            wx, wy, ww, wh = tuple(manifest["zones"]["well"])
            well_ok = True
            for y in range(wy, wy + wh):
                for x in range(wx, wx + ww):
                    if mockup.pixel(x, y) != overlay.pixel(x, y):
                        well_ok = False
                        break
                if not well_ok:
                    break
            check(well_ok, "%s: design shows inside the board" % label)

            # The frame ring stays visible at its scene color.
            fx, fy, fw, fh = tuple(manifest["zones"]["frame"])
            ring = ([(x, fy) for x in range(fx, fx + fw)] +
                    [(x, fy + fh - 1) for x in range(fx, fx + fw)] +
                    [(fx, y) for y in range(fy, fy + fh)] +
                    [(fx + fw - 1, y) for y in range(fy, fy + fh)])
            check(all(mockup.pixel(x, y) == FRAME_COLOR for x, y in ring),
                  "%s: frame ring not intact" % label)

            # The real HUD sits in the quiet zones: the capture's bright
            # text pixels survive the composite there.
            quiet = zone_rects(manifest, "quiet")
            hud_ok = False
            for x0, y0, w, h in quiet:
                for y in range(y0, y0 + h):
                    for x in range(x0, x0 + w):
                        if overlay.pixel(x, y) != SCENE_GROUND and \
                                mockup.pixel(x, y) == overlay.pixel(x, y):
                            hud_ok = True
                            break
                    if hud_ok:
                        break
                if hud_ok:
                    break
            check(hud_ok, "%s: HUD missing from the quiet zone" % label)


def wrap_faults(element):
    """The seamless-wrap faults of one moving element.

    An element over 4 px in its motion axis must wrap over a span that
    includes its own extent, so it leaves its margin (or the canvas)
    fully before re-entering. Elements of 4 px or less may wrap at the
    margin span: their edge crossing is sub-perceptual. Texture scrolls
    (a rule set or a full-width staircase shifting its phase) are marked
    `texture` and skipped: their pattern is periodic by construction, so
    a phase wrap cannot jump. Elements without a wrap record do not
    wrap at all.
    """
    faults = []
    wrap = element.get("wrap")
    if not wrap or wrap.get("texture"):
        return faults
    vx, vy = element["vx"], element["vy"]
    axes = []
    if vx and wrap["axis"] in ("x", "both"):
        axes.append(("x", 0, element["w"]))
    if vy and wrap["axis"] in ("y", "both"):
        axes.append(("y", 1, element["h"]))
    for axis, index, extent in axes:
        if extent <= 4:
            continue
        home = wrap["home"][index]
        span = wrap["span"][index] if isinstance(wrap["span"], list) \
            else wrap["span"]
        if span < home + extent:
            faults.append(
                "%dx%d element wraps at span %d over margin %d on axis "
                "%s: it would re-enter while still visible"
                % (element["w"], element["h"], span, home, axis))
    return faults


def check_counts():
    """The headline counts of the approved contract, by element tag."""
    for design in designs.catalogue():
        claimed = design.get("counts") or {}
        if not claimed:
            continue
        for orientation in ("landscape", "portrait"):
            elements = designs.draw_design(
                design, designs.zones_for(orientation), 0.0)
            seen = {}
            for element in elements:
                tag = element.get("tag")
                if tag:
                    seen[tag] = seen.get(tag, 0) + 1
            for tag, count in sorted(claimed.items()):
                check(seen.get(tag, 0) == count,
                      "%02d/%s: %s count %d, contract says %d" %
                      (design["level"], orientation, tag, seen.get(tag, 0),
                       count))


def check_motion(build):
    catalogue = {d["level"]: d for d in designs.catalogue()}
    for level in range(1, 21):
        tag = "%02d" % level
        design = catalogue[level]
        motion = design["motion"]
        transition = design["transition"]

        # The cut-or-fade rule: a cut, or a fade of at most 1 s.
        if transition[0] == "fade":
            check(transition[1] <= 1.0,
                  "%s: fade longer than 1 s" % tag)
            check(transition[2] >= 10,
                  "%s: fade in fewer than 10 visible steps" % tag)

        if level <= 4:
            check(motion is None, "%s: still chapter carries motion" % tag)
            for orientation in ("landscape", "portrait"):
                name = "%s-%s" % (tag, orientation)
                plate = read_png(os.path.join(build, "plates", name +
                                              ".png"))
                phase = read_png(os.path.join(build, "phases", name +
                                              "-t1.png"))
                check(plate.key() == phase.key(),
                      "%s/%s: still design changes over 1 s" %
                      (tag, orientation))
            continue

        check(motion is not None, "%s: no motion record" % tag)
        if motion is None:
            continue
        check(motion["speed"] <= 10.0,
              "%s: speed above 10 px/s" % tag)
        # The brief's four-second floor bounds pulse cycles. Drift and
        # scroll carry a texture or wrap period instead, bounded by the
        # speed ceiling and the seamless-wrap rule below.
        if motion["kind"] == "pulse":
            check(motion["cycle"] >= 4.0,
                  "%s: pulse cycle under 4 s" % tag)

        # One moving layer: a drift or scroll is one velocity shared by
        # every moving element and matching the record; a pulse is one
        # cycle shared by every pulsing element. Nothing mixes the two.
        velocities = set()
        pulses = set()
        seamless_bad = []
        for orientation in ("landscape", "portrait"):
            name = "%s-%s" % (tag, orientation)
            with open(os.path.join(build, "manifests", name + ".json")) as fh:
                manifest = json.load(fh)
            for element in manifest["elements"]:
                if element["vx"] or element["vy"]:
                    velocities.add((round(element["vx"], 3),
                                    round(element["vy"], 3)))
                    seamless_bad.extend(wrap_faults(element))
                if element["pulse"]:
                    pulses.add(element["pulse"]["cycle"])
        for fault in seamless_bad:
            check(False, "%s: %s" % (tag, fault))
        if motion["kind"] in ("drift", "scroll"):
            check(not pulses, "%s: drift design carries a pulse" % tag)
            check(len(velocities) <= 1,
                  "%s: %d drift velocities %r" %
                  (tag, len(velocities), velocities))
            for vx, vy in velocities:
                speed = (vx * vx + vy * vy) ** 0.5
                check(abs(speed - motion["speed"]) <= 0.05,
                      "%s: elements move at %.1f px/s, record says %.1f" %
                      (tag, speed, motion["speed"]))
        else:
            check(not velocities, "%s: pulse design carries a drift" % tag)
            check(len(pulses) <= 1,
                  "%s: %d pulse cycles %r" % (tag, len(pulses), pulses))
            for cycle in pulses:
                check(abs(cycle - motion["cycle"]) <= 0.05,
                      "%s: elements pulse every %.1f s, record says %.1f" %
                      (tag, cycle, motion["cycle"]))

        # A moving design really moves. Slow textures step only every
        # few pixels - the 3 px/s dither over 4 px cells first changes at
        # 1.33 s - so a drift is compared over the time a 4 px
        # displacement takes and a pulse at its half-cycle peak.
        if motion["kind"] in ("drift", "scroll"):
            horizon = max(1.0, 4.0 / motion["speed"])
        else:
            horizon = motion["cycle"] / 2.0
        design_record = next(d for d in designs.catalogue()
                             if d["level"] == level)
        moved = False
        for orientation in ("landscape", "portrait"):
            zones = designs.zones_for(orientation)
            still = Canvas(zones.w, zones.h,
                           parse_hex(design_record["base"]))
            designs.expand(designs.draw_design(design_record, zones, 0.0),
                           still)
            later = Canvas(zones.w, zones.h,
                           parse_hex(design_record["base"]))
            designs.expand(designs.draw_design(design_record, zones,
                                               horizon), later)
            if still.key() != later.key():
                moved = True
        check(moved, "%s: no visible motion within %.1f s" % (tag, horizon))


def check_budget():
    for design in designs.catalogue():
        limit = 40 if design["budget"] == "bands" else 300
        counts = []
        for orientation in ("landscape", "portrait"):
            elements = designs.draw_design(design,
                                           designs.zones_for(orientation), 0.0)
            counts.append(len([e for e in elements
                               if e["role"] not in ("frame", "well")]))
        check(max(counts) <= limit,
              "%02d: %d elements over budget %d" %
              (design["level"], max(counts), limit))


def check_palette():
    """Grayscale separation, the way the brief states it.

    Rule 16 of the brief: luminance alone must separate the pieces from
    the base fields, and a small accent may echo a piece hue. So the
    2-gray-step separation holds for the base field and every secondary
    (large) shade, and every piece gray must sit above the base field;
    accents are exempt from the distance rule but may not equal any
    piece color exactly.
    """
    piece_grays = [gray_of(color) for color in designs.PIECE_COLORS]
    piece_hex = {designs.hex_of(color) for color in designs.PIECE_COLORS}
    for design in designs.catalogue():
        for hexcolor in [design["base"]] + design["secondary"] + \
                design["accent"]:
            check(hexcolor not in piece_hex,
                  "%02d: %s equals a piece color" %
                  (design["level"], hexcolor))
        base_gray = gray_of(parse_hex(design["base"]))
        for hexcolor in [design["base"]] + design["secondary"]:
            gray = gray_of(parse_hex(hexcolor))
            near = min((abs(gray - piece) for piece in piece_grays),
                       default=255)
            check(near >= 2,
                  "%02d: %s gray %d within 2 of a piece gray" %
                  (design["level"], hexcolor, gray))
        for piece_gray in piece_grays:
            check(piece_gray - base_gray >= 2,
                  "%02d: base %s gray %d not separated from piece gray %d"
                  % (design["level"], design["base"], base_gray,
                     piece_gray))


def check_grammar(build):
    for level in range(1, 21):
        tag = "%02d" % level
        manifests = {}
        for orientation in ("landscape", "portrait"):
            with open(os.path.join(build, "manifests", "%s-%s.json" %
                                   (tag, orientation))) as fh:
                manifests[orientation] = json.load(fh)
        land, port = manifests["landscape"], manifests["portrait"]
        same = (land["palette"]["base"] == port["palette"]["base"] and
                land["palette"]["secondary"] == port["palette"]["secondary"]
                and land["palette"]["accent"] == port["palette"]["accent"] and
                land["motion"] == port["motion"] and
                land["transition"] == port["transition"] and
                land["name"] == port["name"])
        check(same, "%s: orientations differ in palette or motion" % tag)


def check_arc(build):
    catalogue = designs.catalogue()
    density = {}
    counts = {}
    speeds = {}
    thumbs = {}
    for design in catalogue:
        level = design["level"]
        plate = read_png(os.path.join(build, "plates", "%02d-landscape.png"
                                      % level))
        density[level] = edge_density(plate)
        counts[level] = len([e for e in designs.draw_design(
            design, designs.zones_for("landscape"), 0.0)
            if e["role"] not in ("frame", "well")])
        speeds[level] = (design["motion"] or {}).get("speed", 0.0)
        thumbs[level] = eighth(plate)

    # Adjacent levels stay distinct at one-eighth scale: at least 1
    # percent of the samples differ by a visible step of 6 of 255 gray,
    # about 80 samples of the 8160-sample landscape tile.
    for level in range(1, 20):
        _, _, a = thumbs[level]
        _, _, b = thumbs[level + 1]
        assert len(a) == len(b)
        differ = sum(1 for i in range(len(a)) if abs(a[i] - b[i]) >= 6)
        share = differ * 100.0 / len(a)
        check(share >= 1.0,
              "%02d vs %02d: only %.1f%% distinct at one-eighth scale" %
              (level, level + 1, share))

    # The five chapters read as one rise in density, count, and motion.
    chapters = [("Still", 1, 4), ("Stir", 5, 8), ("Build", 9, 12),
                ("Pressure", 13, 16), ("Peak", 17, 20)]
    means = []
    for _, first, last in chapters:
        levels = range(first, last + 1)
        means.append((
            sum(density[l] for l in levels) / 4.0,
            sum(counts[l] for l in levels) / 4.0,
            max(speeds[l] for l in levels),
        ))
    for index in range(4):
        for metric, kind in (("density", 0), ("count", 1), ("motion", 2)):
            check(means[index + 1][kind] >= means[index][kind] - 1e-9,
                  "chapter %d to %d: %s falls (%.1f to %.1f)" %
                  (index + 1, index + 2, kind, means[index][kind],
                   means[index + 1][kind]))
    check(means[4][0] > means[0][0],
          "Peak chapter no denser than Still at review scale")
    return density, counts, speeds


def main():
    build = os.path.join(REPO, "build", "backgrounds")
    images = os.path.join(REPO, "docs", "backgrounds", "images")
    for name in ("plates", "phases", "manifests"):
        if not os.path.isdir(os.path.join(build, name)):
            print("missing %s under %s; run render.py first" %
                  (name, build))
            return 2

    print("plates and manifests")
    check_plates(build)
    print("mockups")
    check_mockups(build, images)
    print("motion")
    check_motion(build)
    print("counts")
    check_counts()
    print("budget")
    check_budget()
    print("palette")
    check_palette()
    print("grammar")
    check_grammar(build)
    print("arc")
    density, counts, speeds = check_arc(build)

    results = {
        "checks": CHECKS[0],
        "failures": FAILURES,
        "designs": [
            {"level": level, "density": round(density[level], 1),
             "count": counts[level], "speed": speeds[level]}
            for level in range(1, 21)],
    }
    with open(os.path.join(build, "check-results.json"), "w") as handle:
        json.dump(results, handle, indent=1, sort_keys=True)

    if FAILURES:
        print("%d of %d checks FAILED" % (len(FAILURES), CHECKS[0]))
        return 1
    print("all %d checks passed" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
