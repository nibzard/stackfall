#!/usr/bin/env python3
"""Build the T065 comparison tables from one evidence set (task T065).

Walks build/agent-checks/<run-id>/ and turns the recorded samples into the
tables the report needs. Nothing here runs the game; it reads evidence, so a
table can be rebuilt after the fact from the directory alone.

    t065_summarize.py --run-dir DIR [--tag TAG]

Tables printed to stdout:

  work       work-mode microseconds per frame, grouped by scenario,
             renderer, and save mode, with spread and the fresh-minus-banked
             delta - the in-workload cost of the T052 mid-round saves
  paced      frame-time percentiles per scenario, renderer, and save mode
  sustained  the sustained session's frame, memory, and write summary
  pressure   frame percentiles and save cost per pressure kind
  audio      callback work and command traffic of the recorded runs
  logging    log lines and write counts of the logging pair

With --tag, only that bench tag's samples land in the comparison; without
it, every tag present is compared tag by tag.
"""

import argparse
import csv
import json
import os
import re
import sys


def read_text(path):
    if not os.path.exists(path):
        return ""
    with open(path, encoding="utf-8", errors="replace") as handle:
        return handle.read()


def parse_time_txt(path):
    """The /usr/bin/time -v fields the tables use."""
    values = {"cpu": "", "rss_kib": 0, "elapsed": 0.0}
    text = read_text(path)
    match = re.search(r"Elapsed \(wall clock\) time.*: (?:(\d+):)?([\d.]+)",
                      text)
    if match:
        minutes = int(match.group(1) or 0)
        values["elapsed"] = minutes * 60 + float(match.group(2))
    match = re.search(r"Percent of CPU this job got: (\d+)", text)
    if match:
        values["cpu"] = match.group(1)
    match = re.search(r"Maximum resident set size \(kbytes\): (\d+)", text)
    if match:
        values["rss_kib"] = int(match.group(1))
    return values


def monitor_totals(path):
    if not os.path.exists(path):
        return None
    with open(path, newline="", encoding="ascii") as handle:
        rows = [row for row in csv.DictReader(handle)]
    if not rows:
        return None
    last = rows[-1]
    return {"syscw": int(float(last["syscw"])),
            "write_kib": int(float(last["write_bytes"])) // 1024,
            "samples": len(rows)}


def sample_value(path, pattern):
    match = re.search(pattern, read_text(path))
    return match.group(1) if match else ""


def work_rows(bench_dir):
    """One row per work-mode sample directory."""
    rows = []
    root = os.path.join(bench_dir, "work")
    if not os.path.isdir(root):
        return rows
    for renderer in sorted(os.listdir(root)):
        for mode in sorted(os.listdir(os.path.join(root, renderer))):
            scenario_root = os.path.join(root, renderer, mode)
            for scenario in sorted(os.listdir(scenario_root)):
                for sample in sorted(os.listdir(
                        os.path.join(scenario_root, scenario))):
                    directory = os.path.join(scenario_root, scenario, sample)
                    run_log = os.path.join(directory, "run.log")
                    match = re.search(r"Loop finished: (\d+) frames rendered, "
                                      r"(\d+) ticks simulated", read_text(run_log))
                    if not match:
                        continue
                    frames = int(match.group(1))
                    ticks = int(match.group(2))
                    time_values = parse_time_txt(
                        os.path.join(directory, "time.txt"))
                    monitor = monitor_totals(
                        os.path.join(directory, "mon.csv"))
                    saves = read_text(os.path.join(
                        directory, "il.log")).count("record save=")
                    rows.append({
                        "scenario": scenario, "renderer": renderer,
                        "mode": mode, "sample": sample,
                        "us_per_frame": (time_values["elapsed"] * 1e6 /
                                         max(1, frames)) if frames else 0.0,
                        "frames": frames, "ticks": ticks,
                        "cpu": time_values["cpu"],
                        "rss_kib": time_values["rss_kib"],
                        "saves": saves,
                        "syscw": monitor["syscw"] if monitor else 0,
                        "write_kib": monitor["write_kib"] if monitor else 0,
                    })
    return rows


def paced_rows(bench_dir):
    rows = []
    root = os.path.join(bench_dir, "paced")
    if not os.path.isdir(root):
        return rows
    for renderer in sorted(os.listdir(root)):
        for mode in sorted(os.listdir(os.path.join(root, renderer))):
            scenario_root = os.path.join(root, renderer, mode)
            for scenario in sorted(os.listdir(scenario_root)):
                for sample in sorted(os.listdir(
                        os.path.join(scenario_root, scenario))):
                    directory = os.path.join(scenario_root, scenario, sample)
                    stats_path = os.path.join(directory, "frame-stats.json")
                    if not os.path.exists(stats_path):
                        continue
                    with open(stats_path, encoding="ascii") as handle:
                        stats = json.load(handle)
                    monitor = monitor_totals(
                        os.path.join(directory, "mon.csv"))
                    rows.append({
                        "scenario": scenario, "renderer": renderer,
                        "mode": mode, "sample": sample, "stats": stats,
                        "saves": read_text(os.path.join(
                            directory, "il.log")).count("record save="),
                        "write_kib": (monitor["write_kib"]
                                      if monitor else 0),
                    })
    return rows


