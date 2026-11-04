# AArch64 build environment

This directory holds the PortMaster-aligned Ubuntu 22.04 ARM64 build
environment of STACKFALL (docs/SPEC.md section 10.2, task T016).

## Contents

- `Dockerfile` — the environment image: Ubuntu 22.04 LTS ARM64 with the
  PortMaster toolchain and SDL 2.26.2 built from source.
- `build-arm64.sh` — the one command that builds the tree inside that
  environment, publishes `stackfall.aarch64`, documents its runtime
  dependencies, and checks the state-hash parity with the desktop build.

## Alignment with the PortMaster guide

The image follows the PortMaster build-environments guide
(<https://portmaster.games/build-environments.html>), method 2 (the
Ubuntu 22.04 ARM64 chroot), moved into a Docker container:

- Base: Ubuntu 22.04 LTS, ARM64, run under QEMU user emulation. The
  guide's docker companion page
  (<https://portmaster.games/docker.html>) uses the same emulation for
  its official builder images.
- Toolchain: the distribution `gcc`/`g++`, `cmake`, `ninja-build`, and
  `pkg-config`, as in the guide's chroot package list.
- SDL2: the guide's pinned source build. The distribution SDL 2.0.20
  packages are installed only to pull their build dependencies, then
  purged with `apt-get remove --purge -y libsdl2-2.0-0 libsdl2-dev`.
  SDL 2.26.2 is then built from the guide's tag URL with
  `./configure --prefix=/usr`, installed, registered with `ldconfig`,
  and the package names are held with
  `apt-mark hold libsdl2-2.0-0 libsdl2-dev` so no later upgrade
  overwrites the source build.

Deviations from the guide's chroot package list, all deliberate and
recorded here: the SDL 1.2 family (`libsdl-mixer1.2-dev`,
`libsdl1.2-dev`, `libsdl-gfx1.2-dev`, `libsdl-image1.2-dev`),
`libsdl2-mixer-dev`, boost, ffmpeg, openal, freetype, magics++,
mercurial, premake4, and clang are not installed. STACKFALL links SDL2
only and synthesizes its own audio, so no other port library is
referenced by the source tree.

A cross-check against the guide's docker method is recorded in
`notes/agent-testing.md` (task T016): the official builder image
`ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest`
(Ubuntu 20.04, SDL 2.32.0 from `/usr/local`) also builds the tree and
prints the same state hashes.

The environment's SDL 2.26.2 also runs the SDL platform suites of the
full ctest pass. That surfaced one version difference: when the
developer virtual pad carries no USB IDs, SDL packs its name into the
device GUID, and SDL 2.26 then rejects the exact-GUID mapping the game
adds. The pad now carries explicit fictional IDs (see
`src/input_sdl.cpp`).

## Reproducing the binary

Requirements: Docker, and the QEMU aarch64 binfmt handler registered
once per host boot:

```bash
docker run --rm --privileged tonistiigi/binfmt --install arm64
```

Then, from the repository root:

```bash
tools/arm64/build-arm64.sh
```

The script:

1. Builds the environment image `stackfall-arm64-builder:22.04` from
   `Dockerfile` (or reuses it with
   `STACKFALL_ARM64_SKIP_IMAGE_BUILD=1`).
2. Configures and builds the whole tree with CMake and Ninja inside an
   ARM64 container, in `build-arm64/` (Release by default;
   `STACKFALL_ARM64_CONFIG` overrides).
3. Publishes the game as `build-arm64/stackfall.aarch64` and records
   `file`, `ldd`, `readelf -d`, `sdl2-config`, `gcc`, and `sha256`
   output in the evidence directory.
4. Runs `stackfall_tests --seed 987654321` twice inside the ARM64
   environment, plus the full `ctest` pass (disable with
   `STACKFALL_ARM64_SKIP_CTEST=1`).
5. Runs the desktop suite and requires every printed state-hash line
   to match (specification section 11, item 18, ARM64 half).

Evidence lands in `build/agent-checks/<run-id>/`. The default run ID is
`t016-arm64-<date>`; set `STACKFALL_ARM64_RUN_ID` to override. Every
build product stays under `build-arm64/` and `build/`, which
`.gitignore` covers through its `build*/` rule.

## Host note

Both the image build and every container phase run with
`--network=host`. On this machine the Docker bridge network drops the
plain-HTTP apt traffic of an emulated ARM64 process (each request
timeouts out after 20 seconds), while the same requests succeed from
the host path. The game itself needs no network at build or run time.
