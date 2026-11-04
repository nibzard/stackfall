# PortMaster-aligned Ubuntu 22.04 ARM64 build environment for STACKFALL
# (docs/SPEC.md section 10.2, task T016).
#
# This image follows the PortMaster build-environments guide
# (https://portmaster.games/build-environments.html):
#
#   * Ubuntu 22.04 LTS ARM64 userspace. The host runs it under QEMU user
#     emulation; register the handler once per host boot with
#     `docker run --rm --privileged tonistiigi/binfmt --install arm64`.
#   * The guide's toolchain: the distribution gcc/g++, CMake, and Ninja.
#   * The guide's SDL2: the distribution packages are installed for their
#     build dependencies, purged, and replaced by SDL 2.26.2 built from
#     source with `./configure --prefix=/usr`, then the package names are
#     held so no later upgrade overwrites the source build.
#
# Deviations from the guide's chroot package list are recorded in
# tools/arm64/README.md: the SDL 1.2 family, boost, ffmpeg, openal,
# freetype, mercurial, premake4, magics++, and clang are left out because
# STACKFALL links SDL2 only.

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LC_ALL=C

# Guide step: update, upgrade, install the toolchain, then purge the
# distribution SDL2 packages and hold their names so no later install or
# upgrade overwrites the source build of the next layer. The hold runs
# while the package lists still exist; apt-mark needs the names there.
RUN apt-get update \
 && apt-get -y upgrade \
 && apt-get install -y --no-install-recommends \
      build-essential \
      ca-certificates \
      cmake \
      file \
      git \
      libdrm-dev \
      libevdev-dev \
      libgbm-dev \
      libsdl2-dev \
      ninja-build \
      pkg-config \
      wget \
 && apt-get remove --purge -y libsdl2-2.0-0 libsdl2-dev \
 && apt-mark hold libsdl2-2.0-0 libsdl2-dev \
 && rm -rf /var/lib/apt/lists/*

# SDL 2.26.2, pinned by tag URL exactly as the guide does.
RUN wget -q https://github.com/libsdl-org/SDL/archive/refs/tags/release-2.26.2.tar.gz \
 && tar xf release-2.26.2.tar.gz \
 && cd SDL-release-2.26.2 \
 && ./configure --prefix=/usr \
 && make -j"$(nproc)" \
 && make install \
 && /sbin/ldconfig \
 && cd / \
 && rm -rf /SDL-release-2.26.2 /release-2.26.2.tar.gz

WORKDIR /workspace