def group(rows, keys):
    """Group rows by the named keys, keeping the sample count."""
    grouped = {}
    for row in rows:
        key = tuple(row[name] for name in keys)
        grouped.setdefault(key, []).append(row)
    return grouped


def mean(values):
    return sum(values) / len(values) if values else 0.0


def spread(values):
    return (max(values) - min(values)) if values else 0.0


def work_table(rows):
    print("== work mode: microseconds per frame (pacing off) ==")
    print("scenario\trenderer\tmode\tmean_us\tmin_us\tmax_us\tspread\t"
          "samples\tcpu\trss_kib\tsaves\tsyscw\twrite_kib")
    grouped = group(rows, ("scenario", "renderer", "mode"))
    for key, items in sorted(grouped.items()):
        values = [item["us_per_frame"] for item in items]
        print("\t".join(list(key) + [
            "%.1f" % mean(values), "%.1f" % min(values), "%.1f" % max(values),
            "%.1f" % spread(values), str(len(values)),
            items[-1]["cpu"], str(items[-1]["rss_kib"]),
            "%.1f" % mean([item["saves"] for item in items]),
            "%.1f" % mean([item["syscw"] for item in items]),
            "%.1f" % mean([item["write_kib"] for item in items])]))
    # The save-cost pair: same scenario and renderer, fresh against banked.
    print()
    print("save-cost pairs (fresh minus banked, same scenario and renderer):")
    by_pair = {}
    for key, items in grouped.items():
        by_pair.setdefault(key[:2], {})[key[2]] = items
    for key in sorted(by_pair):
        modes = by_pair[key]
        if "fresh" not in modes or "banked" not in modes:
            continue
        fresh, banked = modes["fresh"], modes["banked"]
        fresh_us = mean([item["us_per_frame"] for item in fresh])
        banked_us = mean([item["us_per_frame"] for item in banked])
        delta = fresh_us - banked_us
        saves = mean([item["saves"] for item in fresh])
        frames = mean([item["frames"] for item in fresh])
        print("  %s/%s: fresh %.1f us, banked %.1f us, delta %.1f us/frame "
              "(%.1f%% of banked)" % (key[0], key[1], fresh_us, banked_us,
                                      delta,
                                      100.0 * delta / banked_us
                                      if banked_us else 0.0))
        if saves > 0 and frames > 0:
            print("    %.1f saves per sample over %.0f frames: about %.0f us "
                  "of frame time per save" % (saves, frames,
                                              delta * frames / saves))


def paced_table(rows):
    print()
    print("== paced mode: frame-time percentiles (whole slot, wait included) ==")
    print("scenario\trenderer\tmode\tmedian_ms\tp95_ms\tp99_ms\tmax_ms\t"
          "ticks_per_second\tsaves\twrite_kib")
    for key, items in sorted(group(rows, ("scenario", "renderer", "mode")).items()):
        stats = [item["stats"] for item in items]
        print("\t".join(list(key) + [
            "%.3f" % mean([s["median_frame_ms"] for s in stats]),
            "%.3f" % mean([s["p95_frame_ms"] for s in stats]),
            "%.3f" % mean([s["p99_frame_ms"] for s in stats]),
            "%.3f" % max(s["max_frame_ms"] for s in stats),
            "%.2f" % mean([s["ticks_per_second"] for s in stats]),
            "%.1f" % mean([item["saves"] for item in items]),
            "%.1f" % mean([item["write_kib"] for item in items])]))


def subdirectories(path):
    """The sorted child directories of `path`, or an empty list."""
    if not os.path.isdir(path):
        return []
    return sorted(name for name in os.listdir(path)
                  if os.path.isdir(os.path.join(path, name)))


def sustained_table(run_dir):
    for tag in subdirectories(os.path.join(run_dir, "sustained")):
        path = os.path.join(run_dir, "sustained", tag, "summary.txt")
        if os.path.exists(path):
            print()
            print("== sustained (%s) ==" % tag)
            print(read_text(path).rstrip())


def pressure_table(run_dir):
    for tag in subdirectories(os.path.join(run_dir, "pressure")):
        path = os.path.join(run_dir, "pressure", tag, "summary.txt")
        if os.path.exists(path):
            print()
            print("== pressure (%s) ==" % tag)
            print(read_text(path).rstrip())


