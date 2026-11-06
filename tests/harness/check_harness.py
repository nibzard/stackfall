#!/usr/bin/env python3
"""Independent checker for the unattended SDL harness (T023, T041, T045).

Reads one harness run directory and decides pass or fail from evidence:

  * every checkpoint record the game binary wrote must equal the record the
    headless reference replay computed for the same script, field by field
    (the SDL event path, the loop wiring, and the developer hooks together
    must reproduce the rules core exactly);
  * the observations must also satisfy expectations derived from
    `docs/SPEC.md` alone - scene sequences, the section 4.6 scoring table,
    hard-drop and soft-drop points per cell, the pause freeze, hold
    restriction, and the line-clear counts - so a defect shared by the game
    and the reference still fails here;
  * every checkpoint screenshot must exist and be a 960x544 BMP;
  * the options-ghost session's well scan reads its BMP with no image
    library and proves from pixels that the ghost outline stopped drawing
    when the GHOST row was turned off, while its save directory holds the
    three-line settings file that toggle wrote;
  * the options-expansion sessions of task T045 add their own pixel and file
    proofs: the options page rows fit the canvas with the marker on the
    highlighted row, the staged relaunch session pins the exact canonical
    settings bytes after every one-preference change and the values the next
    process restored, the legacy stages pin the three-line and two-line files
    byte for byte (the first upgraded by its next save, the second left
    untouched by a read), the hold-off sessions prove every mapped hold input
    changes nothing and spends no lock while the Hold group draws nothing and
    that HOLD on keeps the once-per-piece rule, the five next-count stages
    prove exactly their count of previews in queue order at one shared seed
    with byte-identical records, and the grayscale round proves every piece
    pixel of the play scene gray while the text, the board outline, and the
    ghost keep their readability, with the color round as the control;
  * the record-abandon-save session of task T052 proves the mid-round record
    persistence of SPEC sections 4.7 and 9: a round that scores through two
    hard drops and leaves through QUIT TO TITLE never reaches a game over,
    yet the record file holds the abandoned round's score, the input log
    carries each mid-round save with a tick that precedes the abandon, and
    the title after the abandon shows the persisted record while the
    abandoned round's own counters reset;
  * the staged portrait sessions of tasks T055 and T068 prove the turned mode
    end to end: a landscape stage selects the ORIENTATION row with a
    direction held across the press and the session turns at once, so the
    same process draws the portrait options page, plays a round on the
    rotated table, turns back to landscape and to portrait again, and leaves
    the exact portrait settings bytes; the per-tick input log proves the turn
    spent the select edge, the held level of the probe, and every pending
    edge, and neither log carries next-launch wording any more; the portrait
    relaunch applies the 544x960 canvas with 40 px cells, and from pixels the
    rotated board frame, the HUD strip, and the quiet margins prove the
    composition while the ordered action bits of the per-tick input log pin
    the rotated mapping for the keyboard and for the pad;
  * the music sessions of task T062 prove the music settings page behind
    the eighth options row: the page opens on its MUSIC row, one select per
    row changes the value and never the row, back returns to the options
    screen on the same row, the four-row page layout holds from pixels, and
    the saved bytes and input-log lines carry the changed mode, the
    volumes, and the Next track action;
  * the backgrounds sessions of task T084 prove the backgrounds page behind
    the ninth options row and its saved pair: the keyboard session walks the
    MODE ring Auto/Off/Fixed and every stable DESIGN ID 1..20 with the wrap,
    the pad session proves the same-session application - a round after Off
    draws the legacy flat wallpaper and a round after Fixed the retained
    design - and the relaunch stages cover the saved Fixed pair in portrait,
    the corrupted backgroundid=99 file that recovers to the defaults without
    rewriting a byte, and the legacy eleven-line file that upgrades on its
    next save;
  * the piece-designs sessions of task T095 prove the PIECE DESIGNS row, the
    tenth of the options screen: the keyboard and pad sessions toggle and
    save the setting on both input paths, and over the labeled all-pieces
    fixture the checker reads one settled cell of every piece type from
    pixels - the interior holds only the palette fill and dark shade, the
    dark pixels form the piece's mark, and no two pieces share a mark, in
    color and in grayscale, in both orientations, with the plain frame as
    the negative control;
  * each game run must exit 0, log a clean shutdown, and label every fixture
    it used;
  * the high-score file of a session that set a record must hold that record;
  * each session's log.txt must sit in the --save-dir the harness passed,
    beside the record, and not in the environment's save directory.

A session with a `<session>.stages.txt` manifest is checked stage by stage:
every stage's records are compared against that stage's reference stream over
the shared save directory, and the settings snapshots the driver took around
each process are checked as files.

Exit status is nonzero when any check fails. The run directory keeps all
evidence either way.

Usage: check_harness.py RUN_DIR
"""

import os
import re
import struct
import sys

CANVAS = (960, 544)
CLEAR_POINTS = {0: 0, 1: 100, 2: 300, 3: 500, 4: 800}
PIECE_LETTERS = "IOTSZJL"

# The spawn drawing of each piece in matrix-local cells and the spawn origin
# of the matrix corner, straight from specification section 4.2 with the
# immediate-spawn rule of task T029: origin column 3 for every piece, origin
# row -1 for the I (its first occupied row is local row 1) and 0 for the
# others, so the topmost occupied cell of every spawn sits in visible row 0.
SPAWN_CELLS = {
    "I": [(0, 1), (1, 1), (2, 1), (3, 1)],
    "O": [(1, 0), (2, 0), (1, 1), (2, 1)],
    "T": [(1, 0), (0, 1), (1, 1), (2, 1)],
    "S": [(1, 0), (2, 0), (0, 1), (1, 1)],
    "Z": [(0, 0), (1, 0), (1, 1), (2, 1)],
    "J": [(0, 0), (0, 1), (1, 1), (2, 1)],
    "L": [(2, 0), (0, 1), (1, 1), (2, 1)],
}


def spawn_origin(letter):
    """The matrix corner of a fresh spawn: column 3, row -1 for the I and
    row 0 for every other piece (section 4.2, task T029)."""
    return 3, -1 if letter == "I" else 0


# ---------------------------------------------------------------------------
# The renderer's integer blend, copied from mixChannel/mixToward of
# renderer_sdl.hpp so an expected shade is the shade the game drew. C++
# integer division truncates toward zero; Python's // floors, so negative
# deltas take the explicit truncating path.
# ---------------------------------------------------------------------------

def trunc_div(value, whole):
    quotient = abs(value) // abs(whole)
    return quotient if (value >= 0) == (whole >= 0) else -quotient


def mix_channel(start, target, parts, whole):
    return start + trunc_div((target - start) * parts, whole)


def mix_toward(color, target, parts, whole):
    return tuple(mix_channel(color[i], target[i], parts, whole) for i in range(3))


def piece_fill(letter, grayscale):
    return (GRAYSCALE_FILLS if grayscale else PIECE_FILLS)[letter]


def preview_fill(letter, dimmed, grayscale):
    """The fill a preview cell draws: the palette fill, halved toward the
    canvas ground for every preview after the first (the contrast hierarchy of
    task T037)."""
    fill = piece_fill(letter, grayscale)
    return mix_toward(fill, BACKGROUND_COLOR, 1, 2) if dimmed else fill


def ghost_fill(letter, grayscale):
    """The ghost outline color: the fill blended one quarter toward the well
    (sections 6.2 step 3 and the 3:1 contrast floor of task T031)."""
    return mix_toward(piece_fill(letter, grayscale), WELL_COLOR, 1, 4)

# Board geometry and palette from include/stackfall/renderer_sdl.hpp
# (specification sections 6.1 and 6.2): the well spans x=345..614 and
# y=2..541 in 27 px cells, filled with kBoardWell under kGridLine.
BOARD_ORIGIN = (345, 2)
BOARD_CELL = 27
BOARD_ROWS = 20
BOARD_COLUMNS = 10
WELL_COLOR = (10, 12, 19)
GRID_LINE_COLOR = (26, 30, 42)

# More canvas geometry and palette from include/stackfall/renderer_sdl.hpp,
# for the play-scene pixel checks of the options expansion (task T045): the
# canvas ground, the board frame, the two text colors, the menu accent, the
# preview cell size, the Hold box and the five Next slots of the HUD columns
# (task T037), and the six-row options page of task T044.
BACKGROUND_COLOR = (16, 18, 26)
FRAME_COLOR = (78, 88, 116)
TEXT_COLOR = (232, 236, 246)
TEXT_DIM_COLOR = (138, 148, 172)
ACCENT_COLOR = (36, 204, 216)
CELL_EDGE_COLOR = (8, 9, 14)
PREVIEW_CELL = 15
HOLD_BOX = (196, 30, 120, 60)          # holdBox()
NEXT_SLOT_BOXES = [(644, 35 + 56 * i, 120, 56) for i in range(5)]  # nextSlotBox
# The HOLD heading band above the box, and the whole Hold group - heading plus
# box - which the preference hides when it is off.
HOLD_HEADING_BOX = (192, 4, 128, 14)
HOLD_GROUP_BOX = (192, 4, 128, 86)
# The first statistics field (label over value) of the same column, for the
# proof that grayscale leaves the text its color.
STATS_FIELD_BOX = (192, 108, 128, 38)
OPTIONS_HEADING_BOX = (0, 40, 960, 28)      # kOptionsHeadingY, scale 4
OPTIONS_ROW_BOXES = [(0, 88 + 44 * i, 960, 21) for i in range(10)]
OPTIONS_FOOTER_BOX = (0, 516, 960, 14)      # kOptionsFooterY, hint scale
OPTIONS_BELOW_FOOTER_BOX = (0, 530, 960, 14)
# The backgrounds page behind the BACKGROUNDS row (task T084) shares the
# options metrics over its two rows: MODE and the preferred DESIGN.
BACKGROUNDS_PAGE_ROW_BOXES = OPTIONS_ROW_BOXES[:2]
# The music settings page shares the options metrics (task T062): its four
# rows sit at the top of the same band, and its quiet title line - the
# playing track's name, empty while music is silent - sits between the last
# row and the footer.
MUSIC_PAGE_ROW_BOXES = OPTIONS_ROW_BOXES[:4]
MUSIC_TITLE_BOX = (0, 342, 960, 33)         # kMusicTrackY, menu scale

# The portrait geometry of task T055, from include/stackfall/renderer_sdl.hpp
# and the rotation notes/design-review.md pins, restated in device pixels:
# portrait pixel (x, y) reaches the device canvas at (y, 543 - x), so the
# portrait frame - x=71..472 and y=119..920 around 40 px cells at (72, 120) -
# lands on device x=119..920 and y=71..472, with the HUD strip in device
# columns x=0..118 and the below-board quiet rows in x=921..959.
PORTRAIT_FRAME = (119, 71, 802, 402)
PORTRAIT_STRIP_BOX = (0, 0, 119, 544)
# The solid panel face the overlays draw, mixToward(BACKGROUND, FRAME, 2, 5),
# and the device boxes of the portrait pause and game-over panels: portrait
# {72, 280, 400, 360} and {32, 200, 480, 440} through the rotation.
PANEL_FILL_COLOR = mix_toward(BACKGROUND_COLOR, FRAME_COLOR, 2, 5)
PORTRAIT_PAUSE_PANEL = (280, 72, 360, 400)
PORTRAIT_GAMEOVER_PANEL = (200, 32, 440, 480)
# The portrait rows below the board frame, device columns x=921..959: no
# portrait page - play scene, title, controls, options, or overlay - draws
# there, so the box staying background proves the tall composition clipped
# nothing (task T055).
PORTRAIT_BELOW_BOARD_BOX = (921, 0, 39, 544)
# The portrait options page of the same-session turn (task T068), in device
# pixels: the heading, the rows at the shared menu metrics, and the
# footer hint all turn into vertical bands, because a portrait rect (x, y, w,
# h) reaches the device canvas at (y, 543 - x, h, w). Row 6 is ORIENTATION,
# so its band carries the bright text and the accent marker exactly while the
# turned session has that row selected.
PORTRAIT_OPTIONS_HEADING_BOX = (64, 0, 28, 544)    # kPOptionsHeadingY, scale 4
PORTRAIT_OPTIONS_FOOTER_BOX = (880, 0, 14, 544)    # kPOptionsFooterY, hint
PORTRAIT_OPTIONS_MENU_X = 200                      # kPOptionsMenuY in portrait
PORTRAIT_OPTIONS_ROW_HEIGHT = 44                   # kMenuItemHeight
PORTRAIT_OPTIONS_ROW_TEXT_HEIGHT = 21              # kGlyphRows * kMenuScale

# The seven piece fills of the color palette and of the grayscale palette, in
# PieceId order I, O, T, S, Z, J, L (renderer_sdl.hpp). The checker derives
# every other shade from them with the renderer's own integer blend, so an
# exact pixel comparison pins the palette a frame really drew.
PIECE_FILLS = {
    "I": (36, 204, 216),
    "O": (248, 200, 64),
    "T": (176, 112, 224),
    "S": (72, 208, 104),
    "Z": (232, 84, 96),
    "J": (88, 128, 240),
    "L": (244, 148, 60),
}
GRAYSCALE_FILLS = {
    "I": (205, 205, 205),
    "O": (235, 235, 235),
    "T": (150, 150, 150),
    "S": (190, 190, 190),
    "Z": (140, 140, 140),
    "J": (130, 130, 130),
    "L": (170, 170, 170),
}
# The widest channel spread a gray piece pixel can show: the gray fills are
# achromatic and only the ghost's quarter blend toward the well leaves up to
# three channels of spread (J reaches (100, 101, 103)).
GRAYSCALE_CHANNEL_TOLERANCE = 3
# Any colored fill of the seven-piece palette spreads far more than this, so
# the color frame's positive control cannot pass on a gray pixel.
COLOR_CHANNEL_FLOOR = 20

failures = []
passes = []


def fail(rule, detail):
    failures.append("%s: %s" % (rule, detail))
    print("FAIL %s: %s" % (rule, detail))


def good(rule, detail=""):
    passes.append(rule)
    print("pass %s%s" % (rule, (" " + detail) if detail else ""))


def read_records(path):
    """Checkpoint records as an ordered list of field dicts."""
    records = []
    with open(path) as handle:
        for line in handle:
            line = line.strip()
            if not line.startswith("check="):
                continue
            fields = {}
            for token in line.split():
                if "=" in token:
                    key, value = token.split("=", 1)
                    fields[key] = value
            records.append(fields)
    return records


def strip_shot(record):
    """A copy without the shot= field, which only the game binary fills."""
    copy = dict(record)
    copy.pop("shot", None)
    return copy


def strip_storage_fields(record):
    """A copy without the shot path or the two storage-dependent fields:
    `high` names the record the process loaded and raised, and `record` the
    flag the round's end compared against it, so both depend on which
    record the save directory held, not on the round itself. Streams from
    processes that loaded different records compare through this and prove
    their storage fields against their own reference or their own pins
    instead (tasks T045 and T052)."""
    copy = strip_shot(record)
    copy.pop("high", None)
    copy.pop("record", None)
    return copy


def check_bmp(path, rule):
    if not os.path.isfile(path):
        fail(rule, "missing screenshot %s" % path)
        return
    with open(path, "rb") as handle:
        header = handle.read(34)
    if len(header) < 34 or header[:2] != b"BM":
        fail(rule, "%s is not a BMP" % path)
        return
    width, height = struct.unpack("<ii", header[18:26])
    depth = struct.unpack("<H", header[28:30])[0]
    if (width, height) != CANVAS:
        fail(rule, "%s is %dx%d, expected %dx%d" %
             (path, width, height, CANVAS[0], CANVAS[1]))
        return
    if depth not in (8, 24, 32):
        fail(rule, "%s has %d bits per pixel" % (path, depth))
        return
    good(rule, os.path.basename(path))


def decode_rle8(data, offset, width, height, rule, path):
    """The pixel indexes of one BI_RLE8 stream, as a `width x height`
    bytearray whose first row is the image's top row.

    The game binary's palettized writer (task T054) emits encoded pairs,
    absolute runs padded to a 16-bit boundary, the end-of-line escape per
    row, the end-of-bitmap escape last, and no deltas; every form the BMP
    format defines is still decoded here, so a stray delta or an overrun
    fails the read instead of passing silently."""
    indexes = bytearray(width * height)
    pos = offset
    x = 0
    row = height - 1  # the format stores rows bottom-up
    while True:
        if pos + 2 > len(data):
            fail(rule, "%s has a truncated RLE8 stream" % path)
            return None
        count, op = data[pos], data[pos + 1]
        pos += 2
        if count != 0:
            if x + count > width or row < 0:
                fail(rule, "%s has an RLE8 run past the row or canvas edge" %
                     path)
                return None
            start = row * width + x
            indexes[start:start + count] = bytes([op]) * count
            x += count
            continue
        if op == 0:  # end of line
            row -= 1
            x = 0
        elif op == 1:  # end of bitmap
            return indexes
        elif op == 2:  # delta: move the position right and up
            if pos + 2 > len(data):
                fail(rule, "%s has a truncated RLE8 delta" % path)
                return None
            x += data[pos]
            row -= data[pos + 1]
            pos += 2
            if x > width or row < 0:
                fail(rule, "%s has an RLE8 delta off the canvas" % path)
                return None
        elif op < 3:
            fail(rule, "%s has an absolute RLE8 run below three bytes" % path)
            return None
        else:  # absolute run of `op` literal indexes
            if pos + op > len(data):
                fail(rule, "%s has a truncated absolute RLE8 run" % path)
                return None
            if x + op > width or row < 0:
                fail(rule, "%s has an absolute run past the row edge" % path)
                return None
            start = row * width + x
            indexes[start:start + op] = data[pos:pos + op]
            pos += op
            pos += op % 2  # the format pads an odd run to a 16-bit boundary
            x += op


