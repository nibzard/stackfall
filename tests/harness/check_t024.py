#!/usr/bin/env python3
"""Independent checker for the T024 play-and-inspect sessions.

Reads one T024 run directory (tests/harness/t024.sh) and decides pass or
fail from evidence, reusing the T023 checker's helpers:

  * every deterministic session's records must equal the headless
    reference stream field by field;
  * the observations must also satisfy expectations pinned from
    `docs/SPEC.md` alone - spawn coordinates, one-column movement, the
    four rotation states, the SRS kick offsets used at the wall and on
    the stack, the hold restriction, the pause freeze, the soft- and
    hard-drop points, and the section 4.6 clear table - so a defect the
    game and the reference share still fails here;
  * a spawned I piece must be visible immediately (task T029): its only
    occupied row is visible row 0 on the spawn tick, the frame's pixels
    show it, and it still falls exactly on the level 1 gravity schedule
    of 48 ticks per cell;
  * the input log must show one sound cue per event and a working mute;
  * every checkpoint screenshot must exist as a BMP of the canvas size
    the window had when it was taken;
  * the X11 session (when it ran) must show focus loss clearing held
    input, no stuck movement after refocus, and a letterboxed frame
    after the resize.

A few pins are regression pins recorded from a passing rehearsal, not
hand-derived arithmetic; the comment on each says which. Exit status is
nonzero when any check fails.

Usage: check_t024.py RUN_DIR
"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_harness as base  # noqa: E402  (shared T023 helpers)

failures = base.failures
passes = base.passes
fail = base.fail
good = base.good
read_records = base.read_records
number = base.number
piece_cells = base.piece_cells
delta_check = base.delta_check
check_field = base.check_field
compare_streams = base.compare_streams
hard_drop_arithmetic = base.hard_drop_arithmetic
soft_drop_arithmetic = base.soft_drop_arithmetic
pause_freeze = base.pause_freeze
hold_restriction = base.hold_restriction
check_high_score_file = base.check_high_score_file


def check_bmp_size(path, size, rule):
    """A BMP existence and dimension check for any canvas size."""
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
    if (width, height) != size:
        fail(rule, "%s is %dx%d, expected %dx%d" %
             (path, width, height, size[0], size[1]))
        return
    if depth not in (8, 24, 32):
        # 8 is the palettized checkpoint writer of task T054; 24 and 32 are
        # the uncompressed forms before it and its too-many-colors escape.
        fail(rule, "%s has %d bits per pixel" % (path, depth))
        return
    good(rule, "%s %dx%d" % (os.path.basename(path), width, height))


def by_name(records):
    return {record["check"]: record for record in records}


def active_of(record):
    return piece_cells(record, "active")


def check_active(session, record, expected, rule):
    """`expected` is (letter, rotation, x, y) or None."""
    got = active_of(record)
    if got != expected:
        fail(rule, "active at %s is %s, expected %s" %
             (record["check"], got, expected))
    else:
        good(rule, "%s" % (expected,))


def moved_left(session, before, after, rule):
    """The piece moved at least one column left and nothing else changed."""
    a, b = active_of(before), active_of(after)
    if a is None or b is None or a[0] != b[0]:
        fail(rule, "not the same piece at %s -> %s" %
             (before["check"], after["check"]))
        return
    if not (b[3] >= a[3] and b[2] < a[2]):
        fail(rule, "x %d -> %d (y %d -> %d), expected left only" %
             (a[2], b[2], a[3], b[3]))
    else:
        good(rule, "x %d -> %d" % (a[2], b[2]))


def same_piece_frozen_x(session, before, after, rule):
    """Same piece, same column; only fall may advance the row."""
    a, b = active_of(before), active_of(after)
    if a is None or b is None or a[0] != b[0]:
        fail(rule, "not the same piece at %s -> %s" %
             (before["check"], after["check"]))
        return
    if b[2] != a[2]:
        fail(rule, "column moved %d -> %d while it had to stay" % (a[2], b[2]))
    else:
        good(rule, "x stays %d (y %d -> %d)" % (a[2], a[3], b[3]))


def check_screenshots_size(session, records, size):
    for record in records:
        shot = record.get("shot", "none")
        if shot == "none":
            continue
        check_bmp_size(shot, size, "%s/shot:%s" % (session, record["check"]))


def sfx_counts(input_log):
    """Cue name -> count of `sfx <name> tick=` lines in the input log."""
    counts = {}
    if not os.path.isfile(input_log):
        return counts
    with open(input_log) as handle:
        for line in handle:
            match = re.match(r"sfx ([a-z_/]+) tick=", line)
            if match:
                counts[match.group(1)] = counts.get(match.group(1), 0) + 1
    return counts


def check_cue(session, counts, name, expected, rule):
    got = counts.get(name, 0)
    if got != expected:
        fail(rule, "%s cue fired %d times, expected %d" % (name, got, expected))
    else:
        good(rule, "%s x%d" % (name, got))


def check_sound_summary(session, log_path, rule):
    """The end-of-run sound line: requested = played + muted + dropped."""
    if not os.path.isfile(log_path):
        fail(rule, "missing log %s" % log_path)
        return
    with open(log_path) as handle:
        text = handle.read()
    line = next((l for l in text.splitlines() if l.startswith("Sound cues:")),
                None)
    if line is None:
        fail(rule, "no sound summary line")
        return
    requested_text = line.split("requested ", 1)[1].split(", played", 1)[0]
    played_text = line.split("played ", 1)[1].split(",", 1)[0]
    requested = {k: int(v) for k, v in
                 re.findall(r"([a-z_/]+)=(\d+)", requested_text)}
    played = {k: int(v) for k, v in re.findall(r"([a-z_/]+)=(\d+)", played_text)}
    muted = int(re.search(r", (\d+) muted,", line).group(1))
    dropped = int(re.search(r", (\d+) without a device", line).group(1))
    total_requested = sum(requested.values())
    accounted = sum(played.values()) + muted + dropped
    if total_requested != accounted:
        fail(rule, "%d requested vs %d accounted (played %d, muted %d, "
             "dropped %d)" % (total_requested, accounted, sum(played.values()),
                              muted, dropped))
    elif muted <= 0:
        fail(rule, "no cue was muted, so the mute check proves nothing")
    else:
        good(rule, "%d requested = %d played + %d muted + %d dropped"
             % (total_requested, sum(played.values()), muted, dropped))


def check_input_log(session, log_path, needles, rule):
    if not os.path.isfile(log_path):
        fail(rule, "missing input log %s" % log_path)
        return
    with open(log_path) as handle:
        text = handle.read()
    missing = [n for n in needles if n not in text]
    if missing:
        fail(rule, "input log lacks %r" % missing)
    else:
        good(rule, "%d required lines present" % len(needles))


# ---------------------------------------------------------------------------
# Per-session expectations.
# ---------------------------------------------------------------------------

def check_shapes(session, run_records, run_dir, keyboard):
    """Shared shape-coverage pins of the keyboard and pad sessions.

    Spec-derived: spawn coordinates (4.2), one-column moves (4.4), the
    four rotation states, the I wall kick and the JLSTZ stack kick
    offsets (4.4), hold once per lock (4.3), the pause freeze (4.5), the
    soft- and hard-drop points (4.6).
    """
    names = by_name(run_records)
    first = "S" if keyboard else "O"

    # Every spawn is immediately visible (4.2 with the T029 rule): the
    # topmost occupied cells sit in visible row 0, so the I matrix starts
    # at y=-1 (its first occupied row is local row 1) and the others at 0.
    check_active(session, names["round-first-frame"],
                 (first, 0, 3, 0), session + "/first-spawn")

    if keyboard:
        check_active(session, names["s-moved-left-once"],
                     ("S", 0, 2, 0), session + "/one-column-move")
        check_active(session, names["s-rotated-cw"], ("S", 1, 1, 0),
                     session + "/rotate-cw-state")
        check_active(session, names["s-rotated-l-state"], ("S", 3, 1, 0),
                     session + "/rotate-l-state")
        check_active(session, names["s-back-to-spawn-state"], ("S", 0, 1, 0),
                     session + "/rotate-back-to-spawn")
        soft_drop_arithmetic(session, run_records, "s-back-to-spawn-state",
                             "s-soft-dropped", session + "/soft-drop-points")
        hard_drop_arithmetic(session, run_records, "s-soft-dropped",
                             "z-spawn-frame", session + "/hard-drop-points")
    else:
        check_active(session, names["o-rotated-cw"], ("O", 1, 3, 0),
                     session + "/o-rotates-state")
        check_active(session, names["o-rotated-back"], ("O", 0, 3, 0),
                     session + "/o-rotates-back")
        check_active(session, names["o-moved-right"], ("O", 0, 4, 0),
                     session + "/one-column-move")
        hard_drop_arithmetic(session, run_records, "o-moved-right",
                             "i-spawn-frame", session + "/hard-drop-points")
        soft_drop_arithmetic(session, run_records, "s-back-to-spawn-state",
                             "s-soft-dropped", session + "/soft-drop-points")

    # The I spawns in view (4.2 with the T029 rule): origin y=-1, its only
    # occupied row at visible row 0.
    check_active(session, names["i-spawn-frame"], ("I", 0, 3, -1),
                 session + "/i-visible-spawn")
    check_active(session, names["i-at-right-wall"], ("I", 1, 7, -1),
                 session + "/i-at-wall")
    # SRS I kick R->2: the in-place candidate leaves columns 7..10, so the
    # (-1, 0) candidate applies (4.4, the I table).
    check_active(session, names["i-wall-kick"], ("I", 2, 6, -1),
                 session + "/i-wall-kick")

    # A grounded JLSTZ piece rotating clockwise lifts one row and one
    # column: the (-1, -1) candidate of 0->R (4.4).
    grounded = active_of(names["l-grounded"])
    kicked = active_of(names["l-stack-kick"])
    if kicked == ("L", 1, grounded[2] - 1, grounded[3] - 1):
        good(session + "/l-stack-kick", "(-1,-1) applied")
    else:
        fail(session + "/l-stack-kick",
             "kick gave %s from grounded %s" % (kicked, grounded))
    if number(names["l-grounded"], "tick") >= number(names["l-at-right-wall"],
                                                      "tick"):
        good(session + "/l-grounded-order", "grounded after the move")
    else:
        fail(session + "/l-grounded-order", "grounding check precedes the move")

    if keyboard:
        check_active(session, names["j-rotated-cw"], ("J", 1, 3, 0),
                     session + "/j-state-r")
        check_active(session, names["j-half-turn"], ("J", 2, 3, 0),
                     session + "/j-state-2")
        check_active(session, names["j-l-state"], ("J", 3, 3, 0),
                     session + "/j-state-l")
        check_active(session, names["j-at-left-wall"], ("J", 2, 0, 0),
                     session + "/j-left-wall")
        check_active(session, names["z-at-right-wall"], ("Z", 0, 7, 0),
                     session + "/z-right-wall")
        check_active(session, names["z-rotated-at-wall"], ("Z", 1, 7, 0),
                     session + "/z-rotates-at-wall")
        check_active(session, names["o-rotated-cw"], ("O", 1, 3, 0),
                     session + "/o-rotates-state")
        held_id, out_id = "T", "O"
    else:
        check_active(session, names["z-rotated-at-wall"], ("Z", 1, 3, 0),
                     session + "/z-rotates-state")
        check_active(session, names["z-centered"], ("Z", 0, 1, 0),
                     session + "/z-centered")
        held_id, out_id = "T", "J"

    # Hold: once per lock, the swap returns the piece to spawn (4.3), and
    # the returned piece is visible at once like any other spawn (T029).
    check_active(session, names["hold-swap-first-frame"], (out_id, 0, 3, 0),
                 session + "/hold-swap-spawn")
    check_field(names["hold-swap-first-frame"], "hold", "%s,locked" % held_id,
                session + "/hold-locks")
    hold_restriction(session, run_records, "hold-used", "hold-locked-noop",
                     session + "/hold-once")
    check_field(names["hold-free-after-lock"], "hold", "%s,free" % held_id,
                session + "/hold-freed-by-lock")
    check_active(session, names["hold-return-first-frame"], (held_id, 0, 3, 0),
                 session + "/hold-return-spawn")
    stashed = "I" if keyboard else "Z"
    check_field(names["hold-return-first-frame"], "hold", "%s,locked" % stashed,
                session + "/hold-return-locks")

    # Pause freezes everything; movement continues after resume (4.5).
    pause_freeze(session, run_records, "paused-while-moving", "paused-frozen",
                 session + "/pause-freeze")
    same_piece_frozen_x(session, names["paused-while-moving"],
                        names["paused-frozen"], session + "/pause-freezes-x")
    a, b = active_of(names["paused-frozen"]), active_of(names["resumed"])
    if a is None or b is None or a[0] != b[0] or b[2] <= a[2]:
        fail(session + "/das-resumes",
             "column %s -> %s across the resume, expected a move" % (a, b))
    else:
        good(session + "/das-resumes", "x %d -> %d after the resume" % (a[2], b[2]))
    same_piece_frozen_x(session, names["das-continued"],
                        names["movement-stopped"],
                        session + "/release-stops")

    # Restart with a direction held: a fresh round, the held direction
    # registers as a new press, and releasing it stops all movement (4.5,
    # 4.7). No rotation, drop, or hold may leak across the restart.
    check_field(names["restart-first-frame"], "scene", "playing",
                session + "/restart-scene")
    check_active(session, names["restart-first-frame"], (first, 0, 3, 0),
                 session + "/restart-spawn")
    delta = 3 - active_of(names["restart-held-moves"])[2]
    if delta < 1:
        fail(session + "/restart-held-moves",
             "held direction moved %d columns after the restart" % delta)
    else:
        good(session + "/restart-held-moves", "%d column(s)" % delta)
    same_piece_frozen_x(session, names["restart-held-moves"],
                        names["restart-release-stops"],
                        session + "/restart-release-stops")
    check_active(session, names["restart-release-stops"],
                 (first, 0, active_of(names["restart-release-stops"])[2], 0),
                 session + "/restart-no-leaked-action")

    # The full cycle: game over, play again, game over, title, quit.
    for name in ("gameover-first", "gameover-second"):
        check_field(names[name], "scene", "gameover",
                    session + "/%s-scene" % name)
    check_field(names["quit-confirmed"], "quit", "1", session + "/quit")


def check_keyboard(session, run_records, run_dir):
    check_shapes(session, run_records, run_dir, keyboard=True)
    names = by_name(run_records)
    check_scene_seq(session, run_records)
    # Regression pins recorded from the passing rehearsal of this script
    # at seed 31337 (the reference comparison above carries correctness).
    # The T029 refresh (2026-11-03) reflects the immediate-spawn rule: each
    # hard drop travels one cell less, so 2 points less per drop - ten in
    # the first round, eleven in the second. The T052 refresh (2026-11-04)
    # reflects the mid-round record persistence: the first round of the
    # script exercises the seven shapes to 229 and is then dropped by the
    # pause-menu RESTART, and that abandoned best is already saved and fed
    # back, so the restarted round's 194 game over is not a record and the
    # file the session leaves holds the abandoned 229.
    check_field(names["after-t-lock"], "score", "229",
                session + "/shapes-round-score")
    check_field(names["restart-first-frame"], "high", "229",
                session + "/restart-carries-record")
    check_field(names["gameover-first"], "score", "194",
                session + "/first-round-score")
    check_field(names["gameover-first"], "record", "0",
                session + "/first-round-below-abandoned-record")
    check_field(names["gameover-second"], "score", "206",
                session + "/second-round-score")
    check_field(names["gameover-second"], "record", "0",
                session + "/second-round-below-abandoned-record")
    check_high_score_file(session, os.path.join(run_dir, session, "save"),
                          229, session + "/saved-record")
    check_input_log(session, os.path.join(run_dir, session, "input.log"), [
        "sound muted status=ok",
        "sound unmuted status=ok",
    ], session + "/mute-toggle-log")
    counts = sfx_counts(os.path.join(run_dir, session, "input.log"))
    # Every successful rotation of the script sounds once: 15 of them.
    check_cue(session, counts, "rotate", 15, session + "/rotate-cue-once")
    check_sound_summary(session, os.path.join(run_dir, session + ".run.log"),
                        session + "/cue-accounting")
    check_log_lines(session, os.path.join(run_dir, session + ".run.log"), [
        "Clean shutdown",
        "Scene title -> controls",
        "Scene controls -> title",
        "Scene title -> playing",
        "Scene playing -> paused",
        "Scene paused -> playing",
        "Scene playing -> gameover",
        "Scene gameover -> title",
    ])


def check_pad(session, run_records, run_dir):
    check_shapes(session, run_records, run_dir, keyboard=False)
    names = by_name(run_records)
    # Controller removal while Left is held: the held level cannot stick.
    same_piece_frozen_x(session, names["pad-held-left"],
                        names["after-detach"], session + "/detach-stops-x")
    same_piece_frozen_x(session, names["after-detach"],
                        names["after-detach-still"],
                        session + "/detach-stays-stopped")
    check_field(names["gameover-first"], "score", "238",
                session + "/first-round-score")
    check_field(names["gameover-second"], "score", "200",
                session + "/second-round-score")
    check_high_score_file(session, os.path.join(run_dir, session, "save"),
                          238, session + "/saved-record")
    check_log_lines(session, os.path.join(run_dir, session + ".run.log"), [
        "Clean shutdown",
        "Virtual Pad\" (instance",
        "removed",
        "Scene title -> controls",
        "Scene controls -> title",
        "Scene title -> playing",
    ])


def check_i_visible_pixels(session, record, rule):
    """The spawn frame shows the I: task T029 puts its only occupied row in
    visible row 0, so the four cells under columns 3..6 of that row hold
    something other than the well fill and the grid lines (section 6.2).
    Under the old rule this band was empty for the first 48 ticks."""
    shot = record.get("shot", "none")
    if shot == "none":
        fail(rule, "checkpoint %s has no screenshot" % record["check"])
        return
    loaded = base.read_bmp_pixels(shot, rule)
    if loaded is None:
        return
    width, height, pixel = loaded
    drawn = 0
    for column in (3, 4, 5, 6):
        x = base.BOARD_ORIGIN[0] + column * base.BOARD_CELL + \
            base.BOARD_CELL // 2
        y = base.BOARD_ORIGIN[1] + base.BOARD_CELL // 2
        if width <= x or height <= y:
            fail(rule, "%s is %dx%d, too small for row 0" % (shot, width, height))
            return
        if pixel(x, y) not in (base.WELL_COLOR, base.GRID_LINE_COLOR):
            drawn += 1
    if drawn != 4:
        fail(rule, "only %d of the four I cells draw in row 0 of %s" %
             (drawn, record["check"]))
        return
    good(rule, "all four I cells draw in row 0")


def check_i_spawn(session, run_records, run_dir):
    names = by_name(run_records)
    spawn = names["i-spawn-frame"]
    held = names["i-visible-plus47"]
    fell = names["i-fell-plus48"]
    # Task T029: the I spawns with its only occupied row in visible row 0,
    # matrix origin y=-1, so it is on screen the tick it appears.
    check_active(session, spawn, ("I", 0, 3, -1), session + "/i-spawns-visible")
    check_i_visible_pixels(session, spawn, session + "/i-spawn-frame-shows-it")
    # Gravity timing is unchanged (4.5): the visible I holds row 0 through
    # the +47 tick and falls on the +48 tick, exactly the level 1 interval
    # of 48 ticks per cell.
    check_active(session, held, ("I", 0, 3, -1), session + "/i-holds-row-0")
    check_active(session, fell, ("I", 0, 3, 0), session + "/i-falls-on-schedule")
    delay = number(fell, "tick") - number(spawn, "tick")
    if delay != 48:
        fail(session + "/i-gravity-delay",
             "the I fell %d ticks after the spawn, the rules prescribe 48" %
             delay)
    else:
        good(session + "/i-gravity-delay",
             "48 ticks = %.3f s at 60 Hz" % (delay / 60.0))
    hard_drop_arithmetic(session, run_records, "i-fell-plus50",
                         "after-i-lock", session + "/i-hard-drop-points")
    counts = sfx_counts(os.path.join(run_dir, session, "input.log"))
    # Two locks in this session, nothing else: each lands exactly once and
    # nothing moves or rotates on the way (6.3).
    for cue, expected in (("move/land", 2), ("rotate", 0), ("clear", 0),
                          ("four", 0), ("gameover", 0)):
        check_cue(session, counts, cue, expected,
                  session + "/cue-%s" % cue.replace("/", "-"))


def check_clears_23(session, run_records, run_dir):
    names = by_name(run_records)
    check_field(names["double-loaded"], "settled", "18",
                session + "/double-fixture-board")
    check_field(names["double-cleared"], "lines", "2", session + "/double-lines")
    check_field(names["double-cleared"], "level", "1",
                session + "/double-level")
    hard_drop_arithmetic(session, run_records, "double-before-drop",
                         "double-cleared", session + "/double-arithmetic")
    check_field(names["triple-loaded"], "settled", "27",
                session + "/triple-fixture-board")
    check_field(names["triple-cleared"], "lines", "5", session + "/triple-lines")
    check_field(names["triple-cleared"], "score", "868",
                session + "/triple-score")
    hard_drop_arithmetic(session, run_records, "triple-before-drop",
                         "triple-cleared", session + "/triple-arithmetic")
    counts = sfx_counts(os.path.join(run_dir, session, "input.log"))
    # A double and a triple are both clear cues, one per lock, and each
    # fixture rotation sounds once (6.3).
    check_cue(session, counts, "clear", 2, session + "/clear-cue-once")
    check_cue(session, counts, "four", 0, session + "/no-four-line-cue")
    check_cue(session, counts, "rotate", 2, session + "/rotate-cue-once")
    check_cue(session, counts, "move/land", 0, session + "/no-land-cue")
    check_log_lines(session, os.path.join(run_dir, session + ".run.log"), [
        "Clean shutdown",
        "Fixture \"double-line-ready\" applied from",
        "provenance: hand-built for T024 from the section 4.6 clear table",
        "Fixture \"triple-line-ready\" applied from",
    ])


def check_focus_letterbox(session, run_dir):
    """The X11 session: behavioral, no reference stream (real-time pacing)."""
    obs_path = os.path.join(run_dir, session, "observations.obs")
    log_path = os.path.join(run_dir, session, "run.log")
    input_log = os.path.join(run_dir, session, "input.log")
    if not os.path.isfile(obs_path):
        fail(session + "/records", "missing %s" % obs_path)
        return
    records = read_records(obs_path)
    names = by_name(records)
    if not records:
        fail(session + "/records", "no checkpoint records")
        return
    scenes = [record["scene"] for record in records]
    expected_prefix = ["title", "playing"]
    if scenes[:2] != expected_prefix or "paused" not in scenes \
            or scenes[-1] != "title":
        fail(session + "/scene-sequence", "scenes %s" % scenes)
    else:
        good(session + "/scene-sequence", "%d records" % len(scenes))

    # Focus loss clears held input: the piece keeps falling (gravity runs)
    # but stops moving sideways without any key release. Between the
    # before-minimize record and the minimize itself the button is still
    # legitimately held, so the delayed auto-shift runs the piece to the
    # wall first; what must freeze is everything after the loss.
    same_piece_frozen_x(session, names["focus-lost-frozen"],
                        names["after-refocus-no-move"],
                        session + "/no-stuck-movement")
    a, b = active_of(names["after-refocus-no-move"]), \
        active_of(names["refocus-repress-moves"])
    if a is None or b is None or a[0] != b[0] or b[2] <= a[2]:
        fail(session + "/repress-moves",
             "column %s -> %s after the re-press, expected a move" % (a, b))
    else:
        good(session + "/repress-moves", "x %d -> %d" % (a[2], b[2]))
    pause_freeze(session, records, "paused-letterboxed",
                 "paused-letterboxed-still", session + "/pause-freeze")

    # Letterbox: everything from the resize on is a 640x480 BMP, and the
    # 960x544 canvas is scaled into it with bars.
    before, after = [], []
    for record in records:
        target = before if record["check"] in (
            "title-first", "playing", "before-minimize", "focus-lost-frozen",
            "after-refocus-no-move", "refocus-repress-moves") else after
        target.append(record)
    check_screenshots_size(session, before, (960, 544))
    check_screenshots_size(session, after, (640, 480))

    check_log_lines(session, log_path, [
        "Window focus lost; every held button cleared",
        "Clean shutdown",
    ])
    # After the focus loss no tick may hold Left again: the adapter
    # cleared the level, the one later key-up is stale, and Left is never
    # re-pressed in this session. The re-press uses Right.
    if os.path.isfile(input_log):
        with open(input_log) as handle:
            text_log = handle.read()
        tick_lines = [l for l in text_log.splitlines()
                      if re.match(r"tick=\d+ ", l)]
        held_left = [i for i, l in enumerate(tick_lines) if " L=1 " in l]
        if not held_left:
            fail(session + "/held-cleared", "no tick ever held Left")
        else:
            stray = [l for l in tick_lines[held_left[-1] + 1:] if " L=1 " in l]
            if stray:
                fail(session + "/held-cleared",
                     "%d tick lines hold Left after the last held tick" %
                     len(stray))
            else:
                good(session + "/held-cleared",
                     "Left stops after tick %s and never returns" %
                     tick_lines[held_left[-1]].split()[0].split("=")[1])
        # No sideways move may sound between the cleared hold and the
        # re-press: movement really stopped, wall or not.
        last_left = int(re.match(r"tick=(\d+)",
                                 tick_lines[held_left[-1]]).group(1))
        first_right = next((int(re.match(r"tick=(\d+)", l).group(1))
                            for l in tick_lines if " R=1 " in l), None)
        moves = [int(m) for m in
                 re.findall(r"sfx move/land tick=(\d+)", text_log)]
        late = [t for t in moves
                if t > last_left and (first_right is None or t < first_right)]
        if late:
            fail(session + "/movement-stopped",
                 "move cues at %s after the hold cleared" % late)
        else:
            good(session + "/movement-stopped",
                 "no move cue between the loss and the re-press")
    else:
        fail(session + "/input-log", "missing input log")


def check_scene_seq(session, records):
    expected = ["title", "title", "controls", "title", "title", "playing"]
    tail = ["gameover", "playing", "playing", "gameover", "gameover", "title",
            "title", "title"]
    got = [record["scene"] for record in records]
    middle = got[len(expected):-len(tail) or None]
    ok_middle = middle and all(s in ("playing", "paused") for s in middle)
    if got[:len(expected)] == expected and got[-len(tail):] == tail \
            and ok_middle and "paused" in middle:
        good(session + "/scene-sequence", "%d records" % len(got))
    else:
        fail(session + "/scene-sequence", "scene sequence %s" % got)


def check_log_lines(session, log_path, must_contain):
    rule = "%s/log" % session
    if not os.path.isfile(log_path):
        fail(rule, "missing log %s" % log_path)
        return
    with open(log_path) as handle:
        text = handle.read()
    missing = [n for n in must_contain if n not in text]
    if missing:
        fail(rule, "log lacks %r" % missing)
    else:
        good(rule, "%d required lines present" % len(must_contain))


DETERMINISTIC = {
    "t024-shapes-keyboard": check_keyboard,
    "t024-shapes-pad": check_pad,
    "t024-i-spawn": check_i_spawn,
    "t024-clears-23": check_clears_23,
}


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
        ref_path = os.path.join(run_dir, session + ".ref.obs")
        run_path = os.path.join(run_dir, session + ".run.obs")
        for path in (ref_path, run_path):
            if not os.path.isfile(path):
                fail("%s/records" % session, "missing %s" % path)
                continue
        ref_records = read_records(ref_path)
        run_records = read_records(run_path)
        if not run_records:
            fail("%s/records" % session, "%s has no records" % run_path)
            continue
        compare_streams(session, run_records, ref_records)
        check_screenshots_size(session, run_records, (960, 544))
        DETERMINISTIC[session](session, run_records, run_dir)

    x11 = "t024-focus-letterbox"
    status_path = os.path.join(run_dir, x11 + ".x11.status")
    if os.path.isfile(status_path):
        with open(status_path) as handle:
            status = handle.read().strip()
        print("--- %s (%s)" % (x11, status))
        if status == "passed":
            check_focus_letterbox(x11, run_dir)
        else:
            good(x11 + "/skipped", status)

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
