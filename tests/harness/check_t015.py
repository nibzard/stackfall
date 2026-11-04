#!/usr/bin/env python3
"""Independent checker for the T015 M3 desktop gate.

Reads one T015 run directory (tests/harness/t015.sh) and decides pass or
fail for the whole round-M3 gate from evidence:

  * both build configurations ran their full ctest pass and compiled with
    no warning;
  * the deterministic replay hashes printed by the rules suite are the
    pinned values and are byte-identical across Debug and Release
    (specification section 11, item 18, desktop half);
  * every M3 session's records equal the headless reference stream field
    by field and repeat byte-identically inside one run;
  * the DAS/ARR session pins the section 4.5 horizontal repeat arithmetic
    from its own checkpoint ticks: the move on the press tick, the
    ten-tick charge, the two-tick repeat cadence, the parked wall, the
    immediate move on a direction switch, the left-plus-right cancel, and
    a pause mid-charge that freezes the charge instead of resetting it;
  * the lock-delay session pins the section 4.5 lock arithmetic: a
    grounded piece locks exactly on the 30th grounded tick (landing tick
    included), grounded moves reset the timer, the budget is fifteen
    resets, and the sixteenth move still moves the piece without
    resetting the timer;
  * the seven-bag sessions reconstruct the piece stream from the queue
    windows of their own records and prove every block of seven draws is
    a permutation of I O T S Z J L with consistent overlapping windows,
    plus the hold rules of section 4.3 at a bag boundary;
  * the restart session proves every restart path of section 4.7 begins
    a fresh seeded round with reset counters, and that a held direction
    carries into a restart as a held level that stops on release;
  * the four high-score process runs prove a record survives process
    restarts, an equal score is not a record, a better score replaces
    the file, and a damaged file recovers to zero without a crash;
  * the retained gate artifacts reran green at this source identity: the
    T023 harness, the T024 play sessions in both configurations, the
    chosen T025 stability phases, and the T026 scenario verification, and
    the required reports (T024, T025, T026, T027, T028) exist with no
    unresolved desktop target failure.

A few pins are regression pins recorded from a passing rehearsal; the
comment on each says which. The reference-stream comparison above them
carries correctness. Exit status is nonzero when any check fails.

Usage: check_t015.py RUN_DIR
"""

import os
import re
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
check_field = base.check_field
compare_streams = base.compare_streams
hard_drop_arithmetic = base.hard_drop_arithmetic
soft_drop_arithmetic = base.soft_drop_arithmetic
pause_freeze = base.pause_freeze
check_high_score_file = base.check_high_score_file

BAG = sorted("IOTSZJL")

# The pinned replay hashes of the rules suite (specification section 11,
# item 18, desktop half): scenario -> (hash, score, lines). Recorded from
# tests/test_regression.cpp, re-pinned for the 1.0.0 version of the first
# release (2026-11-04); the suite itself pins the same values on every
# ctest run, so the gate checker holding them too is the second,
# independent guard.
PINNED_HASHES = {
    "gravity-lock": ("0xcbc3d0ac48e709d5", 0, 0),
    "soft-drop-moves": ("0x73c9f9b3446d866f", 98, 0),
    "hold-and-pause": ("0x3fe169192382b9d1", 99, 0),
    "stack-and-clear": ("0xeab46107fc034bc1", 539, 2),
    "block-out": ("0x108537ace8574743", 202, 0),
}


def by_name(records):
    return {record["check"]: record for record in records}


def active_of(record):
    return piece_cells(record, "active")


def check_active(session, record, expected, rule):
    got = active_of(record)
    if got != expected:
        fail(rule, "active at %s is %s, expected %s" %
             (record["check"], got, expected))
    else:
        good(rule, "%s" % (expected,))


def same_x(session, before, after, rule):
    """Same piece, same column: only the row may change."""
    a, b = active_of(before), active_of(after)
    if a is None or b is None or a[0] != b[0]:
        fail(rule, "not the same piece at %s -> %s" %
             (before["check"], after["check"]))
        return
    if b[2] != a[2]:
        fail(rule, "column moved %d -> %d, expected no move" % (a[2], b[2]))
    else:
        good(rule, "x stays %d (y %d -> %d)" % (a[2], a[3], b[3]))


def x_of(record):
    piece = active_of(record)
    return None if piece is None else piece[2]


def y_of(record):
    piece = active_of(record)
    return None if piece is None else piece[3]


# ---------------------------------------------------------------------------
# m3-das-arr: section 4.5 horizontal repeat arithmetic.
# ---------------------------------------------------------------------------

