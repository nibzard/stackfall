#!/usr/bin/env python3
"""Scene-share analysis for T065 profiling sessions (task T065).

One question the device-feedback round adds to the T026 method: a benchmark
sample must not mostly sit at a game over screen, because a static game-over
frame measures nothing a player experiences. `stackfall --input-log` records
one `scene <from> -> <to> tick=<n>` line per transition, so the share of any
scene follows from the transition list and the final tick count.

Usage as a module:

    from t065_scenes import scene_shares
    shares, total = scene_shares("/path/to/input.log")

Usage as a command:

    t065_scenes.py LOG [LOG ...]
"""

import re
import sys

TRANSITION = re.compile(r"^scene (\S+) -> (\S+) tick=(\d+)$", re.M)
# Every line the input log prefixes with a known keyword other than a scene
# transition; the tick lines that carry them end the same way.
FINAL_TICK = re.compile(r"^tick=(\d+)$", re.M)
SFX_TICK = re.compile(r"^sfx \S+ tick=(\d+)$", re.M)


def _last_tick(text):
    """The largest tick the log names, from whichever line carries one."""
    ticks = [int(m.group(1)) for m in FINAL_TICK.finditer(text)]
    ticks += [int(m.group(1)) for m in SFX_TICK.finditer(text)]
    transitions = TRANSITION.finditer(text)
    ticks += [int(m.group(3)) for m in transitions]
    return max(ticks) if ticks else 0


def scene_shares(log_path):
    """Return ({scene: ticks}, total ticks) for one input log."""
    with open(log_path, encoding="utf-8") as handle:
        text = handle.read()
    transitions = [(m.group(2), int(m.group(3))) for m in TRANSITION.finditer(text)]
    total = _last_tick(text)
    shares = {}
    previous_tick = 0
    previous_scene = None
    for scene, tick in transitions:
        if previous_scene is not None:
            shares[previous_scene] = shares.get(previous_scene, 0) + \
                max(0, tick - previous_tick)
        previous_scene, previous_tick = scene, tick
    if previous_scene is not None and total > previous_tick:
        shares[previous_scene] = shares.get(previous_scene, 0) + \
            total - previous_tick
    return shares, total


def game_over_share(log_path):
    """The fraction of simulated ticks spent on the game-over screen."""
    shares, total = scene_shares(log_path)
    if total == 0:
        return 0.0, shares, total
    return shares.get("gameover", 0) / float(total), shares, total


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    for path in argv[1:]:
        share, shares, total = game_over_share(path)
        order = sorted(shares.items(), key=lambda item: -item[1])
        detail = ", ".join("%s=%d (%.1f%%)" %
                           (name, ticks, 100.0 * ticks / total if total else 0)
                           for name, ticks in order)
        print("%s: %d ticks, game over %.1f%% [%s]"
              % (path, total, 100.0 * share, detail))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
