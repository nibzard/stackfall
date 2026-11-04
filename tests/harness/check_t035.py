#!/usr/bin/env python3
"""Independent checker for the T035 release-version evidence.

Usage: check_t035.py <run-directory> <repository-root>

Re-derives its expectations from the sources alone: the version constants
of include/stackfall/version.hpp, whose own comments bind them to the
project(...) VERSION of CMakeLists.txt and to the dotted string the binary
prints at startup; the state hash of tests/test_regression.cpp, which the
version string leads by design; the sanity pin of tests/test_sanity.cpp;
and the release notes and packaging metadata of portmaster/. One version
must stand in every place a player, a package browser, or a test can read
it, and a later bump must again break the pinned replay hashes instead of
passing silently. The assembled archive and its launcher banner stay with
the T019 package check; this checker proves the sources those bytes come
from. Reads the evidence directory the driver wrote plus the repository
sources; nothing is executed. Exit status is nonzero when any check fails.
"""

import re
import sys
from pathlib import Path
from xml.etree import ElementTree

CHECKS = 0
FAILURES = []

# The files whose whole version content must be the shipped value: the
# header that defines it, the build that records it, the test that pins it,
# and the notes that announce it. BUILDING.md is absent from this list on
# purpose: it names third-party component versions (GCC 11.4.0, SDL
# 2.26.2) beside the reproduction steps.
VERSION_SOURCES = (
    "include/stackfall/version.hpp",
    "CMakeLists.txt",
    "tests/test_sanity.cpp",
    "portmaster/release-notes.md",
)

# Files that ship in the archive and may not state a version that
# contradicts the shipped one.
SHIPPED_SOURCES = (
    "portmaster/port.json",
    "portmaster/gameinfo.xml",
    "portmaster/README.md",
    "portmaster/Stackfall.sh",
)


def check(condition, message):
    """Records one expectation. Prints pass or FAIL for it."""
    global CHECKS
    CHECKS += 1
    if condition:
        print(f"pass: {message}")
    else:
        FAILURES.append(message)
        print(f"FAIL: {message}")


def read(path):
    try:
        return Path(path).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def triples(text):
    return set(re.findall(r"\d+\.\d+\.\d+", text))


# ---------------------------------------------------------------------------
# The version constants and their one home.
# ---------------------------------------------------------------------------

def read_version(repo):
    text = read(repo / "include" / "stackfall" / "version.hpp")
    numbers = []
    for name in ("Major", "Minor", "Patch"):
        found = re.search(r"kVersion%s\s*=\s*(\d+)\s*;" % name, text)
        check(found is not None, f"version.hpp declares kVersion{name}")
        if found is None:
            return None
        numbers.append(int(found.group(1)))

    found = re.search(r'kVersionString\[\]\s*=\s*"([^"]*)"', text)
    check(found is not None, "version.hpp declares kVersionString")
    if found is None:
        return None
    version = found.group(1)

    check(re.fullmatch(r"\d+\.\d+\.\d+", version) is not None,
          f"kVersionString {version!r} is dotted decimal")
    composed = "%d.%d.%d" % tuple(numbers)
    check(composed == version,
          f"the three constants compose to kVersionString "
          f"({composed} against {version})")
    return version


def check_one_home(repo):
    # The value lives in the header alone; version.cpp hands out the
    # constant, so no second literal can drift away from it.
    source = read(repo / "src" / "version.cpp")
    check("return kVersionString;" in source,
          "version.cpp hands out kVersionString")
    found = triples(source)
    check(not found,
          f"version.cpp states no version literal of its own ({sorted(found)})")


def check_cmake(repo, version):
    # The header's own comment binds it to the project version.
    cmake = read(repo / "CMakeLists.txt")
    found = re.search(r"project\(\s*\S+[^)]*?\bVERSION\s+(\d+(?:\.\d+)+)",
                      cmake, re.S)
    check(found is not None, "CMakeLists.txt states the project version")
    if found is not None:
        check(found.group(1) == version,
              f"the CMake project version equals kVersionString "
              f"({found.group(1)} against {version})")


# ---------------------------------------------------------------------------
# One value in every location that names it.
# ---------------------------------------------------------------------------

def check_one_value(repo, version):
    for relative in VERSION_SOURCES:
        found = triples(read(repo / relative))
        check(found == {version},
              f"{relative} states exactly the shipped version "
              f"({sorted(found) if found else 'none'})")

    # A file that ships may carry no version at all, but never a second one.
    for relative in SHIPPED_SOURCES:
        found = triples(read(repo / relative))
        check(found <= {version},
              f"{relative} states no version that contradicts {version}"
              + (f" (found {sorted(found)})" if found - {version} else ""))


