"""The 20 procedural level backgrounds of STACKFALL (docs/backgrounds).

Task T075 designed this set from the operator brief. Task T077 reconciled
it with the owner-approved imagegen set (owner approval 2026-11-05): the
approved names, palettes, concept lines, element counts, motion, and
transitions are the contract, and the T075 grammar machinery draws them.
The approved references are retained under `docs/backgrounds/reference/`
(brief, concepts.json, prompts.md, and the 20 generated plates); the
plates are visual direction only, never exact captures.

Every design is one pattern grammar: a palette with labeled roles plus a
`draw` function that turns the grammar into a list of elements on either
canvas. The two orientations share one grammar; only the canvas size and
the layout zones change.

An element is one dictionary. Four kinds exist and every one of them
expands into axis-aligned rectangles only:

  rect      one filled rectangle (a hollow rect draws the frame ring)
  band      one full-width band whose top edge is a repeating staircase
  checker   an ordered-dither field: 4x4 Bayer threshold, per-row densities
  hlines    equally spaced horizontal 1 px rules over one span

(The current catalogue resolves its dither patches as explicit 4 px
rects, so it emits no `checker` elements; the kind stays available.)

Each element also carries its motion, so the acceptance check can bound
what moves: `vx`/`vy` plus a wrap span for drift and scroll, or a `pulse`
(amplitude and cycle) for a slow swell. Nothing else may move and no
element may change color, which is what keeps the set free of blinking.

Wrapping is seamless: an element larger than 4 px in its motion axis
wraps over a span that includes its own extent, so it always leaves the
canvas (or the opaque well) fully before it re-enters. Elements of 4 px
or less may wrap at the margin span; their entry is sub-perceptual. The
check enforces both halves of that rule.

The scene partners the designs sit behind (renderer_sdl.hpp): the opaque
well, its one-pixel frame, the quiet HUD zones, and the loud margins where
accents may live.
"""

import struct
import zlib

from imaging import hex_of, parse_hex

# ---------------------------------------------------------------------------
# Scene geometry, from include/stackfall/renderer_sdl.hpp
# ---------------------------------------------------------------------------

WELL_COLOR = parse_hex("#0A0C13")
FRAME_COLOR = parse_hex("#4E5874")

# The three brightness caps of the brief. Every cap comparison in this
# package is componentwise: a color is within a cap when each of its red,
# green, and blue channels is at or below the cap's channel.
BASE_FLOOR = parse_hex("#0A0C13")
BASE_CEIL = parse_hex("#1C2130")
SECONDARY_CAP = parse_hex("#2A3146")
ACCENT_CAP = parse_hex("#8A94AC")

# The seven piece colors, for the grayscale separation check.
PIECE_COLORS = [
    parse_hex("#24CCD8"), parse_hex("#F8C840"), parse_hex("#B070E0"),
    parse_hex("#48D068"), parse_hex("#E85460"), parse_hex("#5880F0"),
    parse_hex("#F4943C"),
]

# The 4x4 ordered-dither threshold matrix, the one both the renderer and
# the checker embed for `checker` elements.
BAYER = [0, 8, 2, 10,
         12, 4, 14, 6,
         3, 11, 1, 9,
         15, 7, 13, 5]

# The lift of one staircase period: rise, crest, fall, rest.
STEP_LIFT = (0, 1, 2, 1)


class Zones:
    """One canvas and the regions the scene partners own."""

    def __init__(self, name, w, h, well, frame, quiet, guard, loud):
        self.name = name
        self.w = w
        self.h = h
        self.well = well        # the opaque board well (x, y, w, h)
        self.frame = frame      # the one-pixel board frame ring
        self.quiet = quiet      # HUD zones: nothing brighter than secondary
        self.guard = guard      # the quiet gaps flanking the frame
        self.loud = loud        # margins where accents may draw

    def fits_loud(self, x, y, w, h):
        """True when the whole rectangle lies inside one loud margin."""
        for rect in self.loud:
            if (x >= rect[0] and y >= rect[1] and
                    x + w <= rect[0] + rect[2] and y + h <= rect[1] + rect[3]):
                return True
        return False

    def loud_rect_of(self, x, y):
        """The loud margin that holds this point, if any."""
        for rect in self.loud:
            if (rect[0] <= x < rect[0] + rect[2] and
                    rect[1] <= y < rect[1] + rect[3]):
                return rect
        return None


def landscape_zones():
    return Zones(
        "landscape", 960, 544,
        well=(345, 2, 270, 540), frame=(344, 1, 272, 542),
        quiet=[(192, 0, 128, 544), (640, 0, 128, 544)],
        guard=[(320, 0, 24, 544), (616, 0, 24, 544)],
        loud=[(0, 0, 192, 544), (768, 0, 192, 544)])


def portrait_zones():
    return Zones(
        "portrait", 544, 960,
        well=(72, 120, 400, 800), frame=(71, 119, 402, 802),
        quiet=[(0, 0, 544, 119)],
        guard=[(59, 119, 12, 802), (473, 119, 12, 802)],
        loud=[(0, 119, 59, 802), (485, 119, 59, 802), (0, 921, 544, 39)])


def zones_for(orientation):
    return landscape_zones() if orientation == "landscape" else portrait_zones()


# ---------------------------------------------------------------------------
# Deterministic placement and motion
# ---------------------------------------------------------------------------


def h32(seed, *values):
    """One stable 32-bit hash of the integers, the same on every run."""
    return zlib.crc32(struct.pack("<%di" % (len(values) + 1), seed, *values))


def triangle(t, cycle):
    """The 0..1..0 triangle of one slow pulse cycle, never a jump."""
    phase = (t % cycle) / cycle
    return phase * 2.0 if phase < 0.5 else 2.0 - phase * 2.0


def drift(x, y, home, vx, vy, t, extent_x=0, extent_y=0):
    """One point moved by its velocity and wrapped inside `home`.

    The wrap span grows by the element's extent, so anything larger than
    a few pixels leaves its margin fully before it re-enters: no pop, no
    half-visible jump. Callers pass the extent only where the exit stays
    off-canvas, inside the opaque well, or inside another loud margin.
    """
    if vx:
        x = home[0] + (x - home[0] + vx * t) % (home[2] + extent_x)
    if vy:
        y = home[1] + (y - home[1] + vy * t) % (home[3] + extent_y)
    return round(x), round(y)


def wrap_of(home, vx, vy, extent_x=0, extent_y=0):
    """The wrap record an element carries for its drift."""
    if vx and vy:
        return {"axis": "both", "span": [home[2] + extent_x, home[3] + extent_y],
                "home": [home[2], home[3]]}
    if vx:
        return {"axis": "x", "span": home[2] + extent_x,
                "home": [home[2], home[3]]}
    if vy:
        return {"axis": "y", "span": home[3] + extent_y,
                "home": [home[2], home[3]]}
    return None


class Pen:
    """Collects the elements of one design on one canvas."""

    def __init__(self, zones):
        self.z = zones
        self.elements = []

    def rect(self, role, color, x, y, w, h, home=None, vx=0.0, vy=0.0,
             pulse=None, tag=None, extent_x=0, extent_y=0):
        self.elements.append({
            "id": len(self.elements), "kind": "rect", "role": role,
            "tag": tag, "color": hex_of(color), "x": int(x), "y": int(y),
            "w": int(w), "h": int(h), "vx": float(vx), "vy": float(vy),
            "wrap": wrap_of(home, vx, vy, extent_x, extent_y) if home else None,
            "pulse": pulse})
        return self.elements[-1]

    def band(self, role, color, y, height, step_w, step_h, drop, offset,
             phase=0, home=None, vx=0.0, vy=0.0, extent_y=0, tag="band"):
        # A horizontal slide moves the staircase phase of a full-width,
        # periodic pattern: a texture scroll, seamless by construction.
        # A vertical slide moves the band itself: real drift over a wrap
        # span that must include the band's extent.
        wrap = None
        if home:
            wrap = wrap_of(home, vx, vy, 0, extent_y)
            if wrap and vx and not vy:
                wrap["texture"] = True
        self.elements.append({
            "id": len(self.elements), "kind": "band", "role": role,
            "tag": tag, "color": hex_of(color), "x": 0, "y": int(y),
            "w": self.z.w, "h": int(height), "step_w": int(step_w),
            "step_h": int(step_h), "drop": int(drop), "offset": int(offset),
            "phase": int(phase), "vx": float(vx), "vy": float(vy),
            "wrap": wrap, "pulse": None})
        return self.elements[-1]

    def checker(self, role, base, ink, x, y, w, h, cell, densities, offset_x):
        self.elements.append({
            "id": len(self.elements), "kind": "checker", "role": role,
            "tag": "checker", "colors": [hex_of(base), hex_of(ink)],
            "x": int(x), "y": int(y), "w": int(w), "h": int(h),
            "cell": int(cell), "densities": list(densities),
            "offset_x": int(offset_x), "vx": 0.0, "vy": 0.0, "wrap": None,
            "pulse": None})
        return self.elements[-1]

    def hlines(self, role, color, x, y, w, h, spacing, thickness, offset_y,
               home=None, vx=0.0, vy=0.0, tag="scanlines"):
        # A rule set scrolls inside its fixed span by shifting the phase
        # of a periodic pattern: a texture scroll, seamless by
        # construction, never an element drift.
        wrap = wrap_of(home, vx, vy) if home else None
        if wrap:
            wrap["texture"] = True
        self.elements.append({
            "id": len(self.elements), "kind": "hlines", "role": role,
            "tag": tag, "color": hex_of(color), "x": int(x),
            "y": int(y), "w": int(w), "h": int(h), "spacing": int(spacing),
            "thickness": int(thickness), "offset_y": int(offset_y),
            "vx": float(vx), "vy": float(vy),
            "wrap": wrap, "pulse": None})
        return self.elements[-1]


