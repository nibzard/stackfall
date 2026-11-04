#!/bin/sh
# T019 PortMaster release-package check (docs/AGENT_TESTING.md,
# docs/SPEC.md section 10.4).
#
# One command assembles the distributable archive and drives it:
#
#   tests/harness/t019.sh
#
# Phases, each writing into build/agent-checks/<run-id>/:
#
#   static   port.json and gameinfo.xml against the current PortMaster
#            format, the package sources against the naming rule of the
#            task (no Tetris name or trade dress), and the archive
#            structure the packaging guide defines
#   arm64    build-arm64/stackfall.aarch64 when it is missing, through
#            the documented build command (T019_SKIP_ARM64=1 requires it
#            pre-built)
#   package  tools/portmaster/package.sh, which captures the 4:3
#            screenshot from the desktop build and writes
#            build/portmaster-package/stackfall.zip
#   unpack   the archive unpacked into a fake ports directory, and the
#            unpacked tree checked against the section 10.3 layout plus
#            every required item of section 10.4
#   run      the archive's own launcher, driven against a stub PortMaster
#            control folder with the desktop binary staged beside the
#            archived one (the ARM64 binary cannot run on an x86-64
#            host), under the dummy video and audio drivers
#   repro    the documented clean-environment rebuild of BUILDING.md in a
#            fresh container, compared byte for byte with the archived
#            binary (T019_SKIP_REPRO=1 skips)
#   check    tests/harness/check_t019.py, the independent gate checker
#
# Environment variables: T019_BUILD_DIR (default build),
# T019_ARM64_BINARY (default build-arm64/stackfall.aarch64),
# T019_RUN_ID (default t019-package-<date>), T019_TIMEOUT_S (default 120),
# T019_SKIP_BUILD=1 to reuse the desktop build, T019_SKIP_ARM64=1,
# T019_SKIP_REPRO=1.

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=${T019_BUILD_DIR:-$repo/build}
arm64_binary=${T019_ARM64_BINARY:-$repo/build-arm64/stackfall.aarch64}
run_id=${T019_RUN_ID:-t019-package-$(date +%Y-%m-%d)}
timeout_s=${T019_TIMEOUT_S:-120}
run_dir="$repo/build/agent-checks/$run_id"
checker="$repo/tests/harness/check_t019.py"

fail() {
  echo "t019: $*" >&2
  exit 1
}

