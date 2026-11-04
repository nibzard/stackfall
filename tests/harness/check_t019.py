#!/usr/bin/env python3
"""Independent checker for the T019 PortMaster release-package evidence.

Usage: check_t019.py <run-directory> <repository-root>

Re-derives its expectations from three sources alone: the PortMaster
packaging guide (https://portmaster.games/packaging.html and the
build_release.py of https://github.com/PortsMaster/PortMaster-New,
reviewed 2026-11-04), docs/SPEC.md sections 10.2 to 10.4, and the task
contract of T019. A package that satisfies its own assembly but departs
from any of these fails here.

Reads the evidence directory the driver wrote plus the repository sources;
nothing is executed. Exit status is nonzero when any check fails.
"""

import hashlib
import json
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path
from xml.etree import ElementTree

CHECKS = 0
FAILURES = []

# The genres the PortMaster build system accepts (its tools/libs/util.py,
# reviewed 2026-11-04).
PORT_GENRES = {
    "action", "adventure", "arcade", "casino/card", "fps", "platformer",
    "puzzle", "racing", "rhythm", "rpg", "simulation", "sports", "strategy",
    "visual novel", "other",
}

# Every entry the release archive must carry (docs/SPEC.md section 10.4,
# the task contract, and the structure build_release.py writes).
REQUIRED_ENTRIES = {
    "Stackfall.sh",
    "stackfall/port.json",
    "stackfall/gameinfo.xml",
    "stackfall/screenshot.png",
    "stackfall/stackfall.md",
    "stackfall/stackfall.aarch64",
    "stackfall/release-notes.md",
    "stackfall/BUILDING.md",
    "stackfall/licenses/LICENSE",
    "stackfall/licenses/LICENSE.SDL2.txt",
}


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


def sha256_file(path):
    try:
        return hashlib.sha256(Path(path).read_bytes()).hexdigest()
    except OSError:
        return ""


# ---------------------------------------------------------------------------
# The metadata of the port.
# ---------------------------------------------------------------------------

def check_port_json(repo):
    raw = read(repo / "portmaster" / "port.json")
    data = None
    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        pass
    check(data is not None, "port.json is valid JSON")
    if data is None:
        return None

    check(data.get("version") == 4, "port.json declares format version 4")
    check(data.get("name") == "stackfall.zip",
          "port.json names the archive stackfall.zip")
    check(data.get("items") == ["Stackfall.sh", "stackfall"],
          "port.json items are the launch script and the game folder")
    check(data.get("items_opt") == [], "port.json ships no optional items")

    attr = data.get("attr")
    check(isinstance(attr, dict), "port.json carries an attr object")
    if not isinstance(attr, dict):
        return data

    check(attr.get("title") == "STACKFALL",
          "the port title is STACKFALL, the project's own name")
    check(isinstance(attr.get("porter"), list) and attr["porter"],
          "port.json credits at least one porter")
    desc = attr.get("desc") or ""
    check(len(desc) >= 40, "the port description says what the game is")
    check(bool(attr.get("inst")), "port.json states the install steps")
    genres = attr.get("genres")
    check(isinstance(genres, list) and genres and
          all(g in PORT_GENRES for g in genres),
          f"genres {genres} come from the PortMaster genre list")
    check(attr.get("rtr") is True, "the port is ready to run")
    check(attr.get("runtime") == [], "the port needs no runtime")
    check(attr.get("arch") == ["aarch64"],
          "the port declares the aarch64 architecture")
    glibc = attr.get("min_glibc")
    check(isinstance(glibc, str) and re.fullmatch(r"\d+\.\d+", glibc or ""),
          f"min_glibc {glibc!r} is a version string")
    return data


