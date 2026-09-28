#!/bin/sh
# Build MYBENCH-<version>-linux-<arch>.AppImage with linuxdeploy + linuxdeploy-plugin-qt.
# Run on the oldest distribution you want to support (CI: ubuntu-22.04 + Qt from aqtinstall),
# with Qt 6's bin/ (qmake/qtpaths) first in PATH or QMAKE set.
#
#   packaging/linux/appimage/build-appimage.sh [build-dir]
#
# The AppImage bundles Qt, the QML modules we import and libusb. It cannot install the udev
# rule: users copy it once (docs/INSTALL.md), it ships inside as usr/lib/udev/rules.d/.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../.." && pwd)
build=${1:-"$src/build-appimage"}
arch=$(uname -m)
tools="$build/appimage-tools"
appdir="$build/AppDir"

cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    ${QT_PREFIX:+-DCMAKE_PREFIX_PATH="$QT_PREFIX"}
cmake --build "$build" --target mybench
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build" --component app

mkdir -p "$tools"
for t in linuxdeploy linuxdeploy-plugin-qt; do
    if [ ! -x "$tools/$t-$arch.AppImage" ]; then
        curl -fsSL -o "$tools/$t-$arch.AppImage" \
            "https://github.com/linuxdeploy/$t/releases/download/continuous/$t-$arch.AppImage"
        chmod +x "$tools/$t-$arch.AppImage"
    fi
done

app_id=$(sed -n 's/^MYBENCH_APP_ID:STRING=//p' "$build/CMakeCache.txt")
app_name=$(sed -n 's/^MYBENCH_APP_NAME:STRING=//p' "$build/CMakeCache.txt")
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build/CMakeCache.txt")

# linuxdeploy-plugin-qt: scan the shipped QML sources for imports; our MyBench.* modules are
# linked statically, their build-tree qmldirs only resolve the imports during the scan.
export QML_SOURCES_PATHS="$src/app:$src/ui/controls:$src/instruments"
export QML_MODULES_PATHS="$build/qml"
export EXTRA_QT_MODULES="svg"
# Platform plugins besides xcb: offscreen (headless --screenshot) and whatever Wayland plugins
# this Qt has (libqwayland.so since Qt 6.10, libqwayland-{egl,generic}.so before).
qmake_bin=${QMAKE:-$(command -v qmake6 || command -v qmake)}
plugins_dir=$("$qmake_bin" -query QT_INSTALL_PLUGINS)
extra="libqoffscreen.so"
for p in "$plugins_dir"/platforms/libqwayland*.so; do
    [ -e "$p" ] && extra="$extra;$(basename "$p")"
done
export EXTRA_PLATFORM_PLUGINS="$extra"
export QMAKE="$qmake_bin"
export DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1
export APPIMAGE_EXTRACT_AND_RUN=1        # no FUSE on CI runners / in containers
export LDAI_OUTPUT="$build/$app_name-$version-linux-$arch.AppImage"
export OUTPUT="$LDAI_OUTPUT"

cd "$build"
# Pass 1: deploy (no output yet). Pass 2: remove the MyBench.* qmldirs that the plugin copied
# from QML_MODULES_PATHS (those modules are compiled into the executable) and make the AppImage.
"$tools/linuxdeploy-$arch.AppImage" --appdir "$appdir" \
    --executable "$appdir/usr/bin/mybench" \
    --desktop-file "$appdir/usr/share/applications/$app_id.desktop" \
    --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/$app_id.png" \
    --plugin qt
rm -rf "$appdir/usr/qml/MyBench"
"$tools/linuxdeploy-$arch.AppImage" --appdir "$appdir" --output appimage

# Smoke test: the AppImage starts and renders Home offscreen, simulator only.
QT_QPA_PLATFORM=offscreen MYBENCH_NO_USB=1 "$OUTPUT" --page home --screenshot "$build/appimage-home.png"
ls -lh "$OUTPUT" "$build/appimage-home.png"
