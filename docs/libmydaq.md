# libmydaq — native NI myDAQ driver library

`libmydaq/` is MYBENCH's driver for the NI myDAQ (USB `3923:755b`): plain **C11**, depends only
on **libusb-1.0**, no global state (a context object carries everything), so the same library can
back MYBENCH's C++ `mydaq::Device` (`core/src/usb/`) and, later, a libsigrok/PulseView driver.
The protocol it speaks is described in `docs/protocol.md` (captures) and `docs/driver-notes.md`
(static analysis of NI's driver); everything here was verified against captures of NI-DAQmx and
on the real device.

## Layout

| Path | What |
|---|---|
| `include/libmydaq/mydaq.h` | public API (documented in the header) |
| `include/libmydaq/mydaq_transport.h` | transport vtable (for tests / other USB stacks) |
| `src/platform.h`, `platform_posix.c`, `platform_win32.c` | mutex, condition variable, thread, monotonic clock |
| `src/frame.[ch]`, `src/proto.h` | frame encoding/parsing, object/method constants |
| `src/command.c` | command channel (one request, one reply), one-way frames, event thread |
| `src/context.c`, `src/error.c` | context, logging, error strings |
| `src/usb_transport.c` | libusb transport: enumeration, open/claim, sync + async (streaming) transfers |
| `src/device.c` | open/close/reset, init sequence (with hwverify retry), calibration memory read |
| `src/calibration.c` | calibration memory parser (directory, CRC-16, all tables), AI/AO conversions |
| `src/ai.c`, `src/ai_task.c` | on-demand AI, scan list, sample clock; hardware-timed AI (finite, continuous) |
| `src/ao.c`, `src/dio.c` | static AO, static DIO |
| `src/dmm.c`, `src/es51981.c` | DMM ranges, reading, NI-exact conversion; ES51981 packet parser |
| `tests/` | unit, replay and emulator tests (`libmydaq_tests`), hardware test (`libmydaq_hwtest`) |
| `tests/mock/` | mock transport: capture replay + myDAQ emulator (also used by the C++ backend tests) |
| `tests/data/` | replay scripts generated from `captures/` (`tools/pcap2mock.py`) + calibration dump |
| `cli/mydaq_cli.c` | `mydaq-cli`, manual testing tool |

CMake target `mydaq` (static library); `add_subdirectory(libmydaq)` or build it standalone
(`cmake -S libmydaq -B build`). Compiled warning-free with `-Wall -Wextra -Wpedantic -Wshadow
-Wstrict-prototypes -Wmissing-prototypes -Wformat=2` (the library also with `-Wconversion
-Wsign-conversion`) and `-ffp-contract=off` (see Calibration). No POSIX or Win32 call outside
`platform_*.c`.

## API overview

```c
#include <libmydaq/mydaq.h>

mydaq_context *ctx;  mydaq_device *dev;
mydaq_context_new(&ctx);                       /* or mydaq_context_new_with_libusb(usb, &ctx) */
mydaq_context_set_log(ctx, MYDAQ_LOG_INFO, my_log_fn, user);   /* NULL fn = stderr */

mydaq_device_desc list[8]; size_t n;
mydaq_enumerate(ctx, list, 8, &n, 0);          /* never opens a device (flag READ_SERIAL does) */
int rc = mydaq_open(ctx, NULL, 0, &dev);       /* first myDAQ; init ~2 s; or by serial/location/handle */
if (rc != MYDAQ_OK) fprintf(stderr, "%s\n", mydaq_strerror(rc));

/* on demand */
mydaq_ai_channel_config ch[2] = {{MYDAQ_AI0, MYDAQ_AI_RANGE_10V}, {MYDAQ_AI1, MYDAQ_AI_RANGE_2V}};
double v[2];
mydaq_ai_read_scan(dev, ch, 2, v);
mydaq_ao_write(dev, MYDAQ_AO0, 0.5);
mydaq_dio_write(dev, 0x55, 0xff);  mydaq_dio_set_direction(dev, 0xff, 1);
mydaq_dmm_reading r;  mydaq_dmm_read(dev, MYDAQ_DMM_DC_VOLTS, 20.0, &r);

/* hardware timed */
mydaq_ai_config cfg = {0};
cfg.channels[0] = ch[0]; cfg.channels[1] = ch[1]; cfg.num_channels = 2;
cfg.sample_rate = 100e3; cfg.mode = MYDAQ_CONTINUOUS; cfg.samples_per_channel = 10000;
mydaq_ai_task *task;
mydaq_ai_task_create(dev, &cfg, &task);        /* reserves AI */
mydaq_ai_task_start(task);
double *buf = malloc(sizeof *buf * 2 * 10000);
while (running)
    mydaq_ai_task_read(task, buf, 10000, MYDAQ_GROUP_BY_CHANNEL, 1000);   /* blocks, volts */
mydaq_ai_task_destroy(task);
mydaq_close(dev);  mydaq_context_free(ctx);
```

Other entry points: `mydaq_get_info` (serial, location, identity words, init time),
`mydaq_get_calibration` / `mydaq_parse_calibration` (pure) / `mydaq_get_calibration_memory`,
`mydaq_ai_code_to_volts`, `mydaq_ao_volts_to_code`, `mydaq_ai_coerce_rate`,
`mydaq_ai_task_{available,remaining,is_running,rate,channel_skew,read_raw,stop}`,
`mydaq_dmm_ranges`, `mydaq_es51981_parse`, `mydaq_dmm_convert`, `mydaq_dmm_cal_slot`,
`mydaq_reset`, `mydaq_is_connected`, `mydaq_last_device_status`, `mydaq_open_transport`.

**Errors**: functions return `MYDAQ_OK` (0) or a negative `mydaq_error`: `INVALID_ARG`,
`NO_MEM`, `NOT_FOUND`, `NO_DEVICE` (unplugged; sticky per handle), `ACCESS`, `BUSY` (another
program holds the device, or the subsystem is reserved), `TIMEOUT`, `OVERFLOW`, `UNDERFLOW`,
`USB`, `PROTOCOL`, `DEVICE` (non-zero device status, see `mydaq_last_device_status`),
`NOT_SUPPORTED` (protocol not decoded yet), `STATE`, `CALIBRATION`. `mydaq_strerror()` and
`mydaq_error_name()` give text; details go to the log handler.

## Threading rules

- No globals: independent contexts can live in one process (e.g. MYBENCH and a sigrok plugin).
- A **context** is thread-safe. A **device** is thread-safe: every call takes the device's
  command lock for its whole command sequence, because the command channel is strictly one
  request / one reply and the device's objects are stateful. A DMM reading holds it 1–3 s, so
  other calls on the same device wait meanwhile; running acquisitions are unaffected (samples
  flow on EP 0x84, and the continuous read-position reports are one-way frames sent without the
  command lock).
- An **AI task** is read from one thread at a time; `mydaq_ai_task_stop()` and
  `mydaq_ai_task_available()` may be called from any thread (stop wakes a blocked read, which
  then returns `MYDAQ_ERR_STATE`).
- While a task runs, the device has an internal event thread calling the transport's
  `handle_events()`; stream callbacks and log callbacks may run on it. Stream callbacks never
  send commands (libusb forbids synchronous transfers inside callbacks); the hardware cleanup of
  a finished acquisition runs in the reader's thread.
- Destroy tasks before `mydaq_close()` (it destroys leftovers). `mydaq_close()` /
  `mydaq_context_free()` must not race with other calls on the same object.
- Lock order inside the library: task `op_lock` → device `cmd_lock` → `out_lock`; task `lock` is
  a leaf (the only lock stream callbacks take); streams are never closed while it is held.

## Internals worth knowing

- **Init** (`device.c`): the verbatim replay of NI's init (protocol.md §3), except that
  `hwverify.verify` (05 06 m08) is repeated every 50 ms for up to 10 s while it answers -225270
  (device still starting, driver-notes 1.1). Stale frames on 0x81/0x82 are drained first. The
  replay tests prove the sequence is byte-identical to NI's.
- **Calibration** (`calibration.c`): directory, sections, CRC-16 check words (poly 0x8005, init
  0xffff, zero byte after each content byte) of the directory and all five sections, u64
  LabVIEW-epoch timestamps, temperature word, AI table (7 ranges), 4 AO blocks (ao0, ao1, audio
  out L/R nominal) × 4 ranges, 28 DMM slots, user info string, backup-copy comparison.
  Conversions are evaluated in double from the float32 constants in the same operation order as
  NI; FMA contraction is disabled (`#pragma STDC FP_CONTRACT OFF`, `-ffp-contract=off`) because a
  fused multiply-add rounds differently and would break bit-exactness.
- **Timed AI** (`ai_task.c`): scan list → endpoint configure → route reset → in_timing
  configure (convert ticks = min(1500 | 1250, divisor / channels), divisor = 100 MHz / rate,
  N or 0 for continuous, captured flag bytes for finite/continuous) → in_fifo configure (host
  buffer bytes) → endpoint start → stream credit on slot 7 (exact bytes, or 0x7fffffff) → arm,
  start. Samples (LE i16, interleaved by scan) arrive on EP 0x84 into a byte ring through
  asynchronous transfers (up to 8 × ≤32 KiB, sized for ~20 ms each; a finite acquisition requests
  exactly the bytes still expected, so no transfer waits for data that never comes). Event frames
  on EP 0x82 are watched: in_fifo 2 → `OVERFLOW`, in_timing 3/7/8 and in_fifo 3 → `DEVICE`,
  in_timing 5 while a continuous task runs → `DEVICE`. A full host ring → `OVERFLOW`. Continuous
  tasks send the one-way `stream.read_pos` report at start and after each host buffer consumed
  (value 0x7fffffff − (consumed rounded to buffers + one buffer), NI's cadence). Stop/finish:
  endpoint stop, status, disarm, front-end release, unconfigure, endpoint flush (answers
  -225229, as for NI), then the host transfers are cancelled and EP 0x84 drained.
- **Host buffer**: finite = the whole acquisition; continuous = `buffer_samples` if given, else
  the device is told `samples_per_channel` (as DAQmx) and the host keeps at least 2 s of data.
- **DMM**: `set_mode(mode, 0)` then `read(1, 10000 ms)`; the 11-byte ES51981 packet is converted
  exactly like NI's driver (digit arithmetic order, scale table per function byte and range
  digit, calibration slot per requested function); overload → `overload = ±1`, `value = ±range`.

## Transport, mock and how to add a subsystem

The library reaches the device only through `mydaq_transport_ops` (write, read, async stream,
handle_events, close). `tests/mock/` implements it twice:

- **Replay** (`mydaq_mock_new_replay`): a script converted from a capture by
  `tools/pcap2mock.py captures/X.pcap` (`> command`, `< reply`, `e event`, `d samples`,
  `= DAQmx values`). Every command the library sends must equal the next captured one byte for
  byte (one-way read-position reports are matched loosely: their timing depends on the reader);
  replies/events/samples come from the capture; tests compare results with DAQmx's values.
- **Emulator** (`mydaq_mock_new_emulator_file`): a behavioural myDAQ (identity, calibration
  memory, on-demand/finite/continuous AI with configurable sample source and real-time pacing, AO,
  DIO, DMM packets, one-way frames) with fault injection (`mydaq_mock_unplug`,
  `mydaq_mock_inject_event`). Used for API, threading and error-path tests and by the C++ tests.

Adding a subsystem once the USB agent marks it READY in `docs/protocol.md`:

1. Capture it with NI-DAQmx (`tools/capture.sh`); convert: `tools/pcap2mock.py captures/X.pcap`
   (writes `libmydaq/tests/data/X.mock`).
2. Add the public API to `mydaq.h` (enums, config struct, functions, docs; return `mydaq_error`).
3. Implement it in a new `src/<subsystem>.c`: build bodies with `md_buf`/`md_put_*`, send with
   `md_cmd*` under `md_cmd_lock()` for the whole sequence; conversions as pure functions.
   Streaming: `ops->stream_open` + `md_pump_acquire` like `ai_task.c`.
4. Replay test in `tests/test_replay.c` (same operation as DAQmx did, compare values bit-exactly),
   emulator support + behaviour tests in `mock.c` / `test_emulator.c`, unit tests for the pure
   parts in `test_units.c`, a check in `tests/hw_test.c`.
5. Wire it into `core/src/usb/usb_device.cpp` (replace the `NotSupported`) and
   `tests/tst_usbdevice.cpp`; add a `mydaq-cli` command; update the status table below.

## C++ backend (`core/src/usb/`)

- `UsbDevice` implements `mydaq::Device` on a libmydaq handle (shared with its tasks, so a task
  may outlive the device object). Name `myDAQ (<serial>)`, id `usb:<serial>`. libmydaq errors
  map to `DeviceError`: `NO_DEVICE` → `NotConnected`, `BUSY` → `ResourceBusy`, `INVALID_ARG` →
  `InvalidConfig`, `TIMEOUT`, `OVERFLOW`, `UNDERFLOW`, `NOT_SUPPORTED` map by name, transport /
  protocol / device errors → `UsbError`. `UsbAiTask` wraps `mydaq_ai_task` (finite and
  continuous). Library logs go to the Qt category `mybench.libmydaq` (debug with
  `QT_LOGGING_RULES="mybench.*.debug=true"`, every frame with `MYBENCH_USB_TRACE=1`).
- `UsbProvider` (registered by `DeviceManager` when built with USB): a monitor thread enumerates
  the bus every second **without opening devices**; a newly attached myDAQ is opened (and
  initialised, ~2 s) after it has been present for 2 s (settling; avoids grabbing a device another
  program is about to open); `scan()` just returns the snapshot, so the UI thread never blocks.
  Devices that leave the bus, change USB address (re-plug between two polls) or report an unplug
  are marked gone (all calls throw `NotConnected`) and dropped. Busy devices (another program)
  are retried every 5 s, other failures every 10 s. `MYBENCH_NO_USB=1` disables the provider
  (the core unit tests set it so a `ctest` run never touches the shared hardware).
- Opening claims the interface before any other I/O, so retries against a device another
  program holds (e.g. the NI VM) send nothing to it. Still, on the shared development bench a
  running app would grab the myDAQ whenever devlock lends it to the Mac: run the app with
  `MYBENCH_NO_USB=1`, or through `tools/devlock.sh native <you> -- <app>`.

## Tests

```sh
cmake -S . -B build-usb -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DMYBENCH_INSTRUMENTS=none
ninja -C build-usb
ctest --test-dir build-usb -L libmydaq        # C tests: unit, replay, emulator (no hardware)
ctest --test-dir build-usb -R usbdevice       # C++ backend on the mock
build-usb/libmydaq/tests/libmydaq_tests replay.   # one group, or a single test by name prefix
MYDAQ_HW_TESTS=1 ctest --test-dir build-usb -L hw --output-on-failure   # real device, via devlock
```

Hardware tests (`libmydaq_hw`: C library, `usb_hw`: C++ backend through DeviceManager) are
skipped unless `MYDAQ_HW_TESTS=1`; they run through `tools/devlock.sh native libmydaq`, which
detaches the shared myDAQ from the NI VM for the duration of one run. They keep AO within ±1 V,
end with AO at 0 V and DIO as inputs, and treat the floating inputs statistically.
`MYDAQ_HW_ONLY=ai,dio` shortens the C run, `MYDAQ_HW_TRACE=1` logs every frame.
The C tests also pass under AddressSanitizer + UndefinedBehaviorSanitizer and ThreadSanitizer
(standalone build with `-fsanitize=...`).

`mydaq-cli` (built in `build-usb/libmydaq/cli/`), for manual tests — always through devlock on
the shared bench: `tools/devlock.sh native me -- build-usb/libmydaq/cli/mydaq-cli info`; commands
`list`, `info [--dump F]`, `ai [-c 0,1] [-r 10|2] [-n N]`, `ai-stream [-c ..] [--rate R] [-n N]
[--csv F]`, `ao -c 0|1 --volts V`, `dio read|write V [--mask M]|dir MASK in|out`,
`dmm [-f dcv] [-r RANGE] [-n N]`, `reset`; `-v`/`-vv` for debug/frame logs.

## Platforms

- macOS: the vendor-class device is left unconfigured by the OS; libmydaq selects configuration 1
  itself. No driver or entitlement needed.
- Linux: needs read/write access to the USB device, e.g. a udev rule
  `SUBSYSTEM=="usb", ATTR{idVendor}=="3923", ATTR{idProduct}=="755b", MODE="0660", TAG+="uaccess"`.
  Kernel drivers (none expected) are detached automatically.
- Windows: libusb needs the WinUSB driver bound to the device (e.g. with Zadig), which replaces
  NI's driver for that device. The platform layer uses Win32 threads/condition variables.
- The device must have NI's firmware (it enumerates as 755b only after NI's one-time update).

