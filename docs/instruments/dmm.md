# Multimeter (`dmm`)

Read [app.md](app.md) first (application shell, common conventions: run bar, inspector, SiField,
plots, cursors, export, notifications, resource arbiter) and [README.md](README.md) (hardware
constraints). This file is the complete **functional** specification: every control, value list,
default, rule and algorithm below must exist in MYBENCH. The "ELVISmx reference layout" section is
informative only.

## 1. Purpose

A bench-style digital multimeter for the myDAQ's isolated DMM (banana jacks): DC and AC voltage, DC
and AC current, resistance, diode test and audible continuity, with automatic or fixed ranging, a
percent-of-full-scale bar and a relative (null) mode. It is independent of the AI/AO subsystems and
may run at the same time as any other instrument. Replaces the ELVISmx Digital Multimeter.

Reference screenshot: `ni-local/screens/dmm.png` (ELVISmx at launch, DC Voltage).

## MYBENCH design notes

How the functions map to the MYBENCH panel (see UI.md / app.md), plus enhancements. Enhancements are
part of MYBENCH but must not change the default behaviour described in sections 2-6.

- **Display area**: the big readout (value + unit, or `+OVER`/`-OVER`/`---`), the % FS bar
  underneath (a modern horizontal gauge with 10 % ticks), and a status row (range in use with an
  `AUTO` badge, `NULL <value>` badge, `Diode GOOD/OPEN` badge, continuity indicator). Below it, the
  **trend chart** (see enhancements). Colors: readout in the theme's display-cyan.
