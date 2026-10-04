#!/usr/bin/env bash
# UI screenshot matrix (docs/UI.md): renders the app offscreen against the simulator for a set of
# pages x sizes x themes, for before/after review of UI changes. Never opens a USB device.
#
#   tools/ui-matrix.sh <build-dir> <out-dir> [quick|full]
#
# quick: Overview, the 9 instruments and a split, 1440x900 + 1024x700, light + dark.
# full:  adds 1280x800, 1100x720, 900x640, 640x480 and played platform palettes (KDE Breeze, macOS).
# Extra app flags for every shot: MATRIX_FLAGS="--no-animations ..."; parallel jobs: MATRIX_JOBS (3).
set -euo pipefail

build=${1:?build dir}
out=${2:?output dir}
mode=${3:-quick}

app=""
for candidate in "$build/app/MYBENCH.app/Contents/MacOS/MYBENCH" "$build/app/MYBENCH" "$build/app/mybench" \
                 "$build/app/MYBENCH.exe" "$build/app/mybench.exe"; do
    if [ -x "$candidate" ]; then app=$candidate; break; fi
done
[ -n "$app" ] || { echo "ui-matrix: no MYBENCH executable under $build/app" >&2; exit 1; }
mkdir -p "$out"

instruments="dmm scope gen bode dsa dio ctr log aeq"
sizes="1440x900 1024x700"
[ "$mode" = full ] && sizes="1440x900 1280x800 1100x720 1024x700 900x640 640x480"

# name|flags (the size and theme are appended)
pages() {
    echo "overview|--page home"
    for id in $instruments; do echo "$id|--panel $id"; done
    echo "split-scope-gen|--panel scope --split gen --run scope,gen"
    echo "run-scope|--panel scope --run scope"
    echo "run-dmm|--panel dmm --run dmm"
}

jobs_file=$(mktemp)
trap 'rm -f "$jobs_file"' EXIT
for size in $sizes; do
    for theme in light dark; do
        pages | while IFS='|' read -r name flags; do
            echo "$name-$size-$theme|$flags --size $size --theme $theme"
        done
    done
done >> "$jobs_file"
if [ "$mode" = full ]; then
    # played platform palettes (window, base, text, accent) at the default size
    for p in "breeze-light|#eff0f1,#fcfcfc,#232629,#3daee9|light" "breeze-dark|#31363b,#232629,#eff0f1,#3daee9|dark" \
             "macos-light|#ececec,#ffffff,#1d1d1f,#007aff|light" "macos-dark|#323232,#1e1e1e,#ffffff,#0a84ff|dark"; do
        IFS='|' read -r pname pvalue ptheme <<< "$p"
        pages | while IFS='|' read -r name flags; do
            echo "$name-1440x900-$pname|$flags --size 1440x900 --theme $ptheme --system-palette $pvalue"
        done
    done >> "$jobs_file"
fi

shot() {
    local line=$1 name flags
    name=${line%%|*}
    flags=${line#*|}
    # shellcheck disable=SC2086
    if MYBENCH_NO_USB=1 QT_QPA_PLATFORM=offscreen timeout 60 "$UI_MATRIX_APP" $flags $MATRIX_FLAGS \
           --delay 2000 --screenshot "$UI_MATRIX_OUT/$name.png" > "$UI_MATRIX_OUT/$name.log" 2>&1; then
        rm -f "$UI_MATRIX_OUT/$name.log"
        echo "ok   $name"
    else
        echo "FAIL $name (see $name.log)"
    fi
}
export -f shot
export UI_MATRIX_APP=$app UI_MATRIX_OUT=$out MATRIX_FLAGS=${MATRIX_FLAGS:-}

tr '\n' '\0' < "$jobs_file" | xargs -0 -n 1 -P "${MATRIX_JOBS:-3}" bash -c 'shot "$1"' _
echo "ui-matrix: $(ls "$out"/*.png 2>/dev/null | wc -l | tr -d ' ') screenshots in $out"
