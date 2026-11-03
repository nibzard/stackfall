#!/usr/bin/env python3
"""Bounded mutation fuzz pass for the T025 operator extension.

Two parsers get fuzzed, each with the strongest oracle available:

1. The save-file and settings parsers (src/storage.cpp) run in-process: a
   small driver compiled from RUNDIR/fuzz_save_driver.cpp links the real
   src/storage.cpp under AddressSanitizer and UndefinedBehaviorSanitizer,
   then mutates the two canonical files for a bounded number of seconds.
   Every accepted parse must round-trip through the canonical formatter.

2. The input-script parser and the save loader run through the real game
   binary (built with the same sanitizers): each case writes one mutated
   script - or one mutated pair of save files - runs a bounded dummy-driver
   session, and accepts only a clean exit. Exit 1 is the parser's documented
   rejection of a bad script; exit 0 means the session ran. A signal exit
   code (>= 128), a timeout, or a sanitizer report in the output is a
   finding, and the case is kept in RUNDIR/crashes/.

Usage:
  t025_fuzz.py GAME REPO RUNDIR [--budget-seconds N] [--binary-cases N]

GAME is the sanitizer-built stackfall binary, REPO the repository root, and
RUNDIR the evidence directory (fuzz/ inside the T025 run directory).
"""

import glob
import json
import os
import random
import subprocess
import sys
import time

SAVE_DRIVER = r"""// Bounded mutation loop over the two save parsers (T025 fuzz pass).
// Links the real src/storage.cpp; any crash escapes to the sanitizer.
#include "stackfall/storage.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

int main(int argc, char** argv) {
  const int seconds = argc > 1 ? std::atoi(argv[1]) : 60;
  const unsigned seed = argc > 2 ? static_cast<unsigned>(std::atoi(argv[2]))
                                 : 0x5eed25u;
  std::mt19937 rng(seed);
  const std::string bases[] = {
      stackfall::formatSaveFile(0),
      stackfall::formatSaveFile(218),
      stackfall::formatSaveFile(18446744073709551615ULL),
      stackfall::formatSettingsFile(true),
      stackfall::formatSettingsFile(false),
  };
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  unsigned long long cases = 0, accepted = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    std::string bytes = bases[rng() % (sizeof(bases) / sizeof(bases[0]))];
    const int mutations = 1 + static_cast<int>(rng() % 4);
    for (int m = 0; m < mutations; ++m) {
      if (bytes.empty()) {
        bytes.push_back(static_cast<char>(rng() % 256));
        continue;
      }
      switch (rng() % 6) {
        case 0:  // flip one bit of one byte
          bytes[rng() % bytes.size()] ^= static_cast<char>(1u << (rng() % 8));
          break;
        case 1:  // replace one byte with a random byte
          bytes[rng() % bytes.size()] = static_cast<char>(rng() % 256);
          break;
        case 2:  // insert a random byte
          bytes.insert(rng() % bytes.size(), 1, static_cast<char>(rng() % 256));
          break;
        case 3:  // delete one byte
          bytes.erase(rng() % bytes.size(), 1);
          break;
        case 4:  // truncate
          bytes.resize(rng() % (bytes.size() + 1));
          break;
        case 5: {  // append a short random blob
          std::string blob(rng() % 16, ' ');
          for (char& c : blob) {
            c = static_cast<char>(rng() % 256);
          }
          bytes += blob;
          break;
        }
      }
    }
    ++cases;
    std::uint64_t high = 12345;
    if (stackfall::parseSaveFile(bytes, &high)) {
      ++accepted;
      // The parser promises exact canonical form: an accepted record must
      // equal the canonical rendering of the value it carried.
      if (bytes != stackfall::formatSaveFile(high)) {
        std::fprintf(stderr, "FUZZ ORACLE: accepted non-canonical save %llu\n",
                     static_cast<unsigned long long>(high));
        return 2;
      }
    }
    bool sound = false;
    if (stackfall::parseSettingsFile(bytes, &sound)) {
      ++accepted;
      if (bytes != stackfall::formatSettingsFile(sound)) {
        std::fprintf(stderr, "FUZZ ORACLE: accepted non-canonical settings\n");
        return 2;
      }
    }
  }
  std::printf("fuzz loop: %llu cases, %llu accepted parses\n", cases,
              accepted);
  return 0;
}
"""

