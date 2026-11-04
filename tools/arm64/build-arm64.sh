#!/bin/sh
# Reproducible AArch64 build of STACKFALL in the PortMaster-aligned
# Ubuntu 22.04 ARM64 environment (docs/SPEC.md section 10.2, task T016).
#
# This is the one command that produces and checks the device binary:
#
#   tools/arm64/build-arm64.sh
#
# It builds (or reuses) the Docker image from tools/arm64/Dockerfile,
# configures and builds the whole tree with CMake/Ninja inside an ARM64
# container, publishes the game as stackfall.aarch64, records file,
# ldd, and readelf evidence, runs the headless rule suite inside the
# same environment, and compares the printed state hashes with the
# desktop build (section 11, item 18, ARM64 half). Any mismatch, build
# failure, or test failure exits nonzero.
#
# Host requirements: Docker, and the QEMU aarch64 binfmt handler
# (once per host boot):
#   docker run --rm --privileged tonistiigi/binfmt --install arm64
# Both the image build and every container phase run with --network=host:
# on this machine the Docker bridge network drops the plain-HTTP apt
# traffic of an emulated arm64 process, while the host path works.
#
# Options through environment variables:
#   STACKFALL_ARM64_BUILD   build directory, relative to the repository
#                           root (default build-arm64)
#   STACKFALL_ARM64_CONFIG  CMAKE_BUILD_TYPE (default Release)
#   STACKFALL_ARM64_IMAGE   image tag (default stackfall-arm64-builder:22.04)
#   STACKFALL_ARM64_RUN_ID  evidence directory name under
#                           build/agent-checks (default t016-arm64-<date>)
#   STACKFALL_ARM64_SKIP_IMAGE_BUILD  set to 1 to reuse the built image
#   STACKFALL_ARM64_SKIP_CTEST  set to 1 to skip the full ctest pass and
#                           run stackfall_tests only (the hash comparison
#                           and every other check still run)
#   STACKFALL_DESKTOP_BUILD desktop build directory used for the hash
#                           comparison (default build)

set -u

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_rel=${STACKFALL_ARM64_BUILD:-build-arm64}
config=${STACKFALL_ARM64_CONFIG:-Release}
image=${STACKFALL_ARM64_IMAGE:-stackfall-arm64-builder:22.04}
run_id=${STACKFALL_ARM64_RUN_ID:-t016-arm64-$(date +%Y-%m-%d)}
run_dir="$repo/build/agent-checks/$run_id"
desktop_build=${STACKFALL_DESKTOP_BUILD:-$repo/build}
build_dir="$repo/$build_rel"

