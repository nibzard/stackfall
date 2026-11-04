# Building `stackfall.aarch64` from source

This document reproduces the `stackfall/stackfall.aarch64` file of this port
from a clean ARM64 environment. Every command below runs from the repository
root of the STACKFALL source that produced the release; the SHA-256 the last
section records is the identity of the binary they reproduce.

## What the environment is

The build environment follows the PortMaster build-environments guide
(<https://portmaster.games/build-environments.html>):

- Ubuntu 22.04 LTS, ARM64 (`aarch64`), so the compilers and the libraries are
  the ones the binary runs against. On an x86-64 host the container runs
  under QEMU user emulation.
- GCC 11.4.0, CMake, Ninja, and pkg-config from the distribution.
- SDL 2.26.2 built from source with `./configure --prefix=/usr`, the version
  the guide pins. The distribution SDL2 packages are removed first, so the
  binary links the guide's SDL and not the host desktop build.

The repository defines this environment as a Docker image:
`tools/arm64/Dockerfile` builds it, and `tools/arm64/README.md` records its
deviations from the guide's package list.

## One command

From the repository root:

```sh
tools/arm64/build-arm64.sh
```

The script builds (or reuses) the environment image, configures and builds
the tree with CMake and Ninja inside an ARM64 container, publishes
`build-arm64/stackfall.aarch64`, records its identity and dependencies, runs
the headless rule suite twice inside the environment, and compares the state
hashes with a desktop build of the same source.

Requirements on the host: Docker, and the QEMU `aarch64` binfmt handler
registered once per host boot:

```sh
docker run --rm --privileged tonistiigi/binfmt --install arm64
```

## Step by step

The commands the script runs, in order, for anyone rebuilding by hand:

1. Build the environment image:

   ```sh
   docker build --platform=linux/arm64 -t stackfall-arm64-builder:22.04 \
     tools/arm64
   ```

2. Configure and build inside it. The repository is mounted at `/workspace`,
   so every product lands in your own tree:

   ```sh
   docker run --rm --platform=linux/arm64 \
     --user "$(id -u):$(id -g)" --env HOME=/tmp \
     --volume "$PWD:/workspace" --workdir /workspace \
     stackfall-arm64-builder:22.04 \
     bash -c 'cmake -S . -B build-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release &&
              cmake --build build-arm64 -j"$(nproc)"'
   ```

3. Publish and identify the binary:

   ```sh
   cp build-arm64/stackfall build-arm64/stackfall.aarch64
   file build-arm64/stackfall.aarch64
   ldd build-arm64/stackfall.aarch64
   sha256sum build-arm64/stackfall.aarch64
   ```

## What the build produces

For release 1.0 (source commit of 2026-11-04):

- `file`: `ELF 64-bit LSB pie executable, ARM aarch64, version 1 (GNU/Linux),
  dynamically linked, interpreter /lib/ld-linux-aarch64.so.1, for GNU/Linux
  3.7.0`.
- Runtime libraries: `libSDL2-2.0.so.0`, `libstdc++.so.6`, `libgcc_s.so.1`,
  `libc.so.6`, `libm.so.6` (pulled by libstdc++). No other library is
  needed, and none ships with the port.
- The glibc symbol versions referenced run from `GLIBC_2.17` to
  `GLIBC_2.34`, so the package metadata declares `min_glibc` 2.34.
- SHA-256:
  `9220a074bd785b072a77bb37c418ae0f7a3ebda320db6697bead9aca86b69041`.

Two builds from the same source in the same environment produce the same
SHA-256: the sources embed no timestamps, paths, or build dates. The build
record of this release compared a second, clean container build and found
both hashes equal.

## Checking the build

The same script proves the binary behaves like the desktop build. The rule
suite runs inside the ARM64 environment:

```sh
build-arm64/stackfall_tests --seed 987654321
```

Every printed `hash 0x...` state line matches the same suite built and run on
the desktop, which is the cross-architecture determinism check of the
specification (section 11, item 18).

## Assembling the port

`tools/portmaster/package.sh` in the source tree stages this whole port
folder, captures `screenshot.png` from the desktop build, and writes the
`stackfall.zip` archive.