INTERESTING_TOKENS = [
    "", "0", "1", "-1", "2147483647", "2147483648", "4294967295",
    "4294967296", "4294967297", "6442450941", "9223372036854775807",
    "18446744073709551615", "99999999999999999999999999", "0x10", "1e3",
    "007", "+5", "1.5", "down", "up", "left", "space", "nonexistent",
    "\xff\xfe", "a" * 300, "quote'\"\\", "tick\ttab",
]


def mutate_script(rng, lines):
    """One grammar-aware mutation of a script's lines."""
    lines = list(lines)
    for _ in range(1 + rng.randrange(3)):
        if not lines:
            lines.append("key enter down")
            continue
        kind = rng.randrange(7)
        index = rng.randrange(len(lines))
        if kind == 0:  # delete a line
            del lines[index]
        elif kind == 1:  # duplicate a line
            lines.insert(index, lines[index])
        elif kind == 2:  # shuffle two lines
            other = rng.randrange(len(lines))
            lines[index], lines[other] = lines[other], lines[index]
        elif kind == 3:  # splice two lines
            other = rng.randrange(len(lines))
            half = len(lines[index]) // 2
            lines[index] = lines[index][:half] + lines[other][half:]
        elif kind == 4:  # replace one word with an interesting token
            words = lines[index].split(" ")
            words[rng.randrange(len(words))] = rng.choice(INTERESTING_TOKENS)
            lines[index] = " ".join(words)
        elif kind == 5:  # replace the whole line
            verb = rng.choice(["key", "pad", "wait", "stall", "fixture",
                               "check", "shot", "echo", "restart", "detach",
                               "quit", "garbage", "KEY", "Wait"])
            lines[index] = "%s %s" % (verb, rng.choice(INTERESTING_TOKENS))
        else:  # byte-level damage inside one line
            line = lines[index]
            if line:
                pos = rng.randrange(len(line))
                lines[index] = line[:pos] + chr(rng.randrange(32, 127)) + \
                    line[pos + 1:]
    return lines


def run_case(game, case_dir, script=None, saves=None):
    """Run one fuzz case through the real binary. Returns
    (status, detail): 'clean', 'rejected', or 'FINDING: ...'."""
    env = dict(os.environ)
    env["SDL_VIDEODRIVER"] = "dummy"
    env["SDL_AUDIODRIVER"] = "dummy"
    env["ASAN_OPTIONS"] = "detect_leaks=0"
    command = [game, "--seed", "1", "--frames", "40",
               "--save-dir", os.path.join(case_dir, "save")]
    if script is not None:
        with open(os.path.join(case_dir, "case.script.txt"), "w",
                  encoding="utf-8", errors="surrogateescape") as stream:
            stream.write(script)
        command += ["--input-script", os.path.join(case_dir, "case.script.txt")]
    if saves is not None:
        os.makedirs(os.path.join(case_dir, "save"), exist_ok=True)
        for name, blob in saves.items():
            with open(os.path.join(case_dir, "save", name), "wb") as stream:
                stream.write(blob)
    try:
        proc = subprocess.run(command, env=env, cwd=case_dir,
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=8)
    except subprocess.TimeoutExpired:
        return ("FINDING: timeout (process hung)", "timeout")
    output = proc.stdout.decode("utf-8", "replace")
    if "Sanitizer" in output or "runtime error:" in output:
        return ("FINDING: sanitizer report", output[-4000:])
    if proc.returncode >= 128:
        return ("FINDING: signal exit %d" % proc.returncode, output[-4000:])
    if proc.returncode == 0:
        return ("clean", output[-2000:])
    if proc.returncode == 1:
        return ("rejected", output[-2000:])
    return ("FINDING: exit %d" % proc.returncode, output[-4000:])