def weighted_pick(pairs, value):
    """The first entry whose cumulative weight covers `value` of 100."""
    total = 0
    for weight, shape in pairs:
        total += weight
        if value < total:
            return shape
    return pairs[-1][1]


def dot_grid(pen, seed, cell, density, sizes, colors, vy=0.0, vx=0.0,
             tag="dot"):
    """One jittered grid of small rectangles inside the loud margins.

    `density` is the share of grid cells that hold a dot, `sizes` the list
    of (weight, (w, h)) shapes, `colors` the shades to pick from.
    """
    for home in pen.z.loud:
        cols = home[2] // cell
        rows = home[3] // cell
        for cy in range(rows):
            for cx in range(cols):
                if h32(seed, home[0], cx, cy) % 1000 >= density * 1000:
                    continue
                place_dot(pen, seed, home, cx, cy, cell, sizes, colors,
                          vx, vy, tag)


def exact_dots(pen, seed, count, cell, sizes, colors, vy=0.0, vx=0.0,
               tag="dot", bias_outer=False, extent_x=0, extent_y=0):
    """Exactly `count` dots inside the loud margins, chosen by hash rank.

    Both orientations draw the same count from the same seed. With
    `bias_outer`, cells nearer the outer edge of their margin rank first,
    so the dots pile against the canvas edges. A dot whose jittered
    position would leave its margin is skipped and the next candidate
    takes its place, so the count stays exact.
    """
    candidates = []
    for home in pen.z.loud:
        cols = home[2] // cell
        rows = home[3] // cell
        for cy in range(rows):
            for cx in range(cols):
                inner = min(cx, cols - 1 - cx) / max(1, cols - 1)
                rank = (round(inner * 100), h32(seed, home[0], cx, cy)) \
                    if bias_outer else (0, h32(seed, home[0], cx, cy))
                candidates.append((rank, home, cx, cy))
    candidates.sort(key=lambda entry: entry[0])
    placed = 0
    for _, home, cx, cy in candidates:
        if placed >= count:
            break
        if place_dot(pen, seed, home, cx, cy, cell, sizes, colors,
                     vx, vy, tag, extent_x, extent_y):
            placed += 1
    return placed


def place_dot(pen, seed, home, cx, cy, cell, sizes, colors, vx, vy, tag,
              extent_x=0, extent_y=0):
    """One dot at a jittered position inside cell (cx, cy). False when it
    would leave its loud margin."""
    shape = weighted_pick(sizes, h32(seed + 1, home[0], cx, cy) % 100)
    color = colors[h32(seed + 2, home[0], cx, cy) % len(colors)]
    room_x = cell - shape[0]
    room_y = cell - shape[1]
    base_x = home[0] + cx * cell + (h32(seed + 3, home[0], cx, cy)
                                    % (room_x + 1))
    base_y = home[1] + cy * cell + (h32(seed + 4, home[0], cx, cy)
                                    % (room_y + 1))
    if not pen.z.fits_loud(base_x, base_y, shape[0], shape[1]):
        return False
    x, y = drift(base_x, base_y, home, vx, vy, pen.t, extent_x, extent_y)
    pen.rect("accent", color, x, y, shape[0], shape[1], home=home, vx=vx,
             vy=vy, tag=tag, extent_x=extent_x, extent_y=extent_y)
    return True


def clip_to_regions(rect, regions):
    """The pieces of one rectangle that fall inside the given regions."""
    pieces = []
    for region in regions:
        x0 = max(rect[0], region[0])
        y0 = max(rect[1], region[1])
        x1 = min(rect[0] + rect[2], region[0] + region[2])
        y1 = min(rect[1] + rect[3], region[1] + region[3])
        if x1 - x0 > 0 and y1 - y0 > 0:
            pieces.append((x0, y0, x1 - x0, y1 - y0))
    return pieces


def ring_pieces(z, d, thickness, gap, seed):
    """The broken bars of one rectangular contour `thickness` px thick,
    inset `d` px, each side split once by a `gap` px opening at a hashed
    offset. Everything is axis-aligned rectangles."""
    w, h = z.w, z.h
    sides = [
        (d, d, w - 2 * d, thickness),                      # top
        (d, h - d - thickness, w - 2 * d, thickness),      # bottom
        (d, d, thickness, h - 2 * d),                      # left
        (w - d - thickness, d, thickness, h - 2 * d),      # right
    ]
    pieces = []
    for index, (x, y, sw, sh) in enumerate(sides):
        length = max(sw, sh)
        if length <= gap + 8:
            pieces.append((x, y, sw, sh))
            continue
        cut = 8 + h32(seed, index, d) % max(1, length - gap - 16)
        if sw >= sh:  # a horizontal bar: split its x span
            pieces.append((x, y, cut, sh))
            pieces.append((x + cut + gap, y, sw - cut - gap, sh))
        else:         # a vertical bar: split its y span
            pieces.append((x, y, sw, cut))
            pieces.append((x, y + cut + gap, sw, sh - cut - gap))
    return pieces


# ---------------------------------------------------------------------------
# The 20 grammars, reconciled with the approved set (task T077)
# ---------------------------------------------------------------------------


def still_ground(pen, t):
    """L1 Still ground: four faint dark corner steps, nothing else."""
    z = pen.z
    w = round(z.w / 6)
    h = round(z.h / 4.5)
    ink = parse_hex("#0C0E16")
    for corner in ((0, 0), (1, 0), (0, 1), (1, 1)):
        cx = 0 if corner[0] == 0 else z.w - w
        cy = 0 if corner[1] == 0 else z.h - h
        pen.rect("secondary", ink, cx, cy, w, h, tag="corner")
        # The inner step: the same ink, 62 percent of the size, so each
        # corner reads as two nested steps.
        sw = round(w * 0.62)
        sh = round(h * 0.62)
        sx = 0 if corner[0] == 0 else z.w - sw
        sy = 0 if corner[1] == 0 else z.h - sh
        pen.rect("secondary", ink, sx, sy, sw, sh, tag="corner-step")