def check_das_arr(session, records, run_dir):
    names = by_name(records)
    check_active(session, names["round-start"], ("T", 0, 3, 0),
                 session + "/first-spawn")

    # A fresh press moves on its own tick: the record before the press
    # shows the spawn column one tick earlier, the press record the move.
    press = names["press-move"]
    p = number(press, "tick")
    if number(names["round-start"], "tick") != p - 1 or \
            x_of(names["round-start"]) != 3 or x_of(press) != 2:
        fail(session + "/press-tick-move",
             "spawn x=%s at tick %s then x=%s at tick %d, expected the move "
             "on the press tick itself" %
             (x_of(names["round-start"]),
              number(names["round-start"], "tick"), x_of(press), p))
    else:
        good(session + "/press-tick-move", "x 3 -> 2 on tick %d" % p)

    # Ten-tick charge: one record per tick, no movement.
    for k in range(1, 10):
        record = names["charge-%02d" % k]
        if number(record, "tick") != p + k or x_of(record) != 2:
            fail(session + "/das-charge",
                 "charge-%02d at tick %d x=%s, expected tick %d x=2" %
                 (k, number(record, "tick"), x_of(record), p + k))
            return
    good(session + "/das-charge",
         "no movement for the nine charge ticks %d..%d" % (p + 1, p + 9))

    # First repeat exactly ten ticks after the press tick, then the
    # two-tick repeat gap (section 4.5: DAS 10, ARR 2).
    first = names["repeat-first"]
    if number(first, "tick") != p + 10 or x_of(first) != 1:
        fail(session + "/first-repeat",
             "first repeat at tick %d x=%s, expected tick %d x=1" %
             (number(first, "tick"), x_of(first), p + 10))
        return
    good(session + "/first-repeat", "tick %d = press %d + 10" % (p + 10, p))
    for name, tick, x in (("arr-gap", p + 11, 1),
                          ("repeat-second", p + 12, 0)):
        record = names[name]
        if number(record, "tick") != tick or x_of(record) != x:
            fail(session + "/arr-cadence",
                 "%s at tick %d x=%s, expected tick %d x=%d" %
                 (name, number(record, "tick"), x_of(record), tick, x))
            return
    good(session + "/arr-cadence", "repeat gap of exactly two ticks")

    # Repeats against the wall fail without moving the piece.
    for name in ("repeat-third", "repeat-fourth", "released-at-wall"):
        if x_of(names[name]) != 0:
            fail(session + "/wall-parked", "%s x=%s, expected 0 at the wall"
                 % (name, x_of(names[name])))
            return
    good(session + "/wall-parked", "repeats fail at the left wall, x stays 0")

    # A direction switch moves on its own press tick (section 4.5).
    switch = names["switch-move"]
    s = number(switch, "tick")
    if x_of(switch) != 1 or number(names["released-at-wall"], "tick") >= s:
        fail(session + "/switch-tick-move",
             "switch at tick %d x=%s, expected an immediate move to 1" %
             (s, x_of(switch)))
        return
    good(session + "/switch-tick-move",
         "right press moves to x=1 on tick %d" % s)

    # Five charge ticks, then the pause; the pause freezes every counter
    # exactly, so the two paused records are identical and hold the same
    # tick (section 4.5).
    for k in range(1, 6):
        record = names["right-charge-%02d" % k]
        if number(record, "tick") != s + k or x_of(record) != 1:
            fail(session + "/right-charge",
                 "right-charge-%02d at tick %d x=%s, expected tick %d x=1" %
                 (k, number(record, "tick"), x_of(record), s + k))
            return
    good(session + "/right-charge",
         "charge %d..%d with no movement" % (s + 1, s + 5))
    pause_freeze(session, records, "paused-mid-charge", "paused-still",
                 session + "/pause-freeze")
    check_field(names["paused-still"], "tick",
                str(number(names["paused-mid-charge"], "tick")),
                session + "/pause-freezes-clock")

    # The paused charge is frozen, not reset: the first repeat after the
    # resume still lands exactly ten playing ticks after the press tick,
    # because paused ticks never advance the playing clock. A charge
    # reset by the pause would push the first repeat ten ticks past the
    # resume instead (the post-resume records cover one tick each).
    resumed = names["resumed"]
    posts = [resumed] + [names["post-resume-%02d" % k] for k in range(1, 9)]
    moves = []
    for before, after in zip(posts, posts[1:]):
        if x_of(after) is not None and x_of(before) is not None and \
                x_of(after) != x_of(before):
            moves.append(number(after, "tick"))
    if not moves:
        fail(session + "/frozen-charge-resumes",
             "no repeat moved the piece after the resume")
        return
    if moves[0] != s + 10:
        fail(session + "/frozen-charge-resumes",
             "first repeat after the resume at tick %d, expected %d = press "
             "%d + 10 (a reset charge would repeat ten ticks past the "
             "resume instead)" % (moves[0], s + 10, s))
        return
    if not all(b - a == 2 for a, b in zip(moves, moves[1:])):
        fail(session + "/resumed-arr", "repeat ticks %s, expected every 2"
             % moves)
        return
    good(session + "/frozen-charge-resumes",
         "first repeat at tick %d = press %d + 10 across the pause"
         % (moves[0], s))
    good(session + "/resumed-arr", "repeats at %s, every two ticks" % moves)

    # Left and right together move nothing; releasing one leaves the
    # other as a fresh press that moves immediately (section 5).
    left_switch = names["left-switch-move"]
    both = names["both-held"]
    took = names["right-takes-over"]
    back = names["left-switch-back"]
    if x_of(both) != x_of(left_switch):
        fail(session + "/cancel", "column moved while both directions held: "
             "%d -> %d" % (x_of(left_switch), x_of(both)))
    else:
        good(session + "/cancel", "left+right holds the column at %d"
             % x_of(left_switch))
    if x_of(took) == x_of(both) + 1 and x_of(back) == x_of(took) - 1:
        good(session + "/takeover",
             "released left moves right to %d, then left back to %d" %
             (x_of(took), x_of(back)))
    else:
        fail(session + "/takeover",
             "takeover columns both=%s took=%s back=%s" %
             (x_of(both), x_of(took), x_of(back)))


