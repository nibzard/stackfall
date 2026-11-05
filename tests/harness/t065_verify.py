#!/usr/bin/env python3
"""Verify one T065 scenario run measures what its name says (task T065).

A benchmark scenario that mostly sat at a game over screen, or that never
exercised the inputs the round names, measures nothing a player sees. This
checker reads one scenario run's own evidence - the run log and the per-tick
input log - and fails it when either happens.

    t065_verify.py SAMPLE_DIR SCENARIO

Exit status 0 means the scenario's claims held. The message on stdout goes
into the evidence set's verify.txt.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from t065_scenes import scene_shares  # noqa: E402

# The coverage each scenario must show, counted from the per-tick input log
# (one action-bit line per tick) and the run log. The numbers are floors the
# scripts comfortably clear, so a broken generator fails loudly instead of
# drifting.
COVERAGE = {
    "rotate-move": [("clockwise rotations", "cw", 20),
                    ("counter-clockwise rotations", "ccw", 20),
                    ("left ticks", "L", 20), ("right ticks", "R", 20)],
    "drops": [("soft-drop ticks", "D", 20), ("hard drops", "drop", 20)],
    "clears": [("four-line fixture cycles", "fixture", 3)],
    "pause": [("pause cycles", "playing -> paused", 10),
              ("resume cycles", "paused -> playing", 10),
              ("clockwise rotations", "cw", 20),
              ("counter-clockwise rotations", "ccw", 20)],
    "pad-mixed": [("clockwise rotations", "cw", 20),
                  ("counter-clockwise rotations", "ccw", 20),
                  ("left ticks", "L", 20), ("right ticks", "R", 20),
                  ("soft-drop ticks", "D", 20), ("hard drops", "drop", 20),
                  ("pause cycles", "playing -> paused", 3)],
}


def count_pattern(text, pattern):
    """Tick lines carry `cw=1`; scene lines carry the words themselves."""
    if pattern in ("cw", "ccw", "L", "R", "D", "drop"):
        return len(re.findall(r"(?<![A-Za-z])%s=1(?![A-Za-z])" % pattern,
                              text, re.M))
    if pattern == "fixture":
        return text.count('Fixture "four-line-ready" applied')
    return text.count(pattern)


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    directory, scenario = argv[1], argv[2]
    with open(os.path.join(directory, "run.log"), encoding="utf-8",
              errors="replace") as handle:
        log = handle.read()
    with open(os.path.join(directory, "il.log"), encoding="utf-8",
              errors="replace") as handle:
        ticks_log = handle.read()

    shares, total = scene_shares(os.path.join(directory, "il.log"))
    game_over = shares.get("gameover", 0) / float(total) if total else 1.0
    restarts = log.count("script: restart round")

    problems = []
    if total == 0:
        problems.append("no simulation ticks")
    if game_over > 0.15:
        problems.append("game over held %.1f%% of the session"
                        % (100.0 * game_over))
    if restarts < 1:
        problems.append("no restart, so one round carried the whole sample")
    paused_share = shares.get("paused", 0) / float(total) if total else 0
    if scenario != "pause" and paused_share > 0.5:
        problems.append("paused %.0f%% of a non-pause scenario"
                        % (100.0 * paused_share))
    counts = []
    for label, pattern, minimum in COVERAGE.get(scenario, []):
        # Action bits come from the per-tick input log; the fixture and
        # scene-word counts come from the run log that carries them.
        count = count_pattern(log if pattern in ("fixture",) else ticks_log,
                              pattern)
        counts.append((label, count))
        if count < minimum:
            problems.append("%s: %d, expected at least %d"
                            % (label, count, minimum))

    detail = ("play %.0f%%, paused %.0f%%, game over %.1f%%, %d restarts; %s"
              % (100.0 * shares.get("playing", 0) / total if total else 0,
                 100.0 * paused_share, 100.0 * game_over, restarts,
                 ", ".join("%s=%d" % pair for pair in counts)))
    if problems:
        print("%s: FAILED, %s [%s]" % (scenario, "; ".join(problems), detail))
        return 1
    print("%s: OK, %s" % (scenario, detail))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