## Status per subsystem

"Replay" = byte-exact command sequence against NI's capture and bit-exact values against DAQmx's
output (mock transport, runs in every `ctest`). "HW" = verified natively on the real myDAQ
(036CD1C6) through devlock, 2026-09-28 (`libmydaq_hwtest`, `usb_hw_check`, `mydaq-cli`).

| Subsystem | libmydaq | Replay | HW |
|---|---|---|---|
| Enumeration, open/claim, init (+ hwverify retry), close | implemented | init byte-exact | yes: init 2.0 s, listed by the app as "myDAQ (036CD1C6)" 4.1 s after start |
| Reset | implemented | — | yes: 0.53 s, AO 0 V, DIO inputs |
| Calibration memory (directory, CRCs, all tables, backup) | implemented | — | yes: read = reference dump, all CRCs OK |
| AI on demand, 1–2 ch, ±10/±2 V | implemented | 4 captures, 13 values | yes: 41 ms/read |
| AI finite, 1–2 ch, mixed ranges, either order, ≤200 kS/s | implemented | 6 captures, 1,800 values | yes: 1 k–200 kS/s × 1–2 ch, abort + restart |
| AI continuous (credit, read-position reports, overflow events) | implemented | 1 capture, 30,000 values | yes: 1 k, 2×10 k, 2×100 k, 2×200 kS/s for 3 s without loss; 50 kS/s through the C++ backend |
| AI channel skew | implemented (`min(1500 \| 1250, divisor/n)` ticks) | via the configure frames | — |
| Audio in | NOT_SUPPORTED | captures exist, protocol not READY | — |
| AO static ao0/ao1 (±10 V cal) | implemented | 3 captures | yes: codes = DAQmx (output voltage not measured) |
| AO hardware timed, audio out | pending protocol | — | — |
| DIO static (direction, read, write, line subsets) | implemented | 6 captures | yes: 0x55/0xAA/0x0F/0xF0 read back |
| DMM DC V 60/20/2/0.2 V | implemented | 7 readings | yes (values = DAQmx) |
| DMM AC V 20/2/0.2 V | implemented | 6 readings | yes (2 V range) |
| DMM DC/AC current 1 A/200 mA/20 mA | implemented | 12 readings | no |
| DMM resistance 200 Ω, 2 kΩ | implemented | 4 readings (overload) | no |
| DMM resistance 20 kΩ–20 MΩ (modes 16–19 [H]) | implemented | — | no |
| DMM diode, continuity | NOT_SUPPORTED | not reachable via DAQmx | — |
| ES51981 parsing, overload with sign | implemented | overload packets from Ω captures | no provoked overload |
| Counter / FREQOUT / PFI | pending protocol (captures exist) | — | — |
| Power-fault event 0x0c91/0xdead | logged only | — | — |
| Abort of an in-flight command (vendor control) | not used | — | — |

### Hardware verifications still pending (need the device)

1. DMM DC/AC current on all three ranges, and resistance on all six ranges (modes 16–19 are
   [H]): `MYDAQ_HW_ONLY=dmmall MYDAQ_HW_TESTS=1 ctest --test-dir build-usb -R libmydaq_hw -V`
   (one reading each, ~30 s); ideally a known resistor/current for the value.
2. DMM overload with sign (e.g. 200 Ω range with open leads → `+OVERLOAD`).
3. A run of the updated build (quiet logging, claim-before-serial, DMM table, DIO direction sync
   at open) through `MYDAQ_HW_TESTS=1 ctest -L hw`.
4. Hot-plug in the app: unplug during an acquisition → `NotConnected`, device removed from the
   list; re-plug → listed again within ~5 s.
5. Continuous AI beyond the 2^31-byte initial credit (> 44 min at 2 × 200 kS/s): not tested.
6. AO voltages with a wired loopback (AO0 → AI0) to confirm the static AO levels.
