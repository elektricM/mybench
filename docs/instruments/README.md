# MYBENCH instrument specifications

This directory holds the developer specifications of the MYBENCH instruments for the NI myDAQ.
MYBENCH is **one** Qt Quick application (see [`docs/UI.md`](../UI.md)); each instrument is a panel
in its workspace. Each instrument is implemented from its spec here; the matching end-user help
page is `docs/help/<id>.md` (rendered in the app's help drawer).

The goal is functional parity with the NI ELVISmx instruments for the myDAQ: every ELVISmx feature,
control name, value list, default and behaviour is kept, so that students who learned on ELVISmx
recognise everything, while the presentation is modern. Everything here is written from scratch
(clean-room): no NI text, images or code. Where the NI behaviour is unknown or unsuitable, the spec
says what MYBENCH does and lists the choice under "Open questions / assumptions".

## How to read the specs

- [`app.md`](app.md) is read first: application shell (nav rail, top bar, workspace, Home dashboard,
  help drawer, status bar), and **every common convention**: DeviceHub and device selection,
  ResourceArbiter (preemption with pause banners and Resume), run bar and run semantics (Run =
  ELVISmx Run Continuously, Single = Run Once), snapshots and live changes, Focus mode (ELVISmx
  Toggle View), safe states, SiField numerics, knobs, choices, plots and their colors, cursors,
  trace style (Graph Properties), notifications and the error-message table, export (ELVISmx Log
  and Print), sessions, keyboard and command palette, themes, executables and `--screenshot`, and
  what replaced the ELVISmx Instrument Launcher.
- Each instrument spec has the same structure:
  1. *Purpose*.
  2. *MYBENCH design notes*: how the functions map to the panel (display area, inspector cards with
     the ELVISmx group names, run bar, dashboard tile) and the modern enhancements. Enhancements
     must never change the default behaviour.
  3. The **functional specification** (binding): every control and indicator with type, values,
     range, format, increment, default, enable rules and live-change behaviour; algorithms;
     run/stop/pause semantics; errors; resources and interactions; export; Device API mapping.
  4. *ELVISmx reference layout (informative, not binding)*: what the ELVISmx window looks like
     (regions, grouping, sizes, colors), from the reference screenshots.
  5. *Differences from ELVISmx*, *Checks against the simulator*, *Open questions / assumptions*.
- Keywords: **must** = required for parity, **should** = strongly recommended, **may** = optional.
  **[MYBENCH extension]** marks features ELVISmx does not have.

## Instruments

| id | Panel | Replaces (ELVISmx) | Resources | Run modes | Spec | Help |
|---|---|---|---|---|---|---|
| `dmm` | Multimeter | Digital Multimeter | DMM (isolated) | Run / Single; auto-starts | [dmm.md](dmm.md) | [help](../help/dmm.md) |
| `scope` | Oscilloscope | Oscilloscope | AI (AI 0/1 or AudioIn L/R) | Run / Single | [scope.md](scope.md) | [help](../help/scope.md) |
| `gen` | Waveform Generator | Function Generator + Arbitrary Waveform Generator + Waveform Editor | AO (AO 0 + AO 1, or AudioOut L/R, one streaming task) | per channel Function / Arbitrary; Run, Single, Sweep | [gen.md](gen.md) | [help](../help/gen.md) |
| `bode` | Bode Analyzer | Bode Analyzer | AO (AO 0) + AI (AI 0 + AI 1) | Run (single sweep) | [bode.md](bode.md) | [help](../help/bode.md) |
| `dsa` | Spectrum Analyzer | Dynamic Signal Analyzer | AI (one channel) | Run / Single | [dsa.md](dsa.md) | [help](../help/dsa.md) |
| `dio` | Digital I/O | Digital Reader + Digital Writer | DIO, per line | Run / Single | [dio.md](dio.md) | [help](../help/dio.md) |
| `app` | (shell) | Instrument Launcher + common conventions | - | - | [app.md](app.md) | [help](../help/app.md) |

ELVISmx instruments that exist only for NI ELVIS II/II+ and are **not** part of MYBENCH: Variable
Power Supplies, Impedance Analyzer, Two-Wire and Three-Wire Current-Voltage Analyzers (the myDAQ has
no hardware for them). ELVIS-only controls inside the shared instruments (Scope digital trigger,
TRIG/SYNC sources, 10x probe, GND coupling, ELVIS II+ vertical offset/filter, FGEN manual mode and
modulation, DMM capacitance and inductance, ARB PFI trigger, DSA triggers) are shown disabled where
ELVISmx shows them disabled on a myDAQ; each spec says which.

Reference screenshots (NI, gitignored, never commit): `ni-local/screens/{dmm,Scope,Fgen,Bode,DSA,Arb,DigReader,DigWriter,WaveformEditor}.png`
show each ELVISmx instrument at launch (default tab, default values). No screenshot of the ELVISmx
Instrument Launcher exists; the help text was used instead.

## myDAQ hardware constraints relevant to the UIs

These come from `docs/ARCHITECTURE.md` and the `mydaq::Device` contract
(`core/include/mydaq/device.h`); the specs rely on them.

**Analog input**
- AI 0, AI 1: differential (`AI x+` / `AI x-` terminals, referenced to AGND), 16 bit, ranges ±10 V and
  ±2 V (per channel, fixed for the duration of a task), up to 200 kS/s per channel
  (`Device::maxAiRate()`), rates coerced to divisors of an internal timebase (`coerceAiRate()`).
- AudioInput Left/Right: 3.5 mm AUDIO IN jack, ±2 V only, AC-coupled in hardware (no DC; low-frequency
  roll-off of a few Hz).
- AI and AudioInput share the ADCs: one task uses AI channels **or** audio channels, never both; at
  most 2 channels per task; one AI task at a time (resource `AI`).
- Channels of a task may be sampled with a small skew (`AiTask::channelSkew()`); phase measurements
  (Bode, Scope display) must correct it.
- There is **no hardware analog trigger** usable by the instruments: Scope edge triggering is done in
  software on acquired data (hence ELVISmx's note that one period must be on screen). No digital
  trigger input exists (ELVIS TRIG/SYNC are absent).

**Analog output**
- AO 0, AO 1: ±10 V (screw terminals, referenced to AGND, a few mA of drive), 16 bit, up to 200 kS/s per
  channel (`maxAoRate()`, `coerceAoRate()`).
- AudioOutput Left/Right: 3.5 mm AUDIO OUT jack, ±2 V.
- One hardware-timed AO task for all four outputs (resource `AO`); AO and audio outputs cannot be
  mixed in a task. Both AO 0 and AO 1 can be driven by the same task (the Waveform Generator streams
  both channels).
- `AoTask::stop()` leaves outputs at their last value; instruments that want 0 V afterwards write it
  with `aoWriteStatic()` after destroying the task (AO 0/1 only).

**DMM**
- Separate, isolated converter on the banana jacks: `HI` (V, Ω, diode), `COM`, `HI` (A). 3½ digits.
- DC V: 60 V, 20 V, 2 V, 200 mV. AC V: 20 V, 2 V, 200 mV (RMS). DC and AC A: 1 A, 200 mA, 20 mA.
  Resistance: 20 MΩ, 2 MΩ, 200 kΩ, 20 kΩ, 2 kΩ, 200 Ω. Diode: 2 V. Continuity. Ranges listed
  largest first by `Device::dmmRanges()`.
- Readings are blocking and slow (roughly 0.1 s DC, 0.3 s AC); autoranging is done in software.
- No capacitance or inductance (ELVIS-only; the buttons are shown disabled).
- Runs concurrently with every other subsystem (resource `DMM`, never contended).

**Digital I/O**
- DIO 0..7 on the screw terminals (referenced to DGND), 3.3 V LVTTL outputs, 5 V tolerant inputs,
  direction per line, software-timed (static reads/writes, ~ms latency). The device does not
  arbitrate lines; the ResourceArbiter does (resource `DIO n` per line).

**Power**
- +5 V, +15 V, -15 V supplies are fixed and always on (not software controlled; no power-supply
  instrument).

## Simulated device (for development and tests)

Every instrument must work fully against the simulator (`core/include/mydaq/sim_device.h`). Its
default wiring is useful for checking the specs: AO 0 -> AI 0 directly and -> AI 1 through a 1 kHz
RC low-pass (a Bode sweep shows -3 dB / -45° at 1 kHz); AudioOutput -> AudioInput (AC coupled);
undriven inputs show test signals (AI 0: 1 kHz 1 V sine with 1 % third harmonic, AI 1: 200 Hz ±0.5 V
square 20 % duty, audio L 440 Hz, audio R 1 kHz); DMM functions return fixed values (DC V 1.2345 V,
AC V 0.7071 V, DC A 12.34 mA, AC A 5 mA, 4.7 kΩ, diode 0.650 V, continuity 0.8 Ω). Each spec ends
with checks against the simulator.

## Glossary of spec terms

- *Record / block*: the samples shown on one screen / analysed at once.
- *Snapshot*: immutable copy of all settings sent from the UI to the worker.
- *Coerce*: replace an out-of-range value by the nearest allowed value (numerics) or the nearest
  hardware rate (`coerceAiRate`/`coerceAoRate`).
- *Safe state*: what an instrument leaves on the outputs when it stops, pauses or closes.
- *Preemption / pause*: the ResourceArbiter stops the current holder of a resource when another
  panel starts, and shows a banner with Resume (app.md section 5).
