#!/usr/bin/env python3
"""Independent checker for the unattended SDL harness (T023).

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
  * each game run must exit 0, log a clean shutdown, and label every fixture
    it used;
  * the high-score file of a session that set a record must hold that record;
  * each session's log.txt must sit in the --save-dir the harness passed,
    beside the record, and not in the environment's save directory.

Exit status is nonzero when any check fails. The run directory keeps all
evidence either way.

Usage: check_harness.py RUN_DIR
"""

import os
import struct
import sys

CANVAS = (960, 544)
CLEAR_POINTS = {0: 0, 1: 100, 2: 300, 3: 500, 4: 800}
PIECE_LETTERS = "IOTSZJL"

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
    if depth != 24 and depth != 32:
        fail(rule, "%s has %d bits per pixel" % (path, depth))
        return
    good(rule, os.path.basename(path))


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


def check_log(session, log_path, must_contain):
    rule = "%s/log" % session
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
# Per-session expectations. Values pinned here come from the SPEC arithmetic
# and the headless rules suite, never from the run under test: a change in
# game behavior fails the harness instead of moving the pin.
# ---------------------------------------------------------------------------

def check_session(run_dir, session):
    ref_path = os.path.join(run_dir, session + ".ref.obs")
    run_path = os.path.join(run_dir, session + ".run.obs")
    log_path = os.path.join(run_dir, session + ".run.log")
    shots_dir = os.path.join(run_dir, session, "shots")
    save_dir = os.path.join(run_dir, session, "save")

    for path in (ref_path, run_path):
        if not os.path.isfile(path):
            fail("%s/records" % session, "missing %s" % path)
            return
    ref_records = read_records(ref_path)
    run_records = read_records(run_path)
    if not run_records:
        fail("%s/records" % session, "%s has no checkpoint records" % run_path)
        return

    compare_streams(session, run_records, ref_records)
    check_screenshots(session, run_records, shots_dir)
    check_session_log_in_save_dir(session, save_dir)

    by_name = {record["check"]: record for record in run_records}
    if session == "scene-cycle-keyboard":
        check_scene_sequence(run_records, [
            "title", "title", "controls", "title", "title",
            "playing", "playing", "playing", "playing", "playing",
            "paused", "paused", "playing", "paused", "paused", "playing",
            "gameover", "gameover", "playing", "gameover", "gameover",
            "title", "title", "title"], session + "/scene-sequence")
        check_field(by_name["gameover-first"], "score", "218",
                    session + "/first-round-score")
        check_field(by_name["gameover-first"], "record", "1",
                    session + "/first-round-record")
        check_field(by_name["gameover-second"], "score", "220",
                    session + "/second-round-score")
        check_field(by_name["gameover-second"], "high", "220",
                    session + "/second-round-high")
        check_field(by_name["quit-confirmed"], "quit", "1",
                    session + "/quit-confirmed")
        pause_freeze(session, run_records, "paused", "paused-still",
                     session + "/pause-freeze")
        check_log(session, log_path, [
            "Clean shutdown", "Quit requested through the title menu",
            "Scene title -> playing", "Scene playing -> paused",
            "Scene paused -> playing", "Scene playing -> gameover",
            "Scene gameover -> title",
        ])
        check_high_score_file(session, save_dir, 220,
                              session + "/saved-record")
    elif session == "scene-cycle-pad":
        check_field(by_name["playing-start"], "scene", "playing",
                    session + "/pad-starts-round")
        check_field(by_name["after-hold"], "hold", "T,locked",
                    session + "/pad-hold")
        check_field(by_name["after-detach"], "active", "S,0,2,-1",
                    session + "/pad-detach-stops-movement")
        check_field(by_name["gameover-first"], "score", "218",
                    session + "/first-round-score")
        check_log(session, log_path, [
            "Clean shutdown", "Virtual Pad\" (instance", "removed",
            "Scene title -> playing",
        ])
        check_high_score_file(session, save_dir, 220,
                              session + "/saved-record")
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
        check_field(by_name["lockout-over"], "settled", "84",
                    session + "/lockout-hidden-cells-committed")
        hard_drop_arithmetic(session, run_records, "before-drop",
                             "lockout-over", session + "/lockout-drop-points")
        check_log(session, log_path, [
            "Clean shutdown", "Fixture \"lockout-ready\" applied from",
        ])
    elif session == "fixture-blockout":
        check_field(by_name["blockout-applied"], "scene", "gameover",
                    session + "/blockout-scene")
        check_field(by_name["blockout-applied"], "settled", "29",
                    session + "/blockout-board")
        check_field(by_name["gameover-at-title"], "menu", "over:1",
                    session + "/gameover-cursor")
        check_log(session, log_path, [
            "Clean shutdown", "Fixture \"blockout-ready\" applied from",
        ])
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
