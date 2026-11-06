"""The focus-loss check of the integrated background validation (T085).

Reads the captures of tools/backgrounds/focus_session.sh - the X11 session
that minimizes the STACKFALL window mid-round in Fixed design 20 and
restores it - and proves the wallpaper clock behaved:

  * frozen: the checkpoint 120 frames into the focus loss matches the
    design package's own plate at a clock within 0.6 s of the
    pre-minimize checkpoint. Design 20 drifts ten pixels per second, so a
    clock that kept running through those two unfocused seconds would sit
    two seconds ahead;
  * resumed without a jump: the checkpoint half a second after the restore
    matches a plate between 0.1 s and 1.6 s past the frozen clock - it
    moved again, and it did not absorb the unfocused gap (the runtime
    clamps one resume advance at 250 ms);
  * the guard gaps of every solved frame match the same plate, the
    settings still hold Fixed design 20, and the observation records show
    a playing scene throughout.

The probe runs at real-time pacing, so the check solves each frame's
presentation clock from the pixels instead of deriving it from ticks: it
scans plate times, takes the first matching plateau, and never trusts a
single match (a matching plateau spans the grammar's whole-pixel step).

Run: python3 tools/backgrounds/focus_check.py SESSION
where SESSION holds observations.obs, save/settings.dat, and shots/.
Exits nonzero on any failure.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import catalog_check as cc
from imaging import read_bmp

LEVEL = 20
SCAN_STEP_MS = 20


def plateau(cache, loud, low_ms, high_ms):
    """The [first, last] milliseconds at which the design 20 plate equals
    `loud`, scanning `low_ms` to `high_ms`. None when nothing matches."""
    hits = []
    t_ms = low_ms
    while t_ms <= high_ms:
        plate = cc.plate_regions(cache, "landscape", LEVEL, t_ms)
        if loud == plate[0]:
            hits.append(t_ms)
        t_ms += SCAN_STEP_MS
    if not hits:
        return None
    return (hits[0], hits[-1])


def main(session):
    records = cc.read_records(os.path.join(session, "observations.obs"))
    for name in ("foc-a", "foc-b", "foc-c"):
        cc.check(name in records, "the session lacks checkpoint %s" % name)
        cc.check(records.get(name, {}).get("scene") == "playing",
                 "checkpoint %s left the playing scene" % name)
    if cc.failures:
        return report()

    cache = {}
    frames = {}
    guards = {}
    for name in ("foc-a", "foc-b", "foc-c"):
        frames[name] = read_bmp(os.path.join(session, "shots",
                                             "%s.bmp" % name))
        cc.check((frames[name].w, frames[name].h) == (960, 544),
                 "foc-%s is not the exact 960x544 device frame" % name[-1])
        loud, guard, _ = cc.frame_regions(frames[name], "landscape")
        guards[name] = guard

    # The pre-minimize clock: the round began after a short title phase,
    # so the clock sits a little past tick * 50/3 ms. Scan the first six
    # seconds; design 20's cycle outlives that window, so its first
    # matching plateau is the true one.
    tick_a = int(records["foc-a"]["tick"])
    span_a = plateau(cache, cc.frame_regions(frames["foc-a"], "landscape")[0],
                     0, 6000)
    cc.check(span_a is not None,
             "foc-a matches no design 20 plate in the first six seconds")
    if span_a is None:
        return report()
    print("focus: foc-a (tick %d) solves at %d..%d ms"
          % (tick_a, span_a[0], span_a[1]))

    # Frozen: two unfocused seconds later the plate sits within 0.6 s of
    # foc-a, not the two seconds a running clock would have added.
    span_b = plateau(cache, cc.frame_regions(frames["foc-b"], "landscape")[0],
                     span_a[0], span_a[1] + 3000)
    cc.check(span_b is not None, "foc-b matches no design 20 plate")
    if span_b is None:
        return report()
    print("focus: foc-b solves at %d..%d ms (%d frames unfocused)"
          % (span_b[0], span_b[1],
             int(records["foc-b"]["tick"]) - tick_a))
    cc.check(span_b[0] >= span_a[0] and span_b[1] <= span_a[1] + 600,
             "the wallpaper clock ran on through the focus loss "
             "(foc-b solved at %d..%d ms, foc-a at %d..%d ms)"
             % (span_b[0], span_b[1], span_a[0], span_a[1]))

    # Resumed without a jump: half a second past the restore the clock has
    # moved on from the frozen plateau by more than a frame step and by
    # less than the 250 ms resume clamp plus the refocused half second.
    span_c = plateau(cache, cc.frame_regions(frames["foc-c"], "landscape")[0],
                     span_b[0], span_b[1] + 2500)
    cc.check(span_c is not None, "foc-c matches no design 20 plate")
    if span_c is None:
        return report()
    print("focus: foc-c solves at %d..%d ms" % (span_c[0], span_c[1]))
    cc.check(span_c[0] > span_b[1] + 100,
             "the wallpaper did not resume after the restore "
             "(foc-c solved at %d..%d ms, the frozen clock at %d..%d ms)"
             % (span_c[0], span_c[1], span_b[0], span_b[1]))
    cc.check(span_c[1] < span_b[0] + 1600,
             "the wallpaper clock jumped on resume "
             "(foc-c solved at %d..%d ms, the frozen clock at %d..%d ms)"
             % (span_c[0], span_c[1], span_b[0], span_b[1]))

    # The guard gaps of every solved frame hold the same plate, not just
    # the loud margins the plateau search used.
    for name, span in (("foc-a", span_a), ("foc-b", span_b),
                       ("foc-c", span_c)):
        plate = cc.plate_regions(cache, "landscape", LEVEL,
                                 (span[0] + span[1]) // 2)
        cc.check(guards[name] == plate[1],
                 "the guard gaps of %s left their plate" % name)

    settings = open(os.path.join(session, "save", "settings.dat"),
                    encoding="ascii").read()
    cc.check("background=fixed" in settings and "backgroundid=20" in settings,
             "the settings left Fixed design 20: %r" % settings)
    return report()


def report():
    print("focus check: %d checks, %d failures" % (cc.checks,
                                                   len(cc.failures)))
    for message in cc.failures:
        print("  %s" % message)
    return 1 if cc.failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
