#!/bin/sh
# Serialise use of the single physical myDAQ between agents/scripts.
#
#   tools/devlock.sh vm OWNER -- CMD...       run CMD holding the lock, device attached to the NI VM
#                                             (use for captures: capture.sh, DAQmx scripts)
#   tools/devlock.sh native OWNER -- CMD...   run CMD holding the lock, device detached from the VM so
#                                             libusb on the Mac can open it; re-attached afterwards
#   tools/devlock.sh status                   who holds the lock
#
# The lock is the directory .devlock/ in the repo (atomic mkdir); a lock whose holder process is gone
# is broken automatically. Keep each hold short (one capture / one test run) so others can interleave.
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
L="$R/.devlock"
V="${MYDAQ_VM:-$HOME/projects/mydaq-mac/scripts/mydaq-vm}"

holder() { cat "$L/owner" 2>/dev/null || echo "?"; }
vm_running() { "$V" status 2>/dev/null | grep -q '^running'; }
attached() { "$V" mon "info usb" 2>/dev/null | grep -q 'ID: mydaq'; }
present_in_windows() {
    "$V" ssh '@(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like "USB\VID_3923*" -and $_.Status -eq "OK" }).Count' 2>/dev/null </dev/null | tr -d '\r '
}

acquire() {
    while ! mkdir "$L" 2>/dev/null; do
        pid=$(cat "$L/pid" 2>/dev/null || echo "")
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
            echo "devlock: breaking stale lock of $(holder)" >&2; rm -rf "$L"; continue
        fi
        sleep 2
    done
    echo "$1" > "$L/owner"; echo "$$" > "$L/pid"; date '+%H:%M:%S' > "$L/since"
    trap 'rm -rf "$L"' EXIT INT TERM
}

case "${1:-status}" in
    status)
        if [ -d "$L" ]; then echo "held by $(holder) since $(cat "$L/since" 2>/dev/null)"; else echo "free"; fi ;;
    vm|native)
        mode=$1; owner=${2:?owner}; shift 2; [ "${1:-}" = "--" ] && shift
        acquire "$owner ($mode)"
        if [ "$mode" = native ]; then
            if vm_running && attached; then "$V" mon "device_del mydaq" >/dev/null; sleep 3; fi
            set +e; "$@"; rc=$?; set -e
            if vm_running; then
                "$V" mon "device_add usb-host,bus=xhci.0,vendorid=0x3923,id=mydaq" >/dev/null
                n=0; until [ "$(present_in_windows)" = "1" ] || [ $n -ge 60 ]; do sleep 2; n=$((n + 1)); done
            fi
        else
            if vm_running && ! attached; then
                "$V" mon "device_add usb-host,bus=xhci.0,vendorid=0x3923,id=mydaq" >/dev/null
                n=0; until [ "$(present_in_windows)" = "1" ] || [ $n -ge 60 ]; do sleep 2; n=$((n + 1)); done
            fi
            set +e; "$@"; rc=$?; set -e
        fi
        exit $rc ;;
    *) sed -n '2,13p' "$0"; exit 1 ;;
esac