# ---------------------------------------------------------------------------
# m3-lock-delay: section 4.5 lock delay and the reset budget.
# ---------------------------------------------------------------------------

def landing_tick(records, resting_y, prefix):
    """The tick the active piece fell into its resting row: the earliest
    prefixed record at that row with a prefixed record one tick earlier
    one row above, so the landing is observed, not assumed."""
    landed = [r for r in records
              if r["check"].startswith(prefix) and y_of(r) == resting_y]
    if not landed:
        return None, None
    first = min(landed, key=lambda r: number(r, "tick"))
    above = [r for r in records
             if r["check"].startswith(prefix) and
             y_of(r) == resting_y - 1 and
             number(r, "tick") < number(first, "tick")]
    if not above:
        return None, None
    above = max(above, key=lambda r: number(r, "tick"))
    if number(above, "tick") != number(first, "tick") - 1:
        return None, None
    return number(first, "tick"), first


def check_lock_delay(session, records, run_dir):
    names = by_name(records)

    # Part one: the T parks at the left wall, lands by soft drop, and
    # locks on the 30th grounded tick.
    resting = y_of(names["fall-23"])
    if resting != 18:
        fail(session + "/resting-row", "the T rests at y=%s, expected 18"
             % resting)
        return
    land, land_record = landing_tick(records, resting, "fall-")
    if land is None:
        fail(session + "/landing-observed",
             "no record pair shows the fall into row %d" % resting)
        return
    good(session + "/landing-observed",
         "the T lands on tick %d (row %d the tick before)" % (land, resting))

    # Soft drop scores one point per successfully dropped cell (4.6).
    soft_drop_arithmetic(session, records, "parked-left",
                         land_record["check"], session + "/soft-drop-points")

    # No lock before the 30th grounded tick: the landing tick is grounded
    # tick one, so the first lock record sits exactly on land + 29 and
    # every record in between still shows the grounded T.
    locked = [r for r in records if int(r["settled"]) >= 4]
    if not locked:
        fail(session + "/lock-on-30th-tick", "no lock record at all")
        return
    first_lock = min(locked, key=lambda r: number(r, "tick"))
    lock_tick = number(first_lock, "tick")
    if lock_tick != land + 29:
        fail(session + "/lock-on-30th-tick",
             "first lock record at tick %d, expected %d = landing %d + 29 "
             "(30 grounded ticks, landing included)" %
             (lock_tick, land + 29, land))
        return
    held = [r for r in records
            if land <= number(r, "tick") <= land + 28 and
            active_of(r) is not None]
    if not held or not all(active_of(r)[0] == "T" and y_of(r) == resting and
                           r["settled"] == "0" for r in held):
        fail(session + "/no-early-lock",
             "a record between the landing and tick %d does not show the "
             "grounded T" % (land + 28))
        return
    good(session + "/lock-on-30th-tick",
         "T active through tick %d, locked on tick %d" % (land + 28,
                                                          lock_tick))
    check_active(session, first_lock, ("Z", 0, 3, 0),
                 session + "/next-spawn-after-lock")

    # Part two: the Z lands mid-board and spends the reset budget.
    resting2 = y_of(names["z-fall-23"])
    land2, land2_record = landing_tick(records, resting2, "z-fall-")
    if land2 is None:
        fail(session + "/z-landing-observed",
             "no record pair shows the Z falling into its resting row")
        return
    good(session + "/z-landing-observed",
         "the Z lands on tick %d at y=%d" % (land2, resting2))
    soft_drop_arithmetic(session, records, "z-mid", land2_record["check"],
                         session + "/z-soft-drop-points")

    # Sixteen successful grounded moves, one record on each move tick.
    resets = [names["z-reset-%02d" % k] for k in range(1, 17)]
    for record in resets:
        piece = active_of(record)
        if piece is None or piece[0] != "Z" or piece[3] != resting2:
            fail(session + "/grounded-moves",
                 "%s shows %s, expected the grounded Z at y=%d" %
                 (record["check"], piece, resting2))
            return
    columns = [x_of(r) for r in resets]
    if any(a == b for a, b in zip(columns, columns[1:])):
        fail(session + "/grounded-moves",
             "a reset record did not move the piece: %s" % columns)
        return
    ticks = [number(r, "tick") for r in resets]
    good(session + "/grounded-moves",
         "16 grounded moves at ticks %d..%d, columns %s" %
         (ticks[0], ticks[-1], columns))

    # The resets extended the piece past its own no-reset lock point.
    if ticks[14] <= land2 + 29:
        fail(session + "/resets-extend-life",
             "the 15th reset at tick %d did not pass the no-reset lock tick "
             "%d" % (ticks[14], land2 + 29))
        return
    alive = [r for r in records
             if land2 + 29 < number(r, "tick") < ticks[14] + 29 and
             active_of(r) is not None and active_of(r)[0] == "Z"]
    if not alive:
        fail(session + "/resets-extend-life",
             "no record shows the Z alive after its no-reset lock tick")
        return
    good(session + "/resets-extend-life",
         "%d records show the Z alive past tick %d" % (len(alive),
                                                       land2 + 29))

    # The budget is fifteen: the lock lands exactly 29 ticks after the
    # fifteenth reset, strictly before the sixteenth reset's own window,
    # which proves the sixteenth move did not reset the timer.
    locked2 = [r for r in records if int(r["settled"]) >= 8]
    if not locked2:
        fail(session + "/reset-budget-15", "the Z never locked")
        return
    first_lock2 = min(locked2, key=lambda r: number(r, "tick"))
    lock2 = number(first_lock2, "tick")
    if lock2 != ticks[14] + 29:
        fail(session + "/reset-budget-15",
             "lock at tick %d, expected %d = 15th reset %d + 29" %
             (lock2, ticks[14] + 29, ticks[14]))
        return
    if lock2 >= ticks[15] + 29:
        fail(session + "/reset-budget-15",
             "lock at tick %d waited for the 16th reset's window (%d), so "
             "the 16th move reset the timer" % (lock2, ticks[15] + 29))
        return
    good(session + "/reset-budget-15",
         "lock on tick %d = 15th reset + 29; the 16th move at tick %d did "
         "not reset" % (lock2, ticks[15]))


