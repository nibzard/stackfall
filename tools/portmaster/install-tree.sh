#!/bin/sh
# Stage the local ROCKNIX install tree of STACKFALL (docs/SPEC.md
# section 10.3, task T017).
#
#   tools/portmaster/install-tree.sh
#
# This builds the on-device layout, not the distributable archive of
# section 10.4 (task T019 adds port.json, gameinfo.xml, the screenshot, and
# the licenses around it):
#
#   <dest>/
#   ├── Stackfall.sh              copied from portmaster/Stackfall.sh
#   └── stackfall/
#       └── stackfall.<arch>      copied from the built binary
#
# conf/ and log.txt are deliberately absent: specification section 10.3
# marks both "created at runtime", and the launcher creates conf/ on every
# launch. The staged tree is what a device copies to /roms/ports.
#
# Options through environment variables or command-line flags:
#   STACKFALL_PORT_BINARY  binary to install (default
#                          build-arm64/stackfall.aarch64)
#   STACKFALL_PORT_ARCH    architecture name of the file name (default
#                          aarch64)
#   STACKFALL_PORT_DEST    destination directory (default
#                          build/portmaster-install)
#   --binary PATH, --arch NAME, --dest DIR   same, as flags
#
# Any failure exits nonzero. The destination is replaced, so point
# STACKFALL_PORT_DEST at a build directory, never at /roms/ports itself.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
binary=${STACKFALL_PORT_BINARY:-$repo/build-arm64/stackfall.aarch64}
arch=${STACKFALL_PORT_ARCH:-aarch64}
dest=${STACKFALL_PORT_DEST:-$repo/build/portmaster-install}

fail() {
  echo "portmaster-install: $*" >&2
  exit 1
}

while [ $# -gt 0 ]; do
  case $1 in
    --binary) [ $# -ge 2 ] || fail "--binary needs a path"; binary=$2; shift 2 ;;
    --arch)   [ $# -ge 2 ] || fail "--arch needs a name"; arch=$2; shift 2 ;;
    --dest)   [ $# -ge 2 ] || fail "--dest needs a path"; dest=$2; shift 2 ;;
    *) fail "unknown argument: $1" ;;
  esac
done

launcher="$repo/portmaster/Stackfall.sh"

[ -f "$launcher" ] || fail "launcher not found: $launcher"
[ -f "$binary" ] || fail "binary not found: $binary (build it with tools/arm64/build-arm64.sh)"
case $arch in
  aarch64|armhf|x86_64|x86) ;;
  *) fail "unsupported architecture name: $arch" ;;
esac

# Specification section 10.2: never ship an executable of the wrong machine.
if command -v file >/dev/null 2>&1; then
  report=$(file "$binary")
  case $arch in
    aarch64) echo "$report" | grep -q "ARM aarch64" \
               || fail "wrong machine for $arch: $report" ;;
    armhf)   echo "$report" | grep -q "ARM" \
               || fail "wrong machine for $arch: $report" ;;
    x86_64|x86) echo "$report" | grep -q -e "x86-64" -e "80386" \
               || fail "wrong machine for $arch: $report" ;;
  esac
fi

rm -rf "$dest"
mkdir -p "$dest/stackfall" || fail "cannot create $dest/stackfall"
cp "$launcher" "$dest/Stackfall.sh" || fail "cannot copy the launcher"
cp "$binary" "$dest/stackfall/stackfall.$arch" \
  || fail "cannot copy the binary"
chmod 755 "$dest/Stackfall.sh" "$dest/stackfall/stackfall.$arch"

# The staged layout, for the record.
find "$dest" -mindepth 1 | LC_ALL=C sort

echo "staged   : $dest"
echo "launcher : Stackfall.sh"
echo "binary   : stackfall/stackfall.$arch"