def check_gameinfo(repo, port_json):
    path = repo / "portmaster" / "gameinfo.xml"
    game = None
    try:
        root = ElementTree.parse(path).getroot()
        games = root.findall("./game")
        if len(games) == 1:
            game = games[0]
    except ElementTree.ParseError:
        pass
    check(game is not None, "gameinfo.xml holds one game entry")
    if game is None or port_json is None:
        return

    def field(name):
        node = game.find(name)
        return (node.text or "").strip() if node is not None else ""

    check(field("path") == "./Stackfall.sh",
          "gameinfo.xml points the Ports menu at the launcher")
    check(field("name") == "STACKFALL", "gameinfo.xml names the game STACKFALL")
    check(field("name") == port_json["attr"]["title"],
          "gameinfo.xml and port.json carry the same title")
    check(field("desc") == port_json["attr"]["desc"],
          "gameinfo.xml and port.json carry the same description")
    check(re.fullmatch(r"\d{8}T\d{6}", field("releasedate")) is not None,
          f"releasedate {field('releasedate')!r} has the ES form")
    check(bool(field("developer")) and bool(field("publisher")),
          "gameinfo.xml credits a developer and a publisher")
    check(bool(field("genre")), "gameinfo.xml states a genre")
    check(field("image") == "./stackfall/screenshot.png",
          "gameinfo.xml shows the archived screenshot")


# ---------------------------------------------------------------------------
# The archive and its unpacked tree.
# ---------------------------------------------------------------------------

def check_archive(run_dir, repo):
    zip_path = Path(read_first(
        [run_dir / "package.log"], r"archive  : (\S+stackfall\.zip)"))
    if zip_path is None:
        # The default location of tools/portmaster/package.sh.
        zip_path = repo / "build" / "portmaster-package" / "stackfall.zip"
    check(zip_path.is_file(), f"the archive exists at {zip_path}")
    if not zip_path.is_file():
        return None

    with zipfile.ZipFile(zip_path) as zf:
        names = set(zf.namelist())
        check(zf.testzip() is None, "every archive entry passes its CRC")
        check(names == REQUIRED_ENTRIES,
              "the archive holds exactly the required items")
        missing = REQUIRED_ENTRIES - names
        extra = names - REQUIRED_ENTRIES
        if missing or extra:
            print(f"       missing={sorted(missing)} extra={sorted(extra)}")
        launcher = zf.read("Stackfall.sh").decode("utf-8", "replace")
        check(launcher == read(repo / "portmaster" / "Stackfall.sh"),
              "the archived launcher is portmaster/Stackfall.sh")
        readme = zf.read("stackfall/stackfall.md").decode("utf-8", "replace")
        check(readme == read(repo / "portmaster" / "README.md"),
              "the archived readme is portmaster/README.md")
        for name, source in (
                ("stackfall/port.json", "port.json"),
                ("stackfall/gameinfo.xml", "gameinfo.xml"),
                ("stackfall/release-notes.md", "release-notes.md"),
                ("stackfall/BUILDING.md", "BUILDING.md"),
                ("stackfall/licenses/LICENSE", "licenses/LICENSE"),
                ("stackfall/licenses/LICENSE.SDL2.txt",
                 "licenses/LICENSE.SDL2.txt")):
            repo_file = repo / "portmaster" / source
            matches = repo_file.is_file() and \
                zf.read(name) == repo_file.read_bytes()
            check(matches, f"the archived {name} matches the repository source")
        return zf


def read_first(paths, pattern):
    for path in paths:
        match = re.search(pattern, read(path))
        if match:
            return Path(match.group(1))
    return None


def check_unpacked_tree(run_dir):
    tree = [line for line in read(run_dir / "unpack-tree.txt").splitlines()
            if line.strip()]
    names = {line.split("ports/", 1)[1] for line in tree if "/ports/" in line}
    # The section 10.3 install layout the archive unpacks into, plus the
    # package items of section 10.4 that live beside the binary.
    expected = {
        "Stackfall.sh",
        "stackfall",
        "stackfall/stackfall.aarch64",
        "stackfall/port.json",
        "stackfall/gameinfo.xml",
        "stackfall/screenshot.png",
        "stackfall/stackfall.md",
        "stackfall/release-notes.md",
        "stackfall/BUILDING.md",
        "stackfall/licenses",
        "stackfall/licenses/LICENSE",
        "stackfall/licenses/LICENSE.SDL2.txt",
    }
    check(expected <= names,
          "unpacking reproduces the /roms/ports install layout of section 10.3")
    if not expected <= names:
        print(f"       missing={sorted(expected - names)}")
    check(not any("conf" in name or "log.txt" in name for name in names),
          "the archive ships no runtime conf/ or log.txt")