# ---------------------------------------------------------------------------
# m3-bag-*: section 4.3 seven-bag and queue rules.
# ---------------------------------------------------------------------------

def check_bag(session, records, run_dir, with_hold):
    names = by_name(records)

    # Every queue holds exactly five pieces in every record.
    short = [r["check"] for r in records
             if len(r.get("queue", "").split(",")) != 5]
    if short:
        fail(session + "/queue-of-five",
             "records without five previews: %s" % short[:4])
        return
    good(session + "/queue-of-five",
         "%d records, five previews each" % len(records))

    # Rebuild the piece stream from the queue windows. A record's window
    # starts at the number of pieces that have left the queue: the round
    # start takes one, every lock takes one, and the first hold swap
    # takes one more (the returned piece comes back from the hold slot,
    # not the queue). Overlapping windows must agree.
    seq = []
    window = 0
    for index, record in enumerate(records):
        if index > 0:
            previous = records[index - 1]
            window += (int(record["settled"]) -
                       int(previous["settled"])) // 4
            if record["check"] == "piece-00":
                window += 1  # the round start takes piece 0
            if with_hold and record["check"] == "after-hold":
                window += 1  # the swap drew the queue head into play
        queue = record["queue"].split(",")
        for k, letter in enumerate(queue):
            pos = window + k
            if pos < len(seq):
                if seq[pos] != letter:
                    fail(session + "/window-consistency",
                         "record %s puts %s at stream position %d, which "
                         "already holds %s" % (record["check"], letter, pos,
                                               seq[pos]))
                    return
            else:
                seq.append(letter)
    good(session + "/window-consistency",
         "%d windows agree on a %d-piece stream" % (len(records), len(seq)))

    # Every block of seven consecutive draws is a permutation of the set.
    full_bags = len(seq) // 7
    if full_bags < 3:
        fail(session + "/three-bags",
             "stream holds %d pieces, need 21" % len(seq))
        return
    for b in range(full_bags):
        block = seq[b * 7:(b + 1) * 7]
        if sorted(block) != BAG:
            fail(session + "/bag-permutation",
                 "bag %d draws %s, expected a permutation of %s" %
                 (b + 1, block, "".join(BAG)))
            return
    good(session + "/bag-permutation",
         "%d complete bags, each a permutation of IOTSZJL" % full_bags)

    # No clear may happen in this session: the placements stack raw and
    # the window arithmetic above assumes four settled cells per lock.
    cleared = [r["check"] for r in records if r.get("lines") not in
               (None, "0")]
    if cleared:
        fail(session + "/no-clears", "lines grew at %s" % cleared[:4])
        return
    good(session + "/no-clears",
         "no line cleared, four settled cells per lock")

    if not with_hold:
        held = [r["check"] for r in records if r.get("hold") != "none"]
        if held:
            fail(session + "/no-hold", "hold slot touched at %s" % held[:4])
            return
        good(session + "/no-hold", "hold slot empty throughout")
        return

    # The hold interlude, straight from section 4.3.
    before = names["before-hold"]
    after = names["after-hold"]
    stashed = active_of(before)
    head = before["queue"].split(",")[0]
    check_field(after, "hold", "%s,locked" % stashed[0],
                session + "/hold-locks")
    head_origin = base.spawn_origin(head)
    if active_of(after) != (head, 0, head_origin[0], head_origin[1]):
        fail(session + "/hold-draws-queue-head",
             "after the swap the active piece is %s, expected the queue "
             "head %s at spawn" % (active_of(after), head))
        return
    good(session + "/hold-draws-queue-head",
         "queue head %s entered play at spawn" % head)
    wanted = before["queue"].split(",")[1:] + [after["queue"].split(",")[-1]]
    if after["queue"].split(",") != wanted:
        fail(session + "/hold-queue-shift",
             "queue after the swap is %s, expected %s" %
             (after["queue"], ",".join(wanted)))
        return
    good(session + "/hold-queue-shift", "queue shifted by one, five long")

    # The second press before any lock changes nothing.
    noop = names["hold-locked-noop"]
    watched = ["active", "hold", "queue", "board", "settled", "score",
               "lines", "level"]
    differing = [k for k in watched if after.get(k) != noop.get(k)]
    if differing:
        fail(session + "/hold-once", "the second hold changed %s" % differing)
    else:
        good(session + "/hold-once", "second hold is a no-op")

    # A lock re-enables hold; the return swap brings the stashed piece
    # back at spawn rotation 0 and parks the other piece.
    drop_lock = names["hold-drop-lock"]
    check_field(drop_lock, "hold", "%s,free" % stashed[0],
                session + "/lock-frees-hold")
    ret = names["hold-return"]
    origin = base.spawn_origin(stashed[0])
    check_active(session, ret, (stashed[0], 0, origin[0], origin[1]),
                 session + "/hold-return-spawn")
    parked = active_of(drop_lock)
    check_field(ret, "hold", "%s,locked" % parked[0],
                session + "/hold-return-parks")
    check_field(names["hold-return-lock"], "hold", "%s,free" % parked[0],
                session + "/hold-return-lock-frees")


