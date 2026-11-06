"""The integrated acceptance check of the full background catalog (T085).

Reads the captures of tools/backgrounds/t085.sh - the two full-catalog
ladder walks of catalog_gen.py (landscape and portrait), the grayscale and
Off comparison pairs, the orientation continuity probe, and the rapid ring
probe - and checks the complete twenty-design catalog against the retained
design package (tools/backgrounds/designs.py):

  * the forty exact-size orientation views: every settled frame's loud
    margins equal the design package's own plate at one exact presentation
    clock. The harness renders one frame per tick, so the clock of a frame
    is floor((tick + K) * 50 / 3) ms for one constant frame offset K per
    session; the check solves K jointly over the moving designs and then
    requires every settled frame, every motion phase, and every transition
    sample to agree on it;
  * the entry transition of every level: a fade design's three samples
    equal the runtime fade's own arithmetic - the outgoing grammar stepped
    toward the anchor color in the first half, the incoming grammar
    stepping away in the second, the same integer blend the renderer tints
    with - at one consistent fade start; a cut design's first sample is
    already the landed grammar. The sampled steps differ visibly and the
    settled frame that follows holds the plain grammar;
  * the motion contract on the running game: levels 1 through 4 hold their
    margins byte-identical between the two phases (static), every level
    from 5 moves its margins, and the guard gaps change only inside the
    design's own shades;
  * the palette caps of the brief on the real pixels, componentwise:
    nothing above the accent cap #8A94AC in the loud margins, nothing
    above the secondary cap #2A3146 in the quiet gaps, no design accent in
    the portrait HUD strip, accents at most two percent of the complete
    canvas, and no wallpaper shade inside the opaque well;
  * the level cap: eight lines past level 20 keep level 20 and design 20
    at the continuing clock;
  * the pause freeze: the two paused frames are byte-identical whole
    frames (the clock froze while the overlay held), and the resumed
    frames match the plate at the frozen clock plus exactly the resumed
    frames - no time jump;
  * the restart: the Auto round reset cuts design 1 back in, static;
  * the grayscale pairs: the same design at the same clock keeps the
    wallpaper pixels byte-identical while the pieces change;
  * Off: the legacy flat #10121A alone in every margin and gap;
  * the orientation continuity: one Fixed design runs across three turns
    of the ORIENTATION row at one unbroken clock;
  * the rapid ring probe: the landed Fixed pair draws on the next play
    frame;
  * the arc: adjacent levels stay distinct at the one-eighth review
    scale, chapter boundaries included, and the check writes the captured
    twenty-level thumbnail sheet of each orientation.

Both catalog sessions capture through the game's own screenshot path, so
the landscape frames are the 960x544 canvas itself and the portrait frames
are the turned scene inside the same window: every portrait coordinate
below maps through the renderer's rotation (device x = portrait y,
device y = 543 - portrait x).

Run: python3 tools/backgrounds/catalog_check.py ROOT
where ROOT holds catalog/landscape, catalog/portrait, pair-*, orientation,
and rapid-modes. Exits nonzero on any failure.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import designs
from imaging import Canvas, parse_hex, read_bmp, write_png
import render
from render import draw_text, load_glyphs

SECONDARY_CAP = parse_hex("#2A3146")
ACCENT_CAP = parse_hex("#8A94AC")
LEGACY_FLAT = parse_hex("#10121A")

LEVELS = list(range(1, 21))
FADE_STEPS = {2, 3, 4, 6, 7, 9, 10, 11, 13, 15, 16, 19, 20}
# The runtime transition table of src/backgrounds.cpp: (duration ms, steps).
FADE_SPEC = {2: (400, 12), 3: (500, 15), 4: (500, 15), 6: (500, 15),
             7: (500, 15), 9: (500, 15), 10: (500, 15), 11: (600, 18),
             13: (600, 18), 15: (600, 18), 16: (600, 18), 19: (500, 15),
             20: (600, 18)}
FRAME_MS_NUM = 50  # one tick is one frame, 50/3 ms of presentation clock
FRAME_MS_DEN = 3

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


def design_of(level):
    return [d for d in designs.catalogue() if d["level"] == level][0]


# ---------------------------------------------------------------------------
# The regions of one orientation. A region is the ordered list of its
# logical pixels; the same order extracts bytes from a captured device
# frame (device stride) and from a package plate (logical stride), so the
# two compare pixel for pixel.
# ---------------------------------------------------------------------------

class Orientation:
    def __init__(self, name):
        self.name = name
        self.zones = designs.zones_for(name)
        self.w = self.zones.w
        self.h = self.zones.h
        self.device_stride = 960 if name == "portrait" else self.w

    def device(self, x, y):
        if self.name == "landscape":
            return (x, y)
        return (y, 543 - x)

    def points(self, rects):
        """The logical points of the rectangles, row-major."""
        out = []
        for rect in rects:
            x, y, w, h = rect
            for yy in range(y, y + h):
                for xx in range(x, x + w):
                    out.append((xx, yy))
        return out

    def indices(self, points, stride, rotated=False):
        """Flat byte indices of the points on a buffer of `stride`. A
        rotated buffer (a captured device frame) takes each point through
        the device mapping; a package plate takes it as it lies."""
        if not rotated:
            return [(y * stride + x) * 3 for x, y in points]
        return [(dy * stride + dx) * 3
                for dx, dy in (self.device(x, y) for x, y in points)]


ORIENTATIONS = {name: Orientation(name) for name in ("landscape", "portrait")}
REGIONS = {}
for name, Ori in ORIENTATIONS.items():
    loud = Ori.points(Ori.zones.loud)
    guard = Ori.points(Ori.zones.guard)
    well = Ori.points([Ori.zones.well])
    hud = Ori.points(Ori.zones.quiet)
    REGIONS[name] = {
        "loud": loud,
        "guard": guard,
        "well": well,
        "hud": hud,
        # A captured frame is the device window; a plate is the logical
        # canvas. Both index lists walk the same points in the same order,
        # so the extracted bytes compare pixel for pixel.
        "frame": (Ori.indices(loud, Ori.device_stride, rotated=True),
                  Ori.indices(guard, Ori.device_stride, rotated=True),
                  Ori.indices(well, Ori.device_stride, rotated=True),
                  Ori.indices(hud, Ori.device_stride, rotated=True)),
        "plate": (Ori.indices(loud, Ori.w),
                  Ori.indices(guard, Ori.w),
                  Ori.indices(well, Ori.w)),
    }


def extract(buf, indices):
    return b"".join(buf[i:i + 3] for i in indices)


def frame_regions(frame, orientation):
    """The (loud, guard, well) region bytes of one captured device frame."""
    regions = REGIONS[orientation]["frame"]
    buf = frame.buf
    return (extract(buf, regions[0]), extract(buf, regions[1]),
            extract(buf, regions[2]))


# ---------------------------------------------------------------------------
# The plates of the design package, cached as extracted region bytes.
# ---------------------------------------------------------------------------

def mix_channel(a, b, parts, whole):
    # C++ integer division truncates toward zero; Python floor division
    # would round a darkening blend one step too far.
    product = (b - a) * parts
    quotient = product // whole if product >= 0 else -((-product) // whole)
    return a + quotient


def mix_toward(color, anchor, parts, whole):
    return tuple(mix_channel(color[i], anchor[i], parts, whole)
                 for i in range(3))


def mid_color(a, b):
    return tuple((a[i] + b[i]) // 2 for i in range(3))


def fade_state(from_level, to_level, t_ms, fade_start_ms):
    """The (showing level, tint) of the runtime fade at one clock: the
    outgoing grammar stepped toward the anchor in the first half, the
    incoming grammar stepping away in the second - exactly the arithmetic
    of Wallpaper::draw (src/backgrounds.cpp)."""
    duration, steps = FADE_SPEC[to_level]
    elapsed = t_ms - fade_start_ms
    if elapsed >= duration:
        return to_level, None  # the fade is over: the plain grammar
    step = (elapsed * steps) // duration
    if step >= steps:
        step = steps - 1
    anchor = mid_color(parse_hex(design_of(from_level)["base"]),
                       parse_hex(design_of(to_level)["base"]))
    if 2 * step < steps:
        return from_level, (anchor, 2 * step, steps)
    return to_level, (anchor, 2 * (steps - step), steps)


def plate_regions(cache, orientation, level, t_ms, fade=None):
    """The (loud, guard, well) region bytes of the package plate of one
    design at one clock, optionally tinted by a running fade step."""
    Ori = ORIENTATIONS[orientation]
    # The grammars move in whole pixels, so a clock and its rounding window
    # share one plate; 10/3 ms keys cover every grammar's finest motion.
    key = (orientation, level, (t_ms * 3) // 10, fade)
    hit = cache.get(key)
    if hit is not None:
        return hit
    design = design_of(level)
    elements = designs.draw_design(design, Ori.zones, t_ms / 1000.0)
    base = parse_hex(design["base"])
    if fade is not None:
        anchor, parts, whole = fade
        base = mix_toward(base, anchor, parts, whole)
    canvas = bytearray(Ori.w * Ori.h * 3)
    for i in range(3):
        canvas[i::3] = bytes([base[i]]) * (Ori.w * Ori.h)
    for element in elements:
        if element["tag"] in ("frame", "well"):
            continue
        rects = []
        if element["kind"] == "rect":
            if not element.get("hollow"):
                rects.append((element["x"], element["y"], element["w"],
                              element["h"]))
        elif element["kind"] == "band":
            rects = [rect for rect in designs.band_segments(element)]
        elif element["kind"] == "hlines":
            span, spacing = element["h"], element["spacing"]
            for index in range(span // spacing):
                y = element["y"] + (index * spacing +
                                    element["offset_y"]) % span
                rects.append((element["x"], y, element["w"],
                              element["thickness"]))
        else:
            raise ValueError(element["kind"])
        color = parse_hex(element["color"])
        if fade is not None:
            color = mix_toward(color, anchor, parts, whole)
        for x, y, w, h in rects:
            x0, y0 = max(x, 0), max(y, 0)
            x1, y1 = min(x + w, Ori.w), min(y + h, Ori.h)
            if x0 >= x1 or y0 >= y1:
                continue
            span = bytes(color) * (x1 - x0)
            for yy in range(y0, y1):
                start = (yy * Ori.w + x0) * 3
                canvas[start:start + (x1 - x0) * 3] = span
    plate_idx = REGIONS[orientation]["plate"]
    regions = (extract(canvas, plate_idx[0]), extract(canvas, plate_idx[1]),
               extract(canvas, plate_idx[2]))
    cache[key] = regions
    return regions


def shades_of(level):
    design = design_of(level)
    return ([parse_hex(design["base"])] +
            [parse_hex(c) for c in design["secondary"]] +
            [parse_hex(c) for c in design["accent"]])


def accents_of(level):
    design = design_of(level)
    return [parse_hex(c) for c in design["accent"]]


def clock_of(tick, k):
    return (tick + k) * FRAME_MS_NUM // FRAME_MS_DEN


# ---------------------------------------------------------------------------
# The observation records and frames of one session.
# ---------------------------------------------------------------------------

def read_records(path):
    records = {}
    with open(path, encoding="ascii") as stream:
        for line in stream:
            if not line.startswith("check="):
                continue
            fields = dict(part.split("=", 1) for part in line.split()
                          if "=" in part)
            records[fields["check"]] = fields
    return records


def read_frame(session, name):
    frame = read_bmp(os.path.join(session, "shots", "%s.bmp" % name))
    check((frame.w, frame.h) == (960, 544),
          "%s is %dx%d, not the exact 960x544 device frame" %
          (name, frame.w, frame.h))
    return frame


def match_at(cache, orientation, level, tick, k, loud, fade=None):
    plate = plate_regions(cache, orientation, level, clock_of(tick, k),
                          fade)
    return loud == plate[0]


# ---------------------------------------------------------------------------
# The catalog walk of one orientation.
# ---------------------------------------------------------------------------

def check_catalog(orientation, root, evidence):
    Ori = ORIENTATIONS[orientation]
    session = os.path.join(root, "catalog", orientation)
    records = read_records(os.path.join(session, "observations.txt"))
    cache = {}
    frames = {}
    wanted = (["cat-%02d%s" % (level, suffix) for level in LEVELS
               for suffix in ("", "b", "c", "-f0", "-f1", "-f2")] +
              ["cat-title", "cat-20cap", "cat-pzA", "cat-pzB", "cat-pzR1",
               "cat-pzR2", "cat-rst1", "cat-rst1b"])
    for name in wanted:
        if name not in records:
            continue
        frames[name] = (int(records[name]["tick"]), read_frame(session, name),
                        records[name])

    # Every level's settled frame and phase exist, in play, at the level.
    for level in LEVELS:
        for suffix in ("", "b"):
            name = "cat-%02d%s" % (level, suffix)
            if name not in frames:
                check(False, "%s: level %d frame %s missing" %
                      (orientation, level, suffix or "a"))
                continue
            record = frames[name][2]
            check(record["scene"] == "playing",
                  "%s: %s sits in scene %s" %
                  (orientation, name, record["scene"]))
            check(int(record["level"]) == level,
                  "%s: %s records level %s" %
                  (orientation, name, record["level"]))
    for level in range(2, 21):
        name = "cat-%02d-f0" % level
        check(name in frames, "%s: level %d transition sample missing" %
              (orientation, level))

    # Solve the frame offset K over the moving designs' settled frames.
    candidates = None
    for level in (20, 18, 14, 5):
        for suffix in ("", "b"):
            name = "cat-%02d%s" % (level, suffix)
            if name not in frames:
                continue
            if frames[name][2]["scene"] != "playing":
                continue  # the scene check above already failed this frame
            tick, frame, _ = frames[name]
            loud = frame_regions(frame, orientation)[0]
            here = {k for k in range(-32, 65)
                    if match_at(cache, orientation, level, tick, k, loud)}
            check(bool(here),
                  "%s: %s matches no design package plate in the search "
                  "window" % (orientation, name))
            if here and candidates is None:
                candidates = set(here)
            elif here:
                candidates &= here or candidates
    check(bool(candidates),
          "%s: the joint frame offset solve found no candidate" % orientation)
    if not candidates:
        return
    k = min(candidates)
    print("  %s: frame offset K=%d (%d equivalent candidates)" %
          (orientation, k, len(candidates)))
    evidence["frame_offset"][orientation] = sorted(candidates)

    # Every settled frame and phase matches the package plate at K.
    for level in LEVELS:
        for suffix in ("", "b", "c"):
            name = "cat-%02d%s" % (level, suffix)
            if name not in frames:
                continue
            if frames[name][2]["scene"] != "playing":
                continue  # the scene check above already failed this frame
            tick, frame, _ = frames[name]
            loud, guard, well = frame_regions(frame, orientation)
            check(match_at(cache, orientation, level, tick, k, loud),
                  "%s: %s margins do not equal the design package plate at "
                  "%d ms" % (orientation, name, clock_of(tick, k)))
            if not suffix:
                check_frame_rules(orientation, level, frame, loud, guard,
                                  well)

    # The static proof: levels 1..4 hold their margins between the phases.
    for level in (1, 2, 3, 4):
        loud_a = frame_regions(frames["cat-%02d" % level][1], orientation)[0]
        loud_b = frame_regions(frames["cat-%02db" % level][1], orientation)[0]
        check(loud_a == loud_b,
              "%s: level %d moved between the phases; the Still chapter "
              "must hold static" % (orientation, level))

    # The motion proof: levels 5..20 moved between the phases, and the
    # guard gaps changed only inside the design's shades. A pulse design
    # holds whole pixels for seconds at a time, so its window is the wide
    # phase c; a drift or scroll design moves inside the narrow phase b.
    for level in range(5, 21):
        wide = "cat-%02dc" % level in frames
        far = "cat-%02dc" % level if wide else "cat-%02db" % level
        frame_a = frames["cat-%02d" % level][1]
        frame_b = frames[far][1]
        loud_a, guard_a, _ = frame_regions(frame_a, orientation)
        loud_b, guard_b, _ = frame_regions(frame_b, orientation)
        moved = sum(1 for i in range(0, len(loud_a), 3)
                    if loud_a[i:i + 3] != loud_b[i:i + 3])
        check(moved > 0,
              "%s: level %d never moved across its motion window" %
              (orientation, level))
        shades = {bytes(c) for c in shades_of(level)}
        bad = next((i for i in range(0, len(guard_a), 3)
                    if guard_a[i:i + 3] != guard_b[i:i + 3] and
                    (guard_a[i:i + 3] not in shades or
                     guard_b[i:i + 3] not in shades)), None)
        check(bad is None,
              "%s: level %d guard gap changed outside the design shades" %
              (orientation, level))
        evidence["motion_px"]["%s-%d" % (orientation, level)] = moved

    # The entry transitions.
    for level in range(2, 21):
        f0 = frames.get("cat-%02d-f0" % level)
        settled = frames.get("cat-%02d" % level)
        if f0 is None or settled is None:
            continue
        tick0, frame0, _ = f0
        loud0 = frame_regions(frame0, orientation)[0]
        if level not in FADE_STEPS:
            check(match_at(cache, orientation, level, tick0, k, loud0),
                  "%s: level %d cut sample is not the landed grammar" %
                  (orientation, level))
            continue
        samples = [("f0", tick0, loud0)]
        for suffix, tag in (("-f1", "f1"), ("-f2", "f2")):
            name = "cat-%02d%s" % (level, suffix)
            if name in frames:
                tick, frame, _ = frames[name]
                samples.append((tag, tick,
                                frame_regions(frame, orientation)[0]))
        starts = None
        for back in range(1, 25):
            start = clock_of(tick0 - back, k)
            ok = True
            for _, tick, loud in samples:
                show, fade = fade_state(level - 1, level, clock_of(tick, k),
                                        start)
                if not match_at(cache, orientation, show, tick, k, loud,
                                fade):
                    ok = False
                    break
            if ok:
                starts = tick0 - back
                break
        check(starts is not None,
              "%s: level %d fade samples match no single fade start" %
              (orientation, level))
        if starts is None:
            continue
        lounds = [loud for _, _, loud in samples]
        for first, second in zip(lounds, lounds[1:]):
            check(first != second,
                  "%s: level %d fade holds one image across two samples" %
                  (orientation, level))
        tick_s, frame_s, _ = settled
        check(match_at(cache, orientation, level, tick_s, k,
                       frame_regions(frame_s, orientation)[0]),
              "%s: level %d settled frame is not the plain grammar" %
              (orientation, level))
        evidence["fades"]["%s-%d" % (orientation, level)] = starts

    # The level cap.
    cap = frames.get("cat-20cap")
    check(cap is not None, "%s: the level cap capture is missing" %
          orientation)
    if cap is not None:
        tick, frame, record = cap
        check(record["scene"] == "playing" and int(record["level"]) == 20,
              "%s: the cap frame records %s level %s" %
              (orientation, record["scene"], record["level"]))
        check(int(record["lines"]) >= 200,
              "%s: the cap frame holds only %s lines" %
              (orientation, record["lines"]))
        check(match_at(cache, orientation, 20, tick, k,
                       frame_regions(frame, orientation)[0]),
              "%s: the cap frame is not design 20 at the continuing clock" %
              orientation)

    # The pause freeze and the resume continuation.
    pz = [frames.get(name) for name in
          ("cat-pzA", "cat-pzB", "cat-pzR1", "cat-pzR2")]
    check(all(f is not None for f in pz),
          "%s: the pause probe captures are missing" % orientation)
    if all(f is not None for f in pz):
        check(bytes(pz[0][1].buf) == bytes(pz[1][1].buf),
              "%s: the paused frames differ; the clock or the game ran "
              "under the overlay" % orientation)
        tick_a = pz[0][0]
        tick_r1, frame_r1, _ = pz[2]
        tick_r2, frame_r2, _ = pz[3]
        loud1 = frame_regions(frame_r1, orientation)[0]
        loud2 = frame_regions(frame_r2, orientation)[0]
        solved = None
        for frozen_back in range(0, 7):
            frozen = clock_of(tick_a + frozen_back, k)
            for resume in range(0, 9):
                t1 = frozen + (tick_r1 - tick_a - resume) * \
                    FRAME_MS_NUM // FRAME_MS_DEN
                if not _matches_clock(cache, orientation, 20, t1, loud1):
                    continue
                t2 = frozen + (tick_r2 - tick_a - resume) * \
                    FRAME_MS_NUM // FRAME_MS_DEN
                if _matches_clock(cache, orientation, 20, t2, loud2):
                    solved = (frozen, resume, t1, t2)
                    break
            if solved:
                break
        check(solved is not None,
              "%s: the resumed frames match no frozen-clock continuation" %
              orientation)
        if solved:
            frozen, resume, t1, t2 = solved
            # Two independently floored clocks can differ from the floored
            # tick delta by a millisecond of rounding; only a real jump
            # moves further than that.
            span = (tick_r2 - tick_r1) * FRAME_MS_NUM / FRAME_MS_DEN
            check(abs((t2 - t1) - span) <= 2,
                  "%s: the resumed clock advanced %d ms over %d ticks" %
                  (orientation, t2 - t1, tick_r2 - tick_r1))
            banked = t1 - frozen - (tick_r1 - tick_a - resume) * \
                FRAME_MS_NUM // FRAME_MS_DEN
            check(abs(banked) <= 200,
                  "%s: the resume moved the clock %d ms beyond the frozen "
                  "continuation" % (orientation, banked))
            print("  %s: pause froze at %d ms, resumed %d frames later, "
                  "no jump" % (orientation, frozen, resume))
            evidence["pause"][orientation] = {
                "frozen_ms": frozen, "resume_frames": resume,
                "r1_ms": t1, "r2_ms": t2}

    # The restart: the Auto round reset cuts design 1 back in, static.
    rst1 = frames.get("cat-rst1")
    rst1b = frames.get("cat-rst1b")
    check(rst1 is not None and rst1b is not None,
          "%s: the restart captures are missing" % orientation)
    if rst1 is not None and rst1b is not None:
        record = rst1[2]
        check(record["scene"] == "playing" and int(record["level"]) == 1,
              "%s: the restart frame records %s level %s" %
              (orientation, record["scene"], record["level"]))
        loud = frame_regions(rst1[1], orientation)[0]
        plate = plate_regions(cache, orientation, 1, 0)
        check(loud == plate[0],
              "%s: the restart frame is not the design 1 grammar" %
              orientation)
        check(loud == frame_regions(rst1b[1], orientation)[0],
              "%s: the design 1 restart frame moved; Still holds static" %
              orientation)

    # The arc: adjacent levels differ at the one-eighth review scale.
    thumbs = {level: eighth(frames["cat-%02d" % level][1])
              for level in LEVELS if "cat-%02d" % level in frames}
    for first, second in zip(LEVELS, LEVELS[1:]):
        if first not in thumbs or second not in thumbs:
            continue
        _, _, a = thumbs[first]
        _, _, b = thumbs[second]
        differ = sum(1 for i in range(len(a)) if abs(a[i] - b[i]) >= 6)
        share = 100.0 * differ / len(a)
        floor = 1.0 if orientation == "landscape" else 0.5
        check(share >= floor,
              "%s: levels %d and %d differ in only %.2f%% of the one-eighth "
              "samples" % (orientation, first, second, share))
        evidence["distinct"]["%s-%d-%d" % (orientation, first, second)] = \
            round(share, 2)
    write_sheet(thumbs, root, orientation)


def _matches_clock(cache, orientation, level, t_ms, loud):
    return loud == plate_regions(cache, orientation, level, t_ms)[0]


def check_frame_rules(orientation, level, frame, loud, guard, well):
    """The palette caps, the accent share, and the well purity of one real
    frame, over the regions the wallpaper owns alone."""
    peak = (0, 0, 0)
    for i in range(0, len(loud), 3):
        color = (loud[i], loud[i + 1], loud[i + 2])
        if color > peak:
            peak = color
    check(at_most(peak, ACCENT_CAP),
          "%s: level %d loud margins peak at #%02X%02X%02X, above the "
          "accent cap" % ((orientation, level) + peak))
    peak = (0, 0, 0)
    for i in range(0, len(guard), 3):
        color = (guard[i], guard[i + 1], guard[i + 2])
        if color > peak:
            peak = color
    check(at_most(peak, SECONDARY_CAP),
          "%s: level %d guard gaps peak at #%02X%02X%02X, above the "
          "secondary cap" % ((orientation, level) + peak))
    accents = {bytes(c) for c in accents_of(level)}
    accent_pixels = sum(1 for i in range(0, len(frame.buf), 3)
                        if bytes(frame.buf[i:i + 3]) in accents)
    share = 100.0 * accent_pixels / (frame.w * frame.h)
    check(share <= 2.0,
          "%s: level %d accents cover %.3f%% of the canvas" %
          (orientation, level, share))
    shades = {bytes(c) for c in shades_of(level)}
    leaked = next((i for i in range(0, len(well), 3)
                   if bytes(well[i:i + 3]) in shades), None)
    check(leaked is None,
          "%s: level %d well holds a wallpaper shade" % (orientation, level))
    # The portrait HUD strip: none of the design's accent shades, with the
    # documented exception of #8A94AC, the scene's own dim text shade.
    if orientation == "portrait":
        accent_scan = {c for c in accents if c != bytes(ACCENT_CAP)}
        if accent_scan:
            indices = REGIONS["portrait"]["frame"][3]
            leaked = next((i for i in indices
                           if bytes(frame.buf[i:i + 3]) in accent_scan),
                          None)
            check(leaked is None,
                  "%s: level %d HUD strip holds an accent shade" %
                  (orientation, level))


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


def write_sheet(thumbs, root, orientation):
    """The captured twenty-level thumbnail sheet: one row of four
    one-eighth frames per chapter, every cell labeled by level and name."""
    tile_w, tile_h = 120, 68
    gap, label_h, header_h = 14, 16, 30
    sheet = Canvas(4 * tile_w + 5 * gap,
                   header_h + 5 * (tile_h + label_h + gap), (8, 10, 16))
    draw_text(sheet, gap, 8, 2, (232, 236, 246),
              "STACKFALL BACKGROUNDS %s - CAPTURED FRAMES - ONE EIGHTH "
              "SCALE" % orientation.upper())
    for level in LEVELS:
        if level not in thumbs:
            continue
        _, _, gray = thumbs[level]
        chapter_row = (level - 1) // 4
        column = (level - 1) % 4
        x = gap + column * (tile_w + gap)
        y = header_h + chapter_row * (tile_h + label_h + gap)
        for ty in range(tile_h):
            for tx in range(tile_w):
                value = bytes(3 * [gray[ty * tile_w + tx]])
                target = ((y + ty) * sheet.w + x + tx) * 3
                sheet.buf[target:target + 3] = value
        draw_text(sheet, x, y + tile_h + 2, 1, (138, 148, 172),
                  "%02d  %s" % (level, design_of(level)["name"].upper()))
    path = os.path.join(root, "captured-catalog-%s.png" % orientation)
    write_png(path, sheet)
    print("  %s: captured catalog sheet %s" % (orientation, path))


# ---------------------------------------------------------------------------
# The comparison sessions.
# ---------------------------------------------------------------------------

def check_pairs(root, evidence):
    orientation = "landscape"
    for design in (17, 20):
        normal = os.path.join(root, "pair-fixed%d" % design)
        gray = os.path.join(root, "pair-fixed%d-gray" % design)
        if not (os.path.isdir(normal) and os.path.isdir(gray)):
            check(False, "missing grayscale pair sessions of design %d" %
                  design)
            continue
        records_n = read_records(os.path.join(normal, "observations.txt"))
        records_g = read_records(os.path.join(gray, "observations.txt"))
        if "pair-play" not in records_n or "pair-play" not in records_g:
            check(False, "design %d pair sessions lack the play record" %
                  design)
            continue
        tick_n = int(records_n["pair-play"]["tick"])
        tick_g = int(records_g["pair-play"]["tick"])
        check(tick_n == tick_g,
              "design %d pair frames sit at different ticks %d/%d" %
              (design, tick_n, tick_g))
        frame_n = read_frame(normal, "pair-play")
        frame_g = read_frame(gray, "pair-play")
        loud_n, guard_n, well_n = frame_regions(frame_n, orientation)
        loud_g, guard_g, well_g = frame_regions(frame_g, orientation)
        check(loud_n == loud_g and guard_n == guard_g,
              "design %d: grayscale changed the wallpaper palette" % design)
        check(well_n != well_g,
              "design %d: grayscale changed no piece pixel" % design)
        cache = {}
        solved = next((k for k in range(-32, 129)
                       if match_at(cache, orientation, design, tick_n, k,
                                   loud_n)), None)
        check(solved is not None,
              "design %d pair frame matches no design package plate" %
              design)
        evidence["grayscale"][design] = solved
        print("  grayscale pair design %d: identical wallpaper, K=%s" %
              (design, solved))

    off = os.path.join(root, "pair-off")
    if not os.path.isdir(off):
        check(False, "missing session pair-off")
        return
    records = read_records(os.path.join(off, "observations.txt"))
    if "pair-play" not in records:
        check(False, "the Off pair session lacks the play record")
        return
    frame = read_frame(off, "pair-play")
    loud, guard, _ = frame_regions(frame, orientation)
    flat = bytes(LEGACY_FLAT)
    check(all(loud[i:i + 3] == flat for i in range(0, len(loud), 3)),
          "Off: a loud margin pixel is not the legacy flat #10121A")
    check(all(guard[i:i + 3] == flat for i in range(0, len(guard), 3)),
          "Off: a guard gap pixel is not the legacy flat #10121A")
    print("  Off: every margin and guard pixel holds the legacy flat "
          "#10121A")


def check_orientation_probe(root, evidence):
    session = os.path.join(root, "orientation")
    if not os.path.isdir(session):
        check(False, "missing session orientation")
        return
    records = read_records(os.path.join(session, "observations.txt"))
    cache = {}
    frames = {}
    ok = True
    for name in ("ori-land1", "ori-land1b", "ori-port1", "ori-port1b",
                 "ori-port2", "ori-port2b"):
        if name not in records:
            check(False, "orientation probe lacks %s" % name)
            ok = False
            continue
        record = records[name]
        check(record["scene"] == "playing",
              "orientation probe %s sits in scene %s" %
              (name, record["scene"]))
        frames[name] = (int(record["tick"]), read_frame(session, name))
    if not ok:
        return

    def solve(name, orientation, window):
        tick, frame = frames[name]
        loud = frame_regions(frame, orientation)[0]
        return tick, [k for k in window
                      if match_at(cache, orientation, 20, tick, k, loud)]

    # Round 1, landscape: the clock is young, so the window is small.
    tick1, found1 = solve("ori-land1", "landscape", range(-8, 40))
    check(bool(found1), "orientation probe: round 1 matches no plate")
    if not found1:
        return
    k1 = min(found1)
    # Within one round the offset is constant: the b frame of a pair must
    # sit at the same K as its a frame (the clock grows with the ticks).
    _, found1b = solve("ori-land1b", "landscape", range(k1 - 8, k1 + 9))
    check(bool(found1b),
          "orientation probe: the round 1 phase frame left the clock")
    # Across a turn and a new round the clock must sit far past any reset
    # value: the frames between the rounds pushed K up by their count.
    tick2, found2 = solve("ori-port1", "portrait", range(k1 + 40, k1 + 1500))
    check(bool(found2),
          "orientation probe: the portrait round matches no plate at a "
          "continuing clock (a reset to zero would sit near K=%d)" % k1)
    if not found2:
        return
    k2 = min(found2)
    check(k2 > k1 + 40,
          "orientation probe: the portrait clock (%d) did not continue "
          "from the landscape clock (%d)" % (k2, k1))
    _, found2b = solve("ori-port1b", "portrait", range(k2 - 8, k2 + 9))
    check(bool(found2b),
          "orientation probe: the portrait phase frame left the clock")
    tick3, found3 = solve("ori-port2", "portrait", range(k2 + 40, k2 + 1500))
    check(bool(found3),
          "orientation probe: the third round matches no plate at a "
          "continuing clock")
    if found3:
        k3 = min(found3)
        check(k3 > k2 + 40,
              "orientation probe: the third round's clock (%d) did not "
              "continue from %d" % (k3, k2))
        evidence["orientation"] = [k1, k2, k3]
        print("  orientation continuity: K %d -> %d -> %d across three "
              "turns" % (k1, k2, k3))


def check_rapid(root, evidence):
    session = os.path.join(root, "rapid-modes")
    if not os.path.isdir(session):
        check(False, "missing session rapid-modes")
        return
    records = read_records(os.path.join(session, "observations.txt"))
    if "rap-play" not in records:
        check(False, "the rapid probe lacks the play record")
        return
    settings = os.path.join(session, "save", "settings.dat")
    saved = ""
    if os.path.isfile(settings):
        with open(settings, encoding="ascii") as stream:
            saved = stream.read()
    check("background=fixed" in saved,
          "the rapid probe did not save background=fixed")
    check("backgroundid=18" in saved,
          "the rapid probe did not save backgroundid=18")
    record = records["rap-play"]
    check(record["scene"] == "playing",
          "the rapid probe play frame sits in scene %s" % record["scene"])
    tick = int(record["tick"])
    frame = read_frame(session, "rap-play")
    loud = frame_regions(frame, "landscape")[0]
    cache = {}
    solved = next((k for k in range(-32, 129)
                   if match_at(cache, "landscape", 18, tick, k, loud)), None)
    check(solved is not None,
          "the rapid probe play frame is not the Fixed design 18 grammar")
    evidence["rapid"] = solved
    if solved is not None:
        print("  rapid rings: the play frame holds design 18 at K=%d" %
              solved)


def main(root):
    # draw_text reads render.GLYPHS, which only render's own main fills in.
    render.GLYPHS = load_glyphs()
    evidence = {"frame_offset": {}, "motion_px": {}, "fades": {},
                "pause": {}, "distinct": {}, "grayscale": {},
                "orientation": None, "rapid": None}
    for orientation in ("landscape", "portrait"):
        session = os.path.join(root, "catalog", orientation)
        if not os.path.isdir(session):
            check(False, "missing catalog session %s" % session)
        else:
            check_catalog(orientation, root, evidence)
    check_pairs(root, evidence)
    check_orientation_probe(root, evidence)
    check_rapid(root, evidence)
    with open(os.path.join(root, "catalog-evidence.json"), "w",
              encoding="ascii") as stream:
        json.dump(evidence, stream, indent=1, sort_keys=True)
    print("%d checks, %d failures" % (checks, len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
