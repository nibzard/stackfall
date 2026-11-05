#!/usr/bin/env python3
"""External process monitor for T065 (task T065, docs/PERFORMANCE.md).

Runs a command and samples the kernel's own accounting for it from outside
the process: `/proc/<pid>/status` for resident memory and `/proc/<pid>/io`
for write counts and bytes. Nothing is compiled into the game, so a sample
carries no instrumentation overhead of its own.

    t065_monitor.py --out SAMPLES.csv -- COMMAND [ARG ...]

The CSV holds one row per sample: elapsed seconds, VmRSS in KiB, the read
and write syscall counts (syscr, syscw), and the read and write byte counts
the kernel charged to the process. The exit status of the command passes
through, and one summary line lands on stdout:

    monitor: exit=0 samples=301 rss_first_kib=... rss_last_kib=...
    rss_max_kib=... writes=... write_bytes_kib=...

`--interval-ms` (default 250) bounds the sampling rate. Sampling stops when
the child exits, so a terminated run keeps the rows it collected.
"""

import argparse
import csv
import subprocess
import sys
import time


def read_status(pid):
    """VmRSS in KiB from /proc/<pid>/status, or None when it is gone."""
    try:
        with open("/proc/%d/status" % pid, encoding="ascii") as handle:
            for line in handle:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        return None
    return None


def read_io(pid):
    """The io counters of /proc/<pid>/io, or None when it is gone."""
    keys = ("rchar", "wchar", "syscr", "syscw", "read_bytes", "write_bytes")
    values = {}
    try:
        with open("/proc/%d/io" % pid, encoding="ascii") as handle:
            for line in handle:
                name, _, value = line.partition(":")
                if name in keys:
                    values[name] = int(value.strip())
    except OSError:
        return None
    if len(values) != len(keys):
        return None
    return values


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Sample /proc accounting for one child command.")
    parser.add_argument("--out", required=True, help="CSV output path")
    parser.add_argument("--interval-ms", type=int, default=250)
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="-- COMMAND [ARG ...]")
    args = parser.parse_args(argv)
    if not args.command or args.command[0] != "--":
        parser.error("pass the command after --")
    command = args.command[1:]
    if not command:
        parser.error("empty command")

    start = time.monotonic()
    child = subprocess.Popen(command)
    rows = []
    while True:
        code = child.poll()
        rss = read_status(child.pid)
        io = read_io(child.pid)
        if rss is not None and io is not None:
            rows.append((time.monotonic() - start, rss, io["syscr"],
                         io["syscw"], io["read_bytes"], io["write_bytes"]))
        if code is not None:
            break
        time.sleep(max(0.01, args.interval_ms / 1000.0))
    child.wait()

    with open(args.out, "w", newline="", encoding="ascii") as handle:
        writer = csv.writer(handle)
        writer.writerow(["elapsed_s", "rss_kib", "syscr", "syscw",
                         "read_bytes", "write_bytes"])
        for row in rows:
            writer.writerow(["%.3f" % row[0]] + list(row[1:]))
    if rows:
        first, last = rows[0], rows[-1]
        peak = max(row[1] for row in rows)
        print("monitor: exit=%d samples=%d rss_first_kib=%d rss_last_kib=%d "
              "rss_max_kib=%d syscw=%d write_bytes_kib=%d"
              % (child.returncode, len(rows), first[1], last[1], peak,
                 last[3], last[5] // 1024))
    else:
        print("monitor: exit=%d samples=0 (process too short to sample)"
              % child.returncode)
    return child.returncode


if __name__ == "__main__":
    sys.exit(main())
