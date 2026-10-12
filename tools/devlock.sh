#!/bin/sh
# Serialise use of the single physical myDAQ between test runs and scripts.
#
#   tools/devlock.sh vm OWNER -- CMD...       run CMD holding the lock, device attached to the NI VM
#                                             (use for captures in the VM)
#   tools/devlock.sh native OWNER -- CMD...   run CMD holding the lock, device detached from the VM so
#                                             libusb on the Mac can open it (waits up to 15 s, DEVLOCK_USB_WAIT,
#                                             until libusb can claim it); re-attached afterwards
#   tools/devlock.sh status                   who holds the lock
#   tools/devlock.sh session start|end        manual session (hands-on work at the bench): while the file
#                                             .devlock-session exists, only the owners "martin" and
#                                             "usb-manual" (or a run with DEVLOCK_MANUAL=1, e.g. a
#                                             hw ctest) may take the lock; everyone else is refused
#
# The lock is the directory .devlock/ in the repo (atomic mkdir); a lock whose holder process is gone
# is broken automatically. Keep each hold short (one capture / one test run) so others can interleave.
set -eu
R="$(cd "$(dirname "$0")/.." && pwd)"
L="$R/.devlock"
S="$R/.devlock-session"
V="${MYDAQ_VM:-$HOME/projects/mydaq-mac/scripts/mydaq-vm}"

holder() { cat "$L/owner" 2>/dev/null || echo "?"; }
vm_running() { "$V" status 2>/dev/null | grep -q '^running'; }
attached() { "$V" mon "info usb" 2>/dev/null | grep -q 'ID: mydaq'; }
present_in_windows() {
    "$V" ssh '@(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like "USB\VID_3923*" -and $_.Status -eq "OK" }).Count' 2>/dev/null </dev/null | tr -d '\r '
}

# True once libusb on the Mac can take the myDAQ (VM released it): open, configure, claim
# interface 0, release (what libmydaq's open does). Without python3/libusb: not decidable (rc 2).
usb_free() {
    command -v python3 >/dev/null 2>&1 || return 2
    python3 - <<'PY'
import ctypes, ctypes.util, os, sys
name = os.environ.get("MYDAQ_LIBUSB") or ctypes.util.find_library("usb-1.0") or "/opt/homebrew/lib/libusb-1.0.dylib"
try:
    u = ctypes.CDLL(name)
except OSError:
    sys.exit(2)
P, I, VP = ctypes.POINTER, ctypes.c_int, ctypes.c_void_p
for fn, args, res in (("libusb_init", [P(VP)], I), ("libusb_exit", [VP], None),
                      ("libusb_get_device_list", [VP, P(P(VP))], ctypes.c_ssize_t),
                      ("libusb_free_device_list", [P(VP), I], None),
                      ("libusb_get_device_descriptor", [VP, ctypes.c_char_p], I),
                      ("libusb_open", [VP, P(VP)], I), ("libusb_close", [VP], None),
                      ("libusb_get_configuration", [VP, P(I)], I),
                      ("libusb_set_configuration", [VP, I], I),
                      ("libusb_claim_interface", [VP, I], I), ("libusb_release_interface", [VP, I], I)):
    f = getattr(u, fn)
    f.argtypes, f.restype = args, res
ctx = ctypes.c_void_p()
if u.libusb_init(ctypes.byref(ctx)) != 0:
    sys.exit(2)
lst = ctypes.POINTER(ctypes.c_void_p)()
n = u.libusb_get_device_list(ctx, ctypes.byref(lst))
ok = False
desc = ctypes.create_string_buffer(18)
for i in range(max(n, 0)):
    u.libusb_get_device_descriptor(lst[i], desc)
    if int.from_bytes(desc.raw[8:10], "little") != 0x3923:   # idVendor
        continue
    h = ctypes.c_void_p()
    if u.libusb_open(lst[i], ctypes.byref(h)) != 0:
        continue
    cfg = ctypes.c_int()
    good = u.libusb_get_configuration(h, ctypes.byref(cfg)) == 0
    if good and cfg.value != 1:
        good = u.libusb_set_configuration(h, 1) == 0
    if good and u.libusb_claim_interface(h, 0) == 0:
        u.libusb_release_interface(h, 0)
        ok = True
    u.libusb_close(h)
    if ok:
        break
u.libusb_free_device_list(lst, 1)
u.libusb_exit(ctx)
sys.exit(0 if ok else 1)
PY
}

# Waits (default 15 s, DEVLOCK_USB_WAIT) until the myDAQ is usable from the Mac after the VM let go.
wait_usb_free() {
    limit=${DEVLOCK_USB_WAIT:-15}; n=0; rc=1
    while [ $((n * 5)) -lt $((limit * 10)) ]; do
        if usb_free; then rc=0; else rc=$?; fi
        [ $rc -eq 0 ] && return 0
        [ $rc -eq 2 ] && { sleep 3; return 0; }   # cannot probe: the old fixed wait
        sleep 0.5; n=$((n + 1))
    done
    echo "devlock: myDAQ still not usable from the Mac after ${limit}s; running anyway" >&2
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
        if [ -d "$L" ]; then echo "held by $(holder) since $(cat "$L/since" 2>/dev/null)"; else echo "free"; fi
        [ -f "$S" ] && echo "manual session in progress since $(cat "$S")"; exit 0 ;;
    session)
        case "${2:-}" in
            start) date '+%H:%M:%S' > "$S"; echo "manual session started: other owners are refused" ;;
            end) rm -f "$S"; echo "manual session ended" ;;
            *) echo "usage: $0 session start|end" >&2; exit 1 ;;
        esac ;;
    vm|native)
        mode=$1; owner=${2:?owner}; shift 2; [ "${1:-}" = "--" ] && shift
        if [ -f "$S" ] && [ "$owner" != martin ] && [ "$owner" != usb-manual ] && [ "${DEVLOCK_MANUAL:-}" != 1 ]; then
            echo "devlock: manual session in progress (since $(cat "$S")): $owner refused" >&2; exit 75
        fi
        acquire "$owner ($mode)"
        if [ "$mode" = native ]; then
            if vm_running && attached; then "$V" mon "device_del mydaq" >/dev/null; wait_usb_free; fi
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
    *) sed -n '2,17p' "$0"; exit 1 ;;
esac
