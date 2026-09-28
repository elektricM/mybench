#!/bin/sh
# Build a release MYBENCH.app and its DMG (macOS).
#
#   packaging/macos/build-dmg.sh [build-dir] [extra cmake args...]
#
# Environment:
#   QT_PREFIX          Qt 6 prefix (default: $(brew --prefix qt) if Homebrew is present)
#   CODESIGN_IDENTITY  "-" (ad-hoc, default) or a "Developer ID Application: ..." identity
#   MACOSX_DEPLOYMENT_TARGET  optional, e.g. 13.0 (Qt and libusb must support it too)
#
# Result: <build-dir>/package/MYBENCH-<version>-macOS-<arch>.dmg (+ .sha256), and the
# deployed bundle in <build-dir>/package/_CPack_Packages/.../MYBENCH.app.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
build=${1:-"$src/build-release"}
[ $# -gt 0 ] && shift

if [ -z "${QT_PREFIX:-}" ] && command -v brew >/dev/null 2>&1; then
    QT_PREFIX=$(brew --prefix qt)
fi
set -- -DCMAKE_BUILD_TYPE=Release "-DMYBENCH_CODESIGN_IDENTITY=${CODESIGN_IDENTITY:--}" "$@"
[ -n "${QT_PREFIX:-}" ] && set -- "-DCMAKE_PREFIX_PATH=$QT_PREFIX" "$@"
[ -n "${MACOSX_DEPLOYMENT_TARGET:-}" ] && set -- "-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET" "$@"

generator=
command -v ninja >/dev/null 2>&1 && generator="-GNinja"
cmake -S "$src" -B "$build" $generator "$@"
cmake --build "$build" --target mybench
(cd "$build" && cpack -C Release -B "$build/package")
ls -lh "$build"/package/*.dmg