def read_bmp_pixels(path, rule):
    """One BMP as a (width, height, pixel) triple, read with no image library.

    Covers what the checkpoint writer produces: 8-bit palettized frames,
    compressed with BI_RLE8 or stored uncompressed, and the 24- or 32-bit
    uncompressed form of frames with too many colors. `pixel(x, y)` takes
    canvas coordinates and returns an RGB tuple.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if len(data) < 54 or data[:2] != b"BM":
        fail(rule, "%s is not a BMP" % path)
        return None
    offset = struct.unpack("<I", data[10:14])[0]
    width, raw_height = struct.unpack("<ii", data[18:26])
    depth = struct.unpack("<H", data[28:30])[0]
    compression = struct.unpack("<I", data[30:34])[0]
    if depth != 8 and depth != 24 and depth != 32:
        fail(rule, "%s has %d bits per pixel" % (path, depth))
        return None
    if width <= 0 or raw_height == 0:
        fail(rule, "%s is %dx%d" % (path, width, raw_height))
        return None
    height = abs(raw_height)
    if depth == 8:
        return read_bmp_pixels8(path, data, offset, width, height,
                                raw_height, compression, rule)
    if compression != 0:
        fail(rule, "%s has compression %d" % (path, compression))
        return None
    bytes_per_pixel = depth // 8
    stride = (width * bytes_per_pixel + 3) // 4 * 4
    if len(data) < offset + stride * height:
        fail(rule, "%s is truncated" % path)
        return None

    def pixel(x, y):
        row = y if raw_height < 0 else height - 1 - y  # top-down or bottom-up
        base = offset + row * stride + x * bytes_per_pixel
        return data[base + 2], data[base + 1], data[base]  # BGR(A) order

    return width, height, pixel


def read_bmp_pixels8(path, data, offset, width, height, raw_height,
                     compression, rule):
    """The palettized half of read_bmp_pixels: one palette lookup per pixel
    over the BI_RLE8 stream or the uncompressed 8-bit rows."""
    colors_used = struct.unpack("<I", data[46:50])[0]
    if colors_used == 0:
        colors_used = 256
    palette_offset = 14 + 40
    if len(data) < palette_offset + colors_used * 4:
        fail(rule, "%s has a truncated palette" % path)
        return None
    palette = []
    for index in range(colors_used):
        entry = palette_offset + index * 4
        palette.append((data[entry + 2], data[entry + 1], data[entry]))
    if compression == 1:  # BI_RLE8
        indexes = decode_rle8(data, offset, width, height, rule, path)
        if indexes is None:
            return None
    elif compression == 0:  # BI_RGB
        stride = (width + 3) // 4 * 4
        if len(data) < offset + stride * height:
            fail(rule, "%s is truncated" % path)
            return None
        indexes = bytearray(width * height)
        for y in range(height):
            source = offset + y * stride
            target = (y if raw_height < 0 else height - 1 - y) * width
            indexes[target:target + width] = data[source:source + width]
    else:
        fail(rule, "%s has 8-bit compression %d" % (path, compression))
        return None

    # Both branches fill `indexes` with the image's top row first, so a
    # canvas y is the buffer row directly.
    def pixel(x, y):
        return palette[indexes[y * width + x]]

    return width, height, pixel


def number(record, field):
    return int(record[field])


def piece_cells(record, field):
    """The `id,rotation,x,y` of one piece field, or None."""
    value = record.get(field, "none")
    if value == "none":
        return None
    letter, rotation, x, y = value.split(",")
    return letter, int(rotation), int(x), int(y)


def delta_check(name, before, after, expect, rule):
    """Assert one numeric difference; report both records on mismatch."""
    got = number(after, name) - number(before, name)
    if got != expect:
        fail(rule, "%s delta is %d, expected %d (%s -> %s)" %
             (name, got, expect, before["check"], after["check"]))
    else:
        good(rule, "%d" % got)


def check_scene_sequence(records, expected, rule):
    got = [record["scene"] for record in records]
    if got != expected:
        fail(rule, "scene sequence %s, expected %s" % (got, expected))
    else:
        good(rule, "%d records" % len(records))


def check_field(record, field, expected, rule):
    got = record.get(field)
    if got != expected:
        fail(rule, "%s at %s is %s, expected %s" %
             (field, record["check"], got, expected))
    else:
        good(rule, "%s=%s" % (field, expected))


def compare_streams(session, run_records, ref_records):
    rule = "%s/reference-stream" % session
    if len(run_records) != len(ref_records):
        fail(rule, "%d checkpoint records, reference has %d" %
             (len(run_records), len(ref_records)))
        return False
    for run, ref in zip(run_records, ref_records):
        if run.get("check") != ref.get("check"):
            fail(rule, "checkpoint order differs: %s vs %s" %
                 (run.get("check"), ref.get("check")))
            return False
    for run, ref in zip(run_records, ref_records):
        a, b = strip_shot(run), strip_shot(ref)
        if a != b:
            differing = [k for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]
            fail(rule, "checkpoint %s differs in %s:\n  run %s\n  ref %s" %
                 (ref.get("check"), differing,
                  " ".join("%s=%s" % (k, a.get(k)) for k in differing),
                  " ".join("%s=%s" % (k, b.get(k)) for k in differing)))
            return False
    good(rule, "%d identical records" % len(run_records))
    return True


def check_screenshots(session, records, shots_dir):
    for record in records:
        shot = record.get("shot", "none")
        if shot == "none":
            continue
        check_bmp(shot, "%s/shot:%s" % (session, record["check"]))


def check_log(session, log_path, must_contain, name="log"):
    rule = "%s/%s" % (session, name)
    if not os.path.isfile(log_path):
        fail(rule, "missing log %s" % log_path)
        return
    with open(log_path) as handle:
        text = handle.read()
    for needle in must_contain:
        if needle not in text:
            fail(rule, "log lacks %r" % needle)
            return
    good(rule, "%d required lines present" % len(must_contain))


def hard_drop_arithmetic(session, records, before_name, after_name, rule):
    """SPEC section 4.6: a hard drop scores 2 per cell travelled plus the
    clear table for the lines it completed, at the pre-clear level."""
    by_name = {record["check"]: record for record in records}
    before, after = by_name.get(before_name), by_name.get(after_name)
    if before is None or after is None:
        fail(rule, "missing %s or %s" % (before_name, after_name))
        return
    piece = piece_cells(before, "active")
    ghost = piece_cells(before, "ghost")
    if piece is None or ghost is None:
        fail(rule, "no active piece at %s" % before_name)
        return
    distance = ghost[3] - piece[3]
    lines_cleared = number(after, "lines") - number(before, "lines")
    level = number(before, "level")
    expected = 2 * distance + CLEAR_POINTS.get(lines_cleared, -1) * level
    delta_check("score", before, after, expected, rule)
    if lines_cleared not in CLEAR_POINTS:
        fail(rule, "impossible clear count %d" % lines_cleared)


def soft_drop_arithmetic(session, records, before_name, after_name, rule):
    """SPEC section 4.5: soft drop attempts one cell per held tick and scores
    1 per successfully dropped cell."""
    by_name = {record["check"]: record for record in records}
    before, after = by_name.get(before_name), by_name.get(after_name)
    if before is None or after is None:
        fail(rule, "missing %s or %s" % (before_name, after_name))
        return
    fallen = (piece_cells(after, "active")[3] -
              piece_cells(before, "active")[3])
    delta_check("score", before, after, fallen, rule)


def pause_freeze(session, records, first, second, rule):
    """SPEC section 4.5: pause freezes every simulation counter exactly."""
    by_name = {record["check"]: record for record in records}
    a, b = by_name.get(first), by_name.get(second)
    if a is None or b is None:
        fail(rule, "missing %s or %s" % (first, second))
        return
    frozen = ["scene", "tick", "score", "lines", "level", "active", "ghost",
              "hold", "queue", "board", "settled"]
    differing = [k for k in frozen if a.get(k) != b.get(k)]
    if differing:
        fail(rule, "state moved while paused: %s" % differing)
    else:
        good(rule, "all counters frozen")


def hold_restriction(session, records, before, after, rule):
    """SPEC section 4.3: hold runs once per locked piece; the second press
    changes nothing."""
    by_name = {record["check"]: record for record in records}
    a, b = by_name.get(before), by_name.get(after)
    if a is None or b is None:
        fail(rule, "missing %s or %s" % (before, after))
        return
    watched = ["active", "hold", "queue", "board", "settled", "score",
               "lines", "level"]
    differing = [k for k in watched if a.get(k) != b.get(k)]
    if differing:
        fail(rule, "the second hold changed %s" % differing)
    else:
        good(rule, "second hold is a no-op")


def check_high_score_file(session, save_dir, expected, rule):
    path = os.path.join(save_dir, "highscore.dat")
    if not os.path.isfile(path):
        fail(rule, "missing %s" % path)
        return
    with open(path) as handle:
        text = handle.read()
    wanted = "version=1\nhigh_score=%d\n" % expected
    if text != wanted:
        fail(rule, "%s holds %r, expected %r" % (path, text, wanted))
    else:
        good(rule, "high_score=%d" % expected)


def settings_file_bytes(sound_on, ghost_on, grid_on, hold_on, next_previews,
                        grayscale_on, orientation="landscape",
                        music_mode="auto", music_volume=6, effects_volume=8,
                        background_mode="auto", background_id=1,
                        piece_designs=False):
    """The canonical settings file, exactly as `formatSettingsFile` renders it
    (specification sections 6.3 and 9): the sound and ghost lines of the
    earlier eras, the grid, hold, next, and grayscale lines of task T044, the
    orientation line of task T055, the music mode and volume lines of task
    T062 (MUSIC.md persistence), the background mode and preferred design
    lines of task T084, and the piece designs line of task T095."""
    return ("version=1\nsound=%s\nghost=%s\ngrid=%s\nhold=%s\nnext=%d\n"
            "grayscale=%s\norientation=%s\nmusic=%s\nmusicvolume=%d\n"
            "effectsvolume=%d\nbackground=%s\nbackgroundid=%d\n"
            "piecedesigns=%s\n" % (
                "on" if sound_on else "off", "on" if ghost_on else "off",
                "on" if grid_on else "off", "on" if hold_on else "off",
                next_previews, "on" if grayscale_on else "off", orientation,
                music_mode, music_volume, effects_volume, background_mode,
                background_id, "on" if piece_designs else "off"))


def check_settings_path(session, path, sound_on, ghost_on, grid_on, hold_on,
                        next_previews, grayscale_on, rule,
                        orientation="landscape", music_mode="auto",
                        music_volume=6, effects_volume=8,
                        background_mode="auto", background_id=1,
                        piece_designs=False):
    """One settings file against the canonical rendering of thirteen
    values."""
    if not os.path.isfile(path):
        fail(rule, "missing %s" % path)
        return
    with open(path) as handle:
        text = handle.read()
    wanted = settings_file_bytes(sound_on, ghost_on, grid_on, hold_on,
                                 next_previews, grayscale_on, orientation,
                                 music_mode, music_volume, effects_volume,
                                 background_mode, background_id,
                                 piece_designs)
    if text != wanted:
        fail(rule, "%s holds %r, expected %r" % (path, text, wanted))
    else:
        good(rule, "%s: sound=%s ghost=%s grid=%s hold=%s next=%d grayscale=%s "
             "orientation=%s music=%s volumes=%d/%d background=%s/%d "
             "designs=%s"
             % (os.path.basename(os.path.dirname(path)),
                "on" if sound_on else "off", "on" if ghost_on else "off",
                "on" if grid_on else "off", "on" if hold_on else "off",
                next_previews, "on" if grayscale_on else "off", orientation,
                music_mode, music_volume, effects_volume, background_mode,
                background_id, "on" if piece_designs else "off"))


def check_settings_file(session, save_dir, sound_on, ghost_on, grid_on,
                        hold_on, next_previews, grayscale_on, rule,
                        orientation="landscape", music_mode="auto",
                        music_volume=6, effects_volume=8,
                        background_mode="auto", background_id=1,
                        piece_designs=False):
    """The settings file of the player preferences in one save directory."""
    check_settings_path(session, os.path.join(save_dir, "settings.dat"),
                        sound_on, ghost_on, grid_on, hold_on, next_previews,
                        grayscale_on, rule, orientation, music_mode,
                        music_volume, effects_volume, background_mode,
                        background_id, piece_designs)


def check_settings_bytes(session, path, wanted, rule):
    """One settings file against exact bytes, for the legacy forms the
    canonical helper cannot render (specification section 9)."""
    if not os.path.isfile(path):
        fail(rule, "missing %s" % path)
        return
    with open(path) as handle:
        text = handle.read()
    if text != wanted:
        fail(rule, "%s holds %r, expected %r" % (path, text, wanted))
    else:
        good(rule, "%d exact bytes" % len(wanted))


def check_visible_spawn(session, record, rule):
    """SPEC section 4.2 with the immediate-spawn rule of task T029: the
    topmost occupied cells of a fresh spawn sit in visible row 0, so the
    renderer has cells to draw on the spawn tick, while the ghost query
    still names the landing row."""
    piece = piece_cells(record, "active")
    ghost = piece_cells(record, "ghost")
    if piece is None or ghost is None:
        fail(rule, "no active piece and ghost at %s" % record["check"])
        return
    letter, rotation, x, y = piece
    if rotation != 0 or (x, y) != spawn_origin(letter):
        fail(rule, "active piece at %s is %s, expected a fresh spawn at %s" %
             (record["check"], piece, spawn_origin(letter)))
        return
    rows = sorted({y + dy for _, dy in SPAWN_CELLS[letter]})
    if min(rows) != 0:
        fail(rule, "the spawned %s at %s occupies rows %s, expected top "
             "row 0 (task T029)" % (letter, record["check"], rows))
        return
    if ghost[3] < 0 or ghost[3] <= piece[3]:
        fail(rule, "ghost at %s is at y=%d, expected a visible row below y=%d" %
             (record["check"], ghost[3], piece[3]))
        return
    good(rule, "%s occupies rows %s, ghost y=%d" % (letter, rows, ghost[3]))


def check_well_below_piece(session, record, expect_ghost, rule):
    """SPEC section 6.2, step 3, from pixels: the ghost outline draws only
    below the live piece, so the well below the live piece's rows must hold
    nothing besides the well fill and the grid lines once the GHOST row is
    off. The companion checkpoint before the toggle runs the same scan while
    the option is on, so a broken reader cannot pass the zero proof."""
    shot = record.get("shot", "none")
    if shot == "none":
        fail(rule, "checkpoint %s has no screenshot" % record["check"])
        return
    piece = piece_cells(record, "active")
    if piece is None:
        fail(rule, "no active piece at %s" % record["check"])
        return
    loaded = read_bmp_pixels(shot, rule)
    if loaded is None:
        return
    width, height, pixel = loaded
    # Every tetromino except the I occupies its origin row and the one below,
    # so two rows under the origin the scan is strictly below the live piece
    # and still covers every row the outline can draw.
    first_row = max(0, min(BOARD_ROWS - 1, piece[3] + 2))
    last_y = BOARD_ORIGIN[1] + BOARD_ROWS * BOARD_CELL
    last_x = BOARD_ORIGIN[0] + BOARD_COLUMNS * BOARD_CELL
    if width < last_x or height < last_y:
        fail(rule, "%s is %dx%d, too small for the well" % (shot, width, height))
        return
    counts = {}
    for y in range(BOARD_ORIGIN[1] + first_row * BOARD_CELL, last_y):
        for x in range(BOARD_ORIGIN[0], last_x):
            color = pixel(x, y)
            if color in (WELL_COLOR, GRID_LINE_COLOR):
                continue
            counts[color] = counts.get(color, 0) + 1
    drawn = sum(counts.values())
    if expect_ghost and drawn == 0:
        fail(rule, "no outline pixels in the well below the piece at %s, so "
             "the zero proof of the toggled frame proves nothing" %
             record["check"])
        return
    if not expect_ghost and drawn:
        detail = ", ".join("%d x %s" % (count, color)
                           for color, count in sorted(counts.items()))
        fail(rule, "%s draws %d well pixels besides the well and grid colors "
             "(%s)" % (record["check"], drawn, detail))
        return
    good(rule, "%d pixels besides the well and grid colors at %s" %
         (drawn, record["check"]))


def check_session_log_in_save_dir(session, save_dir):
    """T033: every harness session passes --save-dir, so its log.txt belongs
    beside the record in that directory, never in the save directory the
    environment would have picked for the player."""
    rule = "%s/save-dir-log" % session
    path = os.path.join(save_dir, "log.txt")
    if not os.path.isfile(path):
        fail(rule, "missing %s" % path)
        return
    with open(path) as handle:
        text = handle.read()
    for needle in ("Log: stdout and ", "Clean shutdown"):
        if needle not in text:
            fail(rule, "%s lacks %r" % (path, needle))
            return
    good(rule, "banner and shutdown beside the record")


# ---------------------------------------------------------------------------
# Pixel checks of the options expansion (task T045). Every one reads its BMP
# through read_bmp_pixels, the dependency-free reader of task T041, and pins
# geometry and shades the renderer itself derives, so a frame that drew the
# wrong palette, the wrong shape, or the wrong count fails here even when the
# record stream is perfect - rendering is the only place these settings show
# up.
# ---------------------------------------------------------------------------

def scan_box(pixel, box, allowed):
    """One rectangle as a {color: count} census of the pixels that are not in
    `allowed`, plus the total pixel count of the box."""
    counts = {}
    total = 0
    for y in range(box[1], box[1] + box[3]):
        for x in range(box[0], box[0] + box[2]):
            total += 1
            color = pixel(x, y)
            if color in allowed:
                continue
            counts[color] = counts.get(color, 0) + 1
    return counts, total


def load_shot(record, rule):
    """The BMP of one checkpoint record, or None after reporting it missing."""
    shot = record.get("shot", "none")
    if shot == "none":
        fail(rule, "checkpoint %s has no screenshot" % record["check"])
        return None
    return read_bmp_pixels(shot, rule)


def preview_cell_boxes(letter, box):
    """The preview-cell squares of `letter` centered in `box`, mirroring
    drawPreviewPiece: the piece's own bounding box is centered and every local
    cell of the 4x4 matrix becomes one preview-cell square. Returns the
    occupied cells and the empty cells inside the same bounding box, which is
    the part of the matrix that distinguishes the seven shapes."""
    cells = SPAWN_CELLS[letter]
    min_x = min(x for x, _ in cells)
    max_x = max(x for x, _ in cells)
    min_y = min(y for _, y in cells)
    max_y = max(y for _, y in cells)
    width = (max_x - min_x + 1) * PREVIEW_CELL
    height = (max_y - min_y + 1) * PREVIEW_CELL
    origin_x = box[0] + (box[2] - width) // 2 - min_x * PREVIEW_CELL
    origin_y = box[1] + (box[3] - height) // 2 - min_y * PREVIEW_CELL

    def square(local_x, local_y):
        return (origin_x + local_x * PREVIEW_CELL, origin_y + local_y * PREVIEW_CELL)

    occupied = [square(x, y) for x, y in cells]
    empty = [square(x, y) for x in range(min_x, max_x + 1)
             for y in range(min_y, max_y + 1) if (x, y) not in cells]
    return occupied, empty


def check_preview_shape(session, record, letter, box, dimmed, grayscale, rule):
    """One preview box from pixels: every occupied local cell of `letter`
    draws its palette fill in its own square, and every empty cell inside the
    piece's bounding box draws nothing, so the shape and the shade are both
    the renderer's own."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    occupied, empty = preview_cell_boxes(letter, box)
    fill = preview_fill(letter, dimmed, grayscale)
    center = PREVIEW_CELL // 2
    bad = 0
    for x, y in occupied:
        if pixel(x + center, y + center) != fill:
            bad += 1
    if bad:
        fail(rule, "%s of %d occupied cells of %s in %s is not the %s fill %s"
             % (bad, len(occupied), letter, record["check"],
                "gray" if grayscale else "color", fill))
        return
    bad = 0
    for x, y in empty:
        if pixel(x + center, y + center) != BACKGROUND_COLOR:
            bad += 1
    if bad:
        fail(rule, "%s of %d empty cells of %s in %s draws a pixel, so the "
             "shape is not %s" % (bad, len(empty), letter, record["check"], letter))
        return
    good(rule, "%s draws %d cells in the %s fill at %s" %
         (letter, len(occupied), "gray" if grayscale else "color",
          record["check"]))


def check_next_previews(session, record, count, grayscale, rule):
    """SPEC section 6.2 step 5 from pixels: exactly `count` Next slots draw a
    preview, each in queue order - slot i holds the shape and shade of
    queue[i], the first at full color and the rest dimmed - and every slot
    beyond the count draws nothing at all."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    width, height, pixel = loaded
    queue = record["queue"].split(",")
    if len(queue) < 5:
        fail(rule, "record %s carries %d queue ids" % (record["check"], len(queue)))
        return
    for index, box in enumerate(NEXT_SLOT_BOXES):
        if box[0] + box[2] > width or box[1] + box[3] > height:
            fail(rule, "%s is %dx%d, too small for Next slot %d" %
                 (record.get("shot"), width, height, index))
            return
        if index < count:
            check_preview_shape(session, record, queue[index], box, index > 0,
                                grayscale, rule)
            continue
        counts, _ = scan_box(pixel, box, (BACKGROUND_COLOR,))
        if counts:
            detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(counts.items()))
            fail(rule, "Next slot %d beyond the count of %d draws %d pixels "
                 "(%s) at %s" % (index, count, sum(counts.values()), detail,
                                 record["check"]))
            return
    good(rule, "slots %d..4 empty at %s" % (count, record["check"]))


def check_hold_group(session, record, expect_piece, grayscale, rule):
    """The Hold group of the left column with the preference on (task T044):
    the heading draws in the dim text color, and once a piece is stashed the
    box below holds its preview - dimmed while the once-per-piece lock is
    spent - in the palette the setting asks for. With no piece stashed the box
    stays empty."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    heading, _ = scan_box(pixel, HOLD_HEADING_BOX, (BACKGROUND_COLOR,))
    if TEXT_DIM_COLOR not in heading:
        fail(rule, "the HOLD heading draws no glyph at %s" % record["check"])
        return
    counts, _ = scan_box(pixel, HOLD_BOX, (BACKGROUND_COLOR,))
    if not expect_piece:
        if counts:
            detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(counts.items()))
            fail(rule, "the Hold box draws %d pixels with no piece stashed at "
                 "%s (%s)" % (sum(counts.values()), record["check"], detail))
            return
        good(rule, "heading drawn, box empty at %s" % record["check"])
        return
    if record["hold"] == "none":
        fail(rule, "no hold piece at %s, so the Hold box cannot be checked" %
             record["check"])
        return
    letter = record["hold"].split(",")[0]
    check_preview_shape(session, record, letter, HOLD_BOX,
                        record["hold"].endswith(",locked"), grayscale, rule)


def check_hold_group_absent(session, record, rule):
    """HOLD off hides the whole Hold group (SPEC section 6.2 step 5): neither
    the heading nor the box may hold one pixel besides the canvas ground."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    counts, total = scan_box(pixel, HOLD_GROUP_BOX, (BACKGROUND_COLOR,))
    if counts:
        detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(counts.items()))
        fail(rule, "the hidden Hold group draws %d of %d pixels besides the "
             "ground at %s (%s)" % (sum(counts.values()), total,
                                    record["check"], detail))
        return
    good(rule, "all %d pixels of the Hold group are the ground at %s" %
         (total, record["check"]))


def check_play_scene_colors(session, record, grayscale, rule):
    """The grayscale preference from pixels (SPEC section 6.2 step 5, task
    T044). The well holds the settled cells, the active piece, and the ghost;
    with GRAYSCALE on, every one of those pixels must be gray - the gray fills
    are achromatic and only the ghost's quarter blend toward the well leaves a
    spread of three channels - while the statistics text and the board frame
    keep the exact colors they have with GRAYSCALE off. The one-pixel cell
    outline is a shared shade of both modes and the requirement keeps outlines
    as they are, so the census allows it beside the well and grid colors. The
    off frame is the positive control: it must hold clearly colored piece
    pixels, so the gray proof cannot pass on a frame that drew no piece."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    width, height, pixel = loaded
    last_x = BOARD_ORIGIN[0] + BOARD_COLUMNS * BOARD_CELL
    last_y = BOARD_ORIGIN[1] + BOARD_ROWS * BOARD_CELL
    if width < last_x or height < last_y:
        fail(rule, "%s is %dx%d, too small for the well" %
             (record.get("shot"), width, height))
        return
    counts, _ = scan_box(
        pixel, (BOARD_ORIGIN[0], BOARD_ORIGIN[1],
                BOARD_COLUMNS * BOARD_CELL, BOARD_ROWS * BOARD_CELL),
        (WELL_COLOR, GRID_LINE_COLOR, CELL_EDGE_COLOR))
    drawn = sum(counts.values())
    if grayscale:
        colored = {color: n for color, n in counts.items()
                   if max(color) - min(color) > GRAYSCALE_CHANNEL_TOLERANCE}
        if colored:
            detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(colored.items()))
            fail(rule, "%d of the %d piece pixels in the well are colored at "
                 "%s (%s)" % (sum(colored.values()), drawn, record["check"], detail))
            return
        if drawn < 1000:
            fail(rule, "only %d piece pixels in the well at %s, too few to "
                 "prove the play scene gray" % (drawn, record["check"]))
            return
    else:
        colored = sum(n for color, n in counts.items()
                      if max(color) - min(color) >= COLOR_CHANNEL_FLOOR)
        if colored == 0:
            fail(rule, "no colored piece pixel in the well at %s, so the "
                 "color frame proves nothing" % record["check"])
            return
        drawn = colored
    # The text and the outline keep their color-mode colors in both modes.
    text_counts, _ = scan_box(pixel, STATS_FIELD_BOX, (BACKGROUND_COLOR,))
    for color in (TEXT_DIM_COLOR, TEXT_COLOR):
        if color not in text_counts:
            fail(rule, "the statistics text color %s is absent at %s" %
                 (color, record["check"]))
            return
    # The frame corners: the outline runs one pixel outside the well, so its
    # top-left is the pixel before the well origin and its bottom-right is the
    # pixel at (last_x, last_y), one past the well on both axes.
    if (pixel(BOARD_ORIGIN[0] - 1, BOARD_ORIGIN[1] - 1) != FRAME_COLOR or
            pixel(last_x, last_y) != FRAME_COLOR):
        fail(rule, "the board frame is not %s at %s" % (FRAME_COLOR, record["check"]))
        return
    good(rule, "%d %s piece pixels, text and frame in color at %s" %
         (drawn, "gray" if grayscale else "colored", record["check"]))


# The labeled all-pieces fixture of task T095: every tetromino settled once,
# so one settled cell of each type - the representative cells below, in the
# fixture's own coordinates - carries that type's in-cell mark.
PIECE_DESIGN_CELLS = [
    ("I", 0, 3), ("O", 6, 4), ("T", 1, 6), ("J", 6, 7),
    ("S", 1, 9), ("L", 8, 10), ("Z", 0, 12),
]
PORTRAIT_BOARD_ORIGIN = (72, 120)
PORTRAIT_BOARD_CELL = 40


def portrait_device_pixel(x, y):
    """One portrait pixel on the 960x544 device canvas through the fixed
    rotation of task T055: portrait (x, y) reaches the device at (y, 543-x)."""
    return (y, 543 - x)


def piece_design_mask(pixel, letter, cell, grayscale, portrait):
    """The dark-shade pixel set of one settled cell's interior, plus a census
    of every other shade found there. The interior is the band two pixels
    inside the cell edges - past the outline and the bevel - which is exactly
    where cellDesign places a mark (renderer_sdl.hpp, task T095)."""
    fill = piece_fill(letter, grayscale)
    dark = mix_toward(fill, (0, 0, 0), 1, 2)
    origin, size = ((PORTRAIT_BOARD_ORIGIN, PORTRAIT_BOARD_CELL) if portrait
                    else (BOARD_ORIGIN, BOARD_CELL))
    col, row = cell
    x0 = origin[0] + col * size
    y0 = origin[1] + row * size
    mask = set()
    others = {}
    for iy in range(2, size - 2):
        for ix in range(2, size - 2):
            spot = (portrait_device_pixel(x0 + ix, y0 + iy) if portrait
                    else (x0 + ix, y0 + iy))
            color = pixel(*spot)
            if color == dark:
                mask.add((ix, iy))
            elif color != fill:
                others[color] = others.get(color, 0) + 1
    return mask, others


def check_piece_design_marks(session, record, grayscale, portrait, rule,
                             expect_designs=True):
    """The per-piece in-cell designs from pixels (task T095, specification
    section 6.2): over the labeled all-pieces fixture, the interior of one
    settled cell per piece type holds only that palette's fill and dark
    shade, the dark pixels form the piece's mark, and no two pieces share a
    mark - so the pattern alone names the piece, in color and in grayscale,
    in both orientations. With expect_designs off the same walk is the
    negative control: the flat cell draws no dark pixel at all, so the proof
    cannot pass on shading the cell always had."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    masks = {}
    for letter, col, row in PIECE_DESIGN_CELLS:
        mask, others = piece_design_mask(pixel, letter, (col, row), grayscale,
                                         portrait)
        where = "%s cell of %s at (%d,%d)" % (record["check"], letter, col, row)
        if others:
            detail = ", ".join("%d x %s" % (n, c)
                               for c, n in sorted(others.items()))
            fail(rule, "the interior of the %s holds shades beside the fill "
                 "and the dark (%s)" % (where, detail))
            return
        if expect_designs and not mask:
            fail(rule, "the interior of the %s holds no mark pixel" % where)
            return
        if not expect_designs and mask:
            fail(rule, "the interior of the %s holds %d dark pixels with the "
                 "designs off" % (where, len(mask)))
            return
        masks[letter] = mask
    if expect_designs:
        order = sorted(masks)
        for i, first in enumerate(order):
            for second in order[i + 1:]:
                if masks[first] == masks[second]:
                    fail(rule, "the %s and %s marks share one shape at %s" %
                         (first, second, record["check"]))
                    return
    good(rule, "%s %s cell interiors hold %s at %s" %
         (len(masks), "gray" if grayscale else "color",
          "seven distinct marks" if expect_designs else "no dark pixel",
          record["check"]))


def check_ghost_readable(session, record, grayscale, rule):
    """The ghost outline from pixels: the rows below the live piece hold the
    exact blend the renderer derives from the piece's fill, in the palette the
    setting asks for, so the landing preview stays a readable second shape
    beside the live piece in gray mode too."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    piece = piece_cells(record, "active")
    if piece is None:
        fail(rule, "no active piece at %s" % record["check"])
        return
    expected = ghost_fill(piece[0], grayscale)
    fill = piece_fill(piece[0], grayscale)
    first_row = max(0, min(BOARD_ROWS - 1, piece[3] + 2))
    last_y = BOARD_ORIGIN[1] + BOARD_ROWS * BOARD_CELL
    last_x = BOARD_ORIGIN[0] + BOARD_COLUMNS * BOARD_CELL
    found = 0
    for y in range(BOARD_ORIGIN[1] + first_row * BOARD_CELL, last_y):
        for x in range(BOARD_ORIGIN[0], last_x):
            if pixel(x, y) == expected:
                found += 1
    if found < 100:
        fail(rule, "only %d pixels of the ghost color %s below the piece at "
             "%s, so the landing preview is not drawn in the %s palette" %
             (found, expected, record["check"], "gray" if grayscale else "color"))
        return
    if max(fill) - min(fill) > GRAYSCALE_CHANNEL_TOLERANCE and grayscale:
        fail(rule, "the %s fill %s is not gray" % (piece[0], fill))
        return
    # Readability: the outline must stay a different shade from the live fill,
    # so the two shapes never merge into one.
    if abs(sum(expected) - sum(fill)) < 30:
        fail(rule, "the ghost %s and the fill %s of %s differ by %d, too "
             "little to read as two shapes" %
             (expected, fill, piece[0], abs(sum(expected) - sum(fill))))
        return
    good(rule, "%d pixels of the %s ghost %s below the piece at %s" %
         (found, "gray" if grayscale else "color", expected, record["check"]))


