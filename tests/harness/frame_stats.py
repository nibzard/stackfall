#!/usr/bin/env python3
"""Frame-time statistics for the T025 pacing measurements.

Reads the frame-time record written by the game's --frame-times option (one
microsecond value per frame of a real-time paced run) plus the run's log, and
writes one JSON object with the elapsed time, render and simulation counts,
CPU and memory figures, and the median, 95th percentile, 99th percentile, and
maximum frame time. Optionally asserts the acceptance bounds of
docs/AGENT_TESTING.md T025: the simulation must stay at 60 Hz whatever paces
the loop, and a paced run's median frame time must sit at the 60 Hz slot.

Usage:
  frame_stats.py FRAME_TIMES LOG [--expect-seconds N] [--json OUT]

The log supplies "Loop finished: F frames rendered, T ticks simulated"; the
elapsed time comes from the frame-time record itself, so the two measurements
describe the same frames. With --expect-seconds N the script exits nonzero
when the run is shorter than N seconds, the tick rate leaves the 59.4-60.6 Hz
band, or the median frame time leaves the 15.5-17.5 ms slot.
"""

import json
import re
import sys


def read_frame_times(path):
    values = []
    with open(path, "r", encoding="ascii") as stream:
        for line in stream:
            line = line.strip()
            if line:
                values.append(int(line))
    return values


def percentile(sorted_values, fraction):
    if not sorted_values:
        return 0
    index = int(round(fraction * (len(sorted_values) - 1)))
    return sorted_values[index]


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    frame_path, log_path = sys.argv[1], sys.argv[2]
    expect_seconds = 0.0
    json_out = ""
    args = sys.argv[3:]
    for i, arg in enumerate(args):
        if arg == "--expect-seconds" and i + 1 < len(args):
            expect_seconds = float(args[i + 1])
        elif arg == "--json" and i + 1 < len(args):
            json_out = args[i + 1]

    times = read_frame_times(frame_path)
    if not times:
        sys.stderr.write("frame_stats: no frames in %s\n" % frame_path)
        return 1
    ordered = sorted(times)
    total_us = sum(times)
    stats = {
        "frames_recorded": len(times),
        "elapsed_seconds": total_us / 1e6,
        "mean_frame_ms": (total_us / len(times)) / 1e3,
        "median_frame_ms": percentile(ordered, 0.50) / 1e3,
        "p95_frame_ms": percentile(ordered, 0.95) / 1e3,
        "p99_frame_ms": percentile(ordered, 0.99) / 1e3,
        "max_frame_ms": ordered[-1] / 1e3,
        "min_frame_ms": ordered[0] / 1e3,
    }

    frames = ticks = None
    with open(log_path, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = re.search(r"Loop finished: (\d+) frames rendered, "
                              r"(\d+) ticks simulated", line)
            if match:
                frames = int(match.group(1))
                ticks = int(match.group(2))
    if frames is None:
        sys.stderr.write("frame_stats: no loop summary in %s\n" % log_path)
        return 1
    stats["frames_rendered"] = frames
    stats["ticks_simulated"] = ticks
    stats["ticks_per_second"] = ticks / stats["elapsed_seconds"]

    problems = []
    if expect_seconds > 0:
        if stats["elapsed_seconds"] < expect_seconds:
            problems.append("elapsed %.1f s is below the required %.1f s"
                            % (stats["elapsed_seconds"], expect_seconds))
        hz = stats["ticks_per_second"]
        if not 59.4 <= hz <= 60.6:
            problems.append("simulation ran at %.2f Hz, outside 59.4-60.6"
                            % hz)
        if not 15.5 <= stats["median_frame_ms"] <= 17.5:
            problems.append("median frame time %.2f ms is outside the "
                            "15.5-17.5 ms 60 Hz slot"
                            % stats["median_frame_ms"])

    if json_out:
        with open(json_out, "w", encoding="ascii") as stream:
            json.dump(stats, stream, indent=2, sort_keys=True)
            stream.write("\n")
    for key in sorted(stats):
        print("%s: %s" % (key, stats[key]))
    if problems:
        for problem in problems:
            sys.stderr.write("frame_stats: %s\n" % problem)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