def first_light(pen, t):
    """L2 First light: one dim rule at 82 percent height, two under-bands.

    The rule row lands on a multiple of 8 so the one-eighth review scale
    of the thumbnail sheet samples it directly.
    """
    z = pen.z
    rule = parse_hex("#2A3146")
    under = parse_hex("#1C2433")
    y = (round(z.h * 0.82) // 8) * 8
    pen.rect("accent", rule, 0, y, z.w, 1, tag="rule")
    pen.rect("secondary", under, 0, y + 8, z.w, 3, tag="under-band")
    pen.rect("secondary", under, 0, y + 16, z.w, 4, tag="under-band")


def distant_stars(pen, t):
    """L3 Distant stars: exactly 28 square stars, 1 px and 2 px."""
    exact_dots(pen, 0x5355, 28, 24,
               [(55, (1, 1)), (45, (2, 2))],
               [parse_hex("#1B2230"), parse_hex("#64718C")], tag="star")


def quiet_strata(pen, t):
    """L4 Quiet strata: eight uneven sediment bands with stepped ends."""
    z = pen.z
    ink = parse_hex("#222A39")
    shares = [0.055, 0.045, 0.060, 0.040, 0.050, 0.035, 0.045, 0.030]
    y = z.h
    for share in shares:
        height = round(z.h * share)
        y -= height + 9
        pen.rect("secondary", ink, 0, y, z.w, height, tag="stratum")
        # One stepped end inside each loud margin the band reaches: a
        # block 6 px wide and 6 px taller than the band, on the band's
        # top edge at the margin's outer edge.
        for home in z.loud:
            if home[1] + home[3] <= y or home[1] >= y + height:
                continue
            step_w = 6
            for side in (0, 1):
                sx = home[0] if side == 0 else home[0] + home[2] - step_w
                if z.fits_loud(sx, y - 6, step_w, height + 6):
                    pen.rect("secondary", ink, sx, y - 6, step_w,
                             height + 6, tag="stratum-step")


def slow_drift(pen, t):
    """L5 Slow drift: 56 stars sinking, static dust beneath them."""
    exact_dots(pen, 0x5356, 56, 20,
               [(60, (1, 1)), (40, (2, 2))],
               [parse_hex("#20283A"), parse_hex("#687892")], vy=2.0,
               tag="star")
    exact_dots(pen, 0x5357, 10, 28,
               [(100, (3, 1))],
               [parse_hex("#20283A")], tag="dust")


def dither_dusk(pen, t):
    """L6 Dither dusk: stepped dusk zones in the margins, patches rising.

    The five zones alternate the two dark tones and live in the outer
    margins, keeping the center corridor clear - the brief's own rule
    that density grows toward the canvas edges, and the concept's "no
    continuous noisy full-screen texture". Each patch is a 4x3 block of
    4 px squares in the accent ink on the even cells of its own grid.
    Patches wrap over a span that includes their height: they leave the
    canvas fully before re-entering below the bottom edge.
    """
    z = pen.z
    zone = parse_hex("#242B3A")
    ink = parse_hex("#38445B")
    shares = [0.08, 0.10, 0.12, 0.14, 0.16]
    y = round(z.h * 0.30)
    zone_rows = []
    for share in shares:
        height = round(z.h * share)
        zone_rows.append((y, height))
        y += height + 4
    for home in z.loud[:2]:  # the side strips of either orientation
        for index, (zy, zh) in enumerate(zone_rows):
            if index % 2:  # the base-tone steps between the ink zones
                continue
            step_w = 6
            for side in (0, 1):
                sx = home[0] if side == 0 else home[0] + home[2] - step_w
                if z.fits_loud(sx, zy - 5, step_w, zh + 5):
                    pen.rect("secondary", zone, sx, zy - 5, step_w,
                             zh + 5, tag="dusk-zone")
            pen.rect("secondary", zone, home[0] + 6, zy, home[2] - 12, zh,
                     tag="dusk-zone")
    patch_w, patch_h = 16, 12
    for home in z.loud[:2]:
        for zone_index, (zy, zh) in enumerate(zone_rows):
            room_y = max(1, min(zh, home[1] + home[3] - zy - 6) - patch_h - 6)
            for which in range(2):
                px = home[0] + 10 + h32(0x6D, home[0], zone_index, which) % \
                    max(1, home[2] - 10 - patch_w - 10)
                py = zy + 6 + h32(0x6E, home[0], zone_index, which) % room_y
                x, y = drift(px, py, home, 0.0, -2.0, pen.t,
                             extent_y=patch_h)
                for cy in range(3):
                    for cx in range(4):
                        if (cx + cy) % 2:
                            continue
                        pen.rect("accent", ink, x + cx * 4, y + cy * 4, 4, 4,
                                 home=home, vy=-2.0, tag="dither",
                                 extent_y=patch_h)


def sleeping_skyline(pen, t):
    """L7 Sleeping skyline: 14 stepped towers, windows breathing.

    Landscape towers run the full bottom edge and rise highest at the
    corners. Portrait towers live in the outer side strips, anchored on
    the canvas bottom. Windows draw only inside the loud margins and
    swell one pixel in height over the 8 s cycle - the approved window
    pulse, expressed as a slow size change instead of a brightness
    change, because a brightness pulse would blink.
    """
    z = pen.z
    tower = parse_hex("#252D3D")
    window = parse_hex("#64728A")
    swell = round(triangle(t, 8.0))
    pulse_spec = {"axis": "h", "amplitude": 1, "cycle": 8.0}
    portrait = z.w < z.h
    slots = 14
    for slot in range(slots):
        if portrait:
            side = z.loud[slot % 2]
            inner = side[2]
            place = slot // 2
            rows = 7
            slot_w = inner / float(rows)
            base_x = side[0] + round(place * slot_w) + \
                h32(0x571, side[0], place) % max(1, round(slot_w * 0.4))
            width = round(slot_w * 0.6)
        else:
            slot_w = z.w / float(slots)
            base_x = round(slot * slot_w) + \
                h32(0x571, 0, slot) % max(1, round(slot_w * 0.3))
            width = round(slot_w * 0.62)
        center = min(base_x, z.w - base_x) / max(1, z.w / 2.0)
        share = 0.10 + 0.24 * (1.0 - center) + \
            (h32(0x572, slot) % 100) / 1000.0
        height = round(z.h * share)
        top = z.h - height
        pen.rect("secondary", tower, base_x, top, width, height, tag="tower")
        pen.rect("secondary", tower, base_x + round(width * 0.2), top - 8,
                 round(width * 0.6), 8, tag="roof")
        pen.rect("secondary", tower, base_x + round(width * 0.35), top - 14,
                 round(width * 0.3), 6, tag="roof")
        if height < round(z.h * 0.30):
            continue
        columns = [base_x + 4, base_x + width - 8] if width >= 24 else \
            [base_x + max(1, (width - 3) // 2)]
        wy = top + 10
        while wy < top + height // 2:
            for column in columns:
                if z.fits_loud(column, wy - swell, 3, 4 + swell):
                    pen.rect("accent", window, column, wy - swell, 3,
                             4 + swell, pulse=pulse_spec, tag="window")
            wy += 14


def low_signal(pen, t):
    """L8 Low signal: scanline bursts in the margins, edge bars, scrolling."""
    z = pen.z
    line = parse_hex("#242C3B")
    bar = parse_hex("#54617A")
    offset = round(3.0 * t) % 10
    for home in z.loud:
        # Two signal bursts per margin: uneven x spans at uneven y
        # bands, so the runs read as broken transmissions, not a
        # full-field texture.
        for group in range(2):
            band_h = 80 + h32(0x86, home[0], group) % 70
            band_y = home[1] + 8 + (h32(0x87, home[0], group) %
                                    max(1, home[3] - band_h - 16))
            span_w = 44 + h32(0x88, home[0], group) % max(
                1, home[2] - 52)
            span_x = home[0] + 6 + (h32(0x89, home[0], group) %
                                    max(1, home[2] - span_w - 12))
            if span_x + span_w > home[0] + home[2] - 6:
                span_w = home[0] + home[2] - 6 - span_x
            pen.hlines("secondary", line, span_x, band_y, span_w, band_h,
                       10, 1, offset, home=home, vy=3.0, tag="scanlines")
    for home in z.loud[:2]:
        pen.rect("accent", bar, home[0] + 6, home[1] + 14, home[2] - 12, 3,
                 tag="signal-bar")
        pen.rect("accent", bar, home[0] + 6, home[1] + home[3] - 40,
                 home[2] - 12, 3, tag="signal-bar")


def fracture(pen, t):
    """L9 Fracture: two stepped seams in the far margins, ticks sinking."""
    z = pen.z
    seam = parse_hex("#283044")
    tick = parse_hex("#707D96")
    steps = 12
    for which, margin in ((0, z.loud[0]), (1, z.loud[1])):
        step_w = round(margin[2] / steps) + 1
        step_h = round(margin[3] / steps) + 1
        for index in range(steps):
            share = index / float(steps)
            if which == 0:
                x = margin[0] + round(margin[2] * share)
                x = min(x, margin[0] + margin[2] - step_w)
                ledge_x = min(x + 12, margin[0] + margin[2] - step_w)
                chip_x = x + step_w + 2
            else:
                x = margin[0] + margin[2] - round(margin[2] * share) - step_w
                x = max(x, margin[0])
                ledge_x = max(x - 12, margin[0])
                chip_x = x - 4
            y = round(margin[3] * share) + margin[1]
            pen.rect("secondary", seam, x, y, step_w, 5, tag="seam")
            pen.rect("secondary", seam, ledge_x, y, step_w, 22, tag="ledge")
            for side_sign in (1, -1):
                chip_x = x + (step_w + 2 if side_sign > 0 else -4)
                chip_y = y + 6 + (h32(0x9F, which, index, side_sign) % 10)
                if not z.fits_loud(chip_x, chip_y, 2, 2):
                    continue
                cx, cy = drift(chip_x, chip_y, margin, 0.0, 3.0, pen.t)
                pen.rect("accent", tick, cx, cy, 2, 2, home=margin, vy=3.0,
                         tag="fracture-tick")


def circuit_hush(pen, t):
    """L10 Circuit hush: 16 right-angle paths, node cores breathing."""
    z = pen.z
    trace = parse_hex("#283144")
    node = parse_hex("#283144")
    lit = parse_hex("#6B8498")
    swell = round(triangle(t, 7.0))
    pulse_spec = {"axis": "wh", "amplitude": 1, "cycle": 7.0}
    paths = 16
    for index in range(paths):
        home = z.loud[index % 2]  # the side strips of either orientation
        row = index // 2
        rows = paths // 2
        base_y = home[1] + 8 + row * (home[3] - 70) // rows
        run = 24 + h32(0xA1, home[0], row) % 48
        x0 = home[0] + 4 + h32(0xA2, home[0], row) % max(
            1, home[2] - run - 8)
        fall = 16 + h32(0xA3, home[0], row) % 24
        pen.rect("secondary", trace, x0, base_y, run, 2, tag="trace")
        pen.rect("secondary", trace, x0 + run - 2, base_y, 2, fall,
                 tag="trace")
        # A second right-angle jog: a short run back toward the margin
        # edge, then the closing drop, so each path reads as one trace
        # with two bends.
        back = 8 + h32(0xA4, home[0], row) % max(1, run - 10)
        pen.rect("secondary", trace, x0 + run - back, base_y + fall - 2,
                 back, 2, tag="trace")
        drop2 = 14 + h32(0xA5, home[0], row) % 22
        pen.rect("secondary", trace, x0 + run - back, base_y + fall - 2, 2,
                 drop2, tag="trace")
        node_y = base_y + fall + drop2 - 6
        if node_y + 4 <= home[1] + home[3]:
            pen.rect("secondary", node, x0 + run - back - 1, node_y, 4, 4,
                     tag="node")
            if index % 3 == 0:
                pen.rect("accent", lit, x0 + run - back - 1 - swell // 2,
                         node_y - swell // 2, 2 + swell, 2 + swell,
                         pulse=pulse_spec, tag="node-core")


def band_aurora(pen, t):
    """L11 Band aurora: three stepped ribbons rising together.

    Each ribbon wraps over a span of canvas height plus its own extent,
    so it leaves the top edge fully before re-entering below the bottom
    edge: the slide up never pops. The concept's 96 s loop would wrap a
    still-visible ribbon; the contract states the exact seamless period.

    The wrap phase anchors one extent below the ribbon's base row, so a
    ribbon that has left the top re-enters at the bottom edge instead of
    popping back at its base row (the correction task T081 recorded in
    docs/backgrounds/level-11.md).
    """
    z = pen.z
    ribbon = parse_hex("#283044")
    tick = parse_hex("#67758D")
    body = round(z.h * 0.035)
    extent = 3 * (body + 6) + 2 * 8 + 10  # ribbon group plus lift and drop
    span = z.h + extent
    for index in range(3):
        base_y = round(z.h * (0.16 + 0.22 * index))
        y = round((base_y + extent - 4.0 * pen.t) % span) - extent
        # Each ribbon is three overlapping staircase bands, so the edge
        # cascades and the ribbon reads as one broad layered curtain.
        for layer in range(3):
            pen.band("secondary", ribbon, y + layer * (body + 6), body,
                     40, 8, index * 5 + layer * 2, 0,
                     phase=((index + layer) % 2) * 2,
                     home=(0, 0, z.w, z.h), vy=-4.0, extent_y=extent,
                     tag="ribbon")
    for index in range(8):
        home = z.loud[index % len(z.loud)]
        tx = home[0] + 8 + h32(0xB1, home[0], index) % max(1, home[2] - 18)
        ty = home[1] + 10 + h32(0xB2, home[0], index) % max(1, home[3] - 20)
        x, y = drift(tx, ty, (0, 0, z.w, z.h), 0.0, -4.0, pen.t,
                     extent_y=2)
        if z.fits_loud(x, y, 2, 2):
            pen.rect("accent", tick, x, y, 2, 2, home=(0, 0, z.w, z.h),
                     vy=-4.0, tag="aurora-tick", extent_y=2)


def long_trails(pen, t):
    """L12 Long trails: 72 stars and 12 staircase trails, drifting as one."""
    z = pen.z
    star_dim = parse_hex("#293144")
    star_bright = parse_hex("#76849B")
    step = parse_hex("#293144")
    head = parse_hex("#76849B")
    vx, vy = -2.4, 3.2  # 4.0 px/s along the path, down and to the left
    exact_dots(pen, 0xC12, 72, 20,
               [(55, (1, 1)), (45, (2, 2))],
               [star_dim, star_bright], vx=vx, vy=vy, tag="star")
    for index in range(12):
        home = z.loud[index % len(z.loud)]
        fx = (h32(0xC3A, index) % 80) / 100.0
        fy = (h32(0xC3B, index) % 90) / 100.0
        x = home[0] + round(home[2] * fx)
        y = home[1] + round(home[3] * fy)
        for segment in range(6):
            sx, sy = drift(x - segment * 4, y + segment * 2, home,
                           vx, vy, pen.t)
            pen.rect("secondary", step, sx, sy, 3, 1, home=home, vx=vx,
                     vy=vy, tag="trail-step")
        hx, hy = drift(x - 1, y - 3, home, vx, vy, pen.t)
        if z.fits_loud(hx, hy, 2, 2):
            pen.rect("accent", head, hx, hy, 2, 2, home=home, vx=vx, vy=vy,
                     tag="trail-head")


def deep_grid(pen, t):
    """L13 Deep grid: 22 staircase lines, the horizontal set scrolling.

    Horizontal rules sit closer together near the mid-height horizon and
    wider apart toward the canvas edges. All of them share one velocity,
    so their spacing pattern translates rigidly as the field scrolls.
    The vertical guides and their nodes hold still.
    """
    z = pen.z
    line = parse_hex("#293145")
    node = parse_hex("#79869B")
    mid = z.h // 2
    offsets = [6, 11, 18, 27, 38, 51]
    scroll = round(5.0 * pen.t)
    horizontals = 0
    for away in offsets:
        for sign in (-1, 1):
            y0 = mid + sign * away
            if not 0 <= y0 < z.h:
                continue
            horizontals += 1
            for home in z.loud:
                if home[3] > 100:  # a tall side margin: the rule scrolls
                    y = home[1] + (y0 - home[1] + scroll) % home[3]
                    pen.rect("secondary", line, home[0], y, home[2], 1,
                             home=home, vy=5.0, tag="grid-rule")
                elif home[1] <= y0 < home[1] + home[3]:
                    pen.rect("secondary", line, home[0], y0, home[2], 1,
                             tag="grid-rule")
    guides = 0
    for home in z.loud[:2]:
        for guide in range(5):
            x = home[0] + 6 + guide * (home[2] - 14) // 5
            toward = 1 if x < z.w // 2 else -1
            for step_index in range(4):
                shift = toward * step_index
                sy = home[1] + step_index * (home[3] // 4)
                pen.rect("secondary", line, x + shift, sy, 2, home[3] // 4,
                         tag="grid-guide")
            guides += 1
            if z.fits_loud(x, home[1] + home[3] - 6, 2, 2):
                pen.rect("accent", node, x, home[1] + home[3] - 6, 2, 2,
                         tag="grid-node")
    assert horizontals == 12 and guides == 10, "grid line count drifted"


def fine_snow(pen, t):
    """L14 Fine snow: exactly 160 particles falling over dark bands."""
    exact_dots(pen, 0xE14, 160, 20,
               [(55, (1, 1)), (45, (2, 2))],
               [parse_hex("#293145"), parse_hex("#7A879E")], vy=6.0,
               tag="flake")
    z = pen.z
    band = parse_hex("#293145")
    for share in (0.30, 0.55, 0.80):
        pen.rect("secondary", band, 0, round(z.h * share), z.w,
                 round(z.h * 0.035), tag="snow-band")


def undertow(pen, t):
    """L15 Undertow: seven stepped wave bands sliding left as one layer.

    Alternate bands carry the opposite staircase phase, so the contours
    read as two opposing currents while every rectangle shares one
    velocity.
    """
    z = pen.z
    shades = [parse_hex("#2A3146")]
    foam = parse_hex("#74839B")
    # The leftward slide the record and the approved concept name: the
    # offset advances negatively, so the staircase steps move left at the
    # declared vx of -6 px/s. (The pre-T082 generator advanced it
    # positively, sliding the pattern right against its own record.)
    offset = round(-6.0 * pen.t)
    for index in range(7):
        y = round(z.h * (0.28 + 0.09 * index))
        pen.band("secondary", shades[0], y, round(z.h * 0.07), 20, 6,
                 index * 3, offset, phase=(index % 2) * 2, home=(0, 0, z.w, z.h),
                 vx=-6.0, tag="wave-band")
        # Foam ticks ride the band edges but hold their margin slots:
        # the moving layer is the band stack alone.
        for home in z.loud:
            for k in range(2):
                fx = home[0] + 8 + (h32(0x0F, index, home[0], k) %
                                    max(1, home[2] - 12))
                fy = y - 5 + (h32(0xF1, index, home[0], k) % 10)
                if z.fits_loud(fx, fy, 2, 1):
                    pen.rect("accent", foam, fx, fy, 2, 1, tag="foam")


def slow_pulse(pen, t):
    """L16 Slow pulse: nine broken contours breathing outward."""
    z = pen.z
    contour = parse_hex("#2A3146")
    tick = parse_hex("#7C88A0")
    swell = round(24.0 * triangle(t, 8.0))
    pulse_spec = {"axis": "xy", "amplitude": 24, "cycle": 8.0}
    for index in range(9):
        d = 10 + 26 * index + swell
        if 2 * d + 2 >= min(z.w, z.h):
            continue
        for piece in ring_pieces(z, d, 2, 24, 0x161 + index):
            pen.rect("secondary", contour, piece[0], piece[1], piece[2],
                     piece[3], pulse=pulse_spec, tag="contour-bar")
        if index < 3:
            for corner in ((d, d), (z.w - d - 2, d), (d, z.h - d - 2),
                           (z.w - d - 2, z.h - d - 2)):
                if z.fits_loud(corner[0], corner[1], 2, 2):
                    pen.rect("accent", tick, corner[0], corner[1], 2, 2,
                             pulse=pulse_spec, tag="pulse-tick")


def rising_ember(pen, t):
    """L17 Rising ember: exactly 190 warm-gray embers rising past columns."""
    exact_dots(pen, 0xE17, 190, 18,
               [(50, (1, 2)), (50, (2, 2))],
               [parse_hex("#897A72")], vy=-7.0, tag="ember")
    z = pen.z
    column = parse_hex("#2A3040")
    for home in z.loud[:2]:  # stepped block columns along the side strips
        x = home[0] + 4
        floor = home[1] + round(home[3] * 0.8)  # columns fill the lower part
        while x < home[0] + home[2] - 12:
            w = 12 + h32(0xE4, x) % 20
            y = home[1] + home[3] - 10
            while y > floor:
                pen.rect("secondary", column, x, y, w, 10,
                         tag="ember-column")
                y -= 34
            step_y = home[1] + home[3] - 16
            while step_y > floor:
                if z.fits_loud(x, step_y, w // 2, 6):
                    pen.rect("secondary", column, x, step_y, w // 2, 6,
                             tag="ember-column-step")
                step_y -= 34
            x += w + 8
    for home in z.loud[2:]:  # portrait bottom margin: one row of blocks
        x = home[0]
        while x < home[0] + home[2] - 12:
            w = 8 + h32(0xE8, x) % 14
            if z.fits_loud(x, home[1], w, 10):
                pen.rect("secondary", column, x, home[1], w, 10,
                         tag="ember-column")
            x += w + 6


def storm_lines(pen, t):
    """L18 Storm lines: 200 rain dashes and 4 stepped trails, all falling."""
    z = pen.z
    dash_dim = parse_hex("#2A3146")
    dash_bright = parse_hex("#8290A7")
    trail = parse_hex("#2A3146")
    exact_dots(pen, 0x518, 200, 16,
               [(65, (2, 4)), (35, (2, 6))],
               [dash_dim, dash_bright], vy=8.0, tag="dash", extent_y=6)
    for which, fy in ((0, 0.08), (1, 0.38), (2, 0.62), (3, 0.86)):
        home = z.loud[which % len(z.loud)]
        x = home[0] + round(home[2] * 0.5)
        y = home[1] + round(home[3] * fy)
        for segment in range(5):
            sx, sy = drift(x - segment * 4, y + segment * 2, home,
                           0.0, 8.0, pen.t, extent_y=6)
            pen.rect("secondary", trail, sx, sy, 4, 2, home=home, vy=8.0,
                     tag="storm-trail", extent_y=6)


def compression(pen, t):
    """L19 Compression: 30 broken bands pressing inward over 12 s.

    The whole nest contracts 54 px onto the board and returns, a 12 s
    seamless cycle whose fastest edge moves 9 px/s. The outermost bands
    slide off the canvas edges as the nest presses; they re-enter on the
    return, so nothing pops or blinks.
    """
    z = pen.z
    band = parse_hex("#2A3146")
    tick_bright = parse_hex("#8591A8")
    press = round(54.0 * triangle(t, 12.0))
    pulse_spec = {"axis": "xy", "amplitude": 54, "cycle": 12.0}
    inset = 8
    for index in range(30):
        d = inset - press
        inset += max(5, 13 - index // 3)
        if d >= min(z.w, z.h) // 2:
            continue
        for piece in ring_pieces(z, d, 3, 20, 0xC19 + index):
            pen.rect("secondary", band, piece[0], piece[1], piece[2],
                     piece[3], pulse=pulse_spec, tag="press-bar")
        if index < 2:
            for piece in clip_to_regions((d, d, z.w - 2 * d, 3), z.loud):
                if piece[2] < 14:
                    continue
                x = piece[0] + 6
                while x + 2 <= piece[0] + piece[2] - 4:
                    pen.rect("accent", tick_bright, x, piece[1], 2, 2,
                             pulse=pulse_spec, tag="press-tick")
                    x += 32


def collapse(pen, t):
    """L20 Collapse: 280 tiny debris fragments falling past static slabs."""
    z = pen.z
    debris_dim = parse_hex("#2A3146")
    debris_bright = parse_hex("#8A94AC")
    slab = parse_hex("#2A3146")
    exact_dots(pen, 0xC0A, 280, 16,
               [(35, (1, 3)), (35, (2, 5)), (30, (3, 7))],
               [debris_dim, debris_dim, debris_bright], vy=10.0,
               tag="debris", bias_outer=True, extent_y=7)
    for home in z.loud:
        for which in range(3):
            w = 32 + h32(0xC2, home[0], which) % 40
            h = 10 + h32(0xC3, home[0], which) % 10
            x = home[0] + 4 + h32(0xC4, home[0], which) % max(
                1, home[2] - w - 8)
            y = home[1] + 6 + which * (home[3] // 4)
            if z.fits_loud(x, y, w, h):
                pen.rect("secondary", slab, x, y, w, h, tag="slab")
                if z.fits_loud(x, y - 5, w // 2, 5):
                    pen.rect("secondary", slab, x, y - 5, w // 2, 5,
                             tag="slab-step")


# ---------------------------------------------------------------------------
# The catalogue: the approved level mapping of the owner's set
# ---------------------------------------------------------------------------

CHAPTERS = [("Still", 1, 4), ("Stir", 5, 8), ("Build", 9, 12),
            ("Pressure", 13, 16), ("Peak", 17, 20)]


def chapter_of(level):
    for name, first, last in CHAPTERS:
        if first <= level <= last:
            return name
    return "Peak"


def catalogue():
    """The 20 designs in level order, one record per level.

    Names, palettes, concept lines, counts, motion, transitions, and
    intensities follow the owner-approved imagegen set
    (docs/backgrounds/reference/concepts.json, approval 2026-11-05).
    """
    designs = []

    def add(level, name, concept, base, secondary, accent, draw, motion,
            transition, intensity, budget, grammar, counts=None):
        designs.append({
            "level": level, "name": name, "concept": concept,
            "chapter": chapter_of(level), "base": base,
            "secondary": secondary, "accent": accent, "draw": draw,
            "motion": motion, "transition": transition,
            "intensity": intensity, "budget": budget, "grammar": grammar,
            "counts": counts or {}})

    add(1, "Still ground",
        "Almost empty ink-navy field with four exceptionally faint dark "
        "corner steps.",
        "#10121A", ["#0C0E16"], ["#283044"], still_ground, None, ("cut",),
        1, "bands",
        ["Fill the whole canvas with base #10121A.",
         "Into each of the four corners draw two nested steps of secondary "
         "#0C0E16: an outer rectangle of round(width / 6) x round(height / "
         "4.5) px (160 x 121 landscape, 91 x 213 portrait) touching the "
         "two canvas edges, then an inner rectangle of 62 percent of those "
         "sizes touching the same corner. Each corner reads as two faint "
         "steps.",
         "Nothing else draws. The accent shade #283044 is reserved and "
         "unused: the brief asks for no bright marks at all."],
        counts={"corner": 4, "corner-step": 4})

    add(2, "First light",
        "One fine dim horizontal rule low on the canvas with two broader "
        "darker under-bands.",
        "#10131C", ["#1C2433"], ["#2A3146"], first_light, None,
        ("fade", 0.4, 12), 1.4, "small",
        ["Fill the whole canvas with base #10131C.",
         "Draw one horizontal rule of accent #2A3146, 1 px tall, across "
         "the full canvas width at row (round(height * 0.82) rounded down "
         "to a multiple of 8): row 440 landscape, row 784 portrait. The "
         "opaque well and its frame cover it where they overlap.",
         "Draw two broader under-bands of secondary #1C2433 below the "
         "rule: rows rule+8 .. rule+10 (3 px) and rule+16 .. rule+19 "
         "(4 px), both full width.",
         "Static. On level change the design fades in over 0.4 s in 12 "
         "visible steps."],
        counts={"rule": 1, "under-band": 2})

    add(3, "Distant stars",
        "Exactly 28 tiny square stars concentrated toward the outer "
        "margins.",
        "#0E121C", ["#1B2230"], ["#64718C"], distant_stars, None,
        ("fade", 0.5, 15), 1.8, "small",
        ["Fill the whole canvas with base #0E121C.",
         "Lay a 24 px cell grid inside each loud margin. Take the 28 "
         "cells with the lowest hash rank h32(0x5355, margin_x, cx, cy); "
         "both orientations draw exactly 28 stars.",
         "Each star is 1 x 1 px in 55 percent of the cells and 2 x 2 px in "
         "45, at a hashed jitter inside its cell. Stars are secondary "
         "#1B2230 or accent #64718C in equal share. A star that would "
         "leave its margin is skipped and the next-ranked cell takes its "
         "place. Empty near the well; no bright dots under the HUD."],
        counts={"star": 28})

    add(4, "Quiet strata",
        "Eight broad uneven sediment bands with stepped ends near the "
        "outer edges.",
        "#11141D", ["#222A39"], ["#2A3146"], quiet_strata, None,
        ("fade", 0.5, 15), 2.2, "bands",
        ["Fill the whole canvas with base #11141D.",
         "Stack eight full-width bands of secondary #222A39 from the "
         "bottom edge upward, brightest reading first. Band heights are "
         "round(height * share) for shares 5.5, 4.5, 6.0, 4.0, 5.0, 3.5, "
         "4.5 and 3.0 percent, separated by 9 px gaps of base, so the "
         "stack fills the bottom third of the canvas.",
         "At each band end that reaches a loud margin, draw one step of "
         "the same secondary: a block 6 px wide and 6 px taller than the "
         "band, on the band's bottom edge at the margin's outer edge, "
         "only when it fits inside the margin.",
         "The bands run under the quiet zones at their secondary shade; "
         "the well and frame cover them behind the board."],
        counts={"stratum": 8})

    add(5, "Slow drift",
        "Fifty-six square stars sinking slowly, with static dust beneath.",
        "#0F1420", ["#20283A"], ["#687892"], slow_drift,
        {"kind": "drift", "direction": "down", "speed": 2.0, "cycle": 272.0,
         "note": "Every star sinks 2 px/s and wraps at its margin span. "
                 "The landscape margin is 544 px tall, so one star "
                 "completes one pass in 272 s; the portrait side margin "
                 "takes 401 s. The dust flecks hold still, so the layer "
                 "beneath is still."},
        ("cut",), 2.6, "small",
        ["Fill the whole canvas with base #0F1420.",
         "Lay a 20 px cell grid inside each loud margin. Take the 56 "
         "cells with the lowest hash rank h32(0x5356, margin_x, cx, cy). "
         "Each star is 1 x 1 px in 60 percent of them and 2 x 2 px in 40, "
         "at a hashed jitter, colored secondary #20283A or accent #687892 "
         "in equal share.",
         "Add 10 static dust flecks of secondary #20283A, 3 x 1 px, by "
         "hash rank h32(0x5357, ...) on a 28 px grid.",
         "Motion: the star layer drifts down at 2 px/s. A star that "
         "reaches the bottom of its margin re-enters at its top; a 2 px "
         "star crossing the edge is sub-perceptual, so no visible jump."],
        counts={"star": 56, "dust": 10})

    add(6, "Dither dusk",
        "Five stepped dusk zones with coarse checker patches rising at "
        "the outer edges.",
        "#121620", ["#242B3A"], ["#38445B"], dither_dusk,
        {"kind": "drift", "direction": "up", "speed": 2.0, "cycle": 278.0,
         "note": "The patch layer rises 2 px/s. Each patch wraps over "
                 "margin height plus its own 12 px height, so it leaves "
                 "the canvas fully before re-entering below the bottom "
                 "edge: the rise never pops. Landscape wraps after 556 px "
                 "(278 s); the zones hold still."},
        ("fade", 0.5, 15), 3, "small",
        ["Fill the whole canvas with base #121620.",
         "Inside each side loud margin, draw five stepped horizontal "
         "zones from row round(height * 0.30) downward, heights "
         "round(height * share) for shares 8, 10, 12, 14 and 16 percent, "
         "separated by 4 px gaps. The zones alternate the two dark "
         "tones: zones 1, 3 and 5 carry the secondary #242B3A, zones 2 "
         "and 4 stay base, so the dusk reads as five steps. Each ink "
         "zone is one band inset 6 px from the margin edges plus a 6 px "
         "wide stepped end cap at each margin edge, 5 px taller than the "
         "zone (18 zone rectangles in all). The zones stay inside the "
         "margins, keeping the center corridor clear: no continuous "
         "full-screen texture.",
         "Inside each side loud margin, place two checker patches per "
         "zone: a 16 x 12 px block of 4 px squares of accent #38445B on "
         "the even cells of its own 4 px block grid, at hashed positions "
         "at least 10 px inside the margin and the zone.",
         "Motion: the patch layer drifts up at 2 px/s and wraps over "
         "margin height plus 12 px, so patches enter and leave fully "
         "off-canvas. The zones hold still."],
        counts={"dusk-zone": 18, "dither": 120})

    add(7, "Sleeping skyline",
        "Fourteen stepped tower silhouettes with subdued breathing "
        "windows.",
        "#11151F", ["#252D3D"], ["#64728A"], sleeping_skyline,
        {"kind": "pulse", "direction": "none", "speed": 0.25, "cycle": 8.0,
         "note": "The window layer swells 1 px in height and returns over "
                 "an 8 s cycle, 0.25 px/s at the fastest edge: the "
                 "approved window pulse expressed as a slow size change, "
                 "because a brightness pulse would blink. The towers hold "
                 "still."},
        ("fade", 0.5, 15), 3.4, "small",
        ["Fill the whole canvas with base #11151F.",
         "Landscape: divide the canvas bottom into 14 equal slots and "
         "raise one tower per slot, 62 percent of the slot wide at a "
         "hashed offset. Portrait: raise 7 towers inside each outer side "
         "strip (14 total), 60 percent of a seventh of the strip wide, "
         "anchored on the canvas bottom edge.",
         "Tower height is round(height * (0.10 + 0.24 * (1 - corner "
         "closeness) + hash / 1000)), so towers rise highest at the "
         "corners. Each tower carries two stepped roof blocks above its "
         "top: 60 percent of its width 8 px tall, then 30 percent 6 px "
         "taller still.",
         "On towers taller than 30 percent of the canvas, draw windows of "
         "accent #64728A, 3 px wide and 4 to 5 px tall, two columns 4 px "
         "from the tower sides, one every 14 rows from 10 px below the "
         "top to its middle, only where they stay inside a loud margin.",
         "Motion: the windows swell 1 px in height and return over an "
         "8 s cycle. Nothing else moves."],
        counts={"tower": 14})

    add(8, "Low signal",
        "Low-contrast scanline groups in the margins with solid edge "
        "bars, scrolling down.",
        "#11151F", ["#242C3B"], ["#54617A"], low_signal,
        {"kind": "scroll", "direction": "down", "speed": 3.0, "cycle": 3.3,
         "note": "The rule set slides down 3 px/s; the 10 px spacing "
                 "repeats every 3.3 s. The scroll is a rigid texture "
                 "shift, not a pulse, so the brief's four-second pulse "
                 "floor does not apply; the speed stays far under the "
                 "10 px/s drift ceiling."},
        ("cut",), 3.8, "small",
        ["Fill the whole canvas with base #11151F.",
         "Inside each loud margin, draw 1 px rules of secondary #242C3B "
         "every 10 rows, in two signal bursts per margin: each burst "
         "spans an uneven x run of 44 to 95 px at a hashed position, "
         "over an uneven band 80 to 149 px tall at a hashed height, so "
         "the runs read as broken transmissions rather than a full "
         "field.",
         "Draw two solid bars of accent #54617A, 3 px tall, inside the "
         "first two loud margins: one 14 px below the margin top and one "
         "40 px above its bottom, spanning the margin width minus 12 px.",
         "Motion: the rule set scrolls down 3 px/s, wrapping at the "
         "margin height, so the pattern repeats every 3.3 s."],
        counts={"signal-bar": 4})

    add(9, "Fracture",
        "Two stepped diagonal fractures in the far margins with sinking "
        "edge ticks.",
        "#10151F", ["#283044"], ["#707D96"], fracture,
        {"kind": "drift", "direction": "down", "speed": 3.0, "cycle": 181.3,
         "note": "The tick layer sinks 3 px/s and wraps at its margin "
                 "span (544 px landscape, 181 s; 802 px portrait, 267 s). "
                 "The 2 px ticks cross the edge sub-perceptually. Both "
                 "fracture staircases hold still."},
        ("fade", 0.5, 15), 4.2, "small",
        ["Fill the whole canvas with base #10151F.",
         "In the left loud margin, draw one stepped diagonal of secondary "
         "#283044 from the margin's top-left toward its bottom-right "
         "corner in 12 equal steps: each step is a rectangle 5 px thick "
         "and one step-width wide (margin width / 12 + 1: 17 px "
         "landscape, 5 px portrait), each step one twelfth of the margin "
         "height below the one before.",
         "In the right loud margin, draw the mirrored staircase from its "
         "top-right toward its bottom-left corner, same sizes.",
         "Each fracture carries a dark offset ledge: a 22 px tall "
         "staircase of the same secondary, shifted 12 px toward the "
         "canvas center.",
         "Beside every step, on both sides of the seam, place one accent "
         "tick of #707D96, 2 x 2 px, 6 to 15 px below the step, only "
         "inside the margin. Motion: the ticks sink 3 px/s and wrap at "
         "the margin span; both staircases hold still."],
        counts={"seam": 24, "ledge": 24})

    add(10, "Circuit hush",
        "Sixteen right-angle circuit paths with square nodes whose cores "
        "breathe.",
        "#111721", ["#283144"], ["#6B8498"], circuit_hush,
        {"kind": "pulse", "direction": "none", "speed": 0.29, "cycle": 7.0,
         "note": "Every third node core swells 2 to 3 px and returns over "
                 "a 7 s cycle, 0.29 px/s at the fastest edge: the "
                 "approved node pulse as a slow size change. The traces "
                 "hold still."},
        ("fade", 0.5, 15), 4.6, "small",
        ["Fill the whole canvas with base #111721.",
         "Lay out 16 paths, eight per side loud margin, one per band of "
         "margin height / 8 starting 8 px into the band. Each path has "
         "two right-angle bends: a horizontal run of secondary #283144, "
         "2 px thick and 24 to 71 px long at a hashed x; a vertical drop "
         "16 to 39 px tall at the run's right end; a second run 8 to 71 px "
         "back toward the margin edge; and a closing drop 14 to 35 px "
         "tall. Sixteen paths draw 64 trace rectangles.",
         "Close every path with a 4 x 4 px square node of #283144 at the "
         "second drop's foot. Every third node carries a lit core of "
         "accent #6B8498, 2 x 2 px swelling to 3 x 3 px.",
         "Motion: the lit cores swell over a 7 s cycle. No traces in the "
         "well, no letters, no arrowheads; broad clear zones behind the "
         "HUD because paths live in the margins only."],
        counts={"trace": 64, "node": 16})

    add(11, "Band aurora",
        "Three broad stepped ribbons rising together, edges cascading.",
        "#131725", ["#283044"], ["#67758D"], band_aurora,
        {"kind": "drift", "direction": "up", "speed": 4.0, "cycle": 161.25,
         "note": "All three ribbons rise 4 px/s as one layer. Each wraps "
                 "over canvas height plus its own extent - the three "
                 "layered bands, their 6 px offsets, and the tallest 16 px "
                 "lift, 101 px landscape and 146 px portrait - so a "
                 "ribbon leaves the top edge fully before re-entering "
                 "below the bottom edge: 161.25 s per pass landscape, "
                 "276.5 s portrait. The concept's 96 s loop would wrap a "
                 "visible ribbon; this contract keeps the rise seamless "
                 "instead."},
        ("fade", 0.6, 18), 5, "bands",
        ["Fill the whole canvas with base #131725.",
         "Draw three full-width ribbons of secondary #283044 with base "
         "rows at round(height * 0.16), round(height * 0.38) and "
         "round(height * 0.60). Each ribbon is three overlapping "
         "staircase bands, each round(height * 0.035) rows tall and "
         "offset 6 px per layer, so the edge cascades and the ribbon "
         "reads as one broad layered curtain (9 bands in all).",
         "Each band's top edge is a staircase of 40 px steps with an "
         "8 px lift (period 160 px); neighboring bands and ribbons "
         "alternate the mirrored phase and sit 2 to 5 px lower, so the "
         "edges cascade in opposing staircase contours.",
         "Add 8 accent ticks of #67758D, 2 x 2 px, at hashed positions "
         "inside the loud margins, rising with the ribbons.",
         "Motion: the ribbon layer rises 4 px/s, wrapping seamlessly as "
         "above."],
        counts={"ribbon": 9})

    add(12, "Long trails",
        "Seventy-two dim stars and twelve staircase meteor trails "
        "drifting as one layer.",
        "#111620", ["#293144"], ["#76849B"], long_trails,
        {"kind": "drift", "direction": "down-left", "speed": 4.0,
         "cycle": 90.7,
         "note": "Stars and trails share one velocity: 2.4 px/s left and "
                 "3.2 px/s down, 4.0 px/s along the path. Elements wrap "
                 "at their margin spans; every element is 3 px or less in "
                 "each axis, so edge crossings are sub-perceptual. The "
                 "narrower portrait spans wrap first: the 39 px bottom "
                 "strip height every 12.2 s and the 59 px side margin "
                 "width every 24.6 s; the landscape height takes 170 s."},
        ("cut",), 5.4, "small",
        ["Fill the whole canvas with base #111620.",
         "Lay a 20 px cell grid inside each loud margin and take the 72 "
         "cells with the lowest hash rank h32(0xC12, ...). Each star is "
         "1 x 1 px in 55 percent of them and 2 x 2 px in 45, colored "
         "secondary #293144 or accent #76849B in equal share.",
         "Draw 12 trails, alternating between the loud margins, each at "
         "a hashed position between 0 and 79 percent across and 0 and 89 "
         "percent down its margin. A trail is a staircase of six steps "
         "of secondary #293144, each 3 x 1 px, stepping 4 px left and "
         "2 px down per segment, so the trail points to the upper right. "
         "Its head is one 2 x 2 px accent square of #76849B above the "
         "first step, dropped when it would leave the margin.",
         "Motion: the whole star-and-trail layer drifts 2.4 px/s left "
         "and 3.2 px/s down (4.0 px/s total) and wraps inside its "
         "margin."],
        counts={"star": 72, "trail-step": 72, "trail-head": 12})

    add(13, "Deep grid",
        "Twenty-two staircase grid lines receding toward a mid-height "
        "horizon, the horizontal set scrolling.",
        "#121722", ["#293145"], ["#79869B"], deep_grid,
        {"kind": "scroll", "direction": "down", "speed": 5.0, "cycle": 108.8,
         "note": "The horizontal rules scroll down 5 px/s as one rigid "
                 "set and wrap at their margin span (544 px landscape, "
                 "108.8 s; 802 px portrait, 160.4 s). Because every rule "
                 "shares one velocity, the spacing pattern translates "
                 "without deforming. The vertical guides and nodes hold "
                 "still."},
        ("fade", 0.6, 18), 5.8, "small",
        ["Fill the whole canvas with base #121722.",
         "Draw 12 horizontal 1 px rules of secondary #293145, six above "
         "and six below the canvas middle row, at offsets 6, 11, 18, 27, "
         "38 and 51 px: the spacing tightens toward the horizon at "
         "mid-height and widens toward the edges. Each rule draws once "
         "inside every tall loud margin (both side strips), so the plate "
         "carries 24 rule bars for the 12 line positions.",
         "Draw 10 vertical guides of the same color, 2 px wide, five "
         "evenly spaced inside each of the first two loud margins, each "
         "in four segments of one quarter of the margin height (40 bars "
         "for the 10 guides), each segment stepping 1 px toward the "
         "canvas center, so the guides converge in staircases.",
         "Close each guide with one 2 x 2 px accent node of #79869B at "
         "its foot. The 22 staircase lines of the concept are these 12 "
         "horizontal rules plus these 10 vertical guides.",
         "Motion: the horizontal rules scroll down 5 px/s and wrap at "
         "the margin span; the guides and nodes hold still."],
        counts={"grid-rule": 24, "grid-guide": 40, "grid-node": 10})

    add(14, "Fine snow",
        "Exactly 160 tiny square particles falling over dark bands.",
        "#131923", ["#293145"], ["#7A879E"], fine_snow,
        {"kind": "drift", "direction": "down", "speed": 6.0, "cycle": 90.7,
         "note": "Every particle falls 6 px/s and wraps at its margin "
                 "span (544 px landscape, 90.7 s; 802 px portrait, 133.7 "
                 "s). Particles are 2 px or less, so edge crossings are "
                 "sub-perceptual. The bands hold still."},
        ("cut",), 6.2, "small",
        ["Fill the whole canvas with base #131923.",
         "Lay a 20 px cell grid inside each loud margin and take the 160 "
         "cells with the lowest hash rank h32(0xE14, ...). Each particle "
         "is 1 x 1 px in 55 percent of them and 2 x 2 px in 45, colored "
         "secondary #293145 or accent #7A879E in equal share.",
         "Draw three full-width bands of secondary #293145 behind the "
         "field, 3.5 percent of the canvas height tall, at 30, 55 and 80 "
         "percent height.",
         "Motion: the particle layer falls 6 px/s and wraps at its margin "
         "span."],
        counts={"flake": 160, "snow-band": 3})

    add(15, "Undertow",
        "Seven interlocking stepped wave bands sliding left as one layer.",
        "#131923", ["#2A3146"], ["#74839B"], undertow,
        {"kind": "scroll", "direction": "left", "speed": 6.0, "cycle": 13.3,
         "note": "The whole band layer slides left 6 px/s. The 80 px "
                 "staircase period repeats every 13.3 s on both "
                 "canvases. Alternate bands carry mirrored phases, so "
                 "the stepped edges read as two opposing currents while "
                 "every rectangle shares one velocity. (The pre-T082 "
                 "generator advanced its offset positively, sliding the "
                 "staircase right against this note and the declared "
                 "velocity; the corrected generator and the runtime of "
                 "task T082 draw the leftward slide.)"},
        ("fade", 0.6, 18), 6.6, "bands",
        ["Fill the whole canvas with base #131923.",
         "Draw seven full-width stepped bands of secondary #2A3146, "
         "round(height * 0.07) rows tall, at base rows round(height * "
         "(0.28 + 0.09 * i)) for i = 0..6.",
         "Each band's top edge is a staircase of 20 px steps with a 6 px "
         "lift (period 80 px); alternate bands carry the mirrored phase "
         "and each sits 3 px lower per index.",
         "On every band, scatter two foam ticks of accent #74839B, "
         "2 x 1 px, at hashed positions inside each loud margin, 5 to "
         "14 px above the band edge. The foam holds its margin slots "
         "while the band layer moves.",
         "Motion: the whole band layer slides left 6 px/s, wrapping "
         "every 80 px."],
        counts={"wave-band": 7})

    add(16, "Slow pulse",
        "Nine broken rectangular contours breathing outward around the "
        "scene.",
        "#141A26", ["#2A3146"], ["#7C88A0"], slow_pulse,
        {"kind": "pulse", "direction": "out", "speed": 6.0, "cycle": 8.0,
         "note": "Every contour swells 24 px outward from the canvas "
                 "edges and returns over an 8 s cycle: 48 px of travel "
                 "in 8 s, 6 px/s at the fastest edge, exactly the "
                 "approved pulse."},
        ("fade", 0.6, 18), 7, "small",
        ["Fill the whole canvas with base #141A26.",
         "Draw nine nested rectangular contours of secondary #2A3146, "
         "2 px thick, inset 10, 36, 62, 88, 114, 140, 166, 192 and "
         "218 px from the canvas edges.",
         "Each contour is broken: each of its four bars is split once by "
         "a 24 px opening at a hashed offset at least 8 px inside the "
         "bar, so no contour is a closed ring. Nine contours of eight "
         "bars draw 72 bars in all.",
         "On the outermost three contours, draw one 2 x 2 px accent tip "
         "of #7C88A0 at each corner that lies inside a loud margin.",
         "Motion: every contour swells 24 px outward and returns over an "
         "8 s cycle. Rectangular pressure only; never circular, never "
         "neon."],
        counts={"contour-bar": 72})

    add(17, "Rising ember",
        "Exactly 190 warm-gray embers rising past stepped block columns.",
        "#17191F", ["#2A3040"], ["#897A72"], rising_ember,
        {"kind": "drift", "direction": "up", "speed": 7.0, "cycle": 77.7,
         "note": "Every ember rises 7 px/s and wraps at its margin span "
                 "(544 px landscape, 77.7 s; 802 px portrait, 114.6 s). "
                 "Embers are 2 px or less, so edge crossings are "
                 "sub-perceptual. The columns hold still."},
        ("cut",), 7.7, "small",
        ["Fill the whole canvas with base #17191F.",
         "Lay an 18 px cell grid inside each loud margin and take the "
         "190 cells with the lowest hash rank h32(0xE17, ...). Each "
         "ember is 1 x 2 px in half of them and 2 x 2 px in the other "
         "half, at a hashed jitter, all in the warm-gray accent #897A72: "
         "muted warmth lives in the accent pixels only, never in a wash.",
         "Along the lower fifth of each side loud margin, draw stepped "
         "block columns of secondary #2A3040: blocks 12 to 31 px wide "
         "and 10 px tall, end to end with 8 px gaps, stacked every 34 px "
         "up from the bottom edge, each column carrying a half-width "
         "6 px step 6 px above each block. In the portrait bottom "
         "margin, one row of the same blocks lies flat.",
         "Motion: the embers rise 7 px/s and wrap at their margin span; "
         "the columns hold still."],
        counts={"ember": 190})

    add(18, "Storm lines",
        "Two hundred rain dashes and four stepped trails falling as one "
        "layer.",
        "#131925", ["#2A3146"], ["#8290A7"], storm_lines,
        {"kind": "drift", "direction": "down", "speed": 8.0, "cycle": 68.0,
         "note": "Every dash and trail falls 8 px/s. Dashes and trail "
                 "steps alike wrap over margin height plus 6 px - the "
                 "tallest dash's extent, which keeps every element over "
                 "4 px on its fall axis leaving fully before it re-enters "
                 "- while the 2 px dash width keeps the horizontal edge "
                 "sub-perceptual. (The pre-T083 note named the bare "
                 "margin span for the dashes and the extent for the "
                 "trails alone; the generator always wrapped both over "
                 "the span plus 6, and the runtime of task T083 draws "
                 "that wrap.)"},
        ("cut",), 8.4, "small",
        ["Fill the whole canvas with base #131925.",
         "Lay a 16 px cell grid inside each loud margin and take the 200 "
         "cells with the lowest hash rank h32(0x518, ...). Each dash is "
         "2 px wide and 4 px tall in 65 percent of them and 2 x 6 px in "
         "35, at a hashed jitter, colored secondary #2A3146 or accent "
         "#8290A7 in equal share.",
         "Draw four longer stepped trails, cycling through the loud "
         "margins, each at 50 percent across and 8, 38, 62 and 86 "
         "percent down its margin: a staircase of five segments of "
         "secondary #2A3146, each 4 x 2 px, stepping 4 px left and 2 px "
         "down per segment.",
         "Motion: every dash and trail falls 8 px/s and wraps as above. "
         "Dim slate blue; no lightning, no glow."],
        counts={"dash": 200})

    add(19, "Compression",
        "Thirty broad broken bands pressing inward from all four edges.",
        "#171C28", ["#2A3146"], ["#8591A8"], compression,
        {"kind": "pulse", "direction": "in", "speed": 9.0, "cycle": 12.0,
         "note": "The whole nest contracts 54 px onto the board and "
                 "returns over a 12 s seamless cycle: 108 px of travel "
                 "in 12 s, 9 px/s at the fastest edge, exactly the "
                 "approved pulse. A linear inward scroll would teleport "
                 "each band back to its outer edge when it wrapped; the "
                 "triangle pulse keeps the motion continuous."},
        ("fade", 0.5, 15), 9.2, "small",
        ["Fill the whole canvas with base #171C28.",
         "Draw 30 nested broken rectangular bands of secondary #2A3146, "
         "3 px thick, starting 8 px from the canvas edges and stepping "
         "inward by max(5, 13 - index / 3) px per band, so the spacing "
         "tightens from 13 px outside to 5 px near the board: insets 8, "
         "21, 34, 47, 59, 71, 83, 94, 105, 116, 126, 136, 146, 155, "
         "164, 173, 181, 189, 197, 204, 211, 218, 224, 230, 236, 241, "
         "246, 251, 256 and 261 px.",
         "Each band is broken: each of its four bars is split once by a "
         "20 px opening at a hashed offset at least 8 px inside the bar. "
         "The innermost band's two short cross-axis bars (22 px) stay "
         "whole, so the nest draws 238 bars. Bars that would cross the "
         "canvas edges clip there.",
         "On the two outermost bands, lay scarce accent ticks of "
         "#8591A8, 2 x 2 px every 32 px along the horizontal bar pieces "
         "that pass through loud margins.",
         "Motion: every band presses 54 px inward and returns over a "
         "12 s cycle. The board stays the clear center of attention."],
        counts={"press-bar": 238})

    add(20, "Collapse",
        "Two hundred eighty tiny debris fragments falling past stepped "
        "slabs at the far edges.",
        "#191E2B", ["#2A3146"], ["#8A94AC"], collapse,
        {"kind": "drift", "direction": "down", "speed": 10.0, "cycle": 80.2,
         "note": "Every fragment falls 10 px/s and wraps over margin "
                 "span plus its own height, so fragments leave and enter "
                 "fully off-canvas; the portrait side margin wraps after "
                 "802 px plus extent (80.2 s or more). The slabs hold "
                 "still."},
        ("fade", 0.6, 18), 10, "small",
        ["Fill the whole canvas with base #191E2B.",
         "Lay a 16 px cell grid inside each loud margin. Rank the cells "
         "by outer-edge closeness first and hash second, and take the "
         "first 280: the debris piles against the far canvas edges and "
         "thins toward a clear dark corridor beside the board.",
         "Each fragment is 1 x 3 px in 35 percent of the cells, 2 x 5 px "
         "in 35 and 3 x 7 px in 30, at a hashed jitter inside its cell. "
         "Two thirds draw in secondary #2A3146 and one third in accent "
         "#8A94AC. A fragment that would leave its margin is skipped "
         "and the next candidate takes its place, so the count is exact.",
         "In each loud margin draw three stepped slabs of secondary "
         "#2A3146, 32 to 71 px wide and 10 to 19 px tall, at hashed "
         "positions in the upper three quarters, each carrying a "
         "half-width 5 px step on its top.",
         "Motion: the fragment layer falls 10 px/s and wraps over margin "
         "span plus fragment height; the slabs hold still. No tetromino "
         "clusters, no noise texture, no bright flare."],
        counts={"debris": 280})

    return designs


def draw_design(design, zones, t):
    """The elements of one design on one canvas at time `t`."""
    pen = Pen(zones)
    pen.t = t
    design["draw"](pen, t)
    # The opaque well and its frame close every plate: no design element
    # draws inside either, and both paint last.
    frame = zones.frame
    pen.elements.append({
        "id": len(pen.elements), "kind": "rect", "role": "frame",
        "tag": "frame", "color": hex_of(FRAME_COLOR), "x": frame[0],
        "y": frame[1], "w": frame[2], "h": frame[3], "vx": 0.0, "vy": 0.0,
        "wrap": None, "pulse": None, "hollow": True})
    well = zones.well
    pen.elements.append({
        "id": len(pen.elements), "kind": "rect", "role": "well",
        "tag": "well", "color": hex_of(WELL_COLOR), "x": well[0],
        "y": well[1], "w": well[2], "h": well[3], "vx": 0.0, "vy": 0.0,
        "wrap": None, "pulse": None})
    return pen.elements


# ---------------------------------------------------------------------------
# Painting
# ---------------------------------------------------------------------------


def expand(elements, canvas):
    """Paint every element onto `canvas`, which already holds the base.

    The acceptance check re-implements this expansion on its own, so a
    plate and its manifest must agree exactly.
    """
    for element in elements:
        kind = element["kind"]
        if kind == "rect":
            if element.get("hollow"):
                draw_hollow_rect(canvas, element)
            else:
                canvas.rect(element["x"], element["y"], element["w"],
                            element["h"], parse_hex(element["color"]))
        elif kind == "band":
            draw_band(canvas, element)
        elif kind == "hlines":
            draw_hlines(canvas, element)
        elif kind == "checker":
            draw_checker(canvas, element)
        else:
            raise ValueError("unknown element kind %r" % kind)


def draw_hollow_rect(canvas, element):
    """The one-pixel ring of the frame element."""
    color = parse_hex(element["color"])
    x, y, w, h = element["x"], element["y"], element["w"], element["h"]
    canvas.rect(x, y, w, 1, color)
    canvas.rect(x, y + h - 1, w, 1, color)
    canvas.rect(x, y, 1, h, color)
    canvas.rect(x + w - 1, y, 1, h, color)


def band_segments(element):
    """The staircase rectangles of one band: steps of `step_w` whose lift
    runs 0, 1, 2, 1 times `step_h`, scrolled by `offset` px. A `phase` of
    2 rotates the lift to 2, 1, 0, 1, the mirrored contour."""
    step_w = element["step_w"]
    step_h = element["step_h"]
    phase = element.get("phase", 0)
    x = element["x"]
    end = element["x"] + element["w"]
    offset = element["offset"]
    first = (x - offset) // step_w - 1
    last = (end - offset) // step_w + 1
    for k in range(first, last + 1):
        left = x if k == first else max(x, k * step_w + offset)
        right = end if k == last else min(end, (k + 1) * step_w + offset)
        if right <= left:
            continue
        lift = STEP_LIFT[(k + phase) % 4] * step_h
        yield (left, element["y"] - lift + element["drop"],
               right - left, element["h"] + lift)


def draw_band(canvas, element):
    color = parse_hex(element["color"])
    for rect in band_segments(element):
        canvas.rect(*rect, color)


def draw_hlines(canvas, element):
    color = parse_hex(element["color"])
    span = element["h"]
    spacing = element["spacing"]
    for index in range(span // spacing):
        y = element["y"] + (index * spacing + element["offset_y"]) % span
        canvas.rect(element["x"], y, element["w"], element["thickness"],
                    color)


def draw_checker(canvas, element):
    """One ordered-dither field: base cells stay, ink cells fill."""
    base, ink = (parse_hex(value) for value in element["colors"])
    cell = element["cell"]
    densities = element["densities"]
    for row in range(len(densities)):
        y = element["y"] + row * cell
        for column in range(element["w"] // cell):
            x = element["x"] + column * cell
            index_x = (x + element["offset_x"]) // cell
            threshold = BAYER[(y // cell % 4) * 4 + (index_x % 4)]
            canvas.rect(x, y, cell, cell,
                        ink if threshold < densities[row] else base)
