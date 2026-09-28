#!/bin/sh
# Build libusb from its release tarball for distribution builds (CI release workflow):
# a universal (arm64 + x86_64) dylib with an explicit minimum macOS version, so the DMG runs
# on every Mac that Qt supports (Homebrew's libusb targets the build machine's macOS only).
#
#   packaging/macos/build-libusb.sh <install-prefix> [version]
#
# Environment: MACOSX_DEPLOYMENT_TARGET (default 13.0), ARCHS (default "arm64 x86_64").
# Afterwards: PKG_CONFIG_PATH=<prefix>/lib/pkgconfig cmake ...
set -eu
prefix=${1:?usage: build-libusb.sh <install-prefix> [version]}
version=${2:-1.0.30}
: "${MACOSX_DEPLOYMENT_TARGET:=13.0}"
: "${ARCHS:=arm64 x86_64}"
export MACOSX_DEPLOYMENT_TARGET

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fsSL -o "$work/libusb.tar.bz2" \
    "https://github.com/libusb/libusb/releases/download/v$version/libusb-$version.tar.bz2"
tar -xjf "$work/libusb.tar.bz2" -C "$work"
cd "$work/libusb-$version"

arch_flags=
for a in $ARCHS; do arch_flags="$arch_flags -arch $a"; done
# Universal build in one pass (clang accepts several -arch; libusb has no arch-specific code
# paths on macOS). --disable-dependency-tracking is required with multiple -arch flags.
./configure --prefix="$prefix" --disable-static --disable-dependency-tracking \
    CFLAGS="-O2 $arch_flags -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" \
    LDFLAGS="$arch_flags -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
make -j"$(sysctl -n hw.ncpu)"
make install
lipo -info "$prefix/lib/libusb-1.0.0.dylib"