case $run_id in
  */*|""|.|..) fail "T019_RUN_ID must name one directory, not '$run_id'" ;;
esac

[ -f "$checker" ] || fail "missing checker $checker"

rm -rf "$run_dir"
mkdir -p "$run_dir" || fail "cannot create $run_dir"

echo "t019: repository $repo"
echo "t019: evidence   $run_dir"

# ---------------------------------------------------------------------------
# Phase: static metadata.
# ---------------------------------------------------------------------------
echo
echo "=== phase static ==="
mkdir -p "$run_dir/static"
for file in portmaster/port.json portmaster/gameinfo.xml \
            portmaster/README.md portmaster/release-notes.md \
            portmaster/BUILDING.md portmaster/licenses/LICENSE \
            portmaster/licenses/LICENSE.SDL2.txt portmaster/Stackfall.sh; do
  [ -f "$repo/$file" ] || fail "missing package source $file"
done
{
  echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  cd "$repo" && sha256sum portmaster/port.json portmaster/gameinfo.xml \
    portmaster/README.md portmaster/release-notes.md portmaster/BUILDING.md \
    portmaster/licenses/LICENSE portmaster/licenses/LICENSE.SDL2.txt \
    portmaster/Stackfall.sh tools/portmaster/package.sh \
    tools/portmaster/capture-screenshot.sh tools/portmaster/bmp2png.py \
    tests/harness/t019.sh tests/harness/check_t019.py
} >"$run_dir/static/source.sha256" || fail "cannot record source identity"

# The naming rule of the task: the STACKFALL name and its own presentation
# only. The forbidden name is checked case-insensitively in every file that
# ships inside the archive.
if grep -ri "tetris" portmaster/ >/dev/null 2>&1; then
  grep -ri "tetris" portmaster/ >"$run_dir/static/forbidden-name.txt"
  fail "the package sources use a forbidden name; see $run_dir/static/forbidden-name.txt"
fi

# ---------------------------------------------------------------------------
# Phase: the ARM64 binary.
# ---------------------------------------------------------------------------
echo
echo "=== phase arm64 ==="
if [ ! -f "$arm64_binary" ]; then
  if [ "${T019_SKIP_ARM64:-0}" = "1" ]; then
    fail "T019_SKIP_ARM64=1 but $arm64_binary is missing"
  fi
  echo "t019: no ARM64 binary; building it with the documented command"
  STACKFALL_ARM64_SKIP_IMAGE_BUILD=1 \
    STACKFALL_ARM64_RUN_ID="$run_id-arm64-build" \
    "$repo/tools/arm64/build-arm64.sh" >"$run_dir/arm64-build.log" 2>&1 \
    || fail "the ARM64 build failed; see $run_dir/arm64-build.log"
fi
file "$arm64_binary" >"$run_dir/arm64-file.txt" 2>&1
grep -q "ARM aarch64" "$run_dir/arm64-file.txt" \
  || fail "$arm64_binary is not an ARM aarch64 binary"
sha256sum "$arm64_binary" >"$run_dir/arm64-sha256.txt"
cat "$run_dir/arm64-file.txt"

# ---------------------------------------------------------------------------
# Phase: the desktop build, then the package.
# ---------------------------------------------------------------------------
echo
echo "=== phase package ==="
if [ "${T019_SKIP_BUILD:-0}" != "1" ]; then
  cmake -S "$repo" -B "$build_dir" -G Ninja >/dev/null \
    || fail "cmake configure failed in $build_dir"
  cmake --build "$build_dir" || fail "build failed in $build_dir"
fi
[ -x "$build_dir/stackfall" ] || fail "missing desktop game $build_dir/stackfall"

STACKFALL_PACKAGE_RUN_ID="$run_id-package" \
  "$repo/tools/portmaster/package.sh" \
    --binary "$arm64_binary" \
    >"$run_dir/package.log" 2>&1 \
  || fail "package.sh failed; see $run_dir/package.log"
tail -3 "$run_dir/package.log"
zip_file="$repo/build/portmaster-package/stackfall.zip"
[ -f "$zip_file" ] || fail "package.sh wrote no $zip_file"

# ---------------------------------------------------------------------------
# Phase: unpack the archive into a ports directory.
# ---------------------------------------------------------------------------
echo
echo "=== phase unpack ==="
ports="$run_dir/ports"
mkdir -p "$ports"
python3 - "$zip_file" "$ports" >"$run_dir/unpack-list.txt" <<'PYEOF' \
  || fail "the archive cannot be unpacked"
import sys
import zipfile
with zipfile.ZipFile(sys.argv[1]) as zf:
    bad = zf.testzip()
    if bad is not None:
        sys.exit(f"corrupt entry: {bad}")
    zf.extractall(sys.argv[2])
    for name in zf.namelist():
        print(name)
PYEOF
find "$ports" -mindepth 1 | LC_ALL=C sort >"$run_dir/unpack-tree.txt"
cat "$run_dir/unpack-tree.txt"

# ---------------------------------------------------------------------------
# Phase: run the archive's own launcher.
# ---------------------------------------------------------------------------
echo
echo "=== phase run ==="
sandbox="$run_dir/sandbox"
root="$sandbox/root"
mkdir -p "$root/xdg/PortMaster"
stub_mapping="03000000c0160000e0de000069010000,Anbernic RG503,a:b1,b:b0,x:b3,y:b2,back:b6,guide:b8,start:b7,leftshoulder:b9,rightshoulder:b10,dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,leftx:a0,lefty:a1,"
{
  echo '# Stub PortMaster control folder of the T019 package check.'
  echo 'echo "control.txt sourced" >> "$T019_TRACE"'
  echo "export directory=\"$root/roms\""
  echo 'export CFW_NAME="ROCKNIX"'
  echo 'export DEVICE_ARCH="x86_64"'
  echo 'export ESUDO=""'
  echo "export sdl_controllerconfig=\"$stub_mapping\""
  echo 'get_controls() { echo "get_controls" >> "$T019_TRACE"; }'
  echo 'pm_message() { echo "pm_message: $*" >> "$T019_TRACE"; }'
  echo 'pm_show_error() { echo "pm_show_error: $*" >> "$T019_TRACE"; }'
  echo 'pm_platform_helper() { echo "pm_platform_helper: $*" >> "$T019_TRACE"; }'
  echo 'pm_finish() { echo "pm_finish" >> "$T019_TRACE"; }'
} >"$root/xdg/PortMaster/control.txt"
mkdir -p "$root/roms/ports"
# The ports directory receives the unpacked archive the way a device does.
cp -R "$ports/." "$root/roms/ports/" || fail "cannot stage the unpacked archive"
# The desktop binary rides beside the archived one under the host
# architecture name, which is how a port ships more than one machine; the
# launcher picks it, because the ARM64 binary cannot run here.
cp "$build_dir/stackfall" "$root/roms/ports/stackfall/stackfall.x86_64" \
  || fail "cannot stage the desktop binary"
chmod 755 "$root/roms/ports/Stackfall.sh"

: >"$run_dir/trace.txt"
env T019_TRACE="$run_dir/trace.txt" XDG_DATA_HOME="$root/xdg" \
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  timeout "$timeout_s" \
  "$root/roms/ports/Stackfall.sh" --seed 20261104 --frames 240 \
  >"$run_dir/launcher.out" 2>&1
echo "$?" >"$run_dir/status.txt"
sleep 1
find "$root/roms/ports/stackfall" | LC_ALL=C sort >"$run_dir/game-tree-after.txt"

# ---------------------------------------------------------------------------
# Phase: the documented clean-environment rebuild.
# ---------------------------------------------------------------------------
echo
echo "=== phase repro ==="
if [ "${T019_SKIP_REPRO:-0}" = "1" ]; then
  echo "t019: repro phase skipped (T019_SKIP_REPRO=1)"
  echo "skipped" >"$run_dir/repro-skipped.txt"
else
  # A path the container reaches through its /workspace mount.
  repro_rel="build/agent-checks/$run_id/repro-build"
  rm -rf "$repo/$repro_rel"
  docker run --rm --platform=linux/arm64 --network=host \
    --user "$(id -u):$(id -g)" --env HOME=/tmp \
    -l "io.looper.resource-prefix=${LOOPER_RESOURCE_PREFIX:-none}" \
    -l "io.looper.artifact=${LOOPER_ARTIFACT_DIR:-none}" \
    --volume "$repo:/workspace" --workdir /workspace \
    stackfall-arm64-builder:22.04 \
    bash -c "set -e; cmake -S . -B '$repro_rel' -G Ninja \
               -DCMAKE_BUILD_TYPE=Release >/dev/null; \
             cmake --build '$repro_rel' -j\"\$(nproc)\" >/dev/null; \
             sha256sum '$repro_rel/stackfall'" \
    >"$run_dir/repro.log" 2>&1 \
    || fail "the clean rebuild failed; see $run_dir/repro.log"
  cat "$run_dir/repro.log"
fi

# ---------------------------------------------------------------------------
# Phase: the independent checker.
# ---------------------------------------------------------------------------
echo
echo "=== phase check ==="
python3 "$checker" "$run_dir" "$repo" || fail "the package check failed"
echo "t019: pass"
echo "archive  : $zip_file"
echo "evidence : $run_dir"
