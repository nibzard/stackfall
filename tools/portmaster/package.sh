#!/bin/sh
# Assemble the distributable PortMaster archive of STACKFALL
# (docs/SPEC.md section 10.4, task T019).
#
#   tools/portmaster/package.sh
#
# This builds the ready-to-run archive of section 10.4, not the plain
# /roms/ports install tree of section 10.3 (tools/portmaster/install-tree.sh
# stages that one):
#
#   stackfall.zip
#   ├── Stackfall.sh               the launcher (portmaster/Stackfall.sh)
#   └── stackfall/                 the game folder
#       ├── stackfall.aarch64      built by tools/arm64/build-arm64.sh
#       ├── port.json              package metadata (portmaster/port.json)
#       ├── gameinfo.xml           the Ports menu entry
#       ├── screenshot.png         4:3 gameplay frame of the desktop build
#       ├── stackfall.md           the port readme (portmaster/README.md)
#       ├── release-notes.md       changes of this release
#       ├── BUILDING.md            how to reproduce the binary
#       └── licenses/              the MIT license and the SDL2 license
#
# The entry list follows the current PortMaster build system
# (https://portmaster.games/packaging.html and the build_release.py of
# https://github.com/PortsMaster/PortMaster-New, reviewed 2026-11-04): the
# launch script and the game folder sit at the archive root, and the
# metadata files the installer consumes (port.json, gameinfo.xml, the
# screenshot, and the readme, which it renames to <port>.md) land inside
# the game folder. Unpacking the archive into a ports directory therefore
# reproduces the section 10.3 install layout exactly.
#
# The screenshot is captured from the desktop build by
# tools/portmaster/capture-screenshot.sh: a real 640x480 letterboxed
# frame, the 4:3 form the packaging guide requires.
#
# The archive is written deterministically: entries are sorted, every
# timestamp is pinned, and the file modes are fixed, so two runs from the
# same sources produce the same bytes. The manifest of the run records
# the SHA-256 of every entry and of the archive itself.
#
# Options through environment variables or command-line flags:
#   STACKFALL_PACKAGE_BINARY  the AArch64 binary (default
#                             build-arm64/stackfall.aarch64)
#   STACKFALL_PACKAGE_DEST    staging directory (default
#                             build/portmaster-package)
#   STACKFALL_PACKAGE_SCREENSHOT  use this PNG instead of capturing one
#   STACKFALL_PACKAGE_RUN_ID  evidence directory name under
#                             build/agent-checks (default
#                             t019-package-<date>)
#   STACKFALL_PACKAGE_SKIP_SCREENSHOT  set to 1 to keep the PNG already
#                             staged in the staging directory
#   --binary PATH, --dest DIR, --screenshot PATH, --run-id NAME  the same

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${STACKFALL_PACKAGE_BINARY:-$repo/build-arm64/stackfall.aarch64}
dest=${STACKFALL_PACKAGE_DEST:-$repo/build/portmaster-package}
screenshot=${STACKFALL_PACKAGE_SCREENSHOT:-}
run_id=${STACKFALL_PACKAGE_RUN_ID:-t019-package-$(date +%Y-%m-%d)}

fail() {
  echo "package: $*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case $1 in
    --binary) [ $# -ge 2 ] || fail "--binary needs a path"; binary=$2; shift 2 ;;
    --dest) [ $# -ge 2 ] || fail "--dest needs a directory"; dest=$2; shift 2 ;;
    --screenshot) [ $# -ge 2 ] || fail "--screenshot needs a path"; screenshot=$2; shift 2 ;;
    --run-id) [ $# -ge 2 ] || fail "--run-id needs a name"; run_id=$2; shift 2 ;;
    *) fail "unknown argument: $1" ;;
  esac
done

