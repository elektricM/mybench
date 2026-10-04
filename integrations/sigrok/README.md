# ni-mydaq: NI myDAQ driver for libsigrok (PulseView, sigrok-cli)

`ni-mydaq` is a libsigrok hardware driver that streams the myDAQ's analog inputs into
[PulseView](https://sigrok.org/wiki/PulseView) and `sigrok-cli`. It is built on **libmydaq**
(`libmydaq/`, this repository's USB driver library), so no NI software is involved.

The driver lives here, out of tree: `apply.sh` copies it into a libsigrok source tree, adds it to
the build files and installs that libsigrok together with libmydaq.

| Path | What |
|------|------|
| `ni-mydaq/api.c`, `protocol.c`, `protocol.h` | the driver, in libsigrok's driver layout (GPL-3.0-or-later, like libsigrok) |
| `apply.sh` | builds/installs libmydaq, patches and builds libsigrok |
| `CMakeLists.txt`, `capture_check.cmake` | ctest checks (`ctest -L sigrok`) |

## Build and install

Needs a libsigrok git checkout (master) that already builds, plus CMake, pkg-config, autotools and
libusb-1.0.

```sh
integrations/sigrok/apply.sh <libsigrok-src> <prefix>
# Martin's PulseView build:
integrations/sigrok/apply.sh ~/projects/pulseview-pico/src/libsigrok ~/projects/pulseview-pico/prefix
```

What it does (each step is skipped when already done, so it can be re-run at any time):

1. builds libmydaq standalone (`<libsigrok-src>/build/libmydaq`, Release, hidden symbols) and
   installs `lib/libmydaq.a`, `include/libmydaq/*.h` and `lib/pkgconfig/libmydaq.pc` into `<prefix>`;
2. copies `ni-mydaq/` to `<libsigrok-src>/src/hardware/ni-mydaq/`;
3. adds `SR_ARG_OPT_PKG([libmydaq], …)` and `SR_DRIVER([NI myDAQ], [ni-mydaq], [libmydaq libusb])`
   to `configure.ac` and the source block to `Makefile.am`;
4. runs `autogen.sh` (if the build files changed), `configure` with the flags of
   `pulseview-pico/build.sh` (in `<libsigrok-src>/build`), `make -j4` and `make install`;
5. checks that `<prefix>/bin/sigrok-cli -L` lists `ni-mydaq`.

`JOBS` sets the parallelism (default 4), `LIBMYDAQ_BUILD_DIR` the libmydaq build directory, and
`BACKUP_DIR` makes it copy the installed libsigrok libraries (`.dylib`/`.so`, `.la`, `.pc`) there
before the first install. Only libsigrok is rebuilt: sigrok-cli and PulseView load the new library
as they are. libmydaq is linked statically into libsigrok; the libsigrok ABI does not change.

Notes for the pulseview-pico tree:

- `./build.sh` (no argument) keeps the driver: the patched build files and `libmydaq.pc` stay in
  place. `libsigrok.pc` now lists `libmydaq` in `Requires.private`, so anything that runs
  `pkg-config libsigrok` needs `<prefix>/lib/pkgconfig` on `PKG_CONFIG_PATH` (build.sh sets it).
- `./build.sh update` runs `git pull --ff-only` in `src/libsigrok`; it stops if upstream changed
  `configure.ac` or `Makefile.am`, which apply.sh modified. Then:
  `git -C src/libsigrok checkout -- configure.ac Makefile.am`, `./build.sh update`, and re-run
  apply.sh. A pull that succeeds keeps the driver, but re-running apply.sh afterwards is harmless.
- To go back to the stock libsigrok: `git -C src/libsigrok checkout -- configure.ac Makefile.am`,
  `rm -r src/libsigrok/src/hardware/ni-mydaq`, then `./build.sh`.

## Use

### PulseView

*Connect to a Device* → driver **NI myDAQ (ni-mydaq)** → *Scan for devices using driver above* →
select **NI myDAQ** → OK. Or start it directly: `pulseview -d ni-mydaq`
(`/Applications/PulseView.app/Contents/MacOS/pulseview -d ni-mydaq` on Martin's Mac).

- Sample rate and sample count are in the toolbar. The *Configure Device* button has
  **Continuous**: when ticked, the acquisition runs until *Stop*, ignoring the sample count.
- The *Configure Channels* popup enables channels and sets each AI channel's range as **Volts/div**:
  `2 V` = ±10 V, `400 mV` = ±2 V.
- PulseView opens the device when it is selected (about 2 s: initialisation and calibration
  read) and keeps it until another device is selected or PulseView quits. At startup it reopens
  the device of the last session, or with no saved session the first hardware device its scan
  finds: with a myDAQ plugged in, starting PulseView takes the myDAQ (see the one-program limit
  below). `pulseview -c -d demo` starts without it (`-c`: do not restore sessions). If the myDAQ
  is busy, PulseView shows "Failed to open device" and carries on.

### sigrok-cli

```sh
SC=~/projects/pulseview-pico/prefix/bin/sigrok-cli
$SC --driver ni-mydaq --scan                    # list myDAQs (does not open them)
$SC -d ni-mydaq --show                          # options, rates, ranges
$SC -d ni-mydaq -C AI0,AI1 -c samplerate=10k --samples 1000 -o cap.sr     # open in PulseView
$SC -d ni-mydaq -C AI0,AI1 -c samplerate=200k -c channel_group=AI1:vdiv=400mV --time 2s -o cap.sr
$SC -d ni-mydaq -C AI0 -c samplerate=10 --continuous -O analog             # stream, any key stops
$SC -d ni-mydaq -C AudioInL,AudioInR -c samplerate=50k --samples 50000 -o audio.sr
$SC -d ni-mydaq:conn=036CD1C6 ...               # a given myDAQ (serial, USB location or bus.address)
$SC -i cap.sr -O analog                         # print a capture
```

`-O analog` prints every sample of every channel; libsigrok's `-O csv` works too but warns that
multi-channel analog data is untested.

## What the driver offers

| Key | Where | Values |
|-----|-------|--------|
| `samplerate` | device | 1 S/s … 200 kS/s per channel in 1-2-5 steps (default 100 kHz). Other values are coerced to the nearest step, except rates the 100 MHz sample clock makes exactly (e.g. 25 kHz), which are kept. |
| `limit_samples` | device | samples per channel, 0 = no limit |
| `limit_time` | device | ms, counted on the sample clock (rate × time samples), 0 = no limit |
| `continuous` | device | `true`: run until stopped, limits ignored |
| `vdiv` | channel group `AI0`, `AI1` | `2V` (±10 V, default) or `400mV` (±2 V) |
| `conn` | scan | USB location (`1-2.3`), `bus.address` (`1.5`) or serial number |

- **Channels**: `AI0`, `AI1` (enabled by default) and `AudioInL`, `AudioInR` (disabled by default;
  ±2 V, AC coupled). The audio inputs share the converters with AI0/AI1: enabling both kinds fails
  at start with an error (SR_ERR_ARG) asking for one kind only.
- **Data**: one `SR_DF_ANALOG` packet per channel and block, float volts (`SR_MQ_VOLTAGE`,
  `SR_UNIT_VOLT`, DC for AI0/AI1), calibrated with the device's own constants; 4 decimals on ±10 V
  (0.3 mV steps), 5 on ±2 V.
- **Device class**: Oscilloscope. PulseView only lists logic-analyzer and oscilloscope drivers, and
  libsigrok has no data-acquisition class; a hardware-timed analog stream is closest to a scope.
- **Range as volts/div**: PulseView shows channel-group options only for keys it knows; it shows
  `vdiv` but not `range`. The two ranges map to 10 divisions.
- **Acquisition**: a continuous, hardware-timed libmydaq AI task; a 20 ms session timer forwards
  whatever arrived, without blocking. A sample or time limit stops it exactly at the requested
  count. A lost stream (host too slow: overflow), an unplugged myDAQ or no data for several
  seconds ends the acquisition with an error message (`sr_err`) and the normal end of data.

## Limits

- **Analog input only.** No logic channels: the myDAQ's DIO lines are software-timed and cannot
  share a sample clock with the AI stream. No AO, DMM or counters.
- **No trigger** in this version: PulseView/sigrok-cli capture from the start.
- At most two channels at a time: AI0/AI1, or the two audio inputs.
- **One program holds the myDAQ at a time**: MYBENCH, the ngscopeclient bridge, PulseView or
  sigrok-cli. Opening it while another program has it fails with "in use by another program".
- Low rates have latency: the device sends data in bursts of at least 16 samples per channel
  (about 100 ms of data above 160 S/s), so at 1 S/s the first samples arrive after 16 s.
- Built and tested on macOS (Apple silicon). The driver and libmydaq are portable C; on Linux the
  user needs access to USB 3923:755b (udev rule).

## Troubleshooting

| Symptom | What to do |
|---------|------------|
| "in use by another program" when opening | Quit MYBENCH / the ngscopeclient bridge / another PulseView, or release the myDAQ from the VM. |
| PulseView starts with the myDAQ busy, or grabs it from you | Start it with `pulseview -c -d demo` (no session restore, demo device), then connect the myDAQ when needed. |
| Scan finds nothing | `ioreg -p IOUSB -l -w0 \| grep -i mydaq` (macOS) or `lsusb -d 3923:755b` (Linux): is it on the bus? |
| PulseView has no "NI myDAQ" driver | `sigrok-cli -L \| grep ni-mydaq` with the same prefix; `otool -L` (macOS) / `ldd` on pulseview must point to that prefix's `libsigrok`. |
| Acquisition stops with "samples were lost" | The host did not read fast enough: lower the rate or the number of channels. |
| Details | `sigrok-cli -l 5 …` or `pulseview -l 5`: the driver logs as `ni-mydaq:`, libmydaq as `libmydaq:`. |
| apply.sh: "configure did not enable ni-mydaq" | `pkg-config --modversion libmydaq` must work with `<prefix>/lib/pkgconfig` on `PKG_CONFIG_PATH`; see `config.log`. |
| A broken libsigrok after apply.sh | Restore the `BACKUP_DIR` copy into `<prefix>/lib` (and `lib/pkgconfig`). |

## Tests

`CMakeLists.txt` adds ctest entries when the configured `MYBENCH_SIGROK_CLI` (default
`~/projects/pulseview-pico/prefix/bin/sigrok-cli`) has the driver:

- `sigrok_ni_mydaq_listed`, `sigrok_ni_mydaq_scan`: the driver is registered; a scan exits cleanly.
- `sigrok_capture_check_demo`: the capture checker on libsigrok's demo device.
- `sigrok_ni_mydaq_hw`, `sigrok_ni_mydaq_hw_range` (label `hw`, need `MYDAQ_HW_TESTS=1`, run through
  `tools/devlock.sh`): capture AI0+AI1 to a `.sr` file (10 kS/s × 1000; 200 kS/s × 100000 with AI1
  on ±2 V), read it back and check the sample count and that every value is within range.

```sh
ctest --test-dir build-<name> -L sigrok -LE hw
MYDAQ_HW_TESTS=1 ctest --test-dir build-<name> -L sigrok --output-on-failure
```