- **Inspector sections** (cards, ELVISmx names):
  - *Measurement Settings*: the function selector as a segmented control / icon grid with the 9
    functions (Capacitance and Inductance present but disabled with tooltip "Not available on NI
    myDAQ"), Mode (`Auto` / `Specify Range`), Range, Null Offset switch, and the connection hint
    (our drawing of the three banana jacks with the two jacks to use highlighted, changes with the
    function).
  - *Statistics* and *Logging* cards (enhancements).
  - The ELVISmx *Instrument Control* group has no card: the device is chosen in the top bar;
    Acquisition Mode is the run bar's Run (continuous) / Single (Run Once).
- **Run bar**: Run/Stop toggle, Single (one reading), status (`Measuring`, `Idle`), a small
  activity dot that blinks on each reading.
- **Dashboard tile**: function icon, big reading with unit, `AUTO`/range, run state; click opens the
  panel. The tile updates while the DMM runs even when the panel is hidden.
- **Enhancements**:
  - *Trend chart*: readings versus time (MyBench.Plot), last 10 min by default (selector 1 min /
    10 min / 1 h / all), in the function's base unit with SI axis labels; overloads drawn as gaps;
    cleared when the function changes; shows relative values when Null is on.
  - *Statistics*: count, minimum, maximum, average, standard deviation of the displayed values
    (relative when Null is on) since the last `Reset` (button) or function change; overloads are
    counted separately and excluded.
  - *Hold*: a toggle that freezes the main readout (measuring, trend and statistics continue); the
    readout shows a `HOLD` badge.
  - *Logging*: `Record` toggle appends every reading to an in-memory table (unbounded, with a
    visible row count); `Export` writes it through DataExport (CSV/TSV) with columns
    `Time (ISO 8601)`, `Elapsed (s)`, `Function`, `Range`, `Value`, `Unit`, `Overload` (0/1),
    `Relative` (0/1), `Null value`. The table survives function changes (each row carries its
    function).
  - *Continuity feedback*: besides the beep, the continuity indicator in the display lights (and
    the tile flashes) while the reading is below the threshold, because system beeps are silent on
    some desktops.
  - Keyboard: digits 1-9 select the function buttons in order when the panel has focus.

## 2. Functions and ranges

| Function | `DmmFunction` | Ranges (list order) | Readout format per range | Unit |
|---|---|---|---|---|
| DC Voltage | `DcVoltage` | 60V, 20V, 2V, 200mV | `±dd.dd V`, `±dd.dd V`, `±d.ddd V`, `±ddd.d mV` | V |
| AC Voltage | `AcVoltage` | 20V, 2V, 200mV | `dd.dd V`, `d.ddd V`, `ddd.d mV` (RMS) | V |
| DC Current | `DcCurrent` | 1A, 200mA, 20mA | `±d.ddd A`, `±ddd.d mA`, `±dd.dd mA` | A |
| AC Current | `AcCurrent` | 1A, 200mA, 20mA | `d.ddd A`, `ddd.d mA`, `dd.dd mA` (RMS) | A |
| Resistance | `Resistance` | 20MΩ, 2MΩ, 200kΩ, 20kΩ, 2kΩ, 200Ω | `dd.dd MΩ`, `d.ddd MΩ`, `ddd.d kΩ`, `dd.dd kΩ`, `d.ddd kΩ`, `ddd.d Ω` | Ω |
| Diode | `Diode` | (fixed 2V) | `d.ddd V` | V |
| Continuity | `Continuity` | (fixed: first range the device reports) | `ddd.d Ω` | Ω |
| Capacitance | - | - | - | always disabled (ELVIS II only) |
| Inductance | - | - | - | always disabled (ELVIS II only) |

- The range list comes from `Device::dmmRanges(function)` (largest first); the table is what the
  myDAQ reports. Labels are built from `DmmRange::max` in ELVISmx style without a space (`60V`,
  `200mV`, `1A`, `20mA`, `20MΩ`, `200Ω`). If a backend reports other ranges, show them.
- **Range combo order (verified in ELVISmx UI):** the drop-down lists the ranges **smallest first**
  (`200mV, 2V, 20V, 60V`; `20mA, 200mA, 1A`; `200ohm, 2Kohm, 20Kohm, 200Kohm, 2Mohm, 20Mohm`), with
  the largest selected by default. MYBENCH lists them in the same order. ELVISmx writes the
  resistance labels `200ohm`, `2Kohm` ... `20Mohm` and the readout units `Ohms`, `kOhms`, `MOhms`;
  MYBENCH uses `Ω` (`200Ω`, `2kΩ`, `kΩ`) - a deliberate typographic modernization.
- Diode shows the single range `2V` in the Range combo (enabled in Specify Range, a one-item list);
  Continuity shows `No Range` (disabled) - verified in ELVISmx UI.
- Leading zeros are suppressed (`0.0 V`, not `00.0 V`); negative values get `-`, positive values no
  sign. The number of decimals is fixed by the range (3½ digits: resolution = range/2000, 60 V: 10 mV
  as the meter chip resolves it - **[MYBENCH deviation]** ELVISmx prints the 60 V range with one
  decimal, which shows an open input as `0.0 V` although the device reads -0.02 V). The value shown is the device value rounded to that resolution.
- Function order and icon colors (ELVISmx): DC Voltage (V + DC symbol, green), AC Voltage (V~, dark
  red), DC Current (A + DC symbol, dark blue), AC Current (A~, dark red), Resistance (Ω, green),
  Capacitance (capacitor, olive, disabled), Inductance (coil, tan, disabled), Diode (diode symbol,
  dark blue), Continuity (sound waves, blue). Icons are our own (`dmm-*` SVGs).

## 3. Controls and indicators

| Control | Type | Values | Default | Enabled when | Notes |
|---|---|---|---|---|---|
| Function | exclusive selector | 7 functions (+2 disabled) | DC Voltage | always, also while running | applies at the next reading |
| Mode | combo / segmented | Auto, Specify Range | Specify Range (verified at launch) | always; Continuity forces `Auto` (the `Specify Range` item is disabled) | **one global setting**: kept when the function changes (verified in ELVISmx UI) |
| Range | combo | per function (section 2), listed smallest first | largest range | Mode = Specify Range and the function has ranges | In Auto it is disabled and shows the range chosen by the autoranger (empty until the first reading). **Reset to the largest range whenever the function changes** (verified in ELVISmx UI). |
| Null Offset | checkbox / switch | on/off | off | every function, Diode and Continuity included (verified in ELVISmx UI) | cleared (off) when the function changes; see 4.4 |
| Acquisition Mode | run bar Run / Single | Run Continuously, Run Once | Run Continuously | always | |
| Connection hint | picture | V/Ω/diode/continuity: `HI` (V) + `COM`; current: `COM` + `HI` (A) | | always | |

| Indicator | Content |
|---|---|
| Main readout | value (relative value when Null is on), `+OVER`/`-OVER` on overload, `---` before the first reading of a function |
| % FS bar | `100 * |raw reading| / range max`, clamped 0..100 %; 100 % on overload; empty before the first reading |
| Null value | stored null value, only while Null Offset is on |
| Diode Status | `GOOD` / `OPEN`, only in Diode function |
| Auto range | range in use, only in Auto mode |

## 4. Behaviour

### 4.1 Start and run loop

- **No auto-start by default** (verified in ELVISmx UI: launched from its executable, the DMM shows
  `0.0 V`, the Run button is released and no DAQmx task exists until Run is pressed). The help text
  says the DMM "runs in DC Voltage mode when launched", which only describes the default function.
  **[MYBENCH extension]** preference `Start the Multimeter when its panel opens` (default off);
  when on, the DMM starts in Run Continuously the first time its panel or tile is shown, if the
  device is connected, and stays idle silently otherwise.
- Run Continuously: the worker loops `reading = dmmRead(function, rangeMax)` and posts each reading.
  A reading takes ~0.1 s (DC, resistance, diode, continuity) to ~0.3 s (AC). ELVISmx reads back to
  back with no pause (verified: with the simulated device, which answers at once, it issued about 30
  reads per second). MYBENCH enforces a minimum period of 200 ms between displayed readings (sleeps
  the remainder) so the digits are readable and the link is not flooded.
- Run Once (Single): exactly one displayed reading (autoranging may take several internal readings,
  4.2), then idle.
- Changes while running (function, mode, range, null) are picked up before the next reading. When
  the function changes, the readout shows `---`, the % FS bar empties and the connection hint
  changes immediately; the first reading of the new function replaces them.

### 4.2 Ranging

Specify Range: `rangeMax` = selected range; an overload shows `+OVER`/`-OVER`.

Auto (software autoranging, one reading per displayed value). ELVISmx rule, **verified in ELVISmx
code** (algorithms/dmm.md 3) and **verified with NI I/O Trace** on the simulated device (section 13):
1. The first reading of a run (or after a function change) is taken on the **largest** range.
2. After every reading, the next range is chosen directly from `M = |raw reading|` (no step-by-step
   climbing): the smallest range whose 2000-count limit `M` does not reach. **[MYBENCH]** Hysteresis
   on the way down: a smaller range is taken only when `M` is below 90 % of the upper limit of the
   range just under the current one (so 0.19 V stays on 2V; 0.17 V goes to 200mV), because a floating
   input hunted between 200mV and 2V and every change costs the real meter ~2.6 s (`set_mode` ~2 s plus
   the first packet). Climbing and the jump to the fitting range stay immediate.
   | Function | Next range |
   |---|---|
   | DC V | M ≥ 19.99 → 60V; M ≥ 1.999 → 20V; M ≥ 0.1999 → 2V; else 200mV |
   | AC V | M ≥ 1.999 → 20V; M ≥ 0.1999 → 2V; else 200mV |
   | DC A, AC A | M ≥ 0.1999 → 1A; M ≥ 0.01999 → 200mA; else 20mA |
   | Resistance | if the current range is 20MΩ and M < 10 kΩ → 20kΩ at once; otherwise M ≥ 1.999 MΩ → 20MΩ; M ≥ 199.9 kΩ → 2MΩ; M ≥ 19.99 kΩ → 200kΩ; M ≥ 1999 Ω → 20kΩ; M ≥ 199.9 Ω → 2kΩ; else 200Ω |
   | Diode, Continuity | fixed |
   Observed: 8.26 kΩ read on 20MΩ → next reading on 20kΩ; values 2.10 k, 2.03 k stayed on 20kΩ,
   1.93 kΩ switched to 2kΩ; a reading over range on 2kΩ jumped back to 20kΩ.
   An overload reading carries whatever value the device returns; the rule is applied to it
   (a large value selects the largest range).
3. A range change recreates the measurement before the next reading. **Every reading is displayed**,
   including one taken on a range that is then changed (ELVISmx has no hidden internal readings).
   **[MYBENCH]** Run Continuously follows this (the reading on the old range is shown, the next one is
   taken on the new range); only an overload and a Single reading are re-read at once on the range
   that fits, so a range change never costs two settling times.
4. The Range control (disabled) shows the range in use; the display shows `AUTO <range>`.

An overload at the largest range shows `+OVER`/`-OVER` in Auto mode too.

### 4.3 Overload

A reading is an overload when `|raw reading| > upper limit` of the range in use (2000 counts:
59.99 / 19.99 / 1.999 / 0.1999 V, 0.999 / 0.1999 / 0.01999 A - note the 1 A range overloads at
0.999 A -, 19.99 MΩ / 1.999 MΩ / 199.9 kΩ / 19.99 kΩ / 1999 Ω / 199.9 Ω, diode 1.999 V,
continuity 199.99 Ω; verified in ELVISmx code), or when the backend flags `overload`. The readout
then shows **`+Over`** for either sign (ELVISmx has no `-Over`: verified in ELVISmx UI, a negative
out-of-range value displayed `+Over`, and in code). MYBENCH shows `+OVER`; **[MYBENCH extension]**
option `Signed overload` shows `-OVER` for negative overloads (off by default).

### 4.4 Null Offset (relative mode)

- Turning Null Offset **on** arms a capture: the next non-overload reading of the current function
  becomes the null value `N` (displayed as `0` relative) and `Null value: N <unit>` appears (ELVISmx:
  a small boxed text at the top of the display, in the unit of the range, e.g. `mV` on 200mV;
  verified in ELVISmx UI). If idle, the capture happens at the first reading after Run. ELVISmx
  restarts the measurement task when the box is toggled (verified with NI I/O Trace).
- While on, the readout shows `reading - N` with the current range's resolution (in Auto mode `N` is
  kept in base units and subtracted whatever the range).
