# MYBENCH — architecture

MYBENCH is an open-source, cross-platform (macOS, Linux, Windows) instrument suite for the
NI myDAQ that talks to the device directly over USB (libusb). No NI software is required. It
provides the same set of instruments as NI ELVISmx for the myDAQ, with similar layouts and the
same features (see "Instruments" below). The display name is kept in one place
(`MYBENCH_APP_NAME` in the top-level CMakeLists.txt).

Stack: C++20, Qt 6.8+ Quick/QML (see docs/UI.md for the UI design), CMake ≥ 3.21, libusb-1.0 (optional at
build time: without it only the simulated device exists). No other dependencies; DSP (FFT etc.)
is implemented in `core/`.

## Layout and ownership

| Path | What |
|---|---|
| `core/include/mydaq/device.h` | **Hardware API contract** (Device, AiTask, AoTask, DMM, DIO). Do not change without coordinating. |
| `core/include/mydaq/device_manager.h` | Device discovery across backends. |
| `core/include/mydaq/dsp/` | FFT, windows, spectra, measurements, waveform synthesis. |
| `core/src/sim/` | Simulated myDAQ (realistic, time-paced, AO→AI loopback wiring). |
| `libmydaq/` | **Protocol library in plain C (C11), depends only on libusb-1.0.** Open/init, AI/AO streaming, DMM, DIO, calibration. Reverse-engineered from USB captures (`docs/protocol.md`). Kept C and dependency-free so a libsigrok driver (PulseView) can reuse it. |
| `core/src/usb/` | C++ `mydaq::Device` backend wrapping `libmydaq`. |
| `ui/framework/` | QML module `MyBench.Framework` (C++): DeviceHub, InstrumentController + worker, ResourceArbiter, registry, sessions, export. |
| `ui/controls/` | QML module `MyBench.Controls`: design system (Theme, SiField, Knob, …). |
| `ui/plot/` | QML module `MyBench.Plot`: GPU scene-graph plotting. |
| `instruments/<id>/` | QML module per instrument: C++ controller + panel + dashboard tile + dev executable. |
| `app/` | `mybench`: the single-window application shell (nav, workspace, dashboard). |
| `tests/` | Qt Test unit tests. |
| `integrations/` | The myDAQ in other tools: libsigrok driver (PulseView, sigrok-cli), ngscopeclient bridge. |
| `tools/` | Python reverse-engineering tools (pcap decoding, replay). |
| `ni-local/` | **gitignored**: NI reference material (drivers, help text, screenshots). Never copy from it into the repo. |

Instruments (all ELVISmx myDAQ features, some merged, see docs/UI.md): `dmm`, `scope`, `gen`
(FGEN + ARB + Waveform Editor), `bode`, `dsa`, `dio` (Digital Reader + Writer).

## Rules

- Interoperability: learn the protocol from USB captures, from observing NI's software, and
  from studying NI's binaries — but never copy NI code, firmware, driver files, help text or UI
  artwork into the repo; write our own implementation. Our layouts may resemble ELVISmx (grouping, control
  names, behavior), but icons, images and help text are our own. The UI is a modernized single-window app (docs/UI.md).
- Every instrument must work fully against the simulated device, so it can be developed and
  tested without hardware.
- UI thread never blocks on the device. Acquisition/generation runs in a worker thread
  (see `ui/`), results are posted to the UI with queued signals.
- Device errors (`mydaq::DeviceError`) are shown in the instrument (status line / error dialog),
  stop the instrument cleanly, and never crash the app.

## Other tools: PulseView / sigrok, ngscopeclient (`integrations/`, docs/integrations.md)

Only one program can hold the myDAQ at a time (MYBENCH, PulseView/sigrok-cli or the bridge).

1. **libsigrok driver** `ni-mydaq` (`integrations/sigrok/`): a libsigrok hardware driver built on
   `libmydaq`, so PulseView and sigrok-cli use the myDAQ live: analog channels AI0/AI1 (or the
   audio inputs), sample rate, ±10 V / ±2 V range per channel. No logic channels: the myDAQ's DIO
   is software-timed and cannot share a sample clock with AI. Built out of tree: `apply.sh` adds
   it to a libsigrok checkout and installs libmydaq (static, `libmydaq.pc`) next to it. Hence
   `libmydaq` stays C, has no Qt/C++ dependency, is reentrant (context object, no globals) and
   needs a GPL-3-compatible license.
2. **ngscopeclient bridge** `mybench-scopehal-bridge` (`integrations/scopehal/`): a server that
   speaks the protocol of scopehal's `digilent` bridge driver (twinlan: commands on TCP 5025,
   waveforms on 5026), on top of `mydaq_core` (USB or simulated device), with a software edge
   trigger. Stock ngscopeclient builds connect to it unchanged.
3. **Session export** (planned): instruments that record data (Scope, DSA time data, Digital
   Reader) save sigrok session files (`.sr`) that PulseView opens directly.

## Device model (myDAQ)

- AI0/AI1: differential, ±10 V or ±2 V, 16-bit, up to 200 kS/s per channel.
- AudioIn L/R: ±2 V (AC coupled in hardware), shares the ADCs with AI0/AI1 (can't be mixed).
- AO0/AO1: ±10 V, 16-bit, up to 200 kS/s per channel. AudioOut L/R: ±2 V. One timed AO task.
- DMM: DC V (60 V, 20 V, 2 V, 200 mV), AC V (20 V, 2 V, 200 mV), DC/AC A (1 A, 200 mA, 20 mA),
  Ω (20 MΩ … 200 Ω), diode (2 V), continuity. Isolated; runs concurrently with AI/AO.
- DIO 0..7: static, per-line direction, software timed.
- +5 V, ±15 V supplies are fixed (not software controlled).