# ---------------------------------------------------------------------------
# m3-restart: section 4.7 restart paths.
# ---------------------------------------------------------------------------

def check_restart(session, records, run_dir):
    names = by_name(records)

    # Round one: two hard drops with the section 4.6 arithmetic.
    hard_drop_arithmetic(session, records, "round-1-start", "round-1-lock-1",
                         session + "/round-1-first-drop")
    hard_drop_arithmetic(session, records, "round-1-lock-1", "round-1-lock-2",
                         session + "/round-1-second-drop")

    # Pause freezes; the cursor reaches RESTART without moving the round.
    pause_freeze(session, records, "paused", "pause-at-restart",
                 session + "/pause-frozen")
    check_field(names["pause-at-restart"], "menu", "pause:1",
                session + "/pause-cursor-restart")

    # RESTART begins a fresh seeded round: counters, board, hold, and
    # queue reset. The fresh queue differs from round one's (a recorded
    # pin: rounds draw successive seeds, and these two bags differ).
    fresh = names["round-2-fresh"]
    for field, want in (("score", "0"), ("settled", "0"),
                        ("hold", "none"), ("scene", "playing")):
        check_field(fresh, field, want, session + "/restart-resets-" + field)
    if number(fresh, "tick") >= number(names["paused"], "tick"):
        fail(session + "/restart-resets-clock",
             "fresh round tick %d, before the restart the round reached %d"
             % (number(fresh, "tick"), number(names["paused"], "tick")))
    else:
        good(session + "/restart-resets-clock",
             "tick %d after %d" % (number(fresh, "tick"),
                                   number(names["paused"], "tick")))
    piece = active_of(fresh)
    origin = base.spawn_origin(piece[0]) if piece else None
    if piece is None or piece[1] != 0 or (piece[2], piece[3]) != origin:
        fail(session + "/restart-spawn",
             "active %s, expected a fresh spawn" % (piece,))
    else:
        good(session + "/restart-spawn", "%s at spawn" % (piece,))
    if fresh["queue"] == names["round-1-start"]["queue"]:
        fail(session + "/restart-new-seed",
             "the restarted round draws the same queue %s as round one"
             % fresh["queue"])
    else:
        good(session + "/restart-new-seed", "queue %s after %s" %
             (fresh["queue"], names["round-1-start"]["queue"]))

    # QUIT TO TITLE abandons the round; the title starts another one.
    check_field(names["back-at-title"], "scene", "title",
                session + "/quit-to-title")
    check_field(names["back-at-title"], "tick", "0",
                session + "/title-clock-zero")
    check_field(names["round-3-start"], "scene", "playing",
                session + "/title-starts-round")
    for field in ("score", "settled"):
        check_field(names["round-3-start"], field, "0",
                    session + "/round-3-" + field)
    if names["round-3-start"]["queue"] in (names["round-1-start"]["queue"],
                                           fresh["queue"],
                                           names["back-at-title"]["queue"]):
        # A recorded pin: all three rounds drew different bags.
        fail(session + "/round-3-new-seed", "round three repeats a queue")
    else:
        good(session + "/round-3-new-seed", "a third distinct bag")

    # A direction held across a restart moves on the fresh round's first
    # ticks and stops on release, with no other action leaking.
    held_piece = active_of(names["round-4-fresh"])
    origin4 = base.spawn_origin(held_piece[0]) if held_piece else None
    if held_piece is None or held_piece[1] != 0 or \
            held_piece[2] >= origin4[0]:
        fail(session + "/held-moves-at-restart",
             "fresh round piece %s, expected left of its spawn column %d"
             % (held_piece, origin4[0]))
        return
    good(session + "/held-moves-at-restart",
         "held Left moved the piece to x=%d on the fresh round" %
         held_piece[2])
    if not x_of(names["round-4-held-repeat"]) < x_of(names["round-4-fresh"]):
        fail(session + "/held-repeat-continues",
             "the held direction stopped repeating at %s" %
             x_of(names["round-4-held-repeat"]))
        return
    good(session + "/held-repeat-continues", "x %d -> %d under the hold" %
         (x_of(names["round-4-fresh"]), x_of(names["round-4-held-repeat"])))
    same_x(session, names["round-4-held-repeat"],
           names["round-4-release-stops"], session + "/release-stops")
    check_active(session, names["round-4-release-stops"],
                 (held_piece[0], 0,
                  x_of(names["round-4-release-stops"]), held_piece[3]),
                 session + "/no-leaked-action")

    # Game over, then PLAY AGAIN starts another fresh round.
    check_field(names["round-4-gameover"], "scene", "gameover",
                session + "/gameover-scene")
    again = names["round-5-play-again"]
    for field, want in (("scene", "playing"), ("score", "0"),
                        ("settled", "0"), ("hold", "none")):
        check_field(again, field, want, session + "/play-again-" + field)
    piece5 = active_of(again)
    origin5 = base.spawn_origin(piece5[0]) if piece5 else None
    if piece5 is None or (piece5[2], piece5[3]) != origin5 or piece5[1] != 0:
        fail(session + "/play-again-spawn",
             "active %s, expected a spawn" % (piece5,))
    else:
        good(session + "/play-again-spawn", "%s at spawn" % (piece5,))
    if again["queue"] == names["round-3-start"]["queue"]:
        fail(session + "/play-again-new-seed",
             "play again repeats round three's queue")
    else:
        good(session + "/play-again-new-seed", "another distinct bag")

    check_field(names["final-title"], "scene", "title", session + "/title")
    check_field(names["title-at-quit"], "menu", "title:3",
                session + "/quit-cursor")

    # Every title-started round runs its own seed (one input-log line per
    # title start; the pause restarts and the play-again path draw their
    # advanced seeds without passing the title). Rounds one and three
    # started from the title here.
    input_log = os.path.join(run_dir, session, "input.log")
    if not os.path.isfile(input_log):
        fail(session + "/seed-stream", "missing input log")
        return
    with open(input_log) as handle:
        seeds = re.findall(r"^round seed=(\d+)$", handle.read(), re.M)
    if len(seeds) != 2 or len(set(seeds)) != 2:
        fail(session + "/seed-stream",
             "round seeds %s, expected two distinct title-started values"
             % seeds)
    else:
        good(session + "/seed-stream", "seeds %s" % seeds)


