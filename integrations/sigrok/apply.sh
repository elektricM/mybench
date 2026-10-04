#!/usr/bin/env bash
# Adds the ni-mydaq driver (NI myDAQ through libmydaq) to a libsigrok source tree, then builds
# and installs libmydaq and libsigrok into PREFIX. Safe to re-run: every step checks first.
#
#   integrations/sigrok/apply.sh <libsigrok-src> <prefix>
#   e.g. integrations/sigrok/apply.sh ~/projects/pulseview-pico/src/libsigrok ~/projects/pulseview-pico/prefix
#
# Environment:
#   JOBS                 parallel build jobs (default 4)
#   LIBMYDAQ_BUILD_DIR   libmydaq build directory (default <libsigrok-src>/build/libmydaq)
#   BACKUP_DIR           if set, the installed libsigrok libraries (.dylib/.so, .la, .pc) are copied
#                        there before the first install (an existing backup is kept)
#
# Uses the same environment and configure flags as pulseview-pico/build.sh (Homebrew on macOS).
# Only libsigrok is rebuilt: sigrok-cli and PulseView load the new library without a rebuild.
set -euo pipefail

die() { echo "apply.sh: $*" >&2; exit 1; }
[ $# -eq 2 ] || die "usage: $0 <libsigrok-src> <prefix>"
[ -f "$1/configure.ac" ] && grep -q '^SR_DRIVER(' "$1/configure.ac" || die "$1 is not a libsigrok source tree"

SRC="$(cd "$1" && pwd)"
mkdir -p "$2"
PREFIX="$(cd "$2" && pwd)"
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
JOBS="${JOBS:-4}"
BUILD_MYDAQ="${LIBMYDAQ_BUILD_DIR:-$SRC/build/libmydaq}"
CONFIGURE_FLAGS=(--prefix="$PREFIX" --disable-static --enable-cxx --disable-python --disable-ruby --disable-java)

# --- environment (as pulseview-pico/build.sh) -----------------------------------------------
if command -v brew >/dev/null 2>&1; then
    BREW="$(brew --prefix)"
    PY=python@3.12
    # Xcode licence shims make cc/make exit 69 silently otherwise.
    [ -d /Library/Developer/CommandLineTools ] && export DEVELOPER_DIR=/Library/Developer/CommandLineTools
    export PATH="$PREFIX/bin:$BREW/opt/$PY/bin:$BREW/opt/libtool/libexec/gnubin:$PATH"
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$BREW/opt/$PY/lib/pkgconfig:$BREW/opt/libzip/lib/pkgconfig:$BREW/opt/glibmm/lib/pkgconfig"
    export CPPFLAGS="-I$BREW/include"
    export LDFLAGS="-L$BREW/lib"
else
    export PATH="$PREFIX/bin:$PATH"
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi

# --- 1. libmydaq ------------------------------------------------------------------------------
echo "=== libmydaq -> $PREFIX"
GEN=()
if [ ! -f "$BUILD_MYDAQ/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi
# Hidden visibility: libmydaq's symbols stay inside libsigrok instead of being exported by it.
cmake -S "$REPO/libmydaq" -B "$BUILD_MYDAQ" ${GEN[@]+"${GEN[@]}"} -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -DLIBMYDAQ_INSTALL=ON -DLIBMYDAQ_BUILD_TESTS=OFF \
    -DLIBMYDAQ_BUILD_CLI=OFF -DCMAKE_C_VISIBILITY_PRESET=hidden >/dev/null
cmake --build "$BUILD_MYDAQ" -j "$JOBS"
cmake --install "$BUILD_MYDAQ"
pkg-config --exists libmydaq || die "pkg-config does not find libmydaq in $PREFIX/lib/pkgconfig"

# --- 2. driver sources ------------------------------------------------------------------------
echo "=== driver -> $SRC/src/hardware/ni-mydaq"
mkdir -p "$SRC/src/hardware/ni-mydaq"
for f in api.c protocol.c protocol.h; do
    # copy only when different, so an unchanged driver is not recompiled
    cmp -s "$HERE/ni-mydaq/$f" "$SRC/src/hardware/ni-mydaq/$f" || cp "$HERE/ni-mydaq/$f" "$SRC/src/hardware/ni-mydaq/$f"
done

# --- 3. build files (each insertion once) -----------------------------------------------------
cd "$SRC"
if ! grep -q '^SR_ARG_OPT_PKG(\[libmydaq\]' configure.ac; then
    echo "configure.ac: libmydaq dependency"
    perl -0777 -pi -e 's/^(SR_ARG_OPT_PKG\(\[libnettle\].*\n)/$1SR_ARG_OPT_PKG([libmydaq], [LIBMYDAQ], , [libmydaq >= 0.1.0])\n/m
        or die "apply.sh: no SR_ARG_OPT_PKG([libnettle] line to insert after\n"' configure.ac
fi
if ! grep -q '\[ni-mydaq\]' configure.ac; then
    echo "configure.ac: SR_DRIVER ni-mydaq"
    # before the first driver whose name sorts after ni-mydaq; libusb is linked by libsigrok itself
    perl -0777 -pi -e 'our $n = 0;
        s/^(SR_DRIVER\(\[[^\]]*\],\s*\[([^\]]*)\].*\n)/($2 gt "ni-mydaq" && !$n++) ? "SR_DRIVER([NI myDAQ], [ni-mydaq], [libmydaq libusb])\n$1" : $1/gme;
        $n or die "apply.sh: no SR_DRIVER line to insert before\n"' configure.ac
fi
if ! grep -q '^if HW_NI_MYDAQ$' Makefile.am; then
    echo "Makefile.am: ni-mydaq sources"
    perl -0777 -pi -e 'our $n = 0;
        my $b = "if HW_NI_MYDAQ\nsrc_libdrivers_la_SOURCES += \\\n\tsrc/hardware/ni-mydaq/protocol.h \\\n\tsrc/hardware/ni-mydaq/protocol.c \\\n\tsrc/hardware/ni-mydaq/api.c\nendif\n";
        s/^(if HW_([A-Z0-9_]+)\n)/($2 gt "NI_MYDAQ" && !$n++) ? "$b$1" : $1/gme;
        $n or die "apply.sh: no driver block to insert before\n"' Makefile.am
fi

# --- 4. autogen, configure, build, install ----------------------------------------------------
if [ ! -x configure ] || ! grep -q 'enable-ni-mydaq' configure || ! grep -q 'HW_NI_MYDAQ' Makefile.in \
        || [ configure.ac -nt configure ] || [ Makefile.am -nt Makefile.in ]; then
    echo "=== autogen"
    ./autogen.sh >/dev/null
fi
mkdir -p build
cd build
if [ ! -f config.status ] || [ ../configure -nt config.status ] || ! grep -q 'define HAVE_HW_NI_MYDAQ 1' config.h 2>/dev/null; then
    echo "=== configure ${CONFIGURE_FLAGS[*]}"
    ../configure "${CONFIGURE_FLAGS[@]}" >/dev/null
fi
grep -q 'define HAVE_HW_NI_MYDAQ 1' config.h \
    || die "configure did not enable ni-mydaq (see 'ni-mydaq' and 'libmydaq' in $SRC/build/config.log)"
# make does not know that libsigrok links libmydaq.a: relink when the installed library changed
if [ -f libsigrok.la ] && [ "$PREFIX/lib/libmydaq.a" -nt libsigrok.la ]; then rm -f libsigrok.la; fi
echo "=== make -j$JOBS"
make -j"$JOBS"

if [ -n "${BACKUP_DIR:-}" ] && [ ! -e "$BACKUP_DIR/.complete" ]; then
    echo "=== backup of the installed libsigrok -> $BACKUP_DIR"
    mkdir -p "$BACKUP_DIR"
    for f in "$PREFIX"/lib/libsigrok.* "$PREFIX"/lib/libsigrokcxx.* \
             "$PREFIX"/lib/pkgconfig/libsigrok.pc "$PREFIX"/lib/pkgconfig/libsigrokcxx.pc; do
        [ -e "$f" ] || [ -L "$f" ] || continue
        cp -a "$f" "$BACKUP_DIR/"
    done
    touch "$BACKUP_DIR/.complete"
fi

echo "=== make install"
make install >/dev/null

if [ -x "$PREFIX/bin/sigrok-cli" ]; then
    drivers="$("$PREFIX/bin/sigrok-cli" -L 2>/dev/null || true)"   # no pipe: grep -q + pipefail
    if grep -q '^  ni-mydaq ' <<<"$drivers"; then
        echo "done: sigrok-cli lists ni-mydaq ($PREFIX/bin/sigrok-cli -L)"
    else
        die "installed, but $PREFIX/bin/sigrok-cli -L does not list ni-mydaq"
    fi
else
    echo "done (no sigrok-cli in $PREFIX/bin to check with)"
fi
