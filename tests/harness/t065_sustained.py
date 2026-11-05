#!/usr/bin/env python3
"""Summarize one T065 sustained session (task T065).

Reads a sustained sample directory - frame statistics, the /proc monitor
samples, the per-tick input log, and the session's own save directory - and
writes the resource-growth evidence: resident memory over time, write counts
and bytes, record saves, log lines, and what the audio thread did.

    t065_sustained.py SAMPLE_DIR
"""

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


def main(argv):
    if len(argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    directory = argv[1]
    stats = json.load(open(os.path.join(directory, "frame-stats.json")))

    rows = []
    monitor_path = os.path.join(directory, "mon.csv")
    if os.path.exists(monitor_path):
        with open(monitor_path, newline="", encoding="ascii") as handle:
            rows = [row for row in csv.DictReader(handle)]
    # Growth reads from the first sample after the first second, so the
    # process's own bring-up (shared libraries, the catalogue build) is not
    # counted as growth the session caused.
    warm = [row for row in rows if float(row["elapsed_s"]) >= 1.0]
    first_rss = int(float(warm[0]["rss_kib"])) if warm else 0
    last_rss = int(float(rows[-1]["rss_kib"])) if rows else 0
    peak_rss = max(int(float(row["rss_kib"])) for row in rows) if rows else 0
    last_write = int(float(rows[-1]["write_bytes"])) if rows else 0
    last_syscw = int(float(rows[-1]["syscw"])) if rows else 0

    input_log = read_text(os.path.join(directory, "il.log"))
    run_log = read_text(os.path.join(directory, "run.log"))
    session_log = read_text(os.path.join(directory, "save", "log.txt"))
    audio_line = next((line.strip() for line in run_log.splitlines()
                       if line.startswith("Audio:")), "Audio: (none)")
    music_line = next((line.strip() for line in run_log.splitlines()
                       if line.startswith("Music commands:")),
                      "Music commands: (none)")

    print("sustained session: %.1f s, %d frames, %.2f ticks/s"
          % (stats["elapsed_seconds"], stats["frames_rendered"],
             stats["ticks_per_second"]))
    print("frame times ms: median %.3f p95 %.3f p99 %.3f max %.3f"
          % (stats["median_frame_ms"], stats["p95_frame_ms"],
             stats["p99_frame_ms"], stats["max_frame_ms"]))
    print("resident KiB: first %d, last %d, peak %d (growth %+d KiB over "
          "%.0f s)" % (first_rss, last_rss, peak_rss, last_rss - first_rss,
                       stats["elapsed_seconds"]))
    print("writes: %d syscw, %d KiB written; record saves %d; session log "
          "lines %d" % (last_syscw, last_write // 1024,
                        input_log.count("record save="),
                        len(session_log.splitlines())))
    print("audio: %s" % audio_line)
    print("music: %s" % music_line)
    restarts = run_log.count("script: restart round")
    print("rounds: %d restarts inside the session" % restarts)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