case $run_id in
  */*|""|.|..) fail "STACKFALL_PACKAGE_RUN_ID must name one directory" ;;
esac
run_dir="$repo/build/agent-checks/$run_id"

launcher="$repo/portmaster/Stackfall.sh"
port_json="$repo/portmaster/port.json"
gameinfo="$repo/portmaster/gameinfo.xml"
readme="$repo/portmaster/README.md"
licenses="$repo/portmaster/licenses"
notes="$repo/portmaster/release-notes.md"
building="$repo/portmaster/BUILDING.md"

for file in "$launcher" "$port_json" "$gameinfo" "$readme" "$notes" \
            "$building" "$licenses/LICENSE" "$licenses/LICENSE.SDL2.txt"; do
  [ -f "$file" ] || fail "missing package source: $file"
done
[ -f "$binary" ] || fail "missing binary $binary (build it with tools/arm64/build-arm64.sh)"

# Specification section 10.2: never ship an executable of the wrong machine.
if command -v file >/dev/null 2>&1; then
  file "$binary" | grep -q "ARM aarch64" \
    || fail "wrong machine: $(file "$binary")"
fi

rm -rf "$run_dir" "$dest"
mkdir -p "$run_dir" "$dest/stackfall/stackfall/licenses" \
  || fail "cannot create the staging directories"

# ---------------------------------------------------------------------------
# 1. The screenshot: a captured frame, unless the caller brings one.
# ---------------------------------------------------------------------------
shot="$dest/stackfall/screenshot.png"
if [ -n "$screenshot" ]; then
  [ -f "$screenshot" ] || fail "missing screenshot $screenshot"
  cp "$screenshot" "$shot" || fail "cannot copy the screenshot"
elif [ "${STACKFALL_PACKAGE_SKIP_SCREENSHOT:-0}" = "1" ]; then
  fail "STACKFALL_PACKAGE_SKIP_SCREENSHOT is set but no screenshot is staged"
else
  echo "== capturing the 4:3 gameplay screenshot from the desktop build"
  "$repo/tools/portmaster/capture-screenshot.sh" \
    --out "$shot" --run-dir "$run_dir/screenshot" \
    >"$run_dir/screenshot-capture.log" 2>&1 \
    || fail "the screenshot capture failed; see $run_dir/screenshot-capture.log"
  tail -2 "$run_dir/screenshot-capture.log"
fi
[ -f "$shot" ] || fail "no screenshot at $shot"

# ---------------------------------------------------------------------------
# 2. The port directory, the layout of the PortMaster ports tree.
# ---------------------------------------------------------------------------
cp "$launcher" "$dest/stackfall/Stackfall.sh"
cp "$port_json" "$dest/stackfall/port.json"
cp "$gameinfo" "$dest/stackfall/gameinfo.xml"
cp "$readme" "$dest/stackfall/README.md"
cp "$notes" "$dest/stackfall/stackfall/release-notes.md"
cp "$building" "$dest/stackfall/stackfall/BUILDING.md"
cp "$licenses/LICENSE" "$dest/stackfall/stackfall/licenses/LICENSE"
cp "$licenses/LICENSE.SDL2.txt" \
   "$dest/stackfall/stackfall/licenses/LICENSE.SDL2.txt"
cp "$binary" "$dest/stackfall/stackfall/stackfall.aarch64"
chmod 755 "$dest/stackfall/Stackfall.sh" \
          "$dest/stackfall/stackfall/stackfall.aarch64"
chmod 644 "$dest/stackfall/port.json" "$dest/stackfall/gameinfo.xml" \
          "$dest/stackfall/README.md" "$shot" \
          "$dest/stackfall/stackfall/release-notes.md" \
          "$dest/stackfall/stackfall/BUILDING.md" \
          "$dest/stackfall/stackfall/licenses/LICENSE" \
          "$dest/stackfall/stackfall/licenses/LICENSE.SDL2.txt"

find "$dest" -mindepth 1 | LC_ALL=C sort >"$run_dir/staged-tree.txt"

# ---------------------------------------------------------------------------
# 3. The archive: the entry list of the PortMaster build system, written
#    with pinned timestamps and sorted names so the bytes repeat.
# ---------------------------------------------------------------------------
zip_path="$dest/stackfall.zip"
STACKFALL_ZIP_ROOT="$dest/stackfall" STACKFALL_ZIP_OUT="$zip_path" \
  python3 - "$repo/tools/portmaster/package.sh" <<'PYEOF' \
  || fail "the archive could not be written"
import os
import stat
import sys
import zipfile

root = os.environ["STACKFALL_ZIP_ROOT"]
out = os.environ["STACKFALL_ZIP_OUT"]
port_dir = "stackfall"

# (path inside the port directory, path inside the archive). The readme
# moves into the game folder under the name the PortMaster build system
# gives it, exactly as build_release.py of PortMaster-New does.
entries = [
    ("Stackfall.sh", "Stackfall.sh"),
    ("port.json", f"{port_dir}/port.json"),
    ("gameinfo.xml", f"{port_dir}/gameinfo.xml"),
    ("screenshot.png", f"{port_dir}/screenshot.png"),
    ("README.md", f"{port_dir}/stackfall.md"),
    ("stackfall/stackfall.aarch64", f"{port_dir}/stackfall.aarch64"),
    ("stackfall/release-notes.md", f"{port_dir}/release-notes.md"),
    ("stackfall/BUILDING.md", f"{port_dir}/BUILDING.md"),
    ("stackfall/licenses/LICENSE", f"{port_dir}/licenses/LICENSE"),
    ("stackfall/licenses/LICENSE.SDL2.txt",
     f"{port_dir}/licenses/LICENSE.SDL2.txt"),
]

with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED,
                     compresslevel=9) as zf:
    for source, name in sorted(entries, key=lambda pair: pair[1].lower()):
        path = os.path.join(root, source)
        if not os.path.isfile(path):
            sys.exit(f"missing staged file: {path}")
        mode = os.stat(path).st_mode
        # A fixed date and the staged mode, so the archive bytes repeat.
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.external_attr = (stat.S_IMODE(mode) & 0o7777) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        with open(path, "rb") as handle:
            zf.writestr(info, handle.read())
PYEOF

# ---------------------------------------------------------------------------
# 4. The identity of the package, for the record and the handoff.
# ---------------------------------------------------------------------------
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "source commit: $(git -C "$repo" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo
  echo "== staged port directory"
  cat "$run_dir/staged-tree.txt"
  echo
  echo "== archive entries"
  python3 - "$zip_path" <<'PYEOF'
import sys
import zipfile
with zipfile.ZipFile(sys.argv[1]) as zf:
    for info in zf.infolist():
        print(f"{info.file_size:10d}  {info.filename}")
PYEOF
  echo
  echo "== sha256 of every staged file"
  (cd "$dest" && find stackfall -type f | LC_ALL=C sort | xargs sha256sum)
  echo
  echo "== sha256 of the archive"
  sha256sum "$zip_path"
} >"$run_dir/package-report.txt"

echo "== pass"
cat "$run_dir/package-report.txt"
echo "archive  : $zip_path"
echo "evidence : $run_dir"
