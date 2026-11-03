#!/usr/bin/env python3
"""Aggregate the T026 benchmark samples into decision tables.

Reads every bench/<tag>/samples.tsv of one evidence run and writes:

- one row per (tag, scenario, renderer mode, mode) group with the sample
  count, mean, minimum, maximum, and spread of the work-mode work per frame
  (microseconds), plus CPU and memory, and
- a before-and-after comparison of every non-baseline tag against the
  baseline group it matches, with the baseline spread as the noise band: a
  retained patch must move its group beyond that band in the improving
  direction.

Paced rows (us_per_frame 0.0) carry no work number; their percentiles live
in each tag's paced-stats.tsv and are echoed group by group.

Usage: t026_summarize.py --run-dir DIR [--write-tsv OUT] [--write-text OUT]
"""

import argparse
import glob
import math
import os
import sys


def read_samples(path):
    rows = []
    with open(path, encoding="utf-8") as stream:
        header = stream.readline().rstrip("\n").split("\t")
        for line in stream:
            fields = line.rstrip("\n").split("\t")
            if len(fields) != len(header):
                continue
            row = dict(zip(header, fields))
            try:
                row["elapsed_s"] = float(row["elapsed_s"])
                row["frames"] = int(row["frames"])
                row["ticks"] = int(row["ticks"])
                row["us_per_frame"] = float(row["us_per_frame"])
                row["cpu_percent"] = float(row["cpu_percent"])
                row["max_rss_kib"] = int(row["max_rss_kib"])
            except (KeyError, ValueError):
                continue  # a malformed sample row is not a measurement
            rows.append(row)
    return rows


def stats(values):
    if not values:
        return None
    mean = sum(values) / len(values)
    spread = max(values) - min(values)
    dev = math.sqrt(sum((v - mean) ** 2 for v in values) / len(values))
    return mean, min(values), max(values), spread, dev


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--write-tsv")
    parser.add_argument("--write-text")
    args = parser.parse_args()

    groups = {}
    for path in sorted(glob.glob(
            os.path.join(args.run_dir, "bench", "*", "samples.tsv"))):
        tag = os.path.basename(os.path.dirname(path))
        for row in read_samples(path):
            if row["mode"].startswith("paced"):
                continue
            key = (tag, row["scenario"], row["renderer_mode"], row["mode"])
            groups.setdefault(key, []).append(row)

    lines = []
    tsv_lines = ["tag\tscenario\trenderer\tmode\tsamples\tus_per_frame_mean"
                 "\tmin\tmax\tspread\tstddev\tcpu_mean\trss_max"]
    for key in sorted(groups):
        rows = groups[key]
        values = [r["us_per_frame"] for r in rows]
        mean, low, high, spread, dev = stats(values)
        cpu, _, _, _, _ = stats([r["cpu_percent"] for r in rows])
        rss = max(r["max_rss_kib"] for r in rows)
        tsv_lines.append("\t".join(map(str, [
            key[0], key[1], key[2], key[3], len(values),
            "%.1f" % mean, "%.1f" % low, "%.1f" % high,
            "%.1f" % spread, "%.1f" % dev, "%.1f" % cpu, rss])))
        lines.append("%-8s %-7s %-14s %-5s n=%d  %8.1f us/frame  "
                     "min %8.1f  max %8.1f  spread %6.1f  sd %6.1f  "
                     "cpu %5.1f%%  rss %d KiB"
                     % (key[0], key[1], key[2], key[3], len(values), mean,
                        low, high, spread, dev, cpu, rss))

    # Compare every tag against the baseline of the same group.
    lines.append("")
    lines.append("Against the baseline (a kept change must clear the "
                 "baseline spread):")
    baseline_keys = {k for k in groups if k[0] == "baseline"}
    for key in sorted(groups):
        if key[0] == "baseline":
            continue
        base = ("baseline",) + key[1:]
        if base not in baseline_keys:
            continue
        new = stats([r["us_per_frame"] for r in groups[key]])
        old = stats([r["us_per_frame"] for r in groups[base]])
        change = new[0] - old[0]
        # stats() returns (mean, min, max, spread, stddev); the noise band a
        # change must clear is the baseline spread, index 3 (the max at index
        # 2 would swallow any improvement smaller than the largest sample).
        verdict = ("improves beyond noise" if -change > old[3]
                   else "regresses beyond noise" if change > old[3]
                   else "within run variance")
        lines.append("  %-8s %-7s %-14s %-5s %+8.1f us/frame "
                     "(baseline mean %.1f, spread %.1f): %s"
                     % (key[0], key[1], key[2], key[3], change, old[0],
                        old[3], verdict))

    # The paced percentiles, echoed tag by tag.
    for path in sorted(glob.glob(
            os.path.join(args.run_dir, "bench", "*", "paced-stats.tsv"))):
        tag = os.path.basename(os.path.dirname(path))
        lines.append("")
        lines.append("Paced percentiles, tag %s:" % tag)
        with open(path, encoding="utf-8") as stream:
            lines.extend("  " + line.rstrip("\n")
                         for line in stream if line.strip())

    text = "\n".join(lines) + "\n"
    if args.write_tsv:
        with open(args.write_tsv, "w", encoding="utf-8") as stream:
            stream.write("\n".join(tsv_lines) + "\n")
    if args.write_text:
        with open(args.write_text, "w", encoding="utf-8") as stream:
            stream.write(text)
    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