case $build_rel in
  /*|..*) fail_path=1 ;;
  *) fail_path=0 ;;
esac
[ "$fail_path" = 0 ] || {
  echo "arm64-build: STACKFALL_ARM64_BUILD must be relative to the repository root" >&2
  exit 1
}

fail() {
  echo "arm64-build: $*" >&2
  exit 1
}

[ -f "$repo/tools/arm64/Dockerfile" ] || fail "Dockerfile not found: $repo/tools/arm64/Dockerfile"

mkdir -p "$run_dir"

# ---------------------------------------------------------------------------
# 1. Environment image.
# ---------------------------------------------------------------------------
if [ "${STACKFALL_ARM64_SKIP_IMAGE_BUILD:-0}" != "1" ]; then
  echo "== building environment image $image"
  docker build --platform=linux/arm64 --network=host -t "$image" "$repo/tools/arm64" \
    >"$run_dir/docker-build.log" 2>&1 \
    || fail "docker build failed; see $run_dir/docker-build.log"
fi
docker image inspect "$image" >/dev/null 2>&1 \
  || fail "image $image is missing; unset STACKFALL_ARM64_SKIP_IMAGE_BUILD"

# ---------------------------------------------------------------------------
# 2. Every phase runs in its own container mounted at /workspace, so all
#    output files land inside the repository build tree and stay owned by
#    the invoking user. The build directory reaches the command through
#    STACKFALL_BUILD_DIR, always relative to /workspace. The Looper labels
#    let the session's registered cleanup find the containers if this
#    script is interrupted.
# ---------------------------------------------------------------------------
run_container() {
  docker run --rm --platform=linux/arm64 --network=host \
    --user "$(id -u):$(id -g)" --env HOME=/tmp \
    --env STACKFALL_BUILD_DIR="$build_rel" \
    --volume "$repo:/workspace" --workdir /workspace \
    -l "io.looper.resource-prefix=${LOOPER_RESOURCE_PREFIX:-none}" \
    -l "io.looper.artifact=${LOOPER_ARTIFACT_DIR:-none}" \
    "$image" /bin/bash -c "$1" || fail "container phase failed: $1"
}

echo "== configuring and building ($config) inside $image"
run_container '
  set -e
  cmake -S . -B "$STACKFALL_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE='"$config"'
  cmake --build "$STACKFALL_BUILD_DIR" -j"$(nproc)"
' >"$run_dir/arm64-configure-build.log" 2>&1

# ---------------------------------------------------------------------------
# 3. Publish the game as stackfall.aarch64 and document the binary.
# ---------------------------------------------------------------------------
cp "$build_dir/stackfall" "$build_dir/stackfall.aarch64"

echo "== recording file, ldd, and readelf evidence"
run_container '
  set -e
  cd "$STACKFALL_BUILD_DIR"
  echo "== uname"; uname -m
  echo "== file stackfall.aarch64"
  file stackfall.aarch64
  echo "== ldd stackfall.aarch64"
  ldd stackfall.aarch64
  echo "== readelf -d stackfall.aarch64 (NEEDED entries)"
  readelf -d stackfall.aarch64 | grep NEEDED
  echo "== sdl2-config"
  sdl2-config --version
  echo "== gcc"
  gcc --version | head -1
  echo "== sha256"
  sha256sum stackfall.aarch64
' >"$run_dir/arm64-binary-report.txt" 2>&1

grep -q "ARM aarch64" "$run_dir/arm64-binary-report.txt" \
  || fail "stackfall.aarch64 is not an ARM aarch64 binary"

# ---------------------------------------------------------------------------
# 4. Rule suite inside the ARM64 environment, twice, with the same
#    invocation the desktop ctest name stackfall_tests_seed uses.
# ---------------------------------------------------------------------------
echo "== running stackfall_tests inside the ARM64 environment (twice)"
run_container '"$STACKFALL_BUILD_DIR"/stackfall_tests --seed 987654321' \
  >"$run_dir/arm64-hashes.txt" 2>&1
run_container '"$STACKFALL_BUILD_DIR"/stackfall_tests --seed 987654321' \
  >"$run_dir/arm64-hashes-rerun.txt" 2>&1

if [ "${STACKFALL_ARM64_SKIP_CTEST:-0}" != "1" ]; then
  echo "== running the full ctest pass inside the ARM64 environment"
  run_container 'ctest --test-dir "$STACKFALL_BUILD_DIR" --output-on-failure' \
    >"$run_dir/arm64-ctest.log" 2>&1
fi

# ---------------------------------------------------------------------------
# 5. Hash parity with the desktop build (section 11, item 18, ARM64 half).
# ---------------------------------------------------------------------------
echo "== comparing state hashes with the desktop build"
[ -x "$desktop_build/stackfall_tests" ] \
  || fail "desktop suite $desktop_build/stackfall_tests is missing; build it first"
"$desktop_build/stackfall_tests" --seed 987654321 \
  >"$run_dir/desktop-hashes.txt" 2>&1 \
  || fail "desktop suite run failed"

hash_lines() {
  grep " hash 0x" "$1"
}

hash_lines "$run_dir/desktop-hashes.txt" >"$run_dir/desktop-hash-lines.txt"
hash_lines "$run_dir/arm64-hashes.txt" >"$run_dir/arm64-hash-lines.txt"
hash_lines "$run_dir/arm64-hashes-rerun.txt" >"$run_dir/arm64-hash-lines-rerun.txt"

diff "$run_dir/desktop-hash-lines.txt" "$run_dir/arm64-hash-lines.txt" \
  || fail "ARM64 state hashes differ from the desktop build"
diff "$run_dir/arm64-hash-lines.txt" "$run_dir/arm64-hash-lines-rerun.txt" \
  || fail "the two ARM64 suite runs printed different hashes"

# ---------------------------------------------------------------------------
# 6. Source identity for the record.
# ---------------------------------------------------------------------------
{
  git -C "$repo" rev-parse HEAD
  sha256sum "$repo"/src/*.cpp "$repo"/include/stackfall/*.hpp \
            "$repo"/tests/*.cpp "$repo"/tests/harness/reference_replay.cpp \
            "$repo"/CMakeLists.txt "$repo"/tools/arm64/Dockerfile
} >"$run_dir/source.sha256" 2>/dev/null || true

echo "== pass"
echo "binary   : $build_dir/stackfall.aarch64"
echo "evidence : $run_dir"
