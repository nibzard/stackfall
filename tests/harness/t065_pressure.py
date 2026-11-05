#!/usr/bin/env python3
"""Bounded background pressure for the T065 measurements (task T065).

The device-feedback round needs one bounded CPU and storage-pressure case.
This worker makes that pressure and only that: it never fills memory (it
holds at most one small buffer), never fills a disk (a hard byte cap and a
time cap stop it), and writes only inside the directory it was given, which
the driver creates inside the run's own evidence tree.

    t065_pressure.py --kind cpu|storage|both --seconds N --dir DIR
                     [--max-mib M] [--cores C] [--out STATE.txt]

    cpu       `--cores` spinners (default: host cores minus two, at most
              four) each burn one core until the window closes. Every
              spinner carries its own deadline and exits by itself, so no
              pressure outlives the worker even if the parent is killed.
    storage   one writer that writes, flushes, and fsyncs 4 MiB files in a
              loop, deleting each one right after, so the directory stays
              small while the storage queue stays busy - the condition the
              mid-round record save competes with on the handheld.
    both      the two together.

The worker stops on its own at the time cap or the byte cap, whichever
comes first, removes its own files, and writes one status line. SIGTERM
and SIGINT clean up the same way, so a stopped phase leaves nothing behind.
"""

import argparse
import os
import signal
import sys
import time

STOP = False


def stop(signum, _frame):
    global STOP
    STOP = True


def spin_worker(seconds, index, ready):
    """Burn one core until `seconds` elapse, then exit by itself."""
    end = time.monotonic() + seconds
    value = 1.0
    ready.set()
    while time.monotonic() < end:
        for _ in range(20000):
            value = value * 1.0000001 + 0.1
    return value


def storage_worker(directory, deadline, max_bytes, report):
    """fsync-bound writes inside `directory`, bounded in time and bytes."""
    written = 0
    files = 0
    payload = os.urandom(4 * 1024 * 1024)
    while not STOP and time.monotonic() < deadline and written < max_bytes:
        path = os.path.join(directory, "pressure-%04d.bin" % (files % 8))
        with open(path, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
        os.remove(path)
        written += len(payload)
        files += 1
    report.append((written, files))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("cpu", "storage", "both"),
                        required=True)
    parser.add_argument("--seconds", type=float, required=True)
    parser.add_argument("--dir", required=True)
    parser.add_argument("--max-mib", type=int, default=256)
    parser.add_argument("--cores", type=int, default=0)
    parser.add_argument("--out", default=None)
    args = parser.parse_args(argv)

    deadline = time.monotonic() + args.seconds
    max_bytes = args.max_mib * 1024 * 1024
    # A signal handler that only flips a flag: the spinners inherit it, and
    # their own deadline still ends them if a signal never arrives.
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)

    children = []
    report = []
    if args.kind in ("cpu", "both"):
        import multiprocessing as mp
        cores = args.cores or max(1, min(4, (os.cpu_count() or 2) - 2))
        for index in range(cores):
            ready = mp.Event()
            child = mp.Process(target=spin_worker,
                               args=(args.seconds + 10, index, ready))
            child.daemon = True
            child.start()
            ready.wait(timeout=10)
            children.append(child)
    if args.kind in ("storage", "both"):
        import threading
        writer = threading.Thread(target=storage_worker,
                                  args=(args.dir, deadline, max_bytes, report))
        writer.daemon = True
        writer.start()

    while time.monotonic() < deadline and not STOP:
        # A storage worker that hit its byte cap leaves the wait early.
        if args.kind == "storage" and report:
            break
        time.sleep(0.5)

    for child in children:
        child.terminate()
        child.join(timeout=5)
        if child.is_alive():
            child.kill()
            child.join(timeout=5)
    if report:
        deadline = min(deadline, time.monotonic())

    written = report[0][0] if report else 0
    files = report[0][1] if report else 0
    line = ("pressure: kind=%s seconds=%.1f cpu_workers=%d "
            "storage_files=%d storage_mib=%.1f\n"
            % (args.kind, args.seconds, len(children), files,
               written / 1048576.0))
    if args.out:
        with open(args.out, "w", encoding="ascii") as handle:
            handle.write(line)
    sys.stdout.write(line)
    sys.stdout.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