def check_music_page_layout(session, record, rule):
    """The music settings page inside the canvas (task T062, MUSIC.md player
    controls): the MUSIC heading, its four rows at the shared menu metrics
    with the accent marker on the highlighted row, the footer hint below, and
    nothing at all under the footer. The title line between the rows and the
    footer is allowed to be empty - it is silent whenever no track plays."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    selected = record["menu"].split(":")
    if len(selected) != 2 or not selected[1].isdigit():
        fail(rule, "record %s is not on the music page: menu=%s" %
             (record["check"], record.get("menu")))
        return
    selected = int(selected[1])
    heading, _ = scan_box(pixel, OPTIONS_HEADING_BOX, (BACKGROUND_COLOR,))
    if TEXT_COLOR not in heading:
        fail(rule, "the MUSIC heading draws no bright glyph at %s" %
             record["check"])
        return
    for index, box in enumerate(MUSIC_PAGE_ROW_BOXES):
        counts, _ = scan_box(pixel, box, (BACKGROUND_COLOR,))
        if TEXT_DIM_COLOR not in counts and TEXT_COLOR not in counts:
            fail(rule, "music page row %d draws no text at %s" %
                 (index, record["check"]))
            return
        if index == selected and ACCENT_COLOR not in counts:
            fail(rule, "the highlighted music row %d draws no marker at %s" %
                 (index, record["check"]))
            return
    footer, _ = scan_box(pixel, OPTIONS_FOOTER_BOX, (BACKGROUND_COLOR,))
    if TEXT_DIM_COLOR not in footer:
        fail(rule, "the music page footer draws no hint at %s" %
             record["check"])
        return
    below, total = scan_box(pixel, OPTIONS_BELOW_FOOTER_BOX, (BACKGROUND_COLOR,))
    if below:
        detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(below.items()))
        fail(rule, "%d of %d pixels below the footer are drawn at %s (%s), so "
             "the page does not fit the canvas" %
             (sum(below.values()), total, record["check"], detail))
        return
    good(rule, "MUSIC heading, four rows with row %d marked, footer, quiet "
         "canvas at %s" % (selected, record["check"]))


def check_options_screen_layout(session, record, rule):
    """The options page rows inside the canvas (tasks T044, T055, T062, and
    T095): the heading, one text line per row at the shared menu metrics with
    the accent marker on the highlighted row, the footer hint below, and
    nothing at all under the footer, so no row or hint can have slipped off
    the 960x544 canvas."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    selected = record["menu"].split(":")
    if len(selected) != 2 or not selected[1].isdigit():
        fail(rule, "record %s is not on the options screen: menu=%s" %
             (record["check"], record.get("menu")))
        return
    selected = int(selected[1])
    heading, _ = scan_box(pixel, OPTIONS_HEADING_BOX, (BACKGROUND_COLOR,))
    if TEXT_COLOR not in heading:
        fail(rule, "the options heading draws no bright glyph at %s" % record["check"])
        return
    for index, box in enumerate(OPTIONS_ROW_BOXES):
        counts, _ = scan_box(pixel, box, (BACKGROUND_COLOR,))
        if TEXT_DIM_COLOR not in counts and TEXT_COLOR not in counts:
            fail(rule, "options row %d draws no text at %s" % (index, record["check"]))
            return
        if index == selected and ACCENT_COLOR not in counts:
            fail(rule, "the highlighted options row %d draws no marker at %s" %
                 (index, record["check"]))
            return
    footer, _ = scan_box(pixel, OPTIONS_FOOTER_BOX, (BACKGROUND_COLOR,))
    if TEXT_DIM_COLOR not in footer:
        fail(rule, "the options footer draws no hint at %s" % record["check"])
        return
    below, total = scan_box(pixel, OPTIONS_BELOW_FOOTER_BOX, (BACKGROUND_COLOR,))
    if below:
        detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(below.items()))
        fail(rule, "%d of %d pixels below the footer are drawn at %s (%s), so "
             "the page does not fit the canvas" %
             (sum(below.values()), total, record["check"], detail))
        return
    good(rule, "heading, every row with row %d marked, footer, quiet canvas "
         "at %s" % (selected, record["check"]))


def check_backgrounds_page_layout(session, record, rule):
    """The backgrounds page behind the BACKGROUNDS row, inside the canvas
    (task T084, docs/BACKGROUND_IMPLEMENTATION.md player options): the
    BACKGROUNDS heading, its two rows - MODE and the preferred DESIGN - at
    the shared menu metrics with the accent marker on the highlighted row,
    the footer hint below, and nothing at all under the footer."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    selected = record["menu"].split(":")
    if len(selected) != 2 or not selected[1].isdigit():
        fail(rule, "record %s is not on the backgrounds page: menu=%s" %
             (record["check"], record.get("menu")))
        return
    selected = int(selected[1])
    heading, _ = scan_box(pixel, OPTIONS_HEADING_BOX, (BACKGROUND_COLOR,))
    if TEXT_COLOR not in heading:
        fail(rule, "the BACKGROUNDS heading draws no bright glyph at %s" %
             record["check"])
        return
    for index, box in enumerate(BACKGROUNDS_PAGE_ROW_BOXES):
        counts, _ = scan_box(pixel, box, (BACKGROUND_COLOR,))
        if TEXT_DIM_COLOR not in counts and TEXT_COLOR not in counts:
            fail(rule, "backgrounds page row %d draws no text at %s" %
                 (index, record["check"]))
            return
        if index == selected and ACCENT_COLOR not in counts:
            fail(rule, "the highlighted backgrounds row %d draws no marker "
                 "at %s" % (index, record["check"]))
            return
    footer, _ = scan_box(pixel, OPTIONS_FOOTER_BOX, (BACKGROUND_COLOR,))
    if TEXT_DIM_COLOR not in footer:
        fail(rule, "the backgrounds page footer draws no hint at %s" %
             record["check"])
        return
    below, total = scan_box(pixel, OPTIONS_BELOW_FOOTER_BOX,
                            (BACKGROUND_COLOR,))
    if below:
        detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(below.items()))
        fail(rule, "%d of %d pixels below the footer are drawn at %s (%s), so "
             "the page does not fit the canvas" %
             (sum(below.values()), total, record["check"], detail))
        return
    good(rule, "BACKGROUNDS heading, two rows with row %d marked, footer, "
         "quiet canvas at %s" % (selected, record["check"]))


def check_backgrounds_page_layout_portrait(session, record, rule):
    """The backgrounds page in portrait (task T084): the same turned bands
    the portrait options page uses, with the page's own two rows, the
    heading, and the footer hint, so the names and rows read in the tall
    frame exactly as the acceptance asks."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    selected = record["menu"].split(":")
    if len(selected) != 2 or not selected[1].isdigit():
        fail(rule, "record %s is not on the backgrounds page: menu=%s" %
             (record["check"], record.get("menu")))
        return
    selected = int(selected[1])
    heading, _ = scan_box(pixel, PORTRAIT_OPTIONS_HEADING_BOX,
                          (BACKGROUND_COLOR,))
    if TEXT_COLOR not in heading:
        fail(rule, "the portrait BACKGROUNDS heading draws no bright glyph "
             "at %s" % record["check"])
        return
    footer, _ = scan_box(pixel, PORTRAIT_OPTIONS_FOOTER_BOX,
                         (BACKGROUND_COLOR,))
    if TEXT_DIM_COLOR not in footer:
        fail(rule, "the portrait backgrounds footer draws no hint at %s" %
             record["check"])
        return
    for index in range(2):
        band = (PORTRAIT_OPTIONS_MENU_X + index * PORTRAIT_OPTIONS_ROW_HEIGHT,
                0, PORTRAIT_OPTIONS_ROW_TEXT_HEIGHT, 544)
        counts, _ = scan_box(pixel, band, (BACKGROUND_COLOR,))
        if index == selected:
            if TEXT_COLOR not in counts or ACCENT_COLOR not in counts:
                fail(rule, "the highlighted portrait backgrounds row %d draws "
                     "no bright text and marker at %s" %
                     (index, record["check"]))
                return
        elif TEXT_DIM_COLOR not in counts:
            fail(rule, "portrait backgrounds row %d draws no text at %s" %
                 (index, record["check"]))
            return
    good(rule, "turned heading, two rows with row %d marked, footer, at %s"
         % (selected, record["check"]))


def check_play_wallpaper_color(session, record, rule, probes, expected,
                               label):
    """The play scene's wallpaper from pixels (task T084): every probe point
    of the shot must hold exactly `expected`, the base field of the design
    the saved mode selected - the legacy flat #10121A for Off - which proves
    the change applied on the very next play frame of the same session. The
    probes sit in base-only windows of the landscape quiet span, clear of
    every design's secondary shapes."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    width, height, pixel = loaded
    for x, y in probes:
        if not (0 <= x < width and 0 <= y < height):
            fail(rule, "probe %s is outside the %dx%d shot of %s" %
                 ((x, y), width, height, record["check"]))
            return
        actual = pixel(x, y)
        if actual != expected:
            fail(rule, "the play probe (%d, %d) at %s holds %s, expected the "
                 "%s base %s" % (x, y, record["check"], actual, label,
                                 expected))
            return
    good(rule, "%d probes hold the %s base %s at %s" %
         (len(probes), label, expected, record["check"]))


def check_hold_ignored(session, records, before, after, rule):
    """SPEC section 5 through the HOLD gate of task T044: a mapped hold input
    while the preference is off changes nothing a swap would move, and the
    hold slot stays empty, so neither a swap happened nor the once-per-piece
    lock was spent."""
    by_name = {record["check"]: record for record in records}
    a, b = by_name.get(before), by_name.get(after)
    if a is None or b is None:
        fail(rule, "missing %s or %s" % (before, after))
        return
    watched = ["active", "ghost", "hold", "queue", "board", "settled", "score",
               "lines", "level"]
    differing = [k for k in watched if a.get(k) != b.get(k)]
    if differing:
        fail(rule, "the ignored hold changed %s" % differing)
        return
    if b.get("hold") != "none":
        fail(rule, "the hold slot holds %s after the ignored press" % b.get("hold"))
        return
    good(rule, "%s -> %s changed nothing" % (before, after))


def check_no_hold_edges(session, input_log_path, rule):
    """The per-tick input log of a session that never enabled HOLD: not one
    tick may carry a hold edge, because the platform gate drops it before the
    log records the frame (task T044, src/main.cpp)."""
    if not os.path.isfile(input_log_path):
        fail(rule, "missing %s" % input_log_path)
        return
    with open(input_log_path) as handle:
        lines = handle.read().splitlines()
    ticks = [line for line in lines if line.startswith("tick=")]
    edges = [line for line in ticks if " hold=1 " in line + " "]
    if not ticks:
        fail(rule, "%s holds no tick line" % input_log_path)
        return
    if edges:
        fail(rule, "%d of %d tick lines carry a hold edge the round saw" %
             (len(edges), len(ticks)))
        return
    good(rule, "no hold edge in %d tick lines" % len(ticks))


# ---------------------------------------------------------------------------
# The portrait proofs of task T055: the rotated board geometry from pixels,
# the tall pages that clip nothing, and the rotated mappings of both input
# devices pinned from the per-tick input log the game writes.
# ---------------------------------------------------------------------------

def check_portrait_play_frame(session, record, rule):
    """The rotated play scene from pixels (task T055, notes/design-review.md):
    the board frame - x=71..472 and y=119..920 of the 544x960 portrait frame,
    which is device x=119..920 and y=71..472 - draws its outline on all four
    edges, the HUD strip lives in the device columns above it, and the quiet
    margins beside and below the board stay background, so the 40 px cell
    composition clipped nothing."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    x, y, w, h = PORTRAIT_FRAME
    # Both frame edges hold the frame color for their whole span, and the
    # lines one past them hold the background: this pins the rotation and the
    # 40 px cell size together, because a 27 px landscape field or the
    # opposite rotation lands elsewhere.
    for edge_x, outside in ((x, x - 1), (x + w - 1, x + w)):
        wrong = [edge_y for edge_y in range(y, y + h)
                 if pixel(edge_x, edge_y) != FRAME_COLOR]
        if wrong:
            fail(rule, "device column %d holds %d non-frame pixels of %d "
                 "(first at y=%d, color %s) at %s" %
                 (edge_x, len(wrong), h, wrong[0],
                  pixel(edge_x, wrong[0]), record["check"]))
            return
        if any(pixel(outside, edge_y) == FRAME_COLOR
               for edge_y in range(y, y + h)):
            fail(rule, "device column %d, outside the frame, holds frame "
                 "pixels at %s" % (outside, record["check"]))
            return
    for edge_y, outside in ((y, y - 1), (y + h - 1, y + h)):
        wrong = [edge_x for edge_x in range(x, x + w)
                 if pixel(edge_x, edge_y) != FRAME_COLOR]
        if wrong:
            fail(rule, "device row %d holds %d non-frame pixels of %d at %s" %
                 (edge_y, len(wrong), w, record["check"]))
            return
        if any(pixel(edge_x, outside) == FRAME_COLOR
               for edge_x in range(x, x + w)):
            fail(rule, "device row %d, outside the frame, holds frame pixels "
                 "at %s" % (outside, record["check"]))
            return
    # The quiet margins: portrait columns left and right of the board and the
    # rows below it stay background.
    margins = [(5, 500), (539, 500), (272, 945)]
    for portrait_x, portrait_y in margins:
        device_x, device_y = portrait_y, CANVAS[1] - 1 - portrait_x
        if pixel(device_x, device_y) != BACKGROUND_COLOR:
            fail(rule, "portrait margin pixel (%d, %d) is %s, expected "
                 "background at %s" %
                 (portrait_x, portrait_y, pixel(device_x, device_y),
                  record["check"]))
            return
    # The HUD strip above the board: bright and dim text both live there.
    strip, strip_total = scan_box(pixel, PORTRAIT_STRIP_BOX,
                                  (BACKGROUND_COLOR,))
    if TEXT_COLOR not in strip or TEXT_DIM_COLOR not in strip:
        fail(rule, "the portrait HUD strip holds %d bright colors and %d dim "
             "ones at %s; expected both text colors" %
             (len(strip), len(strip), record["check"]))
        return
    good(rule, "rotated frame %dx%d, 40 px cells, strip and margins clean at "
         "%s" % (w, h, record["check"]))