def check_binary(run_dir, repo):
    # The version the binary prints must be the version the package
    # announces: version.hpp feeds the startup banner and the release notes.
    version = re.search(r'kVersionString\[\] = "([^"]+)"',
                        read(repo / "include" / "stackfall" / "version.hpp"))
    check(version is not None, "version.hpp states the version string")
    if version is not None:
        notes = read(repo / "portmaster" / "release-notes.md")
        check(version.group(1) in notes,
              f"the release notes announce version {version.group(1)}")

    report = read(run_dir / "arm64-file.txt")
    check("ARM aarch64" in report,
          "the shipped executable is an ARM aarch64 binary")
    check("x86-64" not in report and "80386" not in report,
          "the archive ships no x86 executable")

    unpacked = run_dir / "ports" / "stackfall" / "stackfall.aarch64"
    built_hash = read(run_dir / "arm64-sha256.txt").split()[0] \
        if read(run_dir / "arm64-sha256.txt").split() else ""
    check(sha256_file(unpacked) == built_hash,
          "the archived binary is the one the ARM64 build produced")

    # The build instructions that ship must describe the binary that ships.
    building = read(repo / "portmaster" / "BUILDING.md")
    check(built_hash in building,
          "BUILDING.md names the SHA-256 of the shipped binary")
    check("libSDL2-2.0.so.0" in building,
          "BUILDING.md documents the SDL2 runtime dependency")
    check("GLIBC_2.34" in building or "min_glibc" in building,
          "BUILDING.md documents the glibc requirement")
    check("tools/arm64/build-arm64.sh" in building,
          "BUILDING.md gives the one-command reproduction")


def check_repro(run_dir):
    if (run_dir / "repro-skipped.txt").is_file():
        print("note: the repro phase was skipped by T019_SKIP_REPRO=1")
        return
    log = read(run_dir / "repro.log")
    match = re.search(r"^([0-9a-f]{64})\s+\S*stackfall$", log, re.M)
    check(match is not None, "the clean rebuild printed a SHA-256")
    built = read(run_dir / "arm64-sha256.txt").split()
    check(match is not None and bool(built) and match.group(1) == built[0],
          "a second build in a clean container reproduced the binary bytes")


# ---------------------------------------------------------------------------
# The screenshot: decoded and measured with no image library.
# ---------------------------------------------------------------------------

def decode_png(path):
    """One PNG as (width, height, pixel(x, y)), or None."""
    try:
        data = Path(path).read_bytes()
    except OSError:
        return None
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    pos = 8
    idat = b""
    header = None
    while pos + 12 <= len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length
    if header is None or not idat:
        return None
    width, height, depth, color, _, _, _ = header
    if depth != 8 or color != 2:
        return None
    raw = zlib.decompress(idat)
    stride = width * 3
    if len(raw) < height * (stride + 1):
        return None

    rows = []
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            up = previous[x]
            up_left = previous[x - 3] if x >= 3 else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + up) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                paeth = left + up - up_left
                pa, pb, pc = abs(paeth - left), abs(paeth - up), \
                    abs(paeth - up_left)
                predictor = left if pa <= pb and pa <= pc else \
                    up if pb <= pc else up_left
                row[x] = (row[x] + predictor) & 0xFF
        rows.append(row)
        previous = row

    def pixel(x, y):
        row = rows[y]
        return row[x * 3], row[x * 3 + 1], row[x * 3 + 2]

    return width, height, pixel