- The % FS bar and overload detection always use the **raw** reading.
- Turning it **off** shows raw readings; turning it on again captures a new null value.
- A function change turns Null Offset off (verified in ELVISmx UI: after DC V -> AC V -> DC V the
  box is unchecked and the range is back to the largest). **[MYBENCH extension]** preference
  `Remember range and null per function` (default off) keeps Mode/Range/Null per function instead.
- Null Offset is available for every function, Diode and Continuity included (verified in ELVISmx
  UI); for Continuity the beep threshold (4.6) uses the raw reading.

### 4.5 Diode

`dmmRead(Diode, 2.0)` returns the forward voltage (a voltage measurement on the 2 V range with the
DMM's internal 1 mA excitation current, verified with NI I/O Trace). Diode Status = `GOOD` when the
reading is not an overload and is below 2.0 V (the valid-diode threshold); otherwise `OPEN` (open
circuit or reversed diode). Verified in ELVISmx UI: on an open circuit the **main readout itself
shows `OPEN`** (large, in place of the value) and the % FS bar is full. MYBENCH shows `OPEN` in the
readout and the `Diode OPEN` badge. A shorted diode reads ~0 V and shows `GOOD` (as ELVISmx).

### 4.6 Continuity

`dmmRead(Continuity, rangeMax)` with the function's single range. The readout shows the resistance.
When `|reading|` is below **15 Ω** a **continuous 1 kHz tone** plays on the computer's sound output
for as long as the condition holds (verified in ELVISmx code: a 1000 Hz sine, 44.1 kS/s stereo,
75 % volume, started and stopped as the state changes; not observable with the simulated device).
MYBENCH plays a generated 1 kHz tone with `QAudioSink` (Qt Multimedia) while the circuit is
closed, stops it when it opens or the DMM stops, and has a mute toggle; an open circuit is silent
and shows `+OVER`, with a secondary status text `OPEN` at the right of the display (verified in
ELVISmx UI). The visual continuity indicator (design notes) lights at the same time. Continuity is
always autoranged in ELVISmx (Mode forced to `Auto`, Range `No Range`): it measures on the 200 Ω
resistance range with the device's continuity mode enabled (NI I/O Trace).

### 4.7 Stop

Stop ends the loop after the reading in progress (never interrupt `dmmRead`). The last reading stays
displayed. No outputs to reset (safe state: nothing).

## 5. Errors and notifications

| Situation | Behaviour |
|---|---|
| no device connected when auto-starting | stay idle, no notification |
| `DeviceError` during a reading | stop, error banner/toast (app.md table), readout `---` |
| `Timeout` from `dmmRead` | treated as an error |
| device unplugged while measuring | stop; hot-plug banner (app.md) |

## 6. Resources and interactions

- Resource `DMM` (independent isolated converter). One DMM instrument per device. It is never
  paused or preempted by other instruments and never preempts anyone.
- Runs concurrently with Scope, Generator, Bode, Spectrum and DIO (e.g. measure the generator's AO 0
  in AC V while the Scope shows it).

## 7. Logging / export

ELVISmx has none. MYBENCH: the Logging card (design notes) and the common "Export data" (current
reading table) / "Export image" actions.

## 8. Device API mapping

| Feature | Device call |
|---|---|
| Range lists | `Device::dmmRanges(DmmFunction)` (cache per device) |
| Each reading | `Device::dmmRead(DmmFunction, rangeMax)` -> `DmmReading{value, overload}` (worker thread, blocking) |
| Units | `mydaq::unitOf(DmmFunction)` |
| Function names | `mydaq::toString(DmmFunction)` |

No AI/AO/DIO calls; no tasks.

## 9. ELVISmx reference layout (informative, not binding)

ELVISmx window `Digital Multimeter`, about 392 x 497 px, fixed size, vertical stack:

```
+--------------------------------------------------------------+
| [logo]                                         [toggle view] |  black display, ~140 px
|                          0.0 V                               |  cyan readout ~32 px
|   |''''|''''|''''|''''|''''|''''|''''|''''|''''|''''|  % FS  |  % FS bar, cyan ticks
+--------------------------------------------------------------+
| Measurement Settings                                         |
|  [V=][V~][A=][A~][ Ω][-||-][coil][->|][ •)) ]                |  9 function buttons (~38x28)
|  Mode                        Banana Jack Connections         |
|  [Specify Range  v]          +-------------------------+     |
|  Range                       |  HI(VΩ⊣▶)  COM   HI(A)  |     |  white picture box, arrows
|  [60V   v]                   +-------------------------+     |  point at the jacks to use
|  [ ] Null Offset                                             |
+--------------------------------------------------------------+
| Instrument Control                                           |
|  Device [myDAQ1 (NI myDAQ) v]   Acquisition Mode [Run Continuously v]
|                                 Run [->]  Stop [■]  |  Help [?]
+--------------------------------------------------------------+
```

- Display: black; readout centered, cyan (#00FFFC); % FS bar = dark-gray track (#171717) with 50
  small cyan ticks, longer every 10 %; label `% FS` at its right end. Null value, diode status and
  over-range indicators appear in the display only when active.
- Checked function button: light-blue face (#CCE4F7) with a blue border.
- Group titles blue, panel #F0F0F0. No Print/Log buttons.

## 10. Differences from ELVISmx

- Capacitance and Inductance are shown disabled (ELVIS II only); the ELVIS protoboard DUT+/DUT-
  connections do not exist on the myDAQ.
- Auto-start, the 200 ms display period and the enhancements (trend, statistics, hold, logging,
  visual continuity indicator) are MYBENCH additions/decisions.

## 11. Checks against the simulator

- Start: DC V, Specify Range 60V, readout `1.2 V` (sim DC V = 1.2345 V), % FS ≈ 2 %.
- Auto: settles on `2V`, readout `1.235 V` (or `1.234 V`), `AUTO 2V`.
- AC V Auto (sim 0.7071 V RMS): 20V -> 2V, stays on 2V (0.7071 V > 0.9 x 200 mV), readout `0.707 V`.
- Resistance Auto (sim 4.7 kΩ): 20MΩ -> 2MΩ -> 200kΩ -> 20kΩ, stays on 20kΩ (4.7 kΩ > 0.9 x 2 kΩ),
  readout `4.70 kΩ`; with `setDmmOpenCircuit(true)` -> `+OVER`.
- Diode: `0.650 V`, `Diode GOOD`; open circuit -> `+OVER`, `Diode OPEN`.
- Continuity: `0.8 Ω`, beeps, indicator lit; open circuit -> `+OVER`, silent.
- Null: DC V 2V range, enable Null -> `0.000 V`, `Null: 1.235 V`; `setDmmValue(DcVoltage, 1.3)` ->
  `0.065 V`.
- Statistics after 10 readings of a constant input: min = max = average, σ ≈ noise.

## 12. Open questions / assumptions

1. **Default Mode** - resolved (verified in ELVISmx UI): `Specify Range` / largest range at launch;
   Mode is a single setting shared by all functions and survives function changes; the Range goes
   back to the largest range on every function change.
2. **Auto-start** - resolved (verified in ELVISmx UI): ELVISmx does not measure until Run is pressed.
   MYBENCH offers auto-start as an opt-in preference (4.1).
3. **Overload sign** - resolved: ELVISmx shows `+Over` for both signs (verified in ELVISmx UI and
   code); `-OVER` is an optional MYBENCH extension.
4. **Null re-arm.** "Relative to the measurement made when the checkbox was initially enabled" is
   read as: each off->on transition captures a new null value; values are kept per function.
5. **Continuity threshold and tone**: 15 Ω and a continuous 1 kHz tone (verified in ELVISmx code);
   not observable with the simulated device (its resistance values are never below 15 Ω).
6. **Display resolution**: 3½-digit formats per range are inferred from the myDAQ range set and the
   `0.0 V` shown on the 60V range. Resistance readouts seen in ELVISmx (simulated values):
   20Mohm `0.01 MOhms`, 2Mohm `0.011 MOhms`, 200Kohm `10.7 kOhms`, 20Kohm `8.91 kOhms` - consistent
   with the table of section 2.
7. **Autorange rule** - resolved: the value-to-range table of 4.2 (verified in code and observed).
   Switching Mode to Auto *while running* had no visible effect in ELVISmx until the next Run
   (observed once, simulated device); MYBENCH applies it at the next reading.
9. **Reading rate** - ELVISmx reads back to back without a pause (observed ~30 readings/s against
   the simulated device); the real rate is set by the DMM conversion time. MYBENCH keeps its 200 ms
   display period.
8. **Overload detection** in ELVISmx could not be characterized with the simulated device (it
   returns values far outside the range on some ranges without `+Over`); MYBENCH relies on
   `DmmReading::overload`.

## 13. Verified DAQmx usage (ELVISmx, observed with NI I/O Trace)

Observed on a simulated NI myDAQ (`myDAQ1`) with ELVISmx 25.8; described in our own words. It tells
how ELVISmx drives the hardware, so MYBENCH's USB backend can reproduce the same device
configuration (`dmmRead` of `core/`).

- **Idle**: no task. Twice per second the program lists the DAQmx devices and reads their product
  type (Device combo refresh / hot-plug).
- **One task per configuration**: Run creates a task with one channel on the physical channel
  `dmm` of the device, starts it, then reads **one scalar sample per call** in a loop (10 s read
  timeout) with no pause between reads. Any change of function, range (manual or by the
  autoranger) or Null Offset stops and clears the task and creates a new one immediately; Stop
  clears the task. Run Once = create, start, one read, clear.
- Channel type per function (range min/max = the selected range):
  | Function | Channel | Parameters |
  |---|---|---|
  | DC Voltage | voltage | -R ... +R V, default terminal configuration |
  | AC Voltage | voltage RMS | 0 ... R V |
  | DC Current | current | -R ... +R A, shunt location left to the driver (value 249 Ω passed, unused) |
  | AC Current | current RMS | 0 ... R A, same shunt arguments |
  | Resistance | resistance | 0 ... R Ω, 2-wire, internal excitation 1 mA |
  | Diode | voltage | -2 ... +2 V with internal current excitation 1 mA |
  | Continuity | resistance | 0 ... 200 Ω, 2-wire, internal 1 mA, device continuity mode enabled |
- Every DMM channel is created with the device's **hardware autoranging disabled**; Auto mode is
  software autoranging (4.2): after each reading the program recreates the task on the range that
  fits (e.g. 20 MΩ -> 20 kΩ in one step).
- Readings arrive as fast as the device delivers them (simulated: ~30/s on this VM; the real myDAQ
  needs ~0.1-0.3 s per reading).