def check_sanity_pin(repo, version):
    text = read(repo / "tests" / "test_sanity.cpp")
    check(f'std::strcmp(version, "{version}") == 0' in text,
          "test_sanity.cpp pins the version string")


def check_state_hash_pins(repo):
    # The version string leads the state hash by design, so the bump that
    # landed inside the T019 package had to re-pin five replay hashes. The
    # fold must stay, and the desktop gate must hold the same pin set as
    # the suite it double-checks.
    regression = read(repo / "tests" / "test_regression.cpp")
    check("for (const char* c = stackfall::kVersionString;" in regression,
          "the state hash folds kVersionString, so a bump breaks the pins")

    suite = {}
    for found in re.finditer(
            r'\{"([a-z0-9-]+)",.*?(0x[0-9a-f]+)ULL,\s*(-?\d+),\s*(-?\d+)\s*\}',
            regression, re.S):
        suite[found.group(1)] = (found.group(2), int(found.group(3)),
                                 int(found.group(4)))
    check(len(suite) >= 5,
          f"test_regression.cpp pins {len(suite)} replay scenarios")

    gate = read(repo / "tests" / "harness" / "check_t015.py")
    second = {}
    for found in re.finditer(
            r'"([a-z0-9-]+)":\s*\("(0x[0-9a-f]+)",\s*(-?\d+),\s*(-?\d+)\)',
            gate):
        second[found.group(1)] = (found.group(2), int(found.group(3)),
                                  int(found.group(4)))
    check(bool(second), "check_t015.py holds the second pin set")
    check(second == suite,
          "the t015 gate pins the same hash values as the rules suite")


def check_package_metadata(repo, version):
    notes = read(repo / "portmaster" / "release-notes.md")
    headline = re.search(r"^## " + re.escape(version) +
                         r" \((\d{4}-\d{2}-\d{2})\)$", notes, re.M)
    check(headline is not None,
          f"the release notes headline version {version} with its date")

    game = None
    try:
        root = ElementTree.parse(repo / "portmaster" / "gameinfo.xml")
        entries = root.getroot().findall("./game")
        if len(entries) == 1:
            game = entries[0]
    except ElementTree.ParseError:
        pass
    check(game is not None, "gameinfo.xml holds one game entry")
    if headline is not None and game is not None:
        node = game.find("releasedate")
        # The ES form of gameinfo.xml is YYYYMMDDTHHMMSS; the day the notes
        # date names is the part that must agree.
        date = (node.text or "").strip() if node is not None else ""
        check(date.split("T")[0] == headline.group(1).replace("-", ""),
              f"gameinfo.xml releasedate {date!r} equals the notes date")


# ---------------------------------------------------------------------------
# The banner run of the real binary.
# ---------------------------------------------------------------------------

def check_banner_run(run_dir, version):
    status = read(run_dir / "exit.txt").strip()
    check(status == "0", f"the banner run exited 0 (got {status!r})")

    out = read(run_dir / "run.log")
    check("Clean shutdown" in out, "the banner run shut down cleanly")
    banner = f"STACKFALL {version}"
    check(re.search(r"^" + banner + r"$", out, re.M) is not None,
          f"the startup banner on stdout reports {banner}")

    session = read(run_dir / "session" / "log.txt")
    check(re.search(r"^" + banner + r"$", session, re.M) is not None,
          "the session log.txt beside the record reports the same banner")

    suites = read(run_dir / "ctest.log")
    check("100% tests passed" in suites and "0 tests failed" in suites,
          "the headless suites pass at this version (the sanity pin and "
          "the re-pinned replay hashes)")


# ---------------------------------------------------------------------------
# Entry point.
# ---------------------------------------------------------------------------

def main(argv):
    if len(argv) != 3:
        print("usage: check_t035.py <run-directory> <repository-root>",
              file=sys.stderr)
        return 2
    run_dir = Path(argv[1])
    repo = Path(argv[2])

    print("== version constants")
    version = read_version(repo)
    print("== one home for the value")
    check_one_home(repo)
    if version is not None:
        print("== CMake project version")
        check_cmake(repo, version)
        print("== every location that names it")
        check_one_value(repo, version)
        check_sanity_pin(repo, version)
        print("== state-hash pins")
        check_state_hash_pins(repo)
        print("== package metadata")
        check_package_metadata(repo, version)
        print("== banner run")
        check_banner_run(run_dir, version)

    print()
    if FAILURES:
        print(f"FAILED {len(FAILURES)} of {CHECKS} checks")
        for failure in FAILURES:
            print(f"  - {failure}")
        return 1
    print(f"passed all {CHECKS} checks")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
