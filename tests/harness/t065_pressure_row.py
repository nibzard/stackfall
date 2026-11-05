#!/usr/bin/env python3
"""One row of the T065 pressure table (task T065).

    t065_pressure_row.py SAMPLE_DIR KIND

Prints one tab-separated row: kind, the frame-time percentiles of the paced
session under that pressure, the record saves it fired, and the save probe's
median cost measured inside the same window.
"""

import json
import os
import sys


def frame_times_after_warmup(path, skip_frames):
    """The frame-time record without its first `skip_frames` frames."""
    with open(path, encoding="ascii") as handle:
        values = [int(line) for line in handle if line.strip()]
    return values[skip_frames:]


def percentile_ms(values, key):
    """One frame_stats percentile recomputed over `values`."""
    fraction = {"median_frame_ms": 0.50, "p95_frame_ms": 0.95,
                "p99_frame_ms": 0.99, "max_frame_ms": 1.0}[key]
    if not values:
        return 0.0
    ordered = sorted(values)
    index = int(round(fraction * (len(ordered) - 1)))
    return ordered[index] / 1e3


def main(argv):
    if len(argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    directory, kind = argv[1], argv[2]
    stats = json.load(open(os.path.join(directory, "frame-stats.json")))
    # Percentiles over the frames after the first second: display bring-up
    # under a fresh Xvfb server lands in the opening frames and belongs to
    # no pressure kind, so the comparison must not credit it to any of them.
    tail = frame_times_after_warmup(
        os.path.join(directory, "frame-times.txt"), 60)
    for key in ("median_frame_ms", "p95_frame_ms", "p99_frame_ms",
                "max_frame_ms"):
        stats[key] = percentile_ms(tail, key)
    probe = open(os.path.join(directory, "save-probe.txt"),
                 encoding="utf-8", errors="replace").read().splitlines()
    store = next(line for line in probe
                 if line.startswith("save-probe store"))
    p50 = store.split("p50=")[1].split("us")[0]
    with open(os.path.join(directory, "il.log"), encoding="utf-8",
              errors="replace") as handle:
        saves = handle.read().count("record save=")
    print("%s\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%s"
          % (kind, stats["median_frame_ms"], stats["p95_frame_ms"],
             stats["p99_frame_ms"], stats["max_frame_ms"], saves, p50))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