def check_portrait_page_quiet(session, record, rule):
    """One portrait page that clears the canvas - title, controls, or
    options - keeps the portrait rows below the board frame empty, so no text
    or preview of the tall composition clipped. Overlay pages are excluded:
    their veil covers those rows with dimmed play-scene pixels by design."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    below, total = scan_box(pixel, PORTRAIT_BELOW_BOARD_BOX,
                            (BACKGROUND_COLOR,))
    if below:
        detail = ", ".join("%d x %s" % (n, c) for c, n in sorted(below.items()))
        fail(rule, "%d of %d pixels below the portrait board frame are drawn "
             "at %s (%s), so the page does not fit the turned canvas" %
             (sum(below.values()), total, record["check"], detail))
        return
    good(rule, "quiet below-board rows at %s" % record["check"])


def check_portrait_overlay_panel(session, record, box, rule):
    """One portrait overlay's panel sits at its rotated device box: the solid
    panel face fills the box's quiet corners, while the veil the overlay
    draws over the frozen round keeps everything outside the panel drawn, so
    a panel that missed the rotation or clipped an edge fails here."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    x, y, w, h = box
    corners = [(x + 8, y + 8), (x + w - 9, y + 8), (x + 8, y + h - 9),
               (x + w - 9, y + h - 9)]
    for corner_x, corner_y in corners:
        if pixel(corner_x, corner_y) != PANEL_FILL_COLOR:
            fail(rule, "portrait panel corner (%d, %d) is %s, expected the "
                 "panel face %s at %s" %
                 (corner_x, corner_y, pixel(corner_x, corner_y),
                  PANEL_FILL_COLOR, record["check"]))
            return
    # The panel must not reach past its box: the pixel just outside each edge
    # is veiled round, never the solid panel face.
    outside = [(x - 2, y + h // 2), (x + w + 1, y + h // 2),
               (x + w // 2, y - 2), (x + w // 2, y + h + 1)]
    for outside_x, outside_y in outside:
        if pixel(outside_x, outside_y) == PANEL_FILL_COLOR:
            fail(rule, "the portrait panel reaches past its box to (%d, %d) "
                 "at %s" % (outside_x, outside_y, record["check"]))
            return
    good(rule, "panel at device (%d, %d, %d, %d) at %s" %
         (x, y, w, h, record["check"]))


def check_rotated_mapping(session, input_log_path, device, rule):
    """The rotated mapping of one device, pinned from the per-tick input log
    (task T055): the gameplay actions must appear in the order the portrait
    script presses them, with exactly the action bits the rotated tables
    produce. The landscape table fails every step - key up would raise a
    clockwise rotation before any move-right, pad B a counter-clockwise one -
    so the pin proves the tables turned with the orientation."""
    if not os.path.isfile(input_log_path):
        fail(rule, "missing %s" % input_log_path)
        return
    with open(input_log_path) as handle:
        ticks = [line for line in handle.read().splitlines()
                 if line.startswith("tick=")]
    if not ticks:
        fail(rule, "%s holds no tick line" % input_log_path)
        return

    def field(line, name):
        match = re.search(r"(?:^| )%s=(\S+)" % name, line)
        return match.group(1) if match else None

    if device == "keyboard":
        # The presses of the portrait-keyboard script, in order: key up moves
        # right, key down moves left, Z rotates clockwise, X
        # counter-clockwise, C holds, key left is the logical up direction so
        # it hard drops and moves the menu cursor up in one press, and key
        # right is soft drop plus menu-down. Space carries nothing since the
        # hard drop left the cluster (T067), so no step names it and the
        # ctest input suite pins the empty slot instead.
        steps = [
            ("key up -> move right", lambda line: field(line, "R") == "1"),
            ("key down -> move left", lambda line: field(line, "L") == "1"),
            ("key z -> rotate clockwise",
             lambda line: field(line, "cw") == "1"),
            ("key x -> rotate counter-clockwise",
             lambda line: field(line, "ccw") == "1"),
            ("key c -> hold", lambda line: field(line, "hold") == "1"),
            ("key left -> hard drop plus menu up",
             lambda line: field(line, "drop") == "1" and
             field(line, "menu") == "1000"),
            ("key esc -> pause", lambda line: field(line, "pause") == "1"),
            ("key right -> soft drop plus menu down",
             lambda line: field(line, "D") == "1" and
             field(line, "menu") == "0100"),
            ("key enter -> select",
             lambda line: field(line, "menu") == "0010"),
        ]
    else:
        # The presses of the portrait-pad script, in order: D-pad up moves
        # right, D-pad down moves left, B rotates clockwise and selects, A
        # rotates counter-clockwise and goes back, Y holds, D-pad left is the
        # logical up direction so it hard drops and moves the menu cursor up
        # in one press, and D-pad right is soft drop plus menu-down. X
        # carries nothing since the hard drop left the cluster (T067), so no
        # step names it and the ctest input suite pins the empty slot.
        steps = [
            ("pad up -> move right", lambda line: field(line, "R") == "1"),
            ("pad down -> move left", lambda line: field(line, "L") == "1"),
            ("pad b -> rotate clockwise and select",
             lambda line: field(line, "cw") == "1" and
             field(line, "menu") == "0010"),
            ("pad a -> rotate counter-clockwise and back",
             lambda line: field(line, "ccw") == "1" and
             field(line, "menu") == "0001"),
            ("pad y -> hold", lambda line: field(line, "hold") == "1"),
            ("pad left -> hard drop plus menu up",
             lambda line: field(line, "drop") == "1" and
             field(line, "menu") == "1000"),
            ("pad start -> pause", lambda line: field(line, "pause") == "1"),
            ("pad right -> soft drop plus menu down",
             lambda line: field(line, "D") == "1" and
             field(line, "menu") == "0100"),
        ]
    # A menu bit from the opening select of the round can precede the
    # gameplay steps, so the walk starts at the first gameplay tick and each
    # step searches forward only.
    position = 0
    for name, matches in steps:
        found = next((index for index in range(position, len(ticks))
                      if matches(ticks[index])), None)
        if found is None:
            fail(rule, "no tick carries the rotated %s bit pattern after tick "
                 "line %d of %s" % (name, position, input_log_path))
            return
        position = found + 1
    good(rule, "%d ordered rotated-%s actions pinned from %d tick lines" %
         (len(steps), device, len(ticks)))


def check_portrait_options_layout(session, record, rule):
    """The portrait options page of the same-session turn, from pixels (task
    T068): the heading, the rows, and the footer hint sit in the
    vertical bands the rotation gives them, with the highlighted row's text
    bright and its accent marker beside it. A landscape page draws nothing in
    the heading and footer bands at all, so the two boxes together prove the
    page the same process drew after the select really turned."""
    loaded = load_shot(record, rule)
    if loaded is None:
        return
    _, _, pixel = loaded
    selected = record["menu"].split(":")
    if len(selected) != 2 or not selected[1].isdigit():
        fail(rule, "record %s is not on the options screen: menu=%s" %
             (record["check"], record.get("menu")))
        return
    selected = int(selected[1])
    heading, _ = scan_box(pixel, PORTRAIT_OPTIONS_HEADING_BOX,
                          (BACKGROUND_COLOR,))
    if TEXT_COLOR not in heading:
        fail(rule, "the portrait options heading band draws no bright glyph "
             "at %s" % record["check"])
        return
    footer, _ = scan_box(pixel, PORTRAIT_OPTIONS_FOOTER_BOX,
                         (BACKGROUND_COLOR,))
    if TEXT_DIM_COLOR not in footer:
        fail(rule, "the portrait options footer band draws no hint at %s" %
             record["check"])
        return
    for index in range(9):
        band = (PORTRAIT_OPTIONS_MENU_X + index * PORTRAIT_OPTIONS_ROW_HEIGHT,
                0, PORTRAIT_OPTIONS_ROW_TEXT_HEIGHT, 544)
        counts, _ = scan_box(pixel, band, (BACKGROUND_COLOR,))
        if index == selected:
            if TEXT_COLOR not in counts or ACCENT_COLOR not in counts:
                fail(rule, "the highlighted portrait options row %d draws no "
                     "bright text and marker at %s" % (index, record["check"]))
                return
        elif TEXT_DIM_COLOR not in counts:
            fail(rule, "portrait options row %d draws no text at %s" %
                 (index, record["check"]))
            return
    good(rule, "turned heading, the rows with row %d marked, footer, at %s"
         % (selected, record["check"]))


def check_turn_spends_input(session, input_log_path, rule, probe_bit=None):
    """The same-session orientation turn of task T068 spends the input that
    crossed it. Every `orientation ... status=ok` line of the per-tick input
    log follows the tick that consumed the select edge, so that tick must
    carry the select bit, the ticks after it must carry nothing at all - no
    held level, no pending edge - until the script presses again, and, when
    the turn is the probed one, the tick that carried the select must also
    carry the probe's held level, which proves a control really was down
    across the turn and still cannot act after it."""
    if not os.path.isfile(input_log_path):
        fail(rule, "missing %s" % input_log_path)
        return
    with open(input_log_path) as handle:
        lines = handle.read().splitlines()

    def fields(line):
        bits = {}
        for token in line.split():
            if "=" in token:
                key, value = token.split("=", 1)
                bits[key] = value
        return bits

    turns = [i for i, line in enumerate(lines)
             if re.match(r"^orientation (portrait|landscape) status=", line)]
    if not turns:
        fail(rule, "%s holds no orientation save line" % input_log_path)
        return
    for position, index in enumerate(turns):
        if index == 0 or not lines[index - 1].startswith("tick="):
            fail(rule, "the orientation save at line %d follows no tick" % index)
            return
        select_tick = fields(lines[index - 1])
        if select_tick.get("menu") != "0010":
            fail(rule, "the tick before the orientation save carries "
                 "menu=%s, expected the select edge 0010" %
                 select_tick.get("menu"))
            return
        if probe_bit is not None and position == 0 and \
                select_tick.get(probe_bit) != "1":
            fail(rule, "the tick that turned the session carries %s=%s, "
                 "expected the probe level 1, so no control was held across "
                 "the turn" % (probe_bit, select_tick.get(probe_bit)))
            return
        # Every tick from the turn to the next tick that carries any bit must
        # be empty, and there must be at least three of them: a held level or
        # a pending edge that survived the turn would fill them.
        quiet = 0
        for line in lines[index + 1:]:
            if not line.startswith("tick="):
                continue
            bits = fields(line)
            if any(bits.get(key, "0") != "0" for key in
                   ("L", "R", "D", "cw", "ccw", "drop", "hold", "pause")) or \
                    bits.get("menu", "0000") != "0000":
                break
            quiet += 1
        if quiet < 3:
            fail(rule, "only %d tick(s) after the orientation save at line "
                 "%d are empty, so the turn leaked input into them" %
                 (quiet, index))
            return
    good(rule, "%d turn(s) spent the select edge and every held level and "
         "pending edge that crossed them" % len(turns))


# ---------------------------------------------------------------------------
# The analog thumbstick (task T86). Each axis reading resolves to the D-pad
# arm it points at, so the mapping of an orientation is the table's business:
# what these checks pin is the reading itself - which action bits a deflection
# raised, that a deadzone reading raised none, and that the piece moved in the
# on-screen direction the arm carries there.
# ---------------------------------------------------------------------------

# One per-tick input-log line, in the log's own field order. The menu field
# is four bits, up, down, select, and back.
TICK_LINE = re.compile(
    r"^tick=(\d+) slot=\S+ L=(\d) R=(\d) D=(\d) cw=(\d) ccw=(\d) drop=(\d) "
    r"hold=(\d) pause=(\d) menu=([01]{4})$")


def round_tick_bits(input_log_path, rule):
    """The non-idle tick lines of the round an input log opened, as tuples of
    the nine action bits in the log's own order. The round runs from the last
    `scene title -> playing` line to the next scene line, and an idle tick -
    nothing held, nothing pressed - is dropped, so the sequence names exactly
    the inputs that acted."""
    try:
        with open(input_log_path) as handle:
            lines = handle.read().splitlines()
    except IOError:
        fail(rule, "cannot read %s" % input_log_path)
        return None
    starts = [i for i, line in enumerate(lines)
              if line.startswith("scene title -> playing")]
    if not starts:
        fail(rule, "no round in %s" % input_log_path)
        return None
    begin = starts[-1]
    end = len(lines)
    for i in range(begin + 1, len(lines)):
        if lines[i].startswith("scene "):
            end = i
            break
    bits = []
    for line in lines[begin:end]:
        match = TICK_LINE.match(line)
        if match is None:
            continue
        values = match.groups()[1:]
        if all(value == "0" for value in values[:8]) and values[8] == "0000":
            continue  # an idle tick holds and presses nothing
        bits.append(tuple(int(v) for v in values[:8]) + (values[8],))
    return bits


def check_stick_round_bits(session, input_log_path, expected, rule):
    """The round's non-idle ticks are exactly `expected`, in order."""
    bits = round_tick_bits(input_log_path, rule)
    if bits is None:
        return
    if bits != expected:
        fail(rule, "round inputs %s, expected %s" % (bits, expected))
    else:
        good(rule, "%d acting ticks in order" % len(bits))


def check_piece_delta(session, before, after, dx, dy, rule):
    """The active piece moved by exactly (dx, dy) between two records with the
    same identity and rotation - the direction proof of the thumbstick
    sessions, where a dead or inverted axis shows up as the wrong delta."""
    a = piece_cells(before, "active")
    b = piece_cells(after, "active")
    if a is None or b is None:
        fail(rule, "no active piece: %s -> %s" %
             (before.get("active"), after.get("active")))
    elif (b[0] != a[0] or b[1] != a[1] or b[2] != a[2] + dx or
          b[3] != a[3] + dy):
        fail(rule, "piece %s -> %s, expected (%+d,%+d)" %
             (before["active"], after["active"], dx, dy))
    else:
        good(rule, "%s -> %s moved (%+d,%+d)" %
             (before["check"], after["check"], dx, dy))


def check_piece_frozen(session, before, after, rule):
    """The active piece did not move at all between two records."""
    check_piece_delta(session, before, after, 0, 0, rule)


# ---------------------------------------------------------------------------
# The controls file of the customization screen (task T066). The canonical
# form is rebuilt here from the specification's section 5 tables, never read
# back from the game's own formatter, so a wrong default in the source fails
# the harness instead of moving the pin. `overrides` replaces whole lines by
# key.
# ---------------------------------------------------------------------------

CONTROLS_ACTION_ORDER = ["left", "right", "down", "rotatecw", "rotateccw",
                         "harddrop", "hold", "pause"]

CONTROLS_DEFAULTS = {
    ("landscape", "pad"): {
        "left": "left", "right": "right", "down": "down", "rotatecw": "a",
        "rotateccw": "b", "harddrop": "up", "hold": "y+l1", "pause": "start"},
    ("landscape", "key"): {
        "left": "left", "right": "right", "down": "down", "rotatecw": "x+up",
        "rotateccw": "z", "harddrop": "space", "hold": "c+lshift",
        "pause": "enter+esc"},
    ("portrait", "pad"): {
        "left": "down", "right": "up", "down": "right", "rotatecw": "b",
        "rotateccw": "a", "harddrop": "left", "hold": "y+l1", "pause": "start"},
    ("portrait", "key"): {
        "left": "down", "right": "up", "down": "right", "rotatecw": "z",
        "rotateccw": "x", "harddrop": "left", "hold": "c+lshift",
        "pause": "enter+esc"},
}


def controls_file_bytes(overrides=None):
    """The canonical controls.dat, with `overrides` replacing whole values."""
    tables = {}
    for block, values in CONTROLS_DEFAULTS.items():
        tables[block] = dict(values)
    for (frame, device), values in (overrides or {}).items():
        tables[(frame, device)] = dict(tables.get((frame, device), {}))
        tables[(frame, device)].update(values)
    lines = ["version=1"]
    for frame, device in (("landscape", "pad"), ("landscape", "key"),
                          ("portrait", "pad"), ("portrait", "key")):
        for action in CONTROLS_ACTION_ORDER:
            lines.append("%s.%s.%s=%s" % (frame, device, action,
                                          tables[(frame, device)][action]))
    return "\n".join(lines) + "\n"


def check_controls_path(session, path, overrides, rule):
    """A controls file holds exactly the canonical bytes of `overrides`."""
    if path is None or not os.path.isfile(path):
        fail(rule, "missing controls file %s" % path)
        return
    with open(path) as handle:
        have = handle.read()
    want = controls_file_bytes(overrides)
    if have != want:
        differing = [line for line in want.split("\n") if line not in have.split("\n")]
        extra = [line for line in have.split("\n") if line not in want.split("\n")]
        fail(rule, "controls file differs (missing %s, extra %s)" %
             (differing[:3], extra[:3]))
    else:
        good(rule, "exact canonical controls bytes")


def check_rebind_flow(session, by_name, input_log, device_word):
    """The customization walk every controls session shares: the screen opens
    on its first row, the cursor lands on HARD DROP, the capture applies, a
    conflicting capture is rejected, a repeat of the current control cancels,
    and RESTORE DEFAULTS lands on its own row."""
    check_field(by_name["rebind-open"], "scene", "rebind",
                session + "/rebind-opens")
    check_field(by_name["rebind-open"], "menu", "rebind:0",
                session + "/rebind-first-row")
    check_field(by_name["rebind-at-harddrop"], "menu", "rebind:5",
                session + "/rebind-harddrop-row")
    check_field(by_name["rebind-at-rotatecw"], "menu", "rebind:3",
                session + "/rebind-rotatecw-row")
    check_field(by_name["rebind-at-restore"], "menu", "rebind:8",
                session + "/rebind-restore-row")
    check_field(by_name["defaults-restored"], "scene", "rebind",
                session + "/restore-stays-on-screen")
    # The custom binding reaches the round: the freed control does nothing and
    # the new one locks the piece.
    check_field(by_name["playing-start"], "scene", "playing",
                session + "/round-starts")
    if "settled" in by_name.get("space-dead", {}):
        check_field(by_name["space-dead"], "settled", "0",
                    session + "/freed-control-dead")
    check_field(by_name["d-harddrops"] if "d-harddrops" in by_name
                else by_name["x-harddrops"], "settled", "4",
                session + "/bound-control-drops")
    # The evidence lines of the capture path, in order.
    check_log(session, input_log, [
        "controls capture action=harddrop device=%s control=%s result=applied"
        % (device_word, "d" if device_word == "key" else "x"),
        "controls capture action=rotatecw device=%s control=%s "
        "result=rejected holds=rotateccw"
        % (device_word, "z" if device_word == "key" else "b"),
        "controls capture action=rotatecw device=%s control=%s result=cancelled"
        % (device_word, "x" if device_word == "key" else "a"),
        "controls restore result=defaults",
    ], name="%s/input-log" % session)


# ---------------------------------------------------------------------------
# Per-session expectations. Values pinned here come from the SPEC arithmetic
# and the headless rules suite, never from the run under test: a change in
# game behavior fails the harness instead of moving the pin. The whole-round
# scores and settled-cell counts are recorded pins: their T029 refresh
# (2026-11-03) reflects the approved immediate-spawn rule - every piece
# starts one row lower, so each hard drop scores 2 points less and a
# block-out round tops out one row earlier.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# The startup line and the per-tick start lines the game writes for the six
# preferences (src/main.cpp), so a stage that relaunches on a saved file pins
# exactly the values it restored.
# ---------------------------------------------------------------------------

def settings_log_line(sound_on, ghost_on, grid_on, hold_on, next_previews,
                      grayscale_on, status, orientation="landscape",
                      music_mode="auto", music_volume=6, effects_volume=8,
                      background_mode="auto", background_id=1,
                      piece_designs=False):
    return ("Settings: sound %s, ghost preview %s, grid %s, hold %s, next "
            "previews %d, grayscale %s, orientation %s, music %s, volumes "
            "%d/%d, backgrounds %s %d, piece designs %s (%s) from" % (
                "on" if sound_on else "off", "on" if ghost_on else "off",
                "on" if grid_on else "off", "on" if hold_on else "off",
                next_previews, "on" if grayscale_on else "off", orientation,
                music_mode, music_volume, effects_volume, background_mode,
                background_id, "on" if piece_designs else "off", status))


def settings_start_lines(sound_on, ghost_on, grid_on, hold_on, next_previews,
                         grayscale_on, orientation="landscape",
                         music_mode="auto", music_volume=6, effects_volume=8,
                         background_mode="auto", background_id=1,
                         piece_designs=False):
    return [
        "sound start=%s" % ("on" if sound_on else "off"),
        "ghost start=%s" % ("on" if ghost_on else "off"),
        "grid start=%s" % ("on" if grid_on else "off"),
        "hold start=%s" % ("on" if hold_on else "off"),
        "next start=%d" % next_previews,
        "grayscale start=%s" % ("on" if grayscale_on else "off"),
        "orientation start=%s" % orientation,
        "music start=%s" % music_mode,
        "musicvolume start=%d" % music_volume,
        "effectsvolume start=%d" % effects_volume,
        "background start=%s" % background_mode,
        "backgroundid start=%d" % background_id,
        "piecedesigns start=%s" % ("on" if piece_designs else "off"),
    ]


def stage_name(session, stage):
    return "%s/%s" % (session, stage["label"]) if stage["label"] else session


def session_stages(run_dir, session):
    """The stages of one session, in driver order.

    A single-script session is one stage with an empty label and the file
    names the driver has always written. A staged session - one with a
    `<session>.stages.txt` manifest - is one stage per game process, named
    stage1, stage2, and so on, over one shared save directory, with the
    settings snapshots the driver took around every process."""
    session_dir = os.path.join(run_dir, session)
    stages = []
    index = 1
    while os.path.isfile(os.path.join(run_dir, "%s.stage%d.run.obs" % (session, index))):
        label = "stage%d" % index
        stages.append({
            "label": label,
            "ref": os.path.join(run_dir, "%s.%s.ref.obs" % (session, label)),
            "run": os.path.join(run_dir, "%s.%s.run.obs" % (session, label)),
            "log": os.path.join(run_dir, "%s.%s.run.log" % (session, label)),
            "input_log": os.path.join(session_dir, label, "input.log"),
            "shots": os.path.join(session_dir, label, "shots"),
            "save": os.path.join(session_dir, "save"),
            "before": os.path.join(session_dir, "settings-before-%s.dat" % label),
            "after": os.path.join(session_dir, "settings-after-%s.dat" % label),
            "controls_before": os.path.join(session_dir,
                                            "controls-before-%s.dat" % label),
            "controls_after": os.path.join(session_dir,
                                           "controls-after-%s.dat" % label),
        })
        index += 1
    if stages:
        return stages
    return [{
        "label": "",
        "ref": os.path.join(run_dir, session + ".ref.obs"),
        "run": os.path.join(run_dir, session + ".run.obs"),
        "log": os.path.join(run_dir, session + ".run.log"),
        "input_log": os.path.join(session_dir, "input.log"),
        "shots": os.path.join(session_dir, "shots"),
        "save": os.path.join(session_dir, "save"),
        "before": None,
        "after": None,
        "controls_before": None,
        "controls_after": None,
    }]


def check_stage_streams(session, stage):
    """The generic half every stage shares: the game's records equal the
    headless reference's, every screenshot is a 960x544 BMP, and the exit was
    clean. Returns the records, or None when the stage cannot be checked."""
    name = stage_name(session, stage)
    for path in (stage["ref"], stage["run"]):
        if not os.path.isfile(path):
            fail("%s/records" % name, "missing %s" % path)
            return None
    run_records = read_records(stage["run"])
    if not run_records:
        fail("%s/records" % name, "%s has no checkpoint records" % stage["run"])
        return None
    compare_streams(name, run_records, read_records(stage["ref"]))
    check_screenshots(name, run_records, stage["shots"])
    return run_records


# ---------------------------------------------------------------------------
# The controls file of the customization screen (task T066). The canonical
# form is rebuilt here from the specification's section 5 tables, never read
# back from the game's own formatter, so a wrong default in the source fails
# the harness instead of moving the pin. `overrides` replaces whole lines by
# key.
# ---------------------------------------------------------------------------

CONTROLS_ACTION_ORDER = ["left", "right", "down", "rotatecw", "rotateccw",
                         "harddrop", "hold", "pause"]

CONTROLS_DEFAULTS = {
    ("landscape", "pad"): {
        "left": "left", "right": "right", "down": "down", "rotatecw": "a",
        "rotateccw": "b", "harddrop": "up", "hold": "y+l1", "pause": "start"},
    ("landscape", "key"): {
        "left": "left", "right": "right", "down": "down", "rotatecw": "x+up",
        "rotateccw": "z", "harddrop": "space", "hold": "c+lshift",
        "pause": "enter+esc"},
    ("portrait", "pad"): {
        "left": "down", "right": "up", "down": "right", "rotatecw": "b",
        "rotateccw": "a", "harddrop": "left", "hold": "y+l1", "pause": "start"},
    ("portrait", "key"): {
        "left": "down", "right": "up", "down": "right", "rotatecw": "z",
        "rotateccw": "x", "harddrop": "left", "hold": "c+lshift",
        "pause": "enter+esc"},
}


def controls_file_bytes(overrides=None):
    """The canonical controls.dat, with `overrides` replacing whole values."""
    tables = {}
    for block, values in CONTROLS_DEFAULTS.items():
        tables[block] = dict(values)
    for (frame, device), values in (overrides or {}).items():
        tables[(frame, device)] = dict(tables.get((frame, device), {}))
        tables[(frame, device)].update(values)
    lines = ["version=1"]
    for frame, device in (("landscape", "pad"), ("landscape", "key"),
                          ("portrait", "pad"), ("portrait", "key")):
        for action in CONTROLS_ACTION_ORDER:
            lines.append("%s.%s.%s=%s" % (frame, device, action,
                                          tables[(frame, device)][action]))
    return "\n".join(lines) + "\n"


def check_controls_path(session, path, overrides, rule):
    """A controls file holds exactly the canonical bytes of `overrides`."""
    if path is None or not os.path.isfile(path):
        fail(rule, "missing controls file %s" % path)
        return
    with open(path) as handle:
        have = handle.read()
    want = controls_file_bytes(overrides)
    if have != want:
        differing = [line for line in want.split("\n") if line not in have.split("\n")]
        extra = [line for line in have.split("\n") if line not in want.split("\n")]
        fail(rule, "controls file differs (missing %s, extra %s)" %
             (differing[:3], extra[:3]))
    else:
        good(rule, "exact canonical controls bytes")


def check_rebind_flow(session, by_name, input_log, device_word):
    """The customization walk every controls session shares: the screen opens
    on its first row, the cursor lands on HARD DROP, the capture applies, a
    conflicting capture is rejected, a repeat of the current control cancels,
    and RESTORE DEFAULTS lands on its own row."""
    check_field(by_name["rebind-open"], "scene", "rebind",
                session + "/rebind-opens")
    check_field(by_name["rebind-open"], "menu", "rebind:0",
                session + "/rebind-first-row")
    check_field(by_name["rebind-at-harddrop"], "menu", "rebind:5",
                session + "/rebind-harddrop-row")
    check_field(by_name["rebind-at-rotatecw"], "menu", "rebind:3",
                session + "/rebind-rotatecw-row")
    check_field(by_name["rebind-at-restore"], "menu", "rebind:8",
                session + "/rebind-restore-row")
    check_field(by_name["defaults-restored"], "scene", "rebind",
                session + "/restore-stays-on-screen")
    # The custom binding reaches the round: the freed control does nothing and
    # the new one locks the piece.
    check_field(by_name["playing-start"], "scene", "playing",
                session + "/round-starts")
    if "settled" in by_name.get("space-dead", {}):
        check_field(by_name["space-dead"], "settled", "0",
                    session + "/freed-control-dead")
    check_field(by_name["d-harddrops"] if "d-harddrops" in by_name
                else by_name["x-harddrops"], "settled", "4",
                session + "/bound-control-drops")
    # The evidence lines of the capture path, in order.
    check_log(session, input_log, [
        "controls capture action=harddrop device=%s control=%s result=applied"
        % (device_word, "d" if device_word == "key" else "x"),
        "controls capture action=rotatecw device=%s control=%s "
        "result=rejected holds=rotateccw"
        % (device_word, "z" if device_word == "key" else "b"),
        "controls capture action=rotatecw device=%s control=%s result=cancelled"
        % (device_word, "x" if device_word == "key" else "a"),
        "controls restore result=defaults",
    ], name="%s/input-log" % session)


# ---------------------------------------------------------------------------
# Per-session expectations. Values pinned here come from the SPEC arithmetic
# and the headless rules suite, never from the run under test: a change in
# game behavior fails the harness instead of moving the pin. The whole-round
# scores and settled-cell counts are recorded pins: their T029 refresh
# (2026-11-03) reflects the approved immediate-spawn rule - every piece
# starts one row lower, so each hard drop scores 2 points less and a
# block-out round tops out one row earlier.
# ---------------------------------------------------------------------------

def check_session(run_dir, session):
    stages = session_stages(run_dir, session)
    records = {}
    for stage in stages:
        stage_records = check_stage_streams(session, stage)
        if stage_records is None:
            return
        records[stage["label"]] = stage_records
    check_session_log_in_save_dir(session, stages[0]["save"])

    # The single-script sessions below read one record stream and the session's
    # own log, input log, and save directory. A staged session has no "" stage:
    # its branch reads the stages itself, so these stay unused there.
    run_records = records.get("")
    by_name = ({record["check"]: record for record in run_records}
               if run_records is not None else None)
    log_path = stages[0]["log"] if run_records is not None else None
    input_log = stages[0]["input_log"] if run_records is not None else None
    save_dir = stages[0]["save"] if run_records is not None else None
    if session == "scene-cycle-keyboard":
        check_scene_sequence(run_records, [
            "title", "title", "controls", "title", "title",
            "playing", "playing", "playing", "playing", "playing",
            "paused", "paused", "playing", "paused",
            "paused", "pausecontrols", "paused", "paused", "playing",
            "gameover", "gameover", "playing", "gameover", "gameover",
            "title", "title", "title"], session + "/scene-sequence")
        # The pause overlay's Controls item opens the mappings screen over the
        # frozen round and back returns to the same overlay on the same item
        # (task T037): the cursor holds 2 across the visit and no frozen field
        # moves.
        check_field(by_name["paused-at-controls"], "menu", "pause:2",
                    session + "/controls-item-cursor")
        check_field(by_name["pause-controls-open"], "scene", "pausecontrols",
                    session + "/controls-screen-open")
        check_field(by_name["pause-controls-open"], "menu", "pause:2",
                    session + "/controls-screen-cursor")
        check_field(by_name["pause-controls-back"], "scene", "paused",
                    session + "/controls-screen-back")
        check_field(by_name["pause-controls-back"], "menu", "pause:2",
                    session + "/controls-screen-back-cursor")
        check_field(by_name["paused-at-restart"], "menu", "pause:1",
                    session + "/restart-item-cursor")
        pause_freeze(session, run_records, "paused", "paused-still",
                     session + "/pause-freeze")
        pause_freeze(session, run_records, "paused-at-controls",
                     "pause-controls-back", session + "/pause-freeze-controls")
        # The record taken on the mappings screen itself carries the same
        # round: every field but the scene and the menu equals the paused
        # record beside it.
        open_rec, paused_rec = (by_name["pause-controls-open"],
                                by_name["paused-at-controls"])
        round_fields = ["tick", "score", "lines", "level", "active", "ghost",
                        "hold", "queue", "board", "settled"]
        moved = [k for k in round_fields
                 if open_rec.get(k) != paused_rec.get(k)]
        if moved:
            fail(session + "/pause-controls-round", "state moved: %s" % moved)
        else:
            good(session + "/pause-controls-round", "round frozen on the "
                 "controls screen")
        check_field(by_name["gameover-first"], "score", "196",
                    session + "/first-round-score")
        check_field(by_name["gameover-first"], "record", "1",
                    session + "/first-round-record")
        check_field(by_name["gameover-second"], "score", "198",
                    session + "/second-round-score")
        check_field(by_name["gameover-second"], "high", "198",
                    session + "/second-round-high")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_log(session, log_path, [
            "Clean shutdown", "Quit requested through the title menu",
            "Scene title -> playing", "Scene playing -> paused",
            "Scene paused -> playing", "Scene playing -> gameover",
            "Scene gameover -> title",
            "Scene paused -> pausecontrols", "Scene pausecontrols -> paused",
        ])
        check_high_score_file(session, save_dir, 198,
                              session + "/saved-record")
    elif session == "scene-cycle-pad":
        check_field(by_name["playing-start"], "scene", "playing",
                    session + "/pad-starts-round")
        check_field(by_name["after-hold"], "hold", "T,locked",
                    session + "/pad-hold")
        check_field(by_name["after-detach"], "active", "S,0,2,0",
                    session + "/pad-detach-stops-movement")
        check_field(by_name["gameover-first"], "score", "196",
                    session + "/first-round-score")
        check_log(session, log_path, [
            "Clean shutdown", "Virtual Pad\" (instance", "removed",
            "Scene title -> playing",
        ])
        check_high_score_file(session, save_dir, 198,
                              session + "/saved-record")
    elif session == "harddrop-pad":
        # The landscape default of task T067: D-pad up hard drops, X carries
        # nothing, the menu role of D-pad up survives on every screen it
        # serves, and no press that returns a round to play drops a piece -
        # not the resume from the pause overlay, not Play Again, and not the
        # one scene change that happens outside a tick, the scripted restart.
        # The desktop keyboard defaults ride the same session: X rotates and
        # Space hard drops, unchanged by the controller change.
        check_scene_sequence(run_records, [
            "title", "title", "title", "playing", "playing", "playing",
            "paused", "paused", "paused", "playing", "playing", "playing",
            "playing", "gameover", "gameover", "gameover",
            "playing", "playing", "paused", "paused", "title", "playing",
            "playing", "paused", "paused", "title", "title", "title"],
            session + "/scene-sequence")
        # D-pad up still moves the cursor up on every menu it serves: the
        # title cursor wraps up to QUIT, the pause cursor to QUIT TO TITLE,
        # and the game-over cursor to TITLE (section 4.7).
        check_field(by_name["title-at-quit"], "menu", "title:3",
                    session + "/title-menu-up")
        check_field(by_name["paused-at-quit"], "menu", "pause:3",
                    session + "/pause-menu-up")
        check_field(by_name["gameover-at-title"], "menu", "over:1",
                    session + "/over-menu-up")
        # X carries nothing: the piece, the board, and the score all stand
        # exactly where the round's first record left them.
        idle_fields = ["active", "ghost", "hold", "queue", "board", "settled",
                       "score"]
        moved = [k for k in idle_fields
                 if by_name["x-carries-nothing"].get(k) !=
                 by_name["playing-start"].get(k)]
        if moved:
            fail(session + "/x-carries-nothing",
                 "the X press moved %s" % moved)
        else:
            good(session + "/x-carries-nothing",
                 "the piece, the board, and the score all stand still")
        # D-pad up hard drops: the piece locks and the SPEC's 4.6 arithmetic
        # holds for the drop.
        delta_check("settled", by_name["x-carries-nothing"],
                    by_name["up-hard-drops"], 4, session + "/up-drops-locks")
        hard_drop_arithmetic(session, run_records, "x-carries-nothing",
                             "up-hard-drops", session + "/up-drop-arithmetic")
        # The press that returns to play drops no piece: the resume leaves
        # the board, the score, and the piece exactly as the overlay froze
        # them, ten ticks after the round resumed.
        resumed_fields = ["active", "ghost", "hold", "queue", "board",
                          "settled", "score"]
        moved = [k for k in resumed_fields
                 if by_name["resumed-no-drop"].get(k) !=
                 by_name["up-hard-drops"].get(k)]
        if moved:
            fail(session + "/resume-drops-no-piece", "the resume moved %s"
                 % moved)
        else:
            good(session + "/resume-drops-no-piece",
                 "the board, the score, and the piece held through the resume")
        # The desktop keyboard defaults are unchanged: X rotates the piece
        # one clockwise step and Space hard drops it.
        before_rot = piece_cells(by_name["resumed-no-drop"], "active")[1]
        after_rot = piece_cells(by_name["keyboard-x-rotates"], "active")[1]
        if (after_rot - before_rot) % 4 != 1:
            fail(session + "/keyboard-x-rotates",
                 "rotation %d -> %d, expected one clockwise step" %
                 (before_rot, after_rot))
        else:
            good(session + "/keyboard-x-rotates",
                 "rotation %d -> %d" % (before_rot, after_rot))
        delta_check("settled", by_name["keyboard-x-rotates"],
                    by_name["keyboard-space-drops"], 4,
                    session + "/keyboard-space-drops")
        hard_drop_arithmetic(session, run_records, "keyboard-x-rotates",
                             "keyboard-space-drops",
                             session + "/keyboard-drop-arithmetic")
        # Play Again returns to play without dropping the fresh round's
        # first piece: nothing settles and no score accrues ten ticks in.
        check_field(by_name["play-again-no-drop"], "settled", "0",
                    session + "/play-again-no-drop")
        check_field(by_name["play-again-no-drop"], "score", "0",
                    session + "/play-again-no-score")
        # The scripted restart is the one scene change that happens outside a
        # tick, so the queued D-pad-up press reaches a round already in play:
        # the return-to-play gate spends its gameplay edge and the fresh
        # round's first piece does not drop.
        check_field(by_name["restart-no-drop"], "settled", "0",
                    session + "/restart-no-drop")
        check_field(by_name["restart-no-drop-later"], "settled", "0",
                    session + "/restart-no-drop-later")
        check_field(by_name["restart-no-drop-later"], "score", "0",
                    session + "/restart-no-score")
        # The same proof from the per-tick input log: the tick that follows
        # the restart marker carries the menu edge of the queued press and no
        # gameplay edge at all, because the platform layer spent it before
        # the round saw the frame.
        rule = session + "/restart-edge-spent"
        if not os.path.isfile(input_log):
            fail(rule, "missing %s" % input_log)
        else:
            with open(input_log) as handle:
                lines = handle.read().splitlines()
            marker = next((i for i, line in enumerate(lines)
                           if line == "script restart"), None)
            ticks_after = [line for line in lines[marker + 1:]
                           if line.startswith("tick=")] if marker is not None \
                else []
            if marker is None or not ticks_after:
                fail(rule, "no tick follows the script restart marker")
            else:
                first = ticks_after[0]
                match = re.search(r"drop=(\d)", first)
                menu = re.search(r"menu=(\d{4})", first)
                if match is None or menu is None:
                    fail(rule, "the tick line %s names no drop or menu" % first)
                elif match.group(1) != "0" or menu.group(1) != "1000":
                    fail(rule, "the tick after the restart is drop=%s menu=%s,"
                         " expected drop=0 menu=1000" %
                         (match.group(1), menu.group(1)))
                else:
                    good(rule, "the first tick after the restart carries the"
                         " menu edge alone")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_log(session, log_path, ["Clean shutdown", "Scene title -> playing"])
    elif session == "stick-pad":
        # The analog thumbstick session of task T86 on the landscape table.
        # The stick's Y axis carries the D-pad arms' menu roles, so it walks
        # the title menu; the round then moves the piece with X, soft drops
        # and hard drops with Y; a reading inside the deadzone holds nothing
        # while the boundary itself moves; and the D-pad and the keyboard
        # beside the stick are unchanged.
        check_scene_sequence(run_records, [
            "title", "title", "title",
            "playing", "playing", "playing", "playing", "playing", "playing",
            "playing", "playing", "playing", "playing",
            "paused", "paused", "title", "title", "title"],
            session + "/scene-sequence")
        check_field(by_name["title-at-options"], "menu", "title:2",
                    session + "/stick-walks-title-down")
        check_field(by_name["title-at-start"], "menu", "title:0",
                    session + "/stick-walks-title-up")
        check_field(by_name["round-start"], "scene", "playing",
                    session + "/round-starts")
        # One column per successful movement (section 4.5), the same rule the
        # D-pad follows: left, right, the deadzone edge, and the two controls
        # beside the stick.
        check_piece_delta(session, by_name["round-start"],
                          by_name["stick-left-moved"], -1, 0,
                          session + "/stick-x-negative-moves-left")
        check_piece_delta(session, by_name["stick-left-moved"],
                          by_name["stick-right-moved"], +1, 0,
                          session + "/stick-x-positive-moves-right")
        check_piece_frozen(session, by_name["stick-right-moved"],
                           by_name["deadzone-x-quiet"],
                           session + "/deadzone-x-holds")
        check_piece_frozen(session, by_name["deadzone-x-quiet"],
                           by_name["deadzone-y-quiet"],
                           session + "/deadzone-y-holds")
        check_piece_delta(session, by_name["deadzone-y-quiet"],
                          by_name["deadzone-edge-moves"], +1, 0,
                          session + "/deadzone-boundary-moves")
        # Soft drop: two held ticks, two cells, two points (section 4.6).
        check_piece_delta(session, by_name["deadzone-edge-moves"],
                          by_name["stick-soft-drop"], 0, +2,
                          session + "/stick-y-positive-soft-drops")
        delta_check("score", by_name["deadzone-edge-moves"],
                    by_name["stick-soft-drop"], 2,
                    session + "/soft-drop-points")
        # Hard drop: the piece locks in the same tick, four cells settle, and
        # the drop scores two points per cell travelled - the ghost of the
        # record before it names the landing row (section 4.5).
        delta_check("settled", by_name["stick-soft-drop"],
                    by_name["stick-hard-drop"], 4,
                    session + "/stick-y-negative-hard-drops")
        drop_cells = piece_cells(by_name["stick-soft-drop"], "ghost")[3] - \
            piece_cells(by_name["stick-soft-drop"], "active")[3]
        delta_check("score", by_name["stick-soft-drop"],
                    by_name["stick-hard-drop"], 2 * drop_cells,
                    session + "/hard-drop-points")
        check_piece_delta(session, by_name["stick-hard-drop"],
                          by_name["dpad-left-moved"], -1, 0,
                          session + "/dpad-unchanged")
        check_piece_delta(session, by_name["dpad-left-moved"],
                          by_name["keyboard-left-moved"], -1, 0,
                          session + "/keyboard-unchanged")
        # The ordered action bits of the round: two held ticks per deflection,
        # one edge for each edge action, nothing at all for the deadzone.
        check_stick_round_bits(session, input_log, [
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 1, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 1, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 1, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 1, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 0, 1, 0, 0, 0, 0, 0, "0100"),
            (0, 0, 1, 0, 0, 0, 0, 0, "0000"),
            (0, 0, 0, 0, 0, 1, 0, 0, "1000"),
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 0, 0, 0, 0, 0, 0, 1, "0000"),
        ], session + "/round-input-bits")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_log(session, log_path, ["Clean shutdown", "Scene title -> playing"])
    elif session == "stick-portrait":
        # The analog thumbstick session of task T86 on the turned table. The
        # turn itself runs with the stick deflected across the select, so the
        # deflected arm is spent with every held level and pending edge. The
        # round then proves the rotated mapping reading by reading - the
        # physical down arm moves the piece left, the physical up arm moves
        # it right, the physical right arm soft drops, and the physical left
        # arm hard drops - with the face buttons playable beside the stick.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options", "options", "title",
            "title", "playing", "playing", "playing", "playing", "playing",
            "playing", "playing", "playing", "playing", "playing", "paused",
            "paused", "title", "title", "options", "options", "options",
            "title", "title", "title"],
            session + "/scene-sequence")
        # The turn spent the deflection: the ticks after the orientation save
        # carry no action bit at all until the script presses again, exactly
        # as the T068 rule demands of a held control. The probe names the
        # move-left level, which the stick's X-negative deflection held
        # across the select on the landscape table.
        check_turn_spends_input(session, input_log,
                                session + "/turn-spends-deflection",
                                probe_bit="L")
        check_field(by_name["title-at-start"], "menu", "title:0",
                    session + "/stick-walks-turned-title")
        check_field(by_name["playing-portrait"], "scene", "playing",
                    session + "/round-starts")
        # The rotated table, one reading at a time. An inverted or dead axis
        # fails here whatever the reference says, because the expectation is
        # the on-screen direction of section 5's portrait table alone.
        check_piece_delta(session, by_name["playing-portrait"],
                          by_name["stick-moved-left"], -1, 0,
                          session + "/physical-down-moves-left")
        check_piece_delta(session, by_name["stick-moved-left"],
                          by_name["stick-moved-right"], +1, 0,
                          session + "/physical-up-moves-right")
        check_piece_delta(session, by_name["stick-moved-right"],
                          by_name["stick-soft-drop"], 0, +2,
                          session + "/physical-right-soft-drops")
        delta_check("score", by_name["stick-moved-right"],
                    by_name["stick-soft-drop"], 2,
                    session + "/soft-drop-points")
        # The reversed cluster beside the stick: B clockwise, A back, Y holds,
        # X carries nothing, and the deadzone holds the turned round too.
        before_rotate = piece_cells(by_name["stick-soft-drop"], "active")
        check_field(by_name["face-b-rotates"], "active",
                    "%s,1,%d,%d" % (before_rotate[0], before_rotate[2],
                                    before_rotate[3]),
                    session + "/face-b-rotates-cw")
        check_field(by_name["face-a-rotates-back"], "active",
                    by_name["stick-soft-drop"]["active"],
                    session + "/face-a-rotates-ccw")
        # The hold field is `<id>,<locked|free>`: Y stashed the piece that
        # was active at the soft-drop record and drew the queue head.
        hold_value = by_name["face-y-holds"].get("hold", "none")
        hold_letter = hold_value.split(",")[0] if hold_value != "none" else None
        if hold_letter != before_rotate[0]:
            fail(session + "/face-y-holds",
                 "hold slot is %s, expected the stashed %s" %
                 (hold_value, before_rotate[0]))
        else:
            good(session + "/face-y-holds", "hold=%s" % hold_value)
        check_piece_frozen(session, by_name["face-x-nothing"],
                           by_name["portrait-deadzone-quiet"],
                           session + "/face-x-and-deadzone-hold")
        delta_check("settled", by_name["portrait-deadzone-quiet"],
                    by_name["stick-hard-drop"], 4,
                    session + "/physical-left-hard-drops")
        # The ordered action bits of the turned round: the deflections land
        # on the rotated actions, the face buttons on the reversed cluster,
        # and the deadzone and the freed X button raise nothing at all.
        check_stick_round_bits(session, input_log, [
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (1, 0, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 1, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 1, 0, 0, 0, 0, 0, 0, "0000"),
            (0, 0, 1, 0, 0, 0, 0, 0, "0100"),
            (0, 0, 1, 0, 0, 0, 0, 0, "0000"),
            (0, 0, 0, 1, 0, 0, 0, 0, "0010"),
            (0, 0, 0, 0, 1, 0, 0, 0, "0001"),
            (0, 0, 0, 0, 0, 0, 1, 0, "0000"),
            (0, 0, 0, 0, 0, 1, 0, 0, "1000"),
            (0, 0, 0, 0, 0, 0, 0, 1, "0000"),
        ], session + "/round-input-bits")
        # The session turned three times and leaves the portrait preference.
        check_settings_path(session, os.path.join(save_dir, "settings.dat"),
                            True, True, True, True, 5, False,
                            rule="%s/saved-orientation" % session,
                            orientation="portrait")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_log(session, log_path, [
            "Clean shutdown",
            "Scene title -> playing",
            "Scene playing -> paused",
        ])
    elif session == "normal-game-clears":
        # A play-only session: the title appears only before the round starts
        # and every later record is playing or paused, ending resumed.
        scenes = [record["scene"] for record in run_records]
        unexpected = [s for s in scenes if s not in ("playing", "paused")]
        if unexpected:
            fail(session + "/stays-in-round",
                 "unexpected scenes %s" % sorted(set(unexpected)))
        else:
            good(session + "/stays-in-round", "%d records" % len(scenes))
        if scenes[-1] != "playing":
            fail(session + "/resumed", "last record is %s" % scenes[-1])
        else:
            good(session + "/resumed")
        check_field(by_name["lock-08"], "lines", "1",
                    session + "/single-clear")
        hard_drop_arithmetic(session, run_records, "piece-08", "lock-08",
                             session + "/clear-drop-arithmetic")
        for i in range(9):
            hard_drop_arithmetic(session, run_records, "piece-%02d" % i,
                                 "lock-%02d" % i,
                                 "%s/drop-arithmetic-%02d" % (session, i))
        soft_drop_arithmetic(session, run_records, "rotated-ccw",
                             "soft-dropped", session + "/soft-drop-arithmetic")
        hard_drop_arithmetic(session, run_records, "das-wall",
                             "das-wall-lock", session + "/das-wall-drop")
        check_field(by_name["after-hold"], "hold", "I,locked",
                    session + "/hold-used")
        hold_restriction(session, run_records, "after-hold", "hold-locked",
                         session + "/hold-once")
        pause_freeze(session, run_records, "paused", "paused-still",
                     session + "/pause-freeze")
        check_log(session, log_path, ["Clean shutdown"])
    elif session == "record-abandon-save":
        # The mid-round record persistence of task T052 (SPEC sections 4.7
        # and 9): a round that scores through two hard drops and leaves
        # through QUIT TO TITLE never reaches a game over, so the record
        # file can hold only what a save wrote while the round still ran.
        # The whole-round scores are recorded pins of the seed; the drop
        # arithmetic beside them is the SPEC's own.
        check_scene_sequence(run_records, [
            "title", "playing", "playing", "playing", "paused", "paused",
            "title", "title", "title"], session + "/scene-sequence")
        check_field(by_name["title-fresh"], "high", "0",
                    session + "/fresh-title-record")
        hard_drop_arithmetic(session, run_records, "playing-start",
                             "first-lock", session + "/first-drop-arithmetic")
        hard_drop_arithmetic(session, run_records, "first-lock",
                             "second-lock",
                             session + "/second-drop-arithmetic")
        check_field(by_name["second-lock"], "score", "68",
                    session + "/abandoned-score")
        # The abandon itself: QUIT TO TITLE is the fourth overlay item, the
        # title returns with every round counter reset, and the record the
        # abandoned round persisted is already the best the session knows.
        check_field(by_name["paused-at-quit-title"], "menu", "pause:3",
                    session + "/abandon-item")
        check_field(by_name["back-at-title"], "scene", "title",
                    session + "/back-at-title")
        check_field(by_name["back-at-title"], "score", "0",
                    session + "/abandoned-round-dropped")
        check_field(by_name["back-at-title"], "high", "68",
                    session + "/title-shows-abandoned-record")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_high_score_file(session, save_dir, 68, session + "/saved-record")
        check_log(session, log_path, [
            "Clean shutdown", "Scene paused -> title",
            "Record 36 saved to", "Record 68 saved to",
        ])
        # The timing proof from the per-tick input log: one save per score
        # that passed the stored record, each on a tick that precedes the
        # abandon, and no game-over save line at all, because no round ended
        # at a game over.
        rule = session + "/mid-round-save-timing"
        if not os.path.isfile(input_log):
            fail(rule, "missing %s" % input_log)
        else:
            with open(input_log) as handle:
                text = handle.read()
            saves = re.findall(r"^record save=(\d+) tick=(\d+) status=(\w+)$",
                               text, re.M)
            abandon = re.search(r"^scene paused -> title tick=(\d+)$",
                                text, re.M)
            problems = []
            if [save[0] for save in saves] != ["36", "68"]:
                problems.append("mid-round saves %s, expected ['36', '68']"
                                % [save[0] for save in saves])
            if any(save[2] != "ok" for save in saves):
                problems.append("a mid-round save did not report ok")
            if abandon is None:
                problems.append("no paused -> title transition line")
            if not problems and saves and abandon:
                last_save_tick = max(int(save[1]) for save in saves)
                if last_save_tick >= int(abandon.group(1)):
                    problems.append(
                        "the last mid-round save tick %d is not before the "
                        "abandon tick %s" % (last_save_tick, abandon.group(1)))
            if "highscore save=" in text:
                problems.append("a game-over save line exists without a game "
                                "over")
            if problems:
                fail(rule, "; ".join(problems))
            else:
                good(rule, "saves %s at ticks %s, abandon at tick %s" % (
                    [save[0] for save in saves],
                    [save[1] for save in saves],
                    abandon.group(1)))
    elif session == "fixture-four-line":
        check_field(by_name["fixture-loaded"], "settled", "36",
                    session + "/fixture-board-loaded")
        check_field(by_name["four-cleared"], "lines", "4",
                    session + "/four-lines")
        check_field(by_name["four-cleared"], "level", "1",
                    session + "/level-after-clear")
        check_field(by_name["four-cleared"], "board",
                    ".".join(["0000"] * 22), session + "/board-emptied")
        hard_drop_arithmetic(session, run_records, "before-drop",
                             "four-cleared", session + "/four-clear-arithmetic")
        check_log(session, log_path, [
            "Clean shutdown",
            "Fixture \"four-line-ready\" applied from",
            "provenance: hand-built for T023",
        ])
    elif session == "fixture-lockout":
        check_field(by_name["lockout-over"], "scene", "gameover",
                    session + "/lockout-scene")
        check_field(by_name["lockout-over"], "active", "none",
                    session + "/lockout-no-piece")
        check_field(by_name["lockout-over"], "settled", "7",
                    session + "/lockout-hidden-cells-committed")
        # The lock-out itself, not a block-out that followed a visible lock:
        # the rotated T committed one cell in hidden row -1 (group 1 of the
        # board masks, y=-2 first) and the queue never dealt a next piece.
        board = by_name["lockout-over"]["board"].split(".")
        if len(board) == 22 and board[1] == "0008":
            good(session + "/lockout-hidden-row-cell", "row -1 mask 0008")
        else:
            fail(session + "/lockout-hidden-row-cell",
                 "row -1 mask is %r of %d groups" %
                 (board[1] if len(board) > 1 else None, len(board)))
        check_field(by_name["lockout-over"], "queue", "Z,L,I,J,S",
                    session + "/lockout-no-spawn-after")
        hard_drop_arithmetic(session, run_records, "before-drop",
                             "lockout-over", session + "/lockout-drop-points")
        check_log(session, log_path, [
            "Clean shutdown", "Fixture \"lockout-ready\" applied from",
        ])
    elif session == "fixture-blockout":
        check_field(by_name["blockout-applied"], "scene", "gameover",
                    session + "/blockout-scene")
        check_field(by_name["blockout-applied"], "settled", "19",
                    session + "/blockout-board")
        check_field(by_name["gameover-at-title"], "menu", "over:1",
                    session + "/gameover-cursor")
        check_log(session, log_path, [
            "Clean shutdown", "Fixture \"blockout-ready\" applied from",
        ])
    elif session == "options-ghost":
        # The options screen round trip (task T041): a round with the ghost at
        # its on default, then SOUND/GHOST rows, one select on GHOST, back to
        # the title with the cursor kept on OPTIONS, and a second round whose
        # well holds no outline while its record still names the landing row.
        check_scene_sequence(run_records, [
            "title", "playing", "title", "title", "options", "options",
            "options", "title", "title", "playing", "title", "title",
            "title"], session + "/scene-sequence")
        check_field(by_name["title-at-options"], "menu", "title:2",
                    session + "/options-item-reached")
        check_field(by_name["options-open"], "menu", "options:0",
                    session + "/options-opens-on-sound")
        check_field(by_name["options-ghost-row"], "menu", "options:1",
                    session + "/ghost-row-reached")
        check_field(by_name["options-toggled"], "menu", "options:1",
                    session + "/select-keeps-the-row")
        check_field(by_name["title-back"], "menu", "title:2",
                    session + "/back-keeps-options-cursor")
        check_field(by_name["title-at-quit"], "menu", "title:3",
                    session + "/title-still-four-items")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        for name in ("ghost-on-first", "ghost-off-first"):
            check_field(by_name[name], "scene", "playing",
                        session + "/%s-scene" % name)
            check_visible_spawn(session, by_name[name],
                                session + "/%s-record" % name)
        check_well_below_piece(session, by_name["ghost-on-first"], True,
                               session + "/well-ghost-on")
        check_well_below_piece(session, by_name["ghost-off-first"], False,
                               session + "/well-ghost-off")
        check_settings_file(session, save_dir, True, False, True, True, 5,
                            False, session + "/saved-settings")
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, os.path.join(run_dir, session, "input.log"), [
            "sound start=on", "ghost start=on", "grid start=on",
            "hold start=on", "next start=5", "grayscale start=off",
            "ghost hidden status=ok",
        ], name="input-log")
    elif session == "options-six-keyboard":
        # The ten-row options screen on the keyboard (tasks T045, T055,
        # T062, T084, and T095): the cursor reaches every row, one select
        # changes each of the four T045 preferences, the cursor reaches
        # ORIENTATION, MUSIC, BACKGROUNDS, and PIECE DESIGNS without
        # selecting them and wraps past PIECE DESIGNS back to SOUND, and the
        # session quits with the settings file holding every new value,
        # landscape, and the designs off.
        check_scene_sequence(run_records, [
            "title", "title"] + ["options"] * 15 + [
            "title", "title", "title"], session + "/scene-sequence")
        check_field(by_name["title-at-options"], "menu", "title:2",
                    session + "/options-item-reached")
        rows = [("options-sound-row", 0), ("options-ghost-row", 1),
                ("options-grid-row", 2), ("options-hold-row", 3),
                ("options-next-row", 4), ("options-grayscale-row", 5),
                ("options-orientation-row", 6), ("options-music-row", 7),
                ("options-backgrounds-row-unselected", 8),
                ("options-piece-designs-row", 9)]
        for name, row in rows:
            check_field(by_name[name], "menu", "options:%d" % row,
                        session + "/row-%d-reached" % row)
        # One select changes the value and never the row; the wrap checkpoint
        # proves the cursor left PIECE DESIGNS downward and landed on SOUND.
        for name, row in [("grid-toggled", 2), ("hold-toggled", 3),
                          ("next-cycled", 4), ("grayscale-toggled", 5),
                          ("options-wrapped-to-sound", 0)]:
            check_field(by_name[name], "menu", "options:%d" % row,
                        session + "/%s-keeps-the-row" % name)
        check_field(by_name["title-back"], "menu", "title:2",
                    session + "/back-keeps-options-cursor")
        check_field(by_name["title-at-quit"], "menu", "title:3",
                    session + "/title-still-four-items")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        # The page itself: every highlighted row proves its own band draws,
        # with the marker on it, and nothing lives below the footer.
        for name, _ in rows + [("options-wrapped-to-sound", 0)]:
            check_options_screen_layout(session, by_name[name],
                                        session + "/layout-%s" % name)
        check_settings_file(session, save_dir, True, True, False, False, 1,
                            True, session + "/saved-settings")
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "grid hidden status=ok", "hold disabled status=ok",
            "next 1 status=ok", "grayscale on status=ok",
        ], name="input-log")
    elif session == "options-six-pad":
        # The same ten rows on the developer virtual controller (tasks T045,
        # T055, T062, T084, and T095), the input path of scene-cycle-pad: A
        # changes each of the four T045 preferences - NEXT PIECES three
        # times, 5 -> 1 -> 2 -> 3 - the cursor reaches ORIENTATION, MUSIC,
        # BACKGROUNDS, and PIECE DESIGNS without selecting them and wraps
        # past PIECE DESIGNS, and a round then proves the pad's own hold
        # inputs, Y and the left shoulder, change nothing while HOLD is off.
        check_scene_sequence(run_records, [
            "title", "title"] + ["options"] * 17 + [
            "title", "title", "playing", "playing", "playing", "playing",
            "playing", "playing", "paused", "paused", "title", "title",
            "title"], session + "/scene-sequence")
        check_field(by_name["title-at-options"], "menu", "title:2",
                    session + "/options-item-reached")
        for name, row in [("options-sound-row", 0), ("options-ghost-row", 1),
                          ("options-grid-row", 2), ("options-hold-row", 3),
                          ("options-next-row", 4),
                          ("options-grayscale-row", 5),
                          ("options-orientation-row", 6),
                          ("options-music-row", 7),
                          ("options-backgrounds-row-unselected", 8),
                          ("options-piece-designs-row", 9)]:
            check_field(by_name[name], "menu", "options:%d" % row,
                        session + "/row-%d-reached" % row)
            check_options_screen_layout(session, by_name[name],
                                        session + "/layout-%s" % name)
        check_field(by_name["options-wrapped-to-sound"], "menu", "options:0",
                    session + "/wrap-past-music")
        check_options_screen_layout(session, by_name["options-wrapped-to-sound"],
                                    session + "/layout-wrapped")
        for name in ("grid-toggled", "hold-toggled", "next-one", "next-two",
                     "next-three", "grayscale-toggled"):
            row = 4 if name.startswith("next") else (
                2 if name == "grid-toggled" else
                3 if name == "hold-toggled" else 5)
            check_field(by_name[name], "menu", "options:%d" % row,
                        session + "/%s-keeps-the-row" % name)
        check_field(by_name["title-back"], "menu", "title:2",
                    session + "/back-keeps-options-cursor")
        check_field(by_name["title-at-start"], "menu", "title:0",
                    session + "/back-to-start")
        check_field(by_name["playing-start"], "scene", "playing",
                    session + "/pad-starts-round")
        check_hold_ignored(session, run_records, "before-hold-y", "after-hold-y",
                           session + "/pad-y-ignored")
        check_hold_ignored(session, run_records, "before-hold-l1",
                           "after-hold-l1", session + "/pad-l1-ignored")
        check_hold_group_absent(session, by_name["hold-group-absent"],
                                session + "/hold-preview-absent")
        check_field(by_name["paused-at-quit"], "menu", "pause:3",
                    session + "/abandon-item")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        # HOLD was never enabled again, so not one tick of this session may
        # carry a hold edge into the round.
        check_no_hold_edges(session, input_log, session + "/no-hold-edge")
        check_settings_file(session, save_dir, True, True, False, False, 3,
                            True, session + "/saved-settings")
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
            "Scene title -> playing", "Scene playing -> paused",
            "Scene paused -> title",
        ])
        check_log(session, input_log, [
            "grid hidden status=ok", "hold disabled status=ok",
            "next 1 status=ok", "next 2 status=ok", "next 3 status=ok",
            "grayscale on status=ok",
        ], name="input-log")
    elif session == "music-settings":
        # The music settings page behind the MUSIC row (task T062, MUSIC.md
        # player controls): the page opens on its MUSIC row, one select per
        # row cycles the mode Auto -> Arcade, steps both volumes once, and
        # requests another track, back returns to the options screen on the
        # MUSIC row, and the session quits with the settings file holding
        # arcade and the new volume steps.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options"] + ["musicoptions"] * 8 + [
            "options", "title", "title", "title"],
            session + "/scene-sequence")
        check_field(by_name["title-at-options"], "menu", "title:2",
                    session + "/options-item-reached")
        check_field(by_name["options-open"], "menu", "options:0",
                    session + "/options-opens-on-sound")
        check_field(by_name["options-music-row"], "menu", "options:7",
                    session + "/music-row-reached")
        check_options_screen_layout(session, by_name["options-music-row"],
                                    session + "/layout-options-music-row")
        # The page itself: it opens on its MUSIC row, the cursor walks the
        # volume and action rows, a select never moves it, and every record
        # proves the four rows, the heading, the footer, and the quiet canvas.
        check_field(by_name["music-page-open"], "scene", "musicoptions",
                    session + "/page-opens")
        for name, row in [("music-page-open", 0), ("music-mode-cycled", 0),
                          ("music-volume-row", 1), ("music-volume-up", 1),
                          ("music-effects-row", 2), ("music-effects-up", 2),
                          ("music-next-row", 3), ("music-next-track", 3)]:
            check_field(by_name[name], "scene", "musicoptions",
                        session + "/%s-stays-on-the-page" % name)
            check_field(by_name[name], "menu", "music:%d" % row,
                        session + "/%s-keeps-the-row" % name)
            check_music_page_layout(session, by_name[name],
                                    session + "/layout-%s" % name)
        # Back lands on the MUSIC row of the options screen, and back again
        # leaves through the title exactly as the seven-row era did.
        check_field(by_name["options-back-on-music"], "menu", "options:7",
                    session + "/back-keeps-music-row")
        check_options_screen_layout(session, by_name["options-back-on-music"],
                                    session + "/layout-back-on-music")
        check_field(by_name["title-back"], "menu", "title:2",
                    session + "/back-keeps-options-cursor")
        check_field(by_name["title-at-quit"], "menu", "title:3",
                    session + "/title-still-four-items")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        # The saved file: every row's change persisted, the Next track action
        # changed nothing, and the seven earlier preferences ride along.
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings",
                            music_mode="arcade", music_volume=7,
                            effects_volume=9)
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> musicoptions",
            "Scene musicoptions -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "music start=auto", "musicvolume start=6",
            "effectsvolume start=8", "music arcade status=ok",
            "musicvolume 7 status=ok", "effectsvolume 9 status=ok",
            "music next track",
        ], name="input-log")
    elif session == "music-settings-pad":
        # The same page on the developer virtual controller (task T062): the
        # pad opens the page from the MUSIC row, A cycles the mode once, the
        # D-pad walks to NEXT TRACK and A runs it, B returns to the options
        # screen on the MUSIC row, and the session quits with arcade saved
        # and both volumes at their defaults.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options"] + ["musicoptions"] * 4 + [
            "options", "title", "title", "title"],
            session + "/scene-sequence")
        check_field(by_name["options-music-row"], "menu", "options:7",
                    session + "/music-row-reached")
        check_options_screen_layout(session, by_name["options-music-row"],
                                    session + "/layout-options-music-row")
        for name, row in [("music-page-open", 0), ("music-mode-cycled", 0),
                          ("music-next-row", 3), ("music-next-track", 3)]:
            check_field(by_name[name], "scene", "musicoptions",
                        session + "/%s-stays-on-the-page" % name)
            check_field(by_name[name], "menu", "music:%d" % row,
                        session + "/%s-keeps-the-row" % name)
            check_music_page_layout(session, by_name[name],
                                    session + "/layout-%s" % name)
        check_field(by_name["options-back-on-music"], "menu", "options:7",
                    session + "/back-keeps-music-row")
        check_field(by_name["title-back"], "menu", "title:2",
                    session + "/back-keeps-options-cursor")
        check_field(by_name["title-at-quit"], "menu", "title:3",
                    session + "/title-still-four-items")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings",
                            music_mode="arcade")
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> musicoptions",
            "Scene musicoptions -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "music start=auto", "musicvolume start=6",
            "effectsvolume start=8", "music arcade status=ok",
            "music next track",
        ], name="input-log")
    elif session == "backgrounds-keyboard":
        # The backgrounds page behind the BACKGROUNDS row on the keyboard
        # (task T084): the page opens on its MODE row, the mode ring walks
        # Auto -> Off -> Fixed, the DESIGN row walks every stable ID 1..20
        # and wraps back to 1 before ending on 7 - every select of the walk a
        # saved Fixed ID - and the mode then steps Auto and lands on Off with
        # the preferred design kept. B returns to the options screen on the
        # BACKGROUNDS row. The saved file holds off and 7.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options"] +
            ["backgrounds"] * 33 + ["options", "title", "title", "title"],
            session + "/scene-sequence")
        check_field(by_name["title-at-options"], "menu", "title:2",
                    session + "/options-item-reached")
        check_field(by_name["options-open"], "menu", "options:0",
                    session + "/options-opens-on-sound")
        check_field(by_name["options-backgrounds-row"], "menu", "options:8",
                    session + "/backgrounds-row-reached")
        check_options_screen_layout(session, by_name["options-backgrounds-row"],
                                    session + "/layout-options-backgrounds-row")
        # The page itself: every checkpoint proves the two rows, the heading,
        # the footer, and the quiet canvas, and a select never moves the
        # cursor.
        page_checks = [("backgrounds-page-open", 0),
                       ("backgrounds-mode-off", 0),
                       ("backgrounds-mode-fixed", 0),
                       ("backgrounds-design-row", 1)]
        for design in range(2, 21):
            page_checks.append(("backgrounds-design-%02d" % design, 1))
        page_checks += [("backgrounds-design-wrap", 1),
                        ("backgrounds-design-02b", 1),
                        ("backgrounds-design-03b", 1),
                        ("backgrounds-design-04b", 1),
                        ("backgrounds-design-05b", 1),
                        ("backgrounds-design-06b", 1),
                        ("backgrounds-design-07b", 1),
                        ("backgrounds-mode-row-again", 0),
                        ("backgrounds-mode-auto", 0),
                        ("backgrounds-mode-off-final", 0)]
        for name, row in page_checks:
            check_field(by_name[name], "scene", "backgrounds",
                        session + "/%s-stays-on-the-page" % name)
            check_field(by_name[name], "menu", "backgrounds:%d" % row,
                        session + "/%s-keeps-the-row" % name)
            check_backgrounds_page_layout(session, by_name[name],
                                          session + "/layout-%s" % name)
        # Back lands on the BACKGROUNDS row of the options screen, and back
        # again leaves through the title.
        check_field(by_name["options-back-on-backgrounds"], "menu", "options:8",
                    session + "/back-keeps-backgrounds-row")
        check_options_screen_layout(
            session, by_name["options-back-on-backgrounds"],
            session + "/layout-back-on-backgrounds")
        check_field(by_name["title-back"], "menu", "title:2",
                    session + "/back-keeps-options-cursor")
        check_field(by_name["title-at-quit"], "menu", "title:3",
                    session + "/title-still-four-items")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        # The saved file: Off keeps the preferred design, and every earlier
        # preference rides along at its default.
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings",
                            background_mode="off", background_id=7)
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> backgrounds",
            "Scene backgrounds -> options", "Scene options -> title",
        ])
        # The per-tick input log carries the whole design ring in order - one
        # line per select, 2 through 20, the wrap to 1, and the walk to 7 -
        # then the mode ring and the final Off, each with its save status.
        ring = ["background %s status=ok" % word
                for word in ("off", "fixed")]
        ring += ["backgroundid %d status=ok" % design
                 for design in list(range(2, 21)) + [1] + list(range(2, 8))]
        ring += ["background auto status=ok", "background off status=ok"]
        check_log(session, input_log, [
            "background start=auto", "backgroundid start=1",
        ] + ring, name="input-log")
    elif session == "backgrounds-pad":
        # The same page on the developer virtual controller (task T084), with
        # the same-session application the acceptance names: a round opened
        # after the mode landed Off draws the legacy flat wallpaper on its
        # first play frame, and a round opened after Fixed draws the retained
        # design 2 on the next play frame of the same session.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options"] +
            ["backgrounds"] * 4 + ["options", "title", "playing", "paused",
            "paused", "title", "options"] +
            ["backgrounds"] * 2 + ["options", "title", "playing", "paused",
            "title", "options"] +
            ["backgrounds"] * 2 + ["title", "playing", "paused",
            "title", "title", "title"],
            session + "/scene-sequence")
        check_field(by_name["options-backgrounds-row"], "menu", "options:8",
                    session + "/backgrounds-row-reached")
        check_options_screen_layout(session, by_name["options-backgrounds-row"],
                                    session + "/layout-options-backgrounds-row")
        for name, row in [("backgrounds-page-open", 0),
                          ("backgrounds-mode-off", 0),
                          ("backgrounds-design-row", 1),
                          ("backgrounds-design-02", 1)]:
            check_field(by_name[name], "scene", "backgrounds",
                        session + "/%s-stays-on-the-page" % name)
            check_field(by_name[name], "menu", "backgrounds:%d" % row,
                        session + "/%s-keeps-the-row" % name)
            check_backgrounds_page_layout(session, by_name[name],
                                          session + "/layout-%s" % name)
        for name, row in [("backgrounds-page-reopen", 0),
                          ("backgrounds-mode-fixed", 0),
                          ("backgrounds-page-auto-open", 0),
                          ("backgrounds-mode-auto", 0)]:
            check_field(by_name[name], "scene", "backgrounds",
                        session + "/%s-stays-on-the-page" % name)
            check_field(by_name[name], "menu", "backgrounds:%d" % row,
                        session + "/%s-keeps-the-row" % name)
            check_backgrounds_page_layout(session, by_name[name],
                                          session + "/layout-%s" % name)
        check_field(by_name["options-back-on-backgrounds"], "menu", "options:8",
                    session + "/back-keeps-backgrounds-row")
        check_field(by_name["options-back-on-backgrounds-again"], "menu",
                    "options:8", session + "/back-keeps-backgrounds-again")
        # The play frames: Off draws the legacy flat ground everywhere the
        # probes look, and Fixed draws the design 2 base #10131C.
        # The probes sit in the quiet span and the two 24 px guard gaps
        # beside the frame, clear of the HUD groups and of every shape the
        # three pinned frames draw there.
        check_play_wallpaper_color(
            session, by_name["playing-off-flat"],
            session + "/play-frame-off-flat",
            [(250, 120), (628, 120), (330, 460), (628, 460)],
            BACKGROUND_COLOR, "legacy flat Off")
        check_play_wallpaper_color(
            session, by_name["playing-fixed-2"],
            session + "/play-frame-fixed-2",
            [(250, 120), (628, 120), (330, 460), (628, 460)],
            (0x10, 0x13, 0x1C), "fixed design 2")
        # Auto at the level 1 boundary draws the design 1 grammar, Still
        # ground: the base equals the legacy flat, and the corner steps paint
        # the darker ink #0C0E16 over the canvas corners, so the ink probe
        # tells Auto from Off while the base probes tell it from design 2.
        check_play_wallpaper_color(
            session, by_name["playing-auto-1"],
            session + "/play-frame-auto-1",
            [(5, 5), (5, 538), (954, 5), (954, 538)], (0x0C, 0x0E, 0x16),
            "Auto level 1 corner ink")
        check_play_wallpaper_color(
            session, by_name["playing-auto-1"],
            session + "/play-frame-auto-1-base",
            [(250, 120), (628, 120), (330, 460), (628, 460)],
            (0x10, 0x12, 0x1A), "Auto level 1 base")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        # The mode changes saved Off, Fixed, then Auto in order, both design
        # selects kept the mode, and the file holds auto and the retained 2.
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings",
                            background_mode="auto", background_id=2)
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> backgrounds",
            "Scene backgrounds -> options", "Scene options -> title",
            "Scene title -> playing", "Scene playing -> paused",
            "Scene paused -> title",
        ])
        check_log(session, input_log, [
            "background start=auto", "backgroundid start=1",
            "background off status=ok", "backgroundid 2 status=ok",
            "background fixed status=ok", "background auto status=ok",
        ], name="input-log")
    elif session == "backgrounds-relaunch":
        # Three relaunches over one save directory (task T084): stage one
        # starts from the saved pair portrait / fixed / 7 and moves the
        # preference to 8, stage two starts from the corrupted
        # backgroundid=99 file and changes nothing, and stage three starts
        # from the legacy eleven-line music-era file, receives the new
        # defaults, and upgrades it on its one save.
        # Stage one: the relaunch applied portrait and Fixed 7.
        stage1 = {r["check"]: r for r in records["stage1"]}
        check_field(stage1["options-backgrounds-row"], "menu", "options:8",
                    session + "/stage1-backgrounds-row")
        for name, row in [("backgrounds-page-portrait-open", 0),
                          ("backgrounds-design-row-portrait", 1),
                          ("backgrounds-design-08-portrait", 1)]:
            check_field(stage1[name], "scene", "backgrounds",
                        session + "/stage1-%s-stays-on-the-page" % name)
            check_field(stage1[name], "menu", "backgrounds:%d" % row,
                        session + "/stage1-%s-keeps-the-row" % name)
            check_backgrounds_page_layout_portrait(
                session, stage1[name], session + "/stage1-layout-%s" % name)
        # The play frame of the relaunch draws the saved fixed design 7 in
        # portrait. The probes are device points of base-only windows the
        # design's portrait grammar leaves flat: the right margin below its
        # logical top rows (logical x about 513, y 480), the below-board
        # strip (logical y about 940), and a quiet span of the HUD strip
        # between its boxes (logical x about 293, y 60) - through the
        # rotation, portrait (x, y) reaches the device canvas at (y, 543-x).
        check_play_wallpaper_color(
            session, stage1["playing-fixed-7-portrait"],
            session + "/stage1-play-frame-fixed-7",
            [(490, 20), (940, 250), (60, 250)],
            (0x11, 0x15, 0x1F), "fixed design 7")
        check_settings_path(session, stages[0]["after"], True, True, True,
                            True, 5, False,
                            session + "/stage1-settings-after",
                            orientation="portrait",
                            background_mode="fixed", background_id=8)
        check_log(session, stages[0]["log"], [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "ok",
                              "portrait", background_mode="fixed",
                              background_id=7),
            "Orientation: portrait",
        ])
        check_log(session, stages[0]["input_log"], [
            "background start=fixed", "backgroundid start=7",
            "backgroundid 8 status=ok",
        ], name="stage1-input-log")
        # Stage two: the corrupted ID recovers to the defaults, and the read
        # rewrites nothing - the file keeps its exact broken bytes.
        stage2 = {r["check"]: r for r in records["stage2"]}
        check_field(stage2["backgrounds-page-open"], "scene", "backgrounds",
                    session + "/stage2-page-opens")
        check_backgrounds_page_layout(
            session, stage2["backgrounds-page-open"],
            session + "/stage2-layout-page")
        broken_bytes = ("version=1\nsound=on\nghost=on\ngrid=on\nhold=on\n"
                        "next=5\ngrayscale=off\norientation=landscape\n"
                        "music=auto\nmusicvolume=6\neffectsvolume=8\n"
                        "background=fixed\nbackgroundid=99\n")
        with open(stages[1]["after"]) as handle:
            after_bytes = handle.read()
        if after_bytes != broken_bytes:
            fail(session + "/stage2-broken-kept",
                 "the corrupted file changed: %r" % after_bytes)
        else:
            good(session + "/stage2-broken-kept",
                 "the load rewrote nothing; the broken bytes survive")
        check_log(session, stages[1]["log"], [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "invalid"),
        ])
        check_log(session, stages[1]["input_log"], [
            "background start=auto", "backgroundid start=1",
        ], name="stage2-input-log")
        # Stage three: the legacy eleven-line file keeps every saved value,
        # receives the new defaults, and one save upgrades it to the current
        # canonical form.
        stage3 = {r["check"]: r for r in records["stage3"]}
        check_field(stage3["backgrounds-mode-off"], "scene", "backgrounds",
                    session + "/stage3-mode-row-change")
        check_field(stage3["backgrounds-mode-off"], "menu", "backgrounds:0",
                    session + "/stage3-select-keeps-the-row")
        check_backgrounds_page_layout(
            session, stage3["backgrounds-mode-off"],
            session + "/stage3-layout-mode-off")
        check_settings_path(session, stages[2]["after"], False, False, True,
                            True, 3, True,
                            session + "/stage3-settings-upgraded",
                            music_mode="focus", music_volume=2,
                            effects_volume=9, background_mode="off",
                            background_id=1)
        check_log(session, stages[2]["log"], [
            "Clean shutdown",
            settings_log_line(False, False, True, True, 3, True, "ok",
                              music_mode="focus", music_volume=2,
                              effects_volume=9),
        ])
        check_log(session, stages[2]["input_log"], [
            "background start=auto", "backgroundid start=1",
            "background off status=ok",
        ], name="stage3-input-log")
    elif session == "hold-off":
        # The HOLD preference against specification sections 4.3 and 5 (task
        # T045): with HOLD off, both mapped keyboard hold inputs change
        # nothing and spend no lock while the Hold group draws nothing at all;
        # with HOLD back on, the first press swaps and locks and the second is
        # the no-op the once-per-piece rule makes it.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options", "options", "title",
            "title", "playing", "playing", "playing", "playing", "playing",
            "playing", "paused", "paused", "title", "title",
            "options", "options", "options", "title", "title", "playing",
            "playing", "playing", "playing", "playing", "playing", "paused",
            "paused", "title", "title", "title"], session + "/scene-sequence")
        check_field(by_name["options-hold-row"], "menu", "options:3",
                    session + "/hold-row-reached")
        check_field(by_name["hold-toggled"], "menu", "options:3",
                    session + "/select-keeps-the-row")
        # B followed by A reopens the screen on the row it left, so the second
        # visit starts on HOLD with no downs in between.
        check_field(by_name["options-open-again"], "menu", "options:3",
                    session + "/reopen-keeps-the-row")
        check_field(by_name["options-hold-row-again"], "menu", "options:3",
                    session + "/hold-row-reached-again")
        check_field(by_name["hold-back-on"], "menu", "options:3",
                    session + "/second-select-keeps-the-row")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        check_hold_ignored(session, run_records, "before-hold-c",
                           "after-hold-c", session + "/key-c-ignored")
        check_hold_ignored(session, run_records, "before-hold-lshift",
                           "after-hold-lshift", session + "/key-lshift-ignored")
        check_hold_group_absent(session, by_name["hold-group-absent"],
                                session + "/hold-preview-absent")
        # HOLD on: the first press swaps in the queue head and spends the
        # lock, the second changes nothing, and the preview returns.
        first, second = by_name["before-hold-on"], by_name["after-hold-on"]
        if second.get("hold") in (None, "none") or not second["hold"].endswith(",locked"):
            fail(session + "/hold-swapped", "hold is %s after the first press, "
                 "expected <id>,locked" % second.get("hold"))
        else:
            good(session + "/hold-swapped", "hold=%s" % second["hold"])
        if first.get("active") == second.get("active"):
            fail(session + "/hold-swap-changed-piece",
                 "the active piece is %s on both sides of the swap" %
                 first.get("active"))
        else:
            good(session + "/hold-swap-changed-piece",
                 "%s -> %s" % (first.get("active"), second.get("active")))
        hold_restriction(session, run_records, "after-hold-on",
                         "second-hold-ignored", session + "/hold-once")
        check_hold_group(session, by_name["hold-group-present"], True, False,
                         session + "/hold-preview-present")
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings")
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
            "Scene title -> playing", "Scene playing -> paused",
            "Scene paused -> title",
        ])
        check_log(session, input_log, [
            "hold start=on", "hold disabled status=ok",
            "hold enabled status=ok",
        ], name="input-log")
    elif session == "grayscale":
        # The GRAYSCALE preference against specification section 6.2 step 5
        # (task T045): one round with settled cells, a live piece, a ghost, a
        # stashed Hold piece, and five previews drawn all gray while the text
        # and the board outline keep their colors, then the same shape of
        # round back in color as the positive control.
        check_scene_sequence(run_records, [
            "title", "title", "options", "options", "options", "title",
            "title", "playing", "playing", "playing", "playing", "paused",
            "paused", "title", "title", "options", "options", "options",
            "title", "title", "playing", "playing", "playing", "playing",
            "paused", "paused", "title", "title", "title"],
            session + "/scene-sequence")
        check_field(by_name["options-grayscale-row"], "menu", "options:5",
                    session + "/grayscale-row-reached")
        check_field(by_name["grayscale-toggled"], "menu", "options:5",
                    session + "/select-keeps-the-row")
        # B followed by A reopens the screen on the row it left.
        check_field(by_name["options-open-again"], "menu", "options:5",
                    session + "/reopen-keeps-the-row")
        check_field(by_name["options-grayscale-row-again"], "menu", "options:5",
                    session + "/grayscale-row-reached-again")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        for prefix, gray in (("gray-scene", True), ("color-scene", False)):
            check_play_scene_colors(session, by_name[prefix], gray,
                                    session + "/%s-piece-colors" % prefix)
            check_ghost_readable(session, by_name[prefix], gray,
                                 session + "/%s-ghost" % prefix)
            check_hold_group(session, by_name[prefix], True, gray,
                             session + "/%s-hold-preview" % prefix)
            check_next_previews(session, by_name[prefix], 5, gray,
                                session + "/%s-next-previews" % prefix)
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings")
        check_log(session, log_path, [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "grayscale on status=ok", "grayscale off status=ok",
        ], name="input-log")
    elif session == "piece-designs-keyboard":
        # The PIECE DESIGNS preference on the keyboard (task T095): nine downs
        # reach the tenth row, one select turns the in-cell marks on, and a
        # round over the labeled all-pieces fixture draws every mark in color
        # - seven distinct shapes, one per piece type, and no dark pixel left
        # once the same select walks the value off for the control round.
        # The final visit turns the marks back on, so the file the session
        # leaves holds piecedesigns=on.
        check_scene_sequence(run_records, [
            "title", "title"] + ["options"] * 3 + ["title", "title"] +
            ["playing"] * 2 + ["paused"] * 2 + ["title", "title"] +
            ["options"] * 2 + ["title", "title"] + ["playing"] * 2 +
            ["paused"] * 2 + ["title", "title"] + ["options"] * 3 +
            ["title", "title", "title"], session + "/scene-sequence")
        for name, field, wanted in [
                ("title-at-options", "menu", "title:2"),
                ("options-open", "menu", "options:0"),
                ("options-piece-designs-row", "menu", "options:9"),
                ("designs-on", "menu", "options:9"),
                ("title-back-on", "menu", "title:2"),
                ("title-at-start", "menu", "title:0"),
                ("paused-on", "menu", "pause:0"),
                ("paused-at-quit", "menu", "pause:3"),
                ("title-after-round", "menu", "title:0"),
                ("title-at-options-again", "menu", "title:2"),
                ("options-on-same-row", "menu", "options:9"),
                ("designs-off", "menu", "options:9"),
                ("title-back-off", "menu", "title:2"),
                ("title-at-start-off", "menu", "title:0"),
                ("paused-off", "menu", "pause:0"),
                ("paused-at-quit-off", "menu", "pause:3"),
                ("title-after-off-round", "menu", "title:0"),
                ("title-at-options-final", "menu", "title:2"),
                # B then A reopens the screen on the row it left.
                ("options-open-final", "menu", "options:9"),
                ("options-on-same-row-final", "menu", "options:9"),
                ("designs-on-again", "menu", "options:9"),
                ("title-back-final", "menu", "title:2"),
                ("title-at-quit", "menu", "title:3"),
                ("quit-confirmed", "quit", "1")]:
            check_field(by_name[name], field, wanted,
                        session + "/%s" % name)
        # The page itself with the row highlighted, on and off.
        for name in ("options-open", "options-piece-designs-row", "designs-on",
                     "options-on-same-row", "designs-off"):
            check_options_screen_layout(session, by_name[name],
                                        session + "/layout-%s" % name)
        # One cell of every piece type, on and off: the marks read from
        # pixels and the control frame proves the flat cell draws none.
        for name in ("designs-frame", "plain-frame"):
            check_field(by_name[name], "settled", "28",
                        session + "/%s-fixture-board" % name)
        check_piece_design_marks(session, by_name["designs-frame"], False,
                                 False, session + "/marks-in-color")
        check_piece_design_marks(session, by_name["plain-frame"], False,
                                 False, session + "/plain-control",
                                 expect_designs=False)
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings",
                            piece_designs=True)
        check_log(session, log_path, [
            "Clean shutdown",
            # The startup line names the values the session began with:
            # the designs only turn on inside it.
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "piecedesigns start=off",
            "piecedesigns on status=ok", "piecedesigns off status=ok",
        ], name="input-log")
    elif session == "piece-designs-pad":
        # The same PIECE DESIGNS row on the developer virtual controller
        # (task T095), the input path of scene-cycle-pad: the D-pad walks the
        # ten rows, A turns the marks on, and the fixture round draws every
        # mark through the controller-driven play path. The session ends with
        # the saved file holding piecedesigns=on.
        check_scene_sequence(run_records, [
            "title", "title"] + ["options"] * 3 + ["title", "title"] +
            ["playing"] * 2 + ["paused"] * 2 +
            ["title", "title", "title"], session + "/scene-sequence")
        for name, field, wanted in [
                ("title-at-options", "menu", "title:2"),
                ("options-open", "menu", "options:0"),
                ("options-piece-designs-row", "menu", "options:9"),
                ("designs-on", "menu", "options:9"),
                ("title-back", "menu", "title:2"),
                ("title-at-start", "menu", "title:0"),
                ("playing-start", "scene", "playing"),
                ("paused", "menu", "pause:0"),
                ("paused-at-quit", "menu", "pause:3"),
                ("title-after-round", "menu", "title:0"),
                ("title-at-quit", "menu", "title:3"),
                ("quit-confirmed", "quit", "1")]:
            check_field(by_name[name], field, wanted,
                        session + "/%s" % name)
        for name in ("options-open", "options-piece-designs-row", "designs-on"):
            check_options_screen_layout(session, by_name[name],
                                        session + "/layout-%s" % name)
        check_field(by_name["designs-frame"], "settled", "28",
                    session + "/designs-frame-fixture-board")
        check_piece_design_marks(session, by_name["designs-frame"], False,
                                 False, session + "/marks-in-color")
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            False, session + "/saved-settings",
                            piece_designs=True)
        check_log(session, log_path, [
            "Clean shutdown",
            # The startup line names the values the session began with:
            # the designs only turn on inside it.
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "piecedesigns start=off", "piecedesigns on status=ok",
        ], name="input-log")
    elif session == "piece-designs-gray":
        # The per-piece designs where they matter most (task T095): one
        # options visit turns GRAYSCALE and PIECE DESIGNS on together, and
        # the fixture frame proves every mark reads in gray - seven distinct
        # shapes with the piece colors gone. The same session then turns
        # portrait through the ORIENTATION row and draws the same fixture
        # again, so the checker reads the marks through the fixed rotation
        # too, and quits through the turned menu roles.
        check_scene_sequence(run_records, [
            "title", "title"] + ["options"] * 5 + ["title", "title"] +
            ["playing"] * 2 + ["paused"] * 2 + ["title", "title"] +
            ["options"] * 3 + ["title", "title"] + ["playing"] * 2 +
            ["paused"] * 2 + ["title", "title", "title"],
            session + "/scene-sequence")
        for name, field, wanted in [
                ("title-at-options", "menu", "title:2"),
                ("options-open", "menu", "options:0"),
                ("options-grayscale-row", "menu", "options:5"),
                ("grayscale-on", "menu", "options:5"),
                ("options-piece-designs-row", "menu", "options:9"),
                ("designs-on", "menu", "options:9"),
                ("title-back", "menu", "title:2"),
                ("title-at-start", "menu", "title:0"),
                ("playing-start", "scene", "playing"),
                ("paused", "menu", "pause:0"),
                ("paused-at-quit", "menu", "pause:3"),
                ("title-after-round", "menu", "title:0"),
                ("title-at-options-again", "menu", "title:2"),
                # The turned session reopens on the row it left, too.
                ("options-open-again", "menu", "options:9"),
                ("options-orientation-row", "menu", "options:6"),
                ("turned-portrait", "menu", "options:6"),
                ("title-turned", "menu", "title:2"),
                ("title-turned-at-start", "menu", "title:0"),
                ("portrait-playing", "scene", "playing"),
                ("portrait-paused", "menu", "pause:0"),
                ("portrait-paused-at-quit", "menu", "pause:3"),
                ("portrait-title-after-round", "menu", "title:0"),
                ("title-turned-at-quit", "menu", "title:3"),
                ("quit-confirmed", "quit", "1")]:
            check_field(by_name[name], field, wanted,
                        session + "/%s" % name)
        check_options_screen_layout(session, by_name["options-open"],
                                    session + "/layout-options-open")
        check_portrait_options_layout(session, by_name["turned-portrait"],
                                      session + "/layout-turned")
        for name in ("gray-designs-frame", "portrait-gray-designs-frame"):
            check_field(by_name[name], "settled", "28",
                        session + "/%s-fixture-board" % name)
        check_piece_design_marks(session, by_name["gray-designs-frame"], True,
                                 False, session + "/marks-in-gray")
        check_piece_design_marks(
            session, by_name["portrait-gray-designs-frame"], True, True,
            session + "/marks-in-gray-portrait")
        check_settings_file(session, save_dir, True, True, True, True, 5,
                            True, session + "/saved-settings",
                            orientation="portrait", piece_designs=True)
        check_log(session, log_path, [
            "Clean shutdown",
            # The startup line names landscape and the designs off: the
            # session turns grayscale, the designs, and portrait itself.
            settings_log_line(True, True, True, True, 5, False, "not found"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, input_log, [
            "grayscale start=off", "piecedesigns start=off",
            "grayscale on status=ok", "piecedesigns on status=ok",
        ], name="input-log")
    elif session == "options-relaunch":
        # Four relaunches over one save directory (task T045): each stage
        # toggles one of the four new preferences and quits, so the snapshot
        # after every stage is one exact canonical rendering, the snapshot
        # before the next stage is the same file a relaunch read, and every
        # startup line names the values that round restored.
        stage_values = [
            ("grid", (True, True, False, True, 5, False)),
            ("hold", (True, True, False, False, 5, False)),
            ("next", (True, True, False, False, 1, False)),
            ("grayscale", (True, True, False, False, 1, True)),
        ]
        fresh_defaults = (True, True, True, True, 5, False)
        stage_change = {
            "grid": "grid hidden status=ok",
            "hold": "hold disabled status=ok",
            "next": "next 1 status=ok",
            "grayscale": "grayscale on status=ok",
        }
        for index, (name, values) in enumerate(stage_values):
            stage = stages[index]
            label = stage["label"]
            stage_records = records[label]
            stage_by_name = {r["check"]: r for r in stage_records}
            # The values this stage starts from: the defaults of a fresh save
            # directory in stage one, the file the previous stage left behind
            # in every later one.
            restored = fresh_defaults if index == 0 else stage_values[index - 1][1]
            status = "not found" if index == 0 else "ok"
            check_field(stage_by_name["options-row"], "menu",
                        "options:%d" % (index + 2),
                        "%s/%s-row" % (session, name))
            check_field(stage_by_name["value-changed"], "menu",
                        "options:%d" % (index + 2),
                        "%s/%s-select-keeps-the-row" % (session, name))
            check_field(stage_by_name["quit-confirmed"], "quit", "1",
                        "%s/%s-quit" % (session, name))
            check_settings_path(session, stage["after"], *values,
                                rule="%s/%s-settings-bytes" % (session, name))
            if index > 0:
                check_settings_path(
                    session, stage["before"], *stage_values[index - 1][1],
                    rule="%s/%s-relaunch-read" % (session, name))
            check_log(session, stage["log"], [
                "Clean shutdown",
                settings_log_line(restored[0], restored[1], restored[2],
                                  restored[3], restored[4], restored[5],
                                  status),
                "Scene title -> options", "Scene options -> title",
            ])
            check_log(session, stage["input_log"], [
                "next start=%d" % restored[4],
                "grayscale start=%s" % ("on" if restored[5] else "off"),
                stage_change[name],
            ], name="%s/%s-input-log" % (session, name))
        check_settings_path(session, stages[-1]["after"], *stage_values[-1][1],
                            rule=session + "/final-settings")
    elif session == "options-legacy":
        # The two legacy settings files (task T045, specification section 9):
        # a three-line file of the sound-and-ghost era loads with both saved
        # values plus the four defaults and its next save upgrades the file
        # without losing either, and a two-line file of the sound-only era
        # loads with its saved sound and every later preference at its default
        # and is left untouched, because a read never writes.
        three_line = "version=1\nsound=off\nghost=off\n"
        two_line = "version=1\nsound=off\n"
        stage = stages[0]
        check_field({r["check"]: r for r in records["stage1"]}["quit-confirmed"],
                    "quit", "1", session + "/three-line-quit")
        check_settings_bytes(session, stage["before"], three_line,
                             session + "/three-line-staged")
        check_log(session, stage["log"], [
            "Clean shutdown",
            settings_log_line(False, False, True, True, 5, False, "ok"),
            "Scene title -> options", "Scene options -> title",
        ])
        check_log(session, stage["input_log"], [
            "sound start=off", "ghost start=off", "grid start=on",
            "hold start=on", "next start=5", "grayscale start=off",
            "grid hidden status=ok",
        ], name=session + "/three-input-log")
        check_settings_path(session, stage["after"], False, False, False,
                            True, 5, False,
                            rule=session + "/three-line-upgraded")
        stage = stages[1]
        stage_by_name = {r["check"]: r for r in records["stage2"]}
        check_field(stage_by_name["quit-confirmed"], "quit", "1",
                    session + "/two-line-quit")
        check_settings_bytes(session, stage["before"], two_line,
                             session + "/two-line-staged")
        check_log(session, stage["log"], [
            "Clean shutdown",
            settings_log_line(False, True, True, True, 5, False, "ok"),
        ])
        check_log(session, stage["input_log"], [
            "sound start=off", "ghost start=on", "grid start=on",
            "hold start=on", "next start=5", "grayscale start=off",
        ], name=session + "/two-input-log")
        check_settings_bytes(session, stage["after"], two_line,
                             session + "/two-line-untouched")
    elif session == "next-count":
        # Five relaunches of one script (task T045): every stage selects the
        # NEXT PIECES row once, so the ring walks 5 -> 1 -> 2 -> 3 -> 4 -> 5
        # across the processes, each stage's round is byte-identical to the
        # others at the same seed, and its play scene shows exactly its count
        # of previews in queue order, in color and dim as the column draws
        # them.
        for index, stage in enumerate(stages):
            label = stage["label"]
            count = index + 1
            stage_records = records[label]
            stage_by_name = {r["check"]: r for r in stage_records}
            check_field(stage_by_name["options-next-row"], "menu", "options:4",
                        "%s/stage%d-next-row" % (session, count))
            check_field(stage_by_name["quit-confirmed"], "quit", "1",
                        "%s/stage%d-quit" % (session, count))
            for name in ("playing-start", "after-lock"):
                check_next_previews("%s/stage%d" % (session, count),
                                    stage_by_name[name], count, False,
                                    "%s/stage%d-%s-previews" % (session, count,
                                                                name))
            check_log(session, stage["log"], [
                "Clean shutdown",
                settings_log_line(True, True, True, True,
                                  5 if count == 1 else count - 1, False,
                                  "not found" if count == 1 else "ok"),
            ])
            check_log(session, stage["input_log"], [
                "next start=%d" % (5 if count == 1 else count - 1),
                "next %d status=ok" % count,
            ], name="%s/stage%d-input-log" % (session, count))
        # The same seed and script must give the same records at every count:
        # the preference changes the drawn previews and nothing else. The
        # loaded record is the one allowed difference (task T052): every
        # stage after the first relaunches on the record the previous
        # stage's abandoned round persisted mid-round, so `high` and
        # `record` name the file each process loaded - and each stage's own
        # reference comparison above already proved those two fields right
        # for exactly that file. Everything else must still match stage one
        # field for field.
        first = [strip_storage_fields(record) for record in records["stage1"]]
        for index, stage in enumerate(stages[1:], start=2):
            other = [strip_storage_fields(record)
                     for record in records[stage["label"]]]
            if other != first:
                differing = [i for i, (a, b) in enumerate(zip(first, other))
                             if a != b] or ["length %d vs %d" % (len(first),
                                                                len(other))]
                fail(session + "/identical-records",
                     "stage %d differs from stage 1 at records %s" %
                     (index, differing))
            else:
                good(session + "/identical-records",
                     "stage %d equals stage 1 outside the loaded record "
                     "(%d records)" % (index, len(other)))
        # The persistence carry itself, pinned: the first stage starts from
        # an empty directory, and every later stage loads the 36 its
        # predecessor's abandoned round persisted.
        for index, stage in enumerate(stages):
            stage_by_name = {r["check"]: r for r in records[stage["label"]]}
            check_field(stage_by_name["title-start"], "high",
                        "0" if index == 0 else "36",
                        "%s/stage%d-loaded-record" % (session, index + 1))
        check_settings_path(session, stages[-1]["after"], True, True, True,
                            True, 5, False, rule=session + "/final-settings")
    elif session in ("portrait-keyboard", "portrait-pad"):
        # The staged portrait sessions of tasks T055 and T068. Stage one runs
        # landscape, walks to the seventh OPTIONS row, and selects it with a
        # direction held across the press: the session turns at once, inside
        # the tick that consumed the select edge, and the rest of the stage
        # plays the turned session on the rotated table of the session's
        # device - the title walk, a round, the pause overlay - then turns
        # back to landscape and to portrait again, so the stage proves the
        # portrait turn, the landscape turn, and repeated changes in one
        # process, and it leaves the settings file on portrait for the
        # relaunch. Stage two is that relaunch: the orientation line it
        # restores names portrait, and a whole round plays on the rotated
        # table through the pause overlay and the game-over screen, with the
        # reference replaying the same script on the same rotated tables (the
        # stage stream comparison above). The pins below add what the stream
        # cannot name: the turned pixels, the rotated mapping of the device,
        # and the input the turn spent.
        device = "keyboard" if session == "portrait-keyboard" else "pad"
        stage = stages[0]
        stage_by_name = {r["check"]: r for r in records["stage1"]}
        check_field(stage_by_name["title-at-options"], "menu", "title:2",
                    "%s/stage1-options-item" % session)
        check_field(stage_by_name["options-orientation-row"], "menu",
                    "options:6", "%s/stage1-orientation-row" % session)
        check_field(stage_by_name["orientation-portrait-live"], "menu",
                    "options:6", "%s/stage1-select-keeps-the-row" % session)
        check_field(stage_by_name["title-at-start"], "menu", "title:0",
                    "%s/stage1-title-walks-up" % session)
        check_field(stage_by_name["paused-portrait"], "scene", "paused",
                    "%s/stage1-pause" % session)
        check_field(stage_by_name["paused-portrait"], "menu", "pause:0",
                    "%s/stage1-pause-opens-on-resume" % session)
        check_field(stage_by_name["paused-at-quit"], "menu", "pause:3",
                    "%s/stage1-rotated-menu-up-wraps" % session)
        check_field(stage_by_name["title-after-round"], "scene", "title",
                    "%s/stage1-round-abandoned" % session)
        check_field(stage_by_name["orientation-landscape-live"], "menu",
                    "options:6", "%s/stage1-second-select-keeps-the-row"
                    % session)
        check_field(stage_by_name["orientation-portrait-again"], "menu",
                    "options:6", "%s/stage1-third-select-keeps-the-row"
                    % session)
        check_field(stage_by_name["title-at-quit"], "menu", "title:3",
                    "%s/stage1-rotated-title-walks-down" % session)
        check_field(stage_by_name["quit-confirmed"], "quit", "1",
                    "%s/stage1-quit" % session)
        # The round between the first and second turn plays the rotated
        # frame: the logical right of the turned grip moves the piece right,
        # the logical left moves it back, the clockwise rotation of the
        # reversed cluster turns it once, and the logical up hard drops and
        # locks four cells.
        check_field(stage_by_name["playing-portrait"], "scene", "playing",
                    "%s/stage1-round-started" % session)
        check_field(stage_by_name["playing-portrait"], "active", "T,0,3,0",
                    "%s/stage1-spawn" % session)
        check_field(stage_by_name["moved-right"], "active", "T,0,4,0",
                    "%s/stage1-rotated-right" % session)
        check_field(stage_by_name["moved-left"], "active", "T,0,3,0",
                    "%s/stage1-rotated-left" % session)
        check_field(stage_by_name["rotated-cw"], "active", "T,1,3,0",
                    "%s/stage1-rotated-clockwise" % session)
        delta_check("settled", stage_by_name["rotated-cw"],
                    stage_by_name["first-lock"], 4,
                    "%s/stage1-rotated-hard-drop" % session)
        # The geometry of both frames from pixels of one process: the
        # landscape page before the first select, the portrait options page
        # and the portrait play scene after it, the portrait page the session
        # still draws when it reopens the screen before the second select, and
        # the landscape page again after the turn back.
        for name in ("options-sound-row", "options-orientation-row",
                     "orientation-landscape-live"):
            check_options_screen_layout(session, stage_by_name[name],
                                        "%s/stage1-layout-%s" % (session,
                                                                 name))
        for name in ("options-row-again", "orientation-portrait-live",
                     "orientation-portrait-again"):
            check_portrait_options_layout(session, stage_by_name[name],
                                          "%s/stage1-turned-layout-%s" %
                                          (session, name))
        check_portrait_play_frame(session, stage_by_name["playing-portrait"],
                                  "%s/stage1-turned-play-frame" % session)
        for name in ("title-portrait", "title-final"):
            check_portrait_page_quiet(session, stage_by_name[name],
                                      "%s/stage1-quiet-%s" % (session, name))
        # The turn spends the input that crossed it: the select edge, the
        # held direction of the probe press, and every pending edge.
        check_turn_spends_input(session, stage["input_log"],
                                "%s/stage1-turn-spends-input" % session,
                                probe_bit="L")
        # The stored preference ended on portrait and nothing else moved.
        check_settings_path(session, stage["after"], True, True, True, True,
                            5, False, rule="%s/stage1-settings" % session,
                            orientation="portrait")
        check_log(session, stage["log"], [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "not found",
                              "landscape"),
            "Orientation: landscape (960 x 544 canvas, 27 px cells)",
            "Orientation: portrait (544 x 960 canvas, 40 px cells)",
            "Scene title -> options", "Scene options -> title",
            "Scene title -> playing", "Scene playing -> paused",
            "Scene paused -> title",
        ])
        check_log(session, stage["input_log"], [
            "orientation start=landscape", "orientation portrait status=ok",
            "orientation landscape status=ok",
            "orientation portrait status=ok",
        ], name="%s/stage1-input-log" % session)
        # The next-launch-only wording is gone: the row applies at once, so
        # neither log may promise a relaunch any more (task T068).
        for path, label in ((stage["log"], "log"),
                            (stage["input_log"], "input-log")):
            with open(path) as handle:
                if "next launch" in handle.read():
                    fail("%s/stage1-no-next-launch-%s" % (session, label),
                         "%s still promises the next launch" % path)
                else:
                    good("%s/stage1-no-next-launch-%s" % (session, label),
                         "no next-launch wording")

        stage = stages[1]
        stage_records = records["stage2"]
        stage_by_name = {r["check"]: r for r in stage_records}
        # The relaunch read the portrait file stage one wrote and applied it.
        check_settings_path(session, stage["before"], True, True, True, True,
                            5, False, rule="%s/stage2-relaunch-read" % session,
                            orientation="portrait")
        check_log(session, stage["log"], [
            "Clean shutdown",
            settings_log_line(True, True, True, True, 5, False, "ok",
                              "portrait"),
            "Orientation: portrait (544 x 960 canvas, 40 px cells)",
        ])
        check_log(session, stage["input_log"], [
            "orientation start=portrait",
        ], name="%s/stage2-input-log" % session)
        # The round itself: the pause overlay navigates on the rotated menu
        # roles - left wraps Resume up to Quit to Title, left again lands on
        # Controls, and the wrap plus the walk back down prove both directions
        # - and the game-over menu and the title quit keep their cursor pins.
        check_field(stage_by_name["paused"], "menu", "pause:0",
                    "%s/stage2-pause-opens-on-resume" % session)
        check_field(stage_by_name["paused-at-quit"], "menu", "pause:3",
                    "%s/stage2-menu-up-wraps" % session)
        check_field(stage_by_name["paused-at-controls"], "menu", "pause:2",
                    "%s/stage2-menu-up-again" % session)
        check_field(stage_by_name["pause-controls-open"], "scene",
                    "pausecontrols",
                    "%s/stage2-controls-screen-open" % session)
        check_field(stage_by_name["pause-controls-back"], "menu", "pause:2",
                    "%s/stage2-controls-screen-back" % session)
        check_field(stage_by_name["paused-at-restart"], "menu", "pause:1",
                    "%s/stage2-menu-down-walks" % session)
        check_field(stage_by_name["resumed"], "scene", "playing",
                    "%s/stage2-resumed" % session)
        check_field(stage_by_name["gameover-at-title"], "menu", "over:1",
                    "%s/stage2-over-cursor" % session)
        check_field(stage_by_name["title-at-quit"], "menu", "title:3",
                    "%s/stage2-title-cursor" % session)
        check_field(stage_by_name["quit-confirmed"], "quit", "1",
                    "%s/stage2-quit" % session)
        # The rotated pixels: the play scene's frame, strip, and margins; the
        # pause and game-over panels at their rotated boxes; and the quiet
        # rows below the board on every page that clears the canvas.
        check_portrait_play_frame(session, stage_by_name["playing-start"],
                                  "%s/stage2-play-frame" % session)
        check_portrait_overlay_panel(session, stage_by_name["paused"],
                                     PORTRAIT_PAUSE_PANEL,
                                     "%s/stage2-pause-panel" % session)
        check_portrait_overlay_panel(session, stage_by_name["gameover-first"],
                                     PORTRAIT_GAMEOVER_PANEL,
                                     "%s/stage2-gameover-panel" % session)
        for name in ("title-start", "title-at-start", "pause-controls-open",
                     "title-return"):
            check_portrait_page_quiet(session, stage_by_name[name],
                                      "%s/stage2-quiet-%s" % (session, name))
        # The rotated mapping of the session's device, from the input log.
        check_rotated_mapping(session, stage["input_log"], device,
                              "%s/stage2-rotated-mapping" % session)
        # The round changed no preference: the file the session leaves holds
        # exactly what stage one wrote.
        check_settings_path(session, stage["after"], True, True, True, True,
                            5, False, rule="%s/stage2-settings" % session,
                            orientation="portrait")
        # The game-over round set a record; its value is the score the
        # gameover record names, and the file holds exactly that (the
        # mid-round saves of task T052 raise it along the way).
        gameover = stage_by_name["gameover-first"]
        if "high" not in gameover:
            fail("%s/stage2-record" % session,
                 "the gameover record names no high field")
        else:
            check_high_score_file(session, stage["save"], int(gameover["high"]),
                                  "%s/stage2-saved-record" % session)
    elif session == "controls-keyboard":
        # The customization session on the keyboard (task T066): the screen
        # opens from CONTROLS, a capture applies, a conflict is rejected, a
        # repeat cancels, the way back out works, the round follows the new
        # table, and RESTORE DEFAULTS returns the file to the section 5 bytes.
        check_scene_sequence(run_records, [
            "title", "title", "controls", "rebind", "rebind", "rebind", "rebind", "rebind", "rebind", "rebind", "controls", "title", "playing", "playing", "playing", "paused", "paused", "title", "controls", "rebind", "rebind", "rebind", "controls", "title", "playing", "playing", "paused", "title", "title", "title"],
            session + "/scene-sequence")
        check_rebind_flow(session, by_name, input_log, "key")
        # Menu egress by keyboard: Esc leaves the customization screen, Esc
        # again leaves the mappings page, and the title cursor survives both.
        check_field(by_name["controls-custom"], "scene", "controls",
                    session + "/egress-to-mappings")
        check_field(by_name["title-back"], "menu", "title:1",
                    session + "/egress-to-title")
        check_field(by_name["title-restored"], "menu", "title:1",
                    session + "/egress-after-restore")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit")
        check_log(session, log_path, [
            "Clean shutdown",
            "Controls: default bindings (not found)",
            "Binding ROTATE CW to z rejected: that control already performs "
            "ROTATE CCW",
            "Scene rebind -> controls",
            "Scene controls -> rebind",
        ])
        check_controls_path(session, os.path.join(save_dir, "controls.dat"),
                            None, session + "/restored-file")
    elif session == "controls-pad":
        # The same walk on the developer virtual controller, plus the unplug
        # and replug: a held direction dies with the pad, the replug opens a
        # new source on the same table, and the custom binding still answers.
        check_scene_sequence(run_records, [
            "title", "title", "controls", "rebind", "rebind", "rebind", "rebind", "rebind", "rebind", "rebind", "controls", "title", "playing", "playing", "playing", "playing", "playing", "paused", "paused", "title", "controls", "rebind", "rebind", "rebind", "controls", "title", "playing", "playing", "paused", "paused", "title", "title", "title"],
            session + "/scene-sequence")
        check_rebind_flow(session, by_name, input_log, "pad")
        check_field(by_name["dpad-up-dead"], "settled", "0",
                    session + "/freed-control-dead")
        # The unplug itself changed nothing the player did not ask for: the
        # one held-left press moved the piece a single column before the pad
        # vanished, and across the detach the piece keeps its identity,
        # rotation, and row, nothing settles, no score moves, and no DAS
        # repeat ever fires from a dead source.
        detach_before = piece_cells(by_name["x-harddrops"], "active")
        detach_after = piece_cells(by_name["detached-mid-hold"], "active")
        if (detach_before is None or detach_after is None or
                detach_after[0] != detach_before[0] or
                detach_after[1] != detach_before[1] or
                detach_after[2] != detach_before[2] - 1 or
                detach_after[3] != detach_before[3] or
                by_name["detached-mid-hold"].get("settled") !=
                by_name["x-harddrops"].get("settled") or
                by_name["detached-mid-hold"].get("score") !=
                by_name["x-harddrops"].get("score")):
            fail(session + "/detach-keeps-round",
                 "state moved across the unplug: %s then %s" %
                 (by_name["x-harddrops"].get("active"),
                  by_name["detached-mid-hold"].get("active")))
        else:
            good(session + "/detach-keeps-round",
                 "the unplug kept every counter and cost only the held level")
        # The replug drop settles the spawned piece beside the first: eight
        # cells, and the custom binding answers on the new source.
        check_field(by_name["replugged-x-harddrops"], "settled", "8",
                    session + "/replug-keeps-binding")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit")
        check_log(session, log_path, [
            "Clean shutdown",
            "Controls: default bindings (not found)",
            "Game controller \"STACKFALL Virtual Pad\" (instance",
            "removed",
            "Virtual controller attached as device",
        ])
        check_controls_path(session, os.path.join(save_dir, "controls.dat"),
                            None, session + "/restored-file")
    elif session == "controls-relaunch":
        # The staged relaunch: stage one writes both custom tables, stage two
        # reads them back at startup, plays on them, and restores the
        # defaults, so the file ends on the section 5 bytes.
        stage = stages[0]
        stage_by_name = {r["check"]: r for r in records["stage1"]}
        check_field(stage_by_name["rebind-harddrop-d"], "menu", "rebind:5",
                    session + "/stage1-keyboard-row")
        check_log(session, stage["log"], [
            "Clean shutdown",
            "Controls: default bindings (not found)",
        ], name="%s/stage1-log" % session)
        custom = {("landscape", "key"): {"harddrop": "d"},
                  ("landscape", "pad"): {"harddrop": "x"}}
        check_controls_path(session, stage["controls_after"], custom,
                            session + "/stage1-wrote-custom-file")
        stage = stages[1]
        stage_by_name = {r["check"]: r for r in records["stage2"]}
        check_controls_path(session, stage["controls_before"], custom,
                            session + "/stage2-read-custom-file")
        check_field(stage_by_name["space-dead"], "settled", "0",
                    session + "/stage2-space-dead")
        check_field(stage_by_name["d-harddrops"], "settled", "4",
                    session + "/stage2-d-drops")
        check_field(stage_by_name["pad-x-harddrops"], "settled", "8",
                    session + "/stage2-pad-x-drops")
        check_field(stage_by_name["pad-up-dead"], "settled", "8",
                    session + "/stage2-pad-up-dead")
        check_field(stage_by_name["space-harddrops-again"], "settled", "4",
                    session + "/stage2-space-drops-again")
        check_log(session, stage["log"], [
            "Clean shutdown",
            "Controls: custom bindings (ok)",
        ], name="%s/stage2-log" % session)
        check_log(session, stage["input_log"], [
            "controls start=custom status=ok",
            "controls restore result=defaults",
        ], name="%s/stage2-input-log" % session)
        check_controls_path(session, stage["controls_after"], None,
                            session + "/stage2-restored-file")
    elif session == "controls-invalid":
        # The damaged binding file: one key bound to two gameplay actions.
        # The session recovers every table to the section 5 defaults, keeps
        # every menu reachable, and leaves the file exactly as it found it.
        stage = stages[0]
        stage_by_name = {r["check"]: r for r in records["stage1"]}
        broken = {("landscape", "key"): {"harddrop": "z"}}
        check_controls_path(session, stage["controls_before"], broken,
                            session + "/damaged-file-staged")
        check_controls_path(session, stage["controls_after"], broken,
                            session + "/damaged-file-untouched")
        check_field(stage_by_name["controls-recovered"], "scene", "controls",
                    session + "/menus-reachable")
        check_field(stage_by_name["rebind-open"], "menu", "rebind:0",
                    session + "/rebind-reachable")
        check_field(stage_by_name["d-dead"], "settled", "0",
                    session + "/d-carries-nothing")
        check_field(stage_by_name["space-harddrops"], "settled", "4",
                    session + "/space-hard-drops")
        check_field(stage_by_name["quit-confirmed"], "quit", "1",
                    session + "/quit")
        check_log(session, stage["log"], [
            "Clean shutdown",
            "Controls: default bindings (invalid)",
        ], name="%s/stage1-log" % session)
        check_log(session, stage["input_log"], [
            "controls start=default status=invalid",
        ], name="%s/stage1-input-log" % session)
    elif session == "harddrop-rebind":
        # The staged hard-drop rebind session (task T87, the operator report
        # of 2026-11-05): hard drop must be movable off the logical Up
        # direction onto any other controller input - the face buttons and
        # the shoulders included - with the stick's up arm edge-triggered
        # behind the deadzone so a light touch never drops, and a rebound
        # Up must stop hard-dropping entirely.
        stage = stages[0]
        by_name = {r["check"]: r for r in records["stage1"]}
        check_scene_sequence(records["stage1"], [
            "title", "playing", "playing", "playing", "playing", "playing",
            "playing", "paused", "paused", "title", "title", "controls",
            "rebind", "rebind", "rebind", "rebind", "rebind", "rebind",
            "controls", "title", "playing", "playing", "playing", "playing",
            "playing", "playing", "paused", "paused", "title", "title",
            "title"], session + "/stage1-scene-sequence")
        # The screen opens on its first row and the cursor lands on HARD
        # DROP, exactly as on the T066 sessions.
        check_field(by_name["rebind-open-landscape"], "menu", "rebind:0",
                    session + "/stage1-rebind-first-row")
        check_field(by_name["rebind-at-harddrop"], "menu", "rebind:5",
                    session + "/stage1-harddrop-row")
        # The T067 defaults before any change: a light stick reading inside
        # the deadzone settles nothing, D-pad up hard drops once, and a full
        # stick-up deflection drops exactly once - the settled count and the
        # score freeze across the long hold that follows, because the arm is
        # an edge and never a repeated one.
        check_field(by_name["stick-light-touch-quiet"], "settled", "0",
                    session + "/stage1-light-touch-quiet")
        check_field(by_name["stick-wobble-quiet"], "settled", "0",
                    session + "/stage1-stick-wobble-quiet")
        check_field(by_name["dpad-up-harddrops"], "settled", "4",
                    session + "/stage1-dpad-up-drops")
        check_field(by_name["stick-up-drops-once"], "settled", "8",
                    session + "/stage1-stick-up-drops-once")
        check_field(by_name["stick-up-held-quiet"], "score", "68",
                    session + "/stage1-stick-up-one-edge")
        check_field(by_name["stick-up-held-quiet"], "settled", "8",
                    session + "/stage1-stick-up-no-second-edge")
        # The rebound tables: the freed D-pad up, the freed stick-up arm, and
        # the face button the second capture replaced all settle nothing,
        # while the shoulder and the key lock the piece.
        check_field(by_name["dpad-up-dead"], "settled", "0",
                    session + "/stage1-freed-dpad-up-dead")
        check_field(by_name["stick-up-dead"], "settled", "0",
                    session + "/stage1-freed-stick-up-dead")
        check_field(by_name["face-x-dead"], "settled", "0",
                    session + "/stage1-freed-face-x-dead")
        check_field(by_name["r1-harddrops"], "settled", "4",
                    session + "/stage1-shoulder-drops")
        check_field(by_name["d-harddrops"], "settled", "8",
                    session + "/stage1-key-drops")
        # The last round's ordered action bits: the two freed Up paths reach
        # the log as menu edges alone - no drop bit ever follows the rebind -
        # and each bound control carries exactly one drop edge before the
        # pause that ends the round.
        check_stick_round_bits(session, stage["input_log"], [
            (0, 0, 0, 0, 0, 0, 0, 0, "1000"),  # the freed D-pad up
            (0, 0, 0, 0, 0, 0, 0, 0, "1000"),  # the freed stick-up arm
            (0, 0, 0, 0, 0, 1, 0, 0, "0000"),  # R1 hard drops
            (0, 0, 0, 0, 0, 1, 0, 0, "0000"),  # D hard drops
            (0, 0, 0, 0, 0, 0, 0, 1, "0000"),  # Start pauses
        ], session + "/stage1-round-bits")
        # Across the whole stage exactly four drop edges exist: two on the
        # default table, both on the Up paths that carry the menu role, and
        # two on the bound controls after the rebind, which carry none.
        try:
            with open(stage["input_log"]) as handle:
                drop_lines = [line for line in handle
                              if line.startswith("tick=") and " drop=1 " in line]
        except IOError:
            drop_lines = None
        if drop_lines is None:
            fail(session + "/stage1-drop-edge-count",
                 "cannot read %s" % stage["input_log"])
        elif len(drop_lines) != 4:
            fail(session + "/stage1-drop-edge-count",
                 "%d drop edges, expected 4: %s" % (len(drop_lines),
                                                    drop_lines[:6]))
        elif sum(1 for line in drop_lines
                 if line.rstrip().endswith("menu=1000")) != 2:
            fail(session + "/stage1-drop-edge-count",
                 "the default-table drops no longer ride the Up menu role: %s"
                 % drop_lines)
        else:
            good(session + "/stage1-drop-edge-count",
                 "four drop edges: two on the Up default, two on the rebind")
        check_log(session, stage["input_log"], [
            "controls capture action=harddrop device=pad control=x "
            "result=applied",
            "controls capture action=harddrop device=pad control=r1 "
            "result=applied",
            "controls capture action=harddrop device=key control=d "
            "result=applied",
        ], name="%s/stage1-input-log" % session)
        check_log(session, stage["log"], [
            "Clean shutdown",
            "Controls: default bindings (not found)",
            "Scene rebind -> controls",
        ], name="%s/stage1-log" % session)
        custom1 = {("landscape", "pad"): {"harddrop": "r1"},
                   ("landscape", "key"): {"harddrop": "d"}}
        check_controls_path(session, stage["controls_after"], custom1,
                            session + "/stage1-wrote-custom-file")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/stage1-quit")

        # Stage two: the relaunch on that file, the portrait turn, and the
        # rebind through the turned menu roles.
        stage = stages[1]
        by_name = {r["check"]: r for r in records["stage2"]}
        check_scene_sequence(records["stage2"], [
            "title", "title", "controls", "title", "playing", "playing",
            "playing", "playing", "playing", "playing", "paused", "paused",
            "title", "title", "options", "options", "options", "title",
            "title", "playing", "playing", "playing", "paused", "paused",
            "title", "title", "controls", "rebind", "rebind", "rebind",
            "rebind", "controls", "title", "playing", "playing", "playing",
            "playing", "paused", "paused", "title", "title", "title"],
            session + "/stage2-scene-sequence")
        check_controls_path(session, stage["controls_before"], custom1,
                            session + "/stage2-read-custom-file")
        # The loaded landscape tables play: R1 and D drop, the freed Up
        # paths and Space settle nothing.
        check_field(by_name["r1-drops-loaded"], "settled", "4",
                    session + "/stage2-shoulder-drops-loaded")
        check_field(by_name["up-dead-loaded"], "settled", "4",
                    session + "/stage2-up-dead-loaded")
        check_field(by_name["stick-up-dead-loaded"], "settled", "4",
                    session + "/stage2-stick-up-dead-loaded")
        check_field(by_name["space-dead-loaded"], "settled", "4",
                    session + "/stage2-space-dead-loaded")
        check_field(by_name["d-drops-loaded"], "settled", "8",
                    session + "/stage2-key-drops-loaded")
        # The untouched portrait defaults: the grip's up direction still
        # hard drops there - the landscape rebind leaked into no other table
        # - and the same edge rule holds, one drop per deflection.
        check_field(by_name["portrait-stick-up-drops"], "settled", "4",
                    session + "/stage2-portrait-default-drops")
        check_field(by_name["portrait-stick-up-held-quiet"], "score", "36",
                    session + "/stage2-portrait-one-edge")
        check_field(by_name["portrait-stick-up-held-quiet"], "settled", "4",
                    session + "/stage2-portrait-no-second-edge")
        # The turned rebind flow: the cursor lands on HARD DROP through the
        # rotated roles, the capture applies, and the rebound portrait round
        # frees the grip's up arm on both paths.
        check_field(by_name["rebind-at-harddrop-portrait"], "menu",
                    "rebind:5", session + "/stage2-harddrop-row-portrait")
        check_field(by_name["portrait-dpad-left-dead"], "settled", "0",
                    session + "/stage2-freed-grip-up-dead")
        check_field(by_name["portrait-stick-up-dead"], "settled", "0",
                    session + "/stage2-freed-grip-stick-dead")
        check_field(by_name["portrait-r1-drops"], "settled", "4",
                    session + "/stage2-shoulder-drops-portrait")
        # The turned rebind pages render at the portrait composition: the
        # rows below the turned board frame stay empty on each capture.
        for name in ("rebind-open-portrait", "rebind-capture-armed-portrait",
                     "rebind-harddrop-r1-portrait"):
            check_portrait_page_quiet(session, by_name[name],
                                      session + "/portrait-%s-fits" % name)
        check_log(session, stage["input_log"], [
            "controls start=custom status=ok",
            "orientation portrait status=ok",
            "controls capture action=harddrop device=pad control=r1 "
            "result=applied",
        ], name="%s/stage2-input-log" % session)
        check_log(session, stage["log"], [
            "Clean shutdown",
            "Controls: custom bindings (ok)",
        ], name="%s/stage2-log" % session)
        custom2 = dict(custom1)
        custom2[("portrait", "pad")] = {"harddrop": "r1"}
        check_controls_path(session, stage["controls_after"], custom2,
                            session + "/stage2-wrote-portrait-file")
        check_settings_file(session, stage["save"], True, True, True, True, 5,
                            False, session + "/stage2-saved-portrait",
                            orientation="portrait")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/stage2-quit")

        # Stage three: the portrait relaunch on the saved preference and the
        # saved custom tables, then RESTORE DEFAULTS.
        stage = stages[2]
        by_name = {r["check"]: r for r in records["stage3"]}
        check_scene_sequence(records["stage3"], [
            "title", "title", "controls", "rebind", "controls", "title",
            "playing", "playing", "playing", "playing", "paused", "paused",
            "title", "title", "controls", "rebind", "rebind", "rebind",
            "controls", "title", "playing", "playing", "paused", "paused",
            "title", "title", "title"], session + "/stage3-scene-sequence")
        check_controls_path(session, stage["controls_before"], custom2,
                            session + "/stage3-read-custom-file")
        check_field(by_name["relaunch-portrait-left-dead"], "settled", "0",
                    session + "/stage3-persisted-grip-up-dead")
        check_field(by_name["relaunch-portrait-stick-dead"], "settled", "0",
                    session + "/stage3-persisted-stick-dead")
        check_field(by_name["relaunch-portrait-r1-drops"], "settled", "4",
                    session + "/stage3-persisted-shoulder-drops")
        check_field(by_name["rebind-at-restore-portrait"], "menu", "rebind:8",
                    session + "/stage3-restore-row")
        check_field(by_name["restored-portrait-stick-drops"], "settled", "4",
                    session + "/stage3-restored-default-drops")
        check_log(session, stage["input_log"], [
            "orientation start=portrait",
            "controls start=custom status=ok",
            "controls restore result=defaults",
        ], name="%s/stage3-input-log" % session)
        check_log(session, stage["log"], [
            "Clean shutdown",
            "Controls: custom bindings (ok)",
        ], name="%s/stage3-log" % session)
        check_controls_path(session, stage["controls_after"], None,
                            session + "/stage3-restored-file")
        check_settings_file(session, stage["save"], True, True, True, True, 5,
                            False, session + "/stage3-kept-portrait",
                            orientation="portrait")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/stage3-quit")
    else:
        fail("%s/unknown" % session, "no expectations defined")


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    run_dir = os.path.abspath(sys.argv[1])
    manifest = os.path.join(run_dir, "sessions.txt")
    if not os.path.isfile(manifest):
        print("no sessions.txt in %s" % run_dir)
        return 2
    with open(manifest) as handle:
        sessions = [line.strip() for line in handle if line.strip()]

    for session in sessions:
        print("--- %s" % session)
        check_session(run_dir, session)

    print()
    print("checks passed: %d, failed: %d" % (len(passes), len(failures)))
    if failures:
        with open(os.path.join(run_dir, "check-failures.txt"), "w") as handle:
            for item in failures:
                handle.write(item + "\n")
        print("failure evidence: %s/check-failures.txt" % run_dir)
        return 1
    with open(os.path.join(run_dir, "check-passed.txt"), "w") as handle:
        handle.write("all %d checks passed\n" % len(passes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