# ---------------------------------------------------------------------------
# m3-highscore: storage rules across process restarts (section 9, item 20).
# ---------------------------------------------------------------------------

HIGHSCORE_RUNS = [
    # name, seed, title high loaded, final score, record flag, saved file
    ("run1", 999, "0", 202, "1", 202),
    ("run2", 999, "202", 202, "0", 202),
    ("run3", 5150, "202", 212, "1", 212),
    ("run4", 20261103, "0", 196, "1", 196),
]


def strip_storage_fields(record):
    """A record without the shot path or the two storage-dependent fields
    (the loaded high score and the record flag): what a replay of the
    same seed and script must reproduce exactly."""
    copy = base.strip_shot(record)
    copy.pop("high", None)
    copy.pop("record", None)
    return copy


def compare_without_storage(session, run_records, other_records, label):
    rule = "%s/%s" % (session, label)
    if len(run_records) != len(other_records):
        fail(rule, "%d records vs %d" % (len(run_records),
                                         len(other_records)))
        return
    for a, b in zip(run_records, other_records):
        if strip_storage_fields(a) != strip_storage_fields(b):
            fail(rule, "record %s differs outside the storage fields" %
                 a.get("check"))
            return
    good(rule, "%d records identical outside high/record" % len(run_records))


def check_highscore(session, run_dir):
    hs_dir = os.path.join(run_dir, session)
    save_dir = os.path.join(hs_dir, "save")
    run_streams = {}
    for name, seed, title_high, score, record, saved in HIGHSCORE_RUNS:
        one = os.path.join(hs_dir, name)
        obs = os.path.join(one, "run.obs")
        if not os.path.isfile(obs):
            fail("%s/%s/records" % (session, name), "missing %s" % obs)
            continue
        records = read_records(obs)
        names = by_name(records)
        run_streams[name] = records
        prefix = "%s/%s" % (session, name)

        # The title shows exactly what the process loaded.
        check_field(names["title"], "high", title_high,
                    prefix + "/title-high")
        # The round plays out deterministically to its score.
        check_field(names["gameover"], "score", str(score),
                    prefix + "/gameover-score")
        # A record is a strict improvement only (T012: newRecord strict).
        check_field(names["gameover"], "record", record,
                    prefix + "/record-flag")
        # The title after the round shows the better of loaded and won.
        check_field(names["title-after"], "high",
                    str(max(int(title_high), score)),
                    prefix + "/title-after-high")
        # The driver snapshots the shared file after every process, so
        # each run's saved state is pinned even though later runs keep
        # writing the same file.
        snapshot = os.path.join(one, "highscore.after")
        if not os.path.isfile(snapshot):
            fail(prefix + "/saved-file", "missing snapshot %s" % snapshot)
        else:
            with open(snapshot) as handle:
                got = handle.read()
            wanted = "version=1\nhigh_score=%d\n" % saved
            if got != wanted:
                fail(prefix + "/saved-file",
                     "the file after this run holds %r, want high_score=%d"
                     % (got, saved))
            else:
                good(prefix + "/saved-file", "high_score=%d after the run"
                     % saved)

        log_path = os.path.join(one, "run.log")
        if not os.path.isfile(log_path):
            fail(prefix + "/clean-exit", "missing %s" % log_path)
            continue
        with open(log_path) as handle:
            log = handle.read()
        if record == "1":
            if "New high score %d saved" % score not in log:
                fail(prefix + "/save-log", "no save line for %d" % score)
            else:
                good(prefix + "/save-log", "save line present")
        elif "New high score" in log:
            fail(prefix + "/save-log", "an equal score saved a record")
        else:
            good(prefix + "/save-log", "no save for the equal score")
        if "Clean shutdown" not in log:
            fail(prefix + "/clean-exit", "no clean shutdown")
        else:
            good(prefix + "/clean-exit", "clean shutdown")

    if len(run_streams) < 4:
        return

    # Same seed and script: the second process replays the first round
    # exactly, outside the storage fields (section 7 determinism).
    compare_without_storage(session, run_streams["run2"],
                            run_streams["run1"], "same-seed-replay")

    # Run 1 saw an empty save directory, so its full stream also equals
    # the headless reference stream.
    ref_path = os.path.join(hs_dir, "run1", "ref.obs")
    if os.path.isfile(ref_path):
        compare_streams(session + "/run1", run_streams["run1"],
                        read_records(ref_path))
    else:
        fail(session + "/run1/reference-stream", "missing %s" % ref_path)

    # Run 4 started from a damaged file: it recovered to zero, and the
    # evidence keeps a copy of the damaged bytes.
    corrupt = os.path.join(hs_dir, "corrupt-highscore.dat")
    if not os.path.isfile(corrupt):
        fail(session + "/run4/damaged-evidence", "missing %s" % corrupt)
    else:
        with open(corrupt, "rb") as handle:
            damaged = handle.read()
        if damaged.startswith(b"version=1\nhigh_score="):
            fail(session + "/run4/damaged-evidence",
                 "the saved copy is a valid record, not a damaged one")
        else:
            good(session + "/run4/damaged-evidence",
                 "%d damaged bytes preserved" % len(damaged))
    log4 = os.path.join(hs_dir, "run4", "run.log")
    if os.path.isfile(log4):
        with open(log4) as handle:
            text = handle.read()
        if "High score 0 (invalid)" not in text:
            fail(session + "/run4/invalid-recovery",
                 "no invalid-load line; the damaged file did not recover")
        else:
            good(session + "/run4/invalid-recovery",
                 "damaged file recovered to 0")

    # The live shared file holds the last run's record.
    check_high_score_file(session + "/final", save_dir, 196,
                          session + "/final-file")