def check_screenshot(run_dir):
    shot = run_dir / "ports" / "stackfall" / "screenshot.png"
    decoded = decode_png(shot)
    check(decoded is not None, "screenshot.png is an 8-bit truecolor PNG")
    if decoded is None:
        return
    width, height, pixel = decoded
    check((width, height) == (640, 480),
          f"the screenshot is 640x480 (got {width}x{height})")
    check(width * 3 == height * 4, "the screenshot has a 4:3 aspect ratio")

    # The letterbox: the canvas scales into the window and centers, so the
    # black bars must sit above and below the scene, never beside it.
    def row_is_black(y):
        return all(pixel(x, y) == (0, 0, 0) for x in range(0, width, 8))

    top = 0
    while top < height and row_is_black(top):
        top += 1
    bottom = 0
    while bottom < height - top and row_is_black(height - 1 - bottom):
        bottom += 1
    check(top >= 40 and bottom >= 40,
          f"letterbox bars frame the scene (top {top}, bottom {bottom})")
    check(abs(top - bottom) <= 2,
          "the letterbox is centered, as the game presents it")
    mid = pixel(width // 2, top + (height - top - bottom) // 2)
    check(mid != (0, 0, 0),
          "the center of the scene is drawn, not letterbox black")

    # The board of the 27 px geometry (canvas x 345..614) scaled by 2/3 and
    # offset by the top bar, plus the two HUD columns beside it.
    def canvas_to_shot(cx, cy):
        return round(cx * 640 / 960), top + round(cy * 640 / 960)

    bx0, by0 = canvas_to_shot(345, 2)
    bx1, by1 = canvas_to_shot(614, 541)
    piece_colors = set()
    stack_rows = 0
    flight_rows = 0
    for y in range(by0 + 2, by1 - 2):
        row_has_piece = False
        for x in range(bx0 + 2, bx1 - 2, 2):
            r, g, b = pixel(x, y)
            # A block fill or its light edge: saturated and bright, never
            # the dark scene background or a gray text tone.
            if max(r, g, b) > 90 and max(r, g, b) - min(r, g, b) > 30:
                piece_colors.add((r // 32, g // 32, b // 32))
                row_has_piece = True
        if row_has_piece:
            if y > by0 + (by1 - by0) * 3 // 4:
                stack_rows += 1
            elif y < by0 + (by1 - by0) // 2:
                flight_rows += 1
    check(len(piece_colors) >= 3,
          f"settled pieces of at least three colors are visible "
          f"({len(piece_colors)} found)")
    check(stack_rows >= 8,
          f"the lower board holds a settled stack ({stack_rows} rows)")
    check(flight_rows >= 1,
          f"a piece or its ghost is in flight above the stack "
          f"({flight_rows} rows)")

    def gray_text(x0, x1):
        # The two text tones of the HUD, bright and desaturated: the dim
        # label tone and the bright value tone. Piece fills are saturated,
        # so they never count.
        found = 0
        for y in range(top + 4, height - bottom - 4):
            for x in range(x0, x1, 2):
                r, g, b = pixel(x, y)
                if max(r, g, b) >= 130 and max(r, g, b) - min(r, g, b) <= 40:
                    found += 1
        return found

    def piece_colors_in(x0, x1):
        colors = set()
        for y in range(top + 4, height - bottom - 4):
            for x in range(x0, x1, 2):
                r, g, b = pixel(x, y)
                if max(r, g, b) > 90 and max(r, g, b) - min(r, g, b) > 30:
                    colors.add((r // 32, g // 32, b // 32))
        return colors

    lx0, _ = canvas_to_shot(192, 0)
    lx1, _ = canvas_to_shot(319, 0)
    rx0, _ = canvas_to_shot(640, 0)
    rx1, _ = canvas_to_shot(767, 0)
    left_text = gray_text(lx0, lx1)
    check(left_text >= 40,
          f"the left HUD column is readable ({left_text} text pixels)")
    right_text = gray_text(rx0, rx1)
    check(right_text >= 20,
          f"the right HUD column carries its label ({right_text} text pixels)")
    # The right column's content is the queue: five previews at the default
    # setting, so several distinct piece colors must sit beside the board.
    previews = piece_colors_in(rx0, rx1)
    check(len(previews) >= 3,
          f"the Next previews are visible ({len(previews)} distinct colors)")


# ---------------------------------------------------------------------------
# The launcher run against the unpacked archive.
# ---------------------------------------------------------------------------

def check_launcher_run(run_dir, repo):
    status = read(run_dir / "status.txt").strip()
    check(status == "0", f"the archived launcher exited 0 (got {status!r})")
    trace = read(run_dir / "trace.txt")
    for expected in ("control.txt sourced", "get_controls",
                     "pm_platform_helper", "pm_finish"):
        check(expected in trace, f"the launcher run reached {expected}")
    out = read(run_dir / "launcher.out")
    check("exited with status 0" in out,
          "the launcher reported the game's clean exit")

    tree = read(run_dir / "game-tree-after.txt")
    check("conf" in tree, "the launcher created the port's conf/ directory")
    log = read(run_dir / "sandbox" / "root" / "roms" / "ports" /
               "stackfall" / "log.txt")
    check(bool(log), "the launcher wrote its own log.txt")
    session = read(run_dir / "sandbox" / "root" / "roms" / "ports" /
                   "stackfall" / "conf" / "log.txt")
    check("STACKFALL" in session and "shutdown" in session.lower(),
          "the game's session log landed in conf/ and closed cleanly")
    # The staged desktop binary stands in for the archived ARM64 one, so its
    # banner still proves the version the package ships.
    version = re.search(r'kVersionString\[\] = "([^"]+)"',
                        read(repo / "include" / "stackfall" / "version.hpp"))
    check(version is not None and f"STACKFALL {version.group(1)}" in session,
          "the game banner in conf/log.txt reports the shipped version")


# ---------------------------------------------------------------------------
# The naming rule and the determinism of the archive.
# ---------------------------------------------------------------------------

def check_naming(run_dir):
    shipped = [
        ("Stackfall.sh", read(run_dir / "ports" / "Stackfall.sh")),
        ("stackfall.md", read(run_dir / "ports" / "stackfall" / "stackfall.md")),
        ("port.json", read(run_dir / "ports" / "stackfall" / "port.json")),
        ("gameinfo.xml",
         read(run_dir / "ports" / "stackfall" / "gameinfo.xml")),
        ("release-notes.md",
         read(run_dir / "ports" / "stackfall" / "release-notes.md")),
        ("BUILDING.md", read(run_dir / "ports" / "stackfall" / "BUILDING.md")),
    ]
    for name, text in shipped:
        check("tetris" not in text.lower(),
              f"{name} uses the STACKFALL name, not the forbidden one")
    check(all("STACKFALL" in text for _, text in shipped[:2]),
          "the launcher and the readme name STACKFALL")


def check_deterministic(run_dir, repo):
    """Rebuilds the archive bytes from the staged port directory."""
    staged = repo / "build" / "portmaster-package" / "stackfall"
    zip_path = repo / "build" / "portmaster-package" / "stackfall.zip"
    if not staged.is_dir() or not zip_path.is_file():
        check(False, "the staged port directory and the archive exist")
        return
    import io
    buffer = io.BytesIO()
    entries = [
        ("Stackfall.sh", "Stackfall.sh"),
        ("port.json", "stackfall/port.json"),
        ("gameinfo.xml", "stackfall/gameinfo.xml"),
        ("screenshot.png", "stackfall/screenshot.png"),
        ("README.md", "stackfall/stackfall.md"),
        ("stackfall/stackfall.aarch64", "stackfall/stackfall.aarch64"),
        ("stackfall/release-notes.md", "stackfall/release-notes.md"),
        ("stackfall/BUILDING.md", "stackfall/BUILDING.md"),
        ("stackfall/licenses/LICENSE", "stackfall/licenses/LICENSE"),
        ("stackfall/licenses/LICENSE.SDL2.txt",
         "stackfall/licenses/LICENSE.SDL2.txt"),
    ]
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as zf:
        for source, name in sorted(entries, key=lambda pair: pair[1].lower()):
            path = staged / source
            if not path.is_file():
                check(False, f"the staged {source} exists")
                return
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.external_attr = 0o644 << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            if name == "Stackfall.sh" or name.endswith(".aarch64"):
                info.external_attr = 0o755 << 16
            zf.writestr(info, path.read_bytes())
    check(hashlib.sha256(buffer.getvalue()).hexdigest() ==
          sha256_file(zip_path),
          "the archive bytes follow deterministically from its files")


def main(argv):
    if len(argv) != 3:
        print("usage: check_t019.py <run-directory> <repository-root>",
              file=sys.stderr)
        return 2
    run_dir = Path(argv[1])
    repo = Path(argv[2])

    print("== port.json")
    port_json = check_port_json(repo)
    print("== gameinfo.xml")
    check_gameinfo(repo, port_json)
    print("== archive")
    check_archive(run_dir, repo)
    print("== unpacked tree")
    check_unpacked_tree(run_dir)
    print("== binary and build instructions")
    check_binary(run_dir, repo)
    print("== clean-environment rebuild")
    check_repro(run_dir)
    print("== screenshot")
    check_screenshot(run_dir)
    print("== launcher run")
    check_launcher_run(run_dir, repo)
    print("== naming")
    check_naming(run_dir)
    print("== determinism")
    check_deterministic(run_dir, repo)

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
