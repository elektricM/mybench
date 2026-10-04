# mybench-scopehal-bridge — the myDAQ in ngscopeclient

`mybench-scopehal-bridge` makes the NI myDAQ's analog inputs usable as a two-channel oscilloscope
in [ngscopeclient](https://www.ngscopeclient.org/) (and other programs built on libscopehal).

ngscopeclient already ships a driver for "remote bridge" oscilloscopes: `digilent`, written for
the Digilent WaveForms bridge. That driver asks the server for its channel count, sample rates and
memory depths, so any server that speaks the same protocol works without changing ngscopeclient.
The bridge is such a server: it opens the myDAQ through MYBENCH's device layer (`mydaq_core`),
streams AI0/AI1 continuously while ngscopeclient is armed, finds the trigger in software and sends
the waveforms over TCP.

```
ngscopeclient ──TCP 5025 (commands)──▶ mybench-scopehal-bridge ──USB──▶ NI myDAQ (AI0, AI1)
              ◀─TCP 5026 (waveforms)──
```

## Build

The bridge is part of the MYBENCH CMake build (target `mybench-scopehal-bridge`, needs Qt Network,
which every Qt installation has). From the repository root:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build --target mybench-scopehal-bridge
# -> build/integrations/scopehal/mybench-scopehal-bridge
```

`-DMYBENCH_INTEGRATIONS=OFF` leaves it out. It is not installed with the MYBENCH app yet: run it
from the build directory.

## Run

```sh
mybench-scopehal-bridge            # first myDAQ found on USB, listens on 127.0.0.1:5025 and :5026
mybench-scopehal-bridge --sim      # simulated myDAQ (AI0: 1 kHz sine, AI1: 200 Hz square)
```

| Option | Meaning |
|---|---|
| `--sim` | Use the simulated myDAQ (no hardware needed, never touches USB). |
| `--serial S` | Use the myDAQ with serial number `S` (several connected). |
| `--bind ADDR` | Address to listen on. Default `127.0.0.1` (this computer only); `0.0.0.0` accepts other machines. There is no authentication: only open it on a trusted network. |
| `--port N` | Command port, default 5025 (`0`: any free port). |
| `--data-port N` | Waveform port, default command port + 1. |
| `--auto-trigger-ms N` | Server-side auto mode: in normal (Start) mode, send the latest record when no trigger came for N ms. Default 0 (off). |
| `-v`, `--verbose` | Log every command, reply and the frame rate. |
| `--version`, `--help` | |

At startup it waits up to 20 s for the myDAQ (opening one takes about 2 s), prints
`listening on 127.0.0.1:5025:5026`, and keeps the device open until it exits (Ctrl+C). The analog
input runs only while ngscopeclient is armed. Messages go to stderr.

## Connect from ngscopeclient

1. Start the bridge (above). Close MYBENCH first if it is using the same myDAQ.
2. In ngscopeclient: **Add → Oscilloscope → Connect...**
3. Fill in:
   - **Nickname**: anything, e.g. `mydaq`
   - **Driver**: `digilent`
   - **Transport**: `twinlan`
   - **Path**: `localhost:5025:5026` (host, command port, waveform port)
4. **Add**. Channel C1 (= AI0) appears; C2 (= AI1) can be shown from the channel list.
5. Arm the trigger with the toolbar: **Start** (normal), **Auto**, **Single** or **Force**.

The same from the command line:

```sh
ngscopeclient mydaq:digilent:twinlan:localhost:5025:5026
```

### What the controls do

| ngscopeclient | myDAQ |
|---|---|
| C1, C2 | AI0, AI1 (differential inputs). The audio inputs are not offered. |
| Vertical range / offset | Pick the input range: ±2 V when the displayed window lies within ±2 V, else ±10 V. Changing the range restarts the acquisition. |
| Probe attenuation | Samples are scaled by it (shown at the probe tip). |
| Coupling | DC only (AC is not offered by the driver for this model). |
| Sample rate | 200, 100, 50, 20, 10, 5, 2, 1 kS/s, 500 ... 10 S/s per channel (both channels always run). |
| Memory depth | 1 k ... 1 M samples per channel. |
| Trigger | Edge on C1 or C2, rising / falling / any, level, horizontal position (time from the start of the record to the trigger; negative values start the record after the trigger, down to ten records). |
| Start / Single / Force / Stop | Normal mode (a record per trigger) / one record / the latest record immediately / stop. ngscopeclient's **Auto** button is Start plus Force after 250 ms without a trigger. |

## How the trigger works

The myDAQ has no hardware trigger for its analog inputs, so the bridge streams AI0 and AI1
continuously while armed and searches the trigger channel for the edge (MYBENCH's
`dsp::findEdge`):

- **Hysteresis**: the signal must first go below *level − h* (rising) or above *level + h*
  (falling) to arm; *h* is 1 % of the trigger channel's displayed window, at least 1 mV. Noise
  smaller than that cannot retrigger. A signal that sits inside that band for longer than one
  record or 1 s (whichever is longer) has to re-arm.
- **Sub-sample position**: the crossing time is interpolated between the two samples around it
  and sent as the trigger phase, so the trigger point lands exactly at the requested position.
  AI1 is converted a few microseconds after AI0 within each scan; its phase includes that skew.
- **Pre-trigger** data comes from the stream history; a trigger whose pre-trigger samples were
  not acquired yet (just after arming) is skipped.
- **Normal mode** re-arms once the previous record has been handed to the network, so a slow
  client lowers the frame rate instead of receiving stale records.
- **No trigger, no picture**: in Start mode a flat or out-of-level signal produces no records.
  Use ngscopeclient's Auto or Force button, or start the bridge with `--auto-trigger-ms 250`.
  `--auto-trigger-ms` never applies to Single.

## Limits

- 2 channels (AI0, AI1), at most 200 kS/s per channel, ±2 V or ±10 V, DC coupled.
- Software trigger, edge only; no external trigger input.
- One program at a time owns the myDAQ: MYBENCH, PulseView (sigrok driver) or the bridge. The
  bridge keeps it from start to exit. One ngscopeclient connection at a time; others are refused.
- After Stop, a record that was already on its way can still appear when acquisition starts again.
- Analog outputs, DIO, counters and the DMM are not exposed.

## Protocol (for reference)

Commands are text lines on the command port. Only lines containing `?` get a reply (exactly one
line); unknown queries get an empty line, unknown commands are logged and ignored.

| Command | Reply / effect |
|---|---|
| `*IDN?` | `National Instruments,NI myDAQ,<serial>,MYBENCH-<version>` |
| `CHANS?` | `2` |
| `RATES?` | Sample periods in fs, each followed by a comma, fastest first (the client starts with the first) |
| `DEPTHS?` | Depths, each followed by a comma, smallest first |
| `ARMED?` | `1` while armed |
| `RATE n`, `DEPTH n` | Sample rate (Hz), depth (samples) |
| `START`, `SINGLE`, `FORCE`, `STOP` | Acquisition control |
| `TRIG:SOU Cn`, `TRIG:LEV v`, `TRIG:EDGE:DIR RISING\|FALLING\|ANY`, `TRIG:DELAY fs`, `TRIG:MODE EDGE` | Trigger |
| `:Cn:ON`, `:Cn:OFF`, `:Cn:RANGE v`, `:Cn:OFFS v`, `:Cn:ATTEN x`, `:Cn:COUP DC1M` | Channel (range = full span, offset = window centre, both at the input) |

Each record is one frame on the waveform port, little endian: `uint16` channel count, `int64`
fs per sample, then for every enabled channel `uint64` channel index (0 = C1), `uint64` depth,
`float` trigger phase (fs: sample *i* is at *i* × period + phase, the trigger at the requested
position), `double[depth]` volts.

## Tests

```sh
ctest --test-dir build -L scopehal                       # simulator, no hardware
MYDAQ_HW_TESTS=1 ctest --test-dir build -R scopehal_bridge_hw   # real myDAQ, AI only, nothing wired
```

`tst_scopehal_bridge` starts the bridge on free ports and drives it like the `digilent` driver:
identification, rate/depth lists, no replies to settings, single/normal/forced/auto records parsed
byte by byte, trigger position, two channels with attenuation, one client at a time, reconnect.
The hardware variant takes the device through `tools/devlock.sh native scopehal`.