# ---------------------------------------------------------------------------
# Gate phases: builds, hashes, and the retained artifacts.
# ---------------------------------------------------------------------------

def check_builds(run_dir):
    for config in ("debug", "release"):
        ctest_log = os.path.join(run_dir, "builds", "ctest-%s.txt" % config)
        if not os.path.isfile(ctest_log):
            fail("builds/ctest-%s" % config, "missing %s" % ctest_log)
            continue
        with open(ctest_log) as handle:
            text = handle.read()
        if "100% tests passed" not in text or "0 tests failed" not in text:
            fail("builds/ctest-%s" % config,
                 "the %s ctest pass is not green" % config)
        else:
            good("builds/ctest-%s" % config, "all suites pass")
        build_log = os.path.join(run_dir, "builds", "build-%s.log" % config)
        if os.path.isfile(build_log):
            with open(build_log) as handle:
                warnings = [line for line in handle if "warning:" in line]
            if warnings:
                fail("builds/warnings-%s" % config,
                     "%d compiler warnings, first: %s" %
                     (len(warnings), warnings[0].strip()))
            else:
                good("builds/warnings-%s" % config, "no compiler warnings")


def check_hashes(run_dir):
    texts = {}
    for config in ("debug", "release"):
        path = os.path.join(run_dir, "hashes", "%s.txt" % config)
        if not os.path.isfile(path):
            fail("hashes/%s" % config, "missing %s" % path)
            continue
        with open(path) as handle:
            texts[config] = handle.read()
    if len(texts) != 2:
        return
    if texts["debug"] != texts["release"]:
        fail("hashes/cross-build", "Debug and Release outputs differ")
        return
    good("hashes/cross-build",
         "identical rules-suite output in both configurations")

    # The pinned values of the five regression scenarios (item 18).
    for scenario, (want_hash, want_score, want_lines) in \
            sorted(PINNED_HASHES.items()):
        pattern = (r"^\s+%s\s+seed \d+\s+hash 0x%s\s+score %d\s+lines %d"
                   % (re.escape(scenario), want_hash[2:], want_score,
                      want_lines))
        if re.search(pattern, texts["debug"], re.M) is None:
            fail("hashes/pinned-%s" % scenario,
                 "no line pins %s to %s" % (scenario, want_hash))
        else:
            good("hashes/pinned-%s" % scenario, want_hash)
    if "0 failures" not in texts["debug"]:
        fail("hashes/suite", "the rules suite reported failures")
    else:
        good("hashes/suite", "0 failures")


def check_phase_status(run_dir, group, name):
    path = os.path.join(run_dir, group, "%s.status" % name)
    if not os.path.isfile(path):
        fail("%s/%s" % (group, name), "missing %s" % path)
        return
    with open(path) as handle:
        status = handle.read().strip()
    # T025 status files are two lines ("passed" plus a detail line); the
    # drivers here write "passed: detail" on one line. Both pass.
    first_line = status.splitlines()[0].strip()
    detail = status.splitlines()[1].strip() if "\n" in status else \
        (first_line.split(":", 1)[1].strip() if ":" in first_line else "")
    if first_line.split(":")[0].strip() == "passed":
        good("%s/%s" % (group, name), detail or "passed")
    else:
        fail("%s/%s" % (group, name), status)