def main():
    if len(sys.argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    game, repo, run_dir = sys.argv[1], sys.argv[2], sys.argv[3]
    budget = 180
    binary_cases = 500
    args = sys.argv[4:]
    for i, arg in enumerate(args):
        if arg == "--budget-seconds" and i + 1 < len(args):
            budget = int(args[i + 1])
        elif arg == "--binary-cases" and i + 1 < len(args):
            binary_cases = int(args[i + 1])

    os.makedirs(run_dir, exist_ok=True)
    crashes_dir = os.path.join(run_dir, "crashes")
    os.makedirs(crashes_dir, exist_ok=True)
    report = {"parser_loop": {}, "binary_cases": 0, "findings": []}
    rng = random.Random(0x72525)
    pass_started = time.time()

    # Part 1: the in-process parser loop under ASan/UBSan.
    driver_src = os.path.join(run_dir, "fuzz_save_driver.cpp")
    with open(driver_src, "w", encoding="ascii") as stream:
        stream.write(SAVE_DRIVER)
    driver = os.path.join(run_dir, "fuzz_save_driver")
    build = subprocess.run(
        ["g++", "-std=c++17", "-g", "-O1",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         "-I", os.path.join(repo, "include"), driver_src,
         os.path.join(repo, "src", "storage.cpp"), "-o", driver],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with open(os.path.join(run_dir, "driver-build.log"), "wb") as stream:
        stream.write(build.stdout)
    if build.returncode != 0:
        report["parser_loop"] = {"status": "unavailable",
                                 "reason": "driver build failed"}
        print("fuzz: parser-loop driver failed to build (see "
              "driver-build.log); recording unavailable")
    else:
        loop_seconds = max(20, budget // 3)
        started = time.time()
        proc = subprocess.run([driver, str(loop_seconds), "15965"],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=loop_seconds + 60)
        with open(os.path.join(run_dir, "parser-loop.log"), "wb") as stream:
            stream.write(proc.stdout)
        report["parser_loop"] = {
            "status": "passed" if proc.returncode == 0 else "failed",
            "exit": proc.returncode,
            "seconds": round(time.time() - started, 1),
        }
        print("fuzz: parser loop exit %d in %.1f s"
              % (proc.returncode, time.time() - started))
        if proc.returncode != 0:
            report["findings"].append({
                "case": "parser-loop",
                "status": "exit %d" % proc.returncode,
                "detail": proc.stdout.decode("utf-8", "replace")[-4000:],
            })

    # Part 2: through the real sanitized binary.
    corpus = sorted(glob.glob(os.path.join(repo, "tests", "harness",
                                           "scripts", "*.script.txt")))
    if not corpus:
        print("fuzz: no script corpus found")
        return 1
    canonical_save = b"version=1\nhigh_score=218\n"
    canonical_settings = b"version=1\nsound=on\n"
    counts = {"clean": 0, "rejected": 0}
    started = time.time()
    case_index = 0
    # The whole pass shares one budget: the binary phase gets whatever the
    # parser loop left, with a floor so it always runs.
    remaining = budget - (time.time() - pass_started)
    deadline = time.time() + max(30, remaining)
    while case_index < binary_cases and time.time() < deadline:
        case_dir = os.path.join(run_dir, "case-%04d" % case_index)
        os.makedirs(case_dir, exist_ok=True)
        if case_index % 4 == 3:
            # A mutated save pair through the load path.
            def damage(blob):
                blob = bytearray(blob)
                for _ in range(1 + rng.randrange(3)):
                    if not blob:
                        break
                    choice = rng.randrange(4)
                    if choice == 0:
                        blob[rng.randrange(len(blob))] = rng.randrange(256)
                    elif choice == 1:
                        del blob[rng.randrange(len(blob))]
                    elif choice == 2:
                        blob = blob[:rng.randrange(len(blob) + 1)]
                    else:
                        blob.extend(bytes(rng.randrange(8)))
                return bytes(blob)
            saves = {"highscore.dat": damage(canonical_save),
                     "settings.dat": damage(canonical_settings)}
            status, detail = run_case(game, case_dir, saves=saves)
        else:
            source = rng.choice(corpus)
            with open(source, "r", encoding="utf-8", errors="replace") as fp:
                lines = fp.read().splitlines()
            mutated = mutate_script(rng, lines)
            status, detail = run_case(game, case_dir,
                                      script="\n".join(mutated) + "\n")
        case_index += 1
        if status.startswith("FINDING"):
            name = "finding-%04d" % case_index
            keep = os.path.join(crashes_dir, name)
            os.rename(case_dir, keep)
            report["findings"].append({"case": name, "status": status,
                                       "detail": detail})
            print("fuzz: %s in %s" % (status, keep))
        else:
            counts[status] = counts.get(status, 0) + 1
            import shutil
            shutil.rmtree(case_dir, ignore_errors=True)
    report["binary_cases"] = case_index
    report["binary_counts"] = counts
    report["binary_seconds"] = round(time.time() - started, 1)
    print("fuzz: %d binary cases in %.1f s: %s"
          % (case_index, report["binary_seconds"], counts))

    with open(os.path.join(run_dir, "fuzz-report.json"), "w",
              encoding="ascii") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    return 1 if report["findings"] else 0


if __name__ == "__main__":
    sys.exit(main())