def portrait_table(run_dir):
    """The portrait-layout rows of the hypotheses phase (task T065)."""
    rows = []
    hypotheses_root = os.path.join(run_dir, "hypotheses")
    if not os.path.isdir(hypotheses_root):
        return
    for tag in subdirectories(hypotheses_root):
        base = os.path.join(hypotheses_root, tag, "portrait")
        if not os.path.isdir(base):
            continue
        for sample in subdirectories(base):
            stats_path = os.path.join(base, sample, "frame-stats.json")
            if not os.path.exists(stats_path):
                continue
            with open(stats_path, encoding="ascii") as handle:
                stats = json.load(handle)
            rows.append((tag, sample, stats["median_frame_ms"],
                         stats["p95_frame_ms"], stats["p99_frame_ms"],
                         stats["max_frame_ms"],
                         stats["ticks_per_second"]))
    if not rows:
        return
    print()
    print("== portrait layout (drops scenario, paced) ==")
    print("tag\tsample\tmedian_ms\tp95_ms\tp99_ms\tmax_ms\tticks_per_second")
    for row in rows:
        print("%s\t%s\t%.3f\t%.3f\t%.3f\t%.3f\t%.2f" % row)


def audio_table(run_dir):
    print()
    print("== audio callback work and command traffic ==")
    found = False
    for root, _dirs, files in os.walk(run_dir):
        if "run.log" not in files:
            continue
        text = read_text(os.path.join(root, "run.log"))
        audio = next((line.strip() for line in text.splitlines()
                      if line.startswith("Audio:")), "")
        music = next((line.strip() for line in text.splitlines()
                      if line.startswith("Music commands:")), "")
        if audio or music:
            found = True
            print("%s: %s" % (os.path.relpath(root, run_dir), audio))
            print("    %s" % music)
    if not found:
        print("(no instrumented runs recorded)")


def music_table(run_dir):
    """The soundtrack pair of the hypotheses phase (task T065)."""
    rows = []
    hypotheses_root = os.path.join(run_dir, "hypotheses")
    if not os.path.isdir(hypotheses_root):
        return
    for tag in subdirectories(hypotheses_root):
        base = os.path.join(hypotheses_root, tag, "music")
        if not os.path.isdir(base):
            continue
        for mode in ("off", "arcade"):
            for sample in subdirectories(os.path.join(base, mode)):
                stats_path = os.path.join(base, mode, sample,
                                          "frame-stats.json")
                if not os.path.exists(stats_path):
                    continue
                with open(stats_path, encoding="ascii") as handle:
                    stats = json.load(handle)
                rows.append((tag, mode, sample, stats["median_frame_ms"],
                             stats["p95_frame_ms"], stats["p99_frame_ms"],
                             stats["max_frame_ms"]))
    if not rows:
        return
    print()
    print("== soundtrack pair (drops scenario, paced) ==")
    print("tag\tmode\tsample\tmedian_ms\tp95_ms\tp99_ms\tmax_ms")
    for row in rows:
        print("%s\t%s\t%s\t%.3f\t%.3f\t%.3f\t%.3f" % row)


def logging_table(run_dir):
    rows = []
    hypotheses_root = os.path.join(run_dir, "hypotheses")
    if not os.path.isdir(hypotheses_root):
        return
    for tag in sorted(os.listdir(hypotheses_root)):
        base = os.path.join(hypotheses_root, tag, "logging")
        if not os.path.isdir(base):
            continue
        for variant in ("logfile", "devnull"):
            directory = os.path.join(base, variant)
            match = re.search(r"Loop finished: (\d+) frames rendered, (\d+) "
                              r"ticks simulated",
                              read_text(os.path.join(directory, "run.log")))
            if not match:
                continue
            values = parse_time_txt(os.path.join(directory, "time.txt"))
            session_log = os.path.join(directory, "session-log.txt")
            monitor = monitor_totals(os.path.join(directory, "mon.csv"))
            rows.append((tag, variant, values["elapsed"],
                         int(match.group(1)),
                         len(read_text(session_log).splitlines()),
                         monitor["syscw"] if monitor else 0,
                         monitor["write_kib"] if monitor else 0))
    if not rows:
        return
    print()
    print("== diagnostic logging pair ==")
    print("tag\tvariant\tseconds\tframes\tlog_lines\tsyscw\twrite_kib")
    for row in rows:
        print("%s\t%s\t%.2f\t%d\t%d\t%d\t%d" % row)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", required=True)
    parser.add_argument("--tag", default="")
    args = parser.parse_args(argv)
    run_dir = args.run_dir
    if not os.path.isdir(os.path.join(run_dir, "bench")):
        sys.stderr.write("t065_summarize: no bench directory in %s; the "
                         "bench tables are skipped\n" % run_dir)
    tags = [args.tag] if args.tag else sorted(
        os.listdir(os.path.join(run_dir, "bench")))
    for tag in tags:
        bench = os.path.join(run_dir, "bench", tag)
        if not os.path.isdir(bench):
            continue
        print("== tag %s ==" % tag)
        work_table(work_rows(bench))
        paced_table(paced_rows(bench))
    sustained_table(run_dir)
    pressure_table(run_dir)
    portrait_table(run_dir)
    audio_table(run_dir)
    music_table(run_dir)
    logging_table(run_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