def check_reports(repo):
    """The gate requires the retained reports: T024 play review, T025
    stability, T026 performance with no unresolved desktop target, and
    the T027/T028 design review closure (docs/AGENT_TESTING.md)."""
    notes = os.path.join(repo, "notes")
    agent = os.path.join(notes, "agent-testing.md")
    performance = os.path.join(notes, "performance.md")
    design = os.path.join(notes, "design-review.md")
    for path in (agent, performance, design):
        if not os.path.isfile(path):
            fail("reports/files", "missing %s" % path)
            return
    with open(agent) as handle:
        agent_text = handle.read()
    for heading in ("## T024: agent play and visual inspection",
                    "## T025: unattended stability and failure checks"):
        if heading not in agent_text:
            fail("reports/agent-testing", "lacks %r" % heading)
        else:
            good("reports/agent-testing", heading)
    with open(performance) as handle:
        perf_text = handle.read()
    if "## Final regression gate" not in perf_text:
        fail("reports/performance", "no final regression gate section")
    else:
        good("reports/performance", "final regression gate recorded")
    parts = perf_text.split("## Unresolved limits", 1)
    if len(parts) < 2:
        fail("reports/performance", "no unresolved-limits section")
        return
    for marker in ("unmet target", "still fails", "not met",
                   "fails the target"):
        if marker in parts[1]:
            fail("reports/performance",
                 "an unresolved desktop target failure is on record (%r); "
                 "the gate cannot claim a performance pass" % marker)
            return
    good("reports/performance",
         "no unresolved desktop target in the limits section")
    with open(design) as handle:
        design_text = handle.read()
    for heading in ("## T027: capture, review, and route findings",
                    "## T028: verify fixes and close the review"):
        if heading not in design_text:
            fail("reports/design-review", "lacks %r" % heading)
        else:
            good("reports/design-review", heading)
    if "closure gate closes" not in design_text:
        fail("reports/design-review", "the T028 closure is not recorded")
    else:
        good("reports/design-review", "T028 closure recorded")


# ---------------------------------------------------------------------------
# Session dispatch.
# ---------------------------------------------------------------------------

DETERMINISTIC = {
    "m3-das-arr": check_das_arr,
    "m3-lock-delay": check_lock_delay,
    "m3-bag-20261103": lambda s, r, d: check_bag(s, r, d, False),
    "m3-bag-31337": lambda s, r, d: check_bag(s, r, d, False),
    "m3-bag-777": lambda s, r, d: check_bag(s, r, d, True),
    "m3-restart": check_restart,
}


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    run_dir = os.path.abspath(sys.argv[1])
    repo = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))

    print("--- builds and hashes")
    check_builds(run_dir)
    check_hashes(run_dir)

    manifest = os.path.join(run_dir, "sessions.txt")
    if not os.path.isfile(manifest):
        print("no sessions.txt in %s" % run_dir)
        return 2
    with open(manifest) as handle:
        sessions = [line.strip() for line in handle if line.strip()]
    for session in sessions:
        if session == "m3-highscore":
            continue
        print("--- %s" % session)
        ref_path = os.path.join(run_dir, session + ".ref.obs")
        run_path = os.path.join(run_dir, session + ".run.obs")
        repeat_path = os.path.join(run_dir, session + ".repeat.obs")
        for path in (ref_path, run_path, repeat_path):
            if not os.path.isfile(path):
                fail("%s/records" % session, "missing %s" % path)
        if not os.path.isfile(run_path):
            continue
        ref_records = read_records(ref_path)
        run_records = read_records(run_path)
        repeat_records = (read_records(repeat_path)
                          if os.path.isfile(repeat_path) else [])
        if not run_records:
            fail("%s/records" % session, "%s has no records" % run_path)
            continue
        compare_streams(session, run_records, ref_records)
        if repeat_records:
            compare_streams(session + "/repeat", run_records, repeat_records)
        for record in run_records:
            shot = record.get("shot", "none")
            if shot != "none":
                base.check_bmp(shot, "%s/shot:%s" % (session,
                                                     record["check"]))
        base.check_log(session, os.path.join(run_dir, session + ".run.log"),
                       ["Clean shutdown"], name="clean-exit")
        base.check_session_log_in_save_dir(
            session, os.path.join(run_dir, session, "save"))
        DETERMINISTIC[session](session, run_records, run_dir)

    # The Release reruns of the same sessions: the driver compared every
    # Release stream against the same reference and wrote one status.
    release = os.path.join(run_dir, "m3-release.status")
    if not os.path.isfile(release):
        fail("m3-release", "missing %s" % release)
    else:
        with open(release) as handle:
            status = handle.read().strip()
        if status.splitlines()[0].split(":")[0].strip() == "passed":
            good("m3-release",
                 "all sessions replay identically in the Release build")
        else:
            fail("m3-release", status)

    if "m3-highscore" in sessions:
        print("--- m3-highscore")
        check_highscore("m3-highscore", run_dir)

    print("--- retained gate artifacts")
    for group, names in (
        ("regress", ("harness", "t024-debug", "t024-release")),
        ("stability", ("failures", "restarts", "san")),
        ("performance", ("verify",)),
    ):
        for name in names:
            check_phase_status(run_dir, group, name)
    check_reports(repo)

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
