# ELVISmx UI inventory (explored) and MYBENCH coverage

Exhaustive checklist of every user-visible capability of the NI ELVISmx programs for the NI myDAQ,
built by driving the real programs (ELVISmx + NI-DAQmx 25.8 on Windows 10, with a **simulated NI
myDAQ** named `myDAQ1` imported into DAQmx) and watching their NI-DAQmx calls with NI I/O Trace.
Everything is written in our own words; control names and value lists are reused so that the
MYBENCH specs can keep them. NI screenshots of this exploration are kept locally in
`ni-local/screens/explore/<app>/` (gitignored, never committed).

Status: all instruments explored except as listed in the "Not explored" section at the end.

Legend for the coverage column:
- `dmm.md 3` = covered by that spec file and section (after this exploration's updates).
- **MISSING** = was not in the spec before this exploration; the spec was extended (the column then
  names the new section) or, if still open, the item is listed under "Still open" at the end of the
  app's table.
- `n/a` = ELVIS-only or not meaningful on MYBENCH (reason given).

Notes on the simulated device: the simulated myDAQ returns synthetic data (the DMM returns large
random-walk values, so DC V and current readings show `+Over` on every range; resistance returns
values around 2-13 kΩ). Behaviour that depends on real signals (exact readouts, noise) was not
judged from the simulator.

General observations that apply to every ELVISmx instrument (verified):
- Each instrument is a separate LabVIEW-built program with a fixed-size window (no maximize), one
  instance per program.
- **No right-click (shortcut) menus** on the instrument controls and indicators we tried (DMM display,
  % FS bar, combos, function buttons): runtime shortcut menus are disabled. Graph-specific palettes
  are listed per instrument.
- Every control has a **tooltip** equal to its label (e.g. `Measurement Display`, `Percent
  Full-Scale`, `Mode`, `DC Voltage`, `Toggle View`).
- While an instrument runs, the **Device** combo is disabled and the Run button stays pressed; the
  measurement settings stay editable.
- Every instrument polls the DAQmx device list twice per second (device names + product type), so
  the Device combo follows hot-plug.
- **Help** opens the ELVISmx CHM help window (separate window, Contents/Index/Search/Favorites) at the
  instrument's controls page.

## 1. Instrument Launcher (`niiql.exe`)

| Area | Item | Values / behaviour | MYBENCH |
|---|---|---|---|
| Window | resizable, maximizable window; the tile grid reflows to the width | | `app.md 2` (nav rail + Home grid) |
| Sidebar | five pages: `Instruments & Apps`, `Favorites`, `Labs`, `My Files`, `Resources`; selected page marked with a pointer | | `app.md 18` |
| Instruments & Apps | tiles (round icon + name), in this order: Arbitrary Waveform Generator, Bode Analyzer, Digital Reader, Digital Writer, Digital Multimeter, Dynamic Signal Analyzer, Function Generator, Oscilloscope, 2-Wire Current-Voltage Analyzer, 3-Wire Current-Voltage Analyzer, Impedance Analyzer, Variable Power Supplies, Digital Waveform Viewer, 8-Channel Oscilloscope, Audio Equalizer, Data Logger, DC Level, Octave Analyzer, Add Item | ELVIS-only tiles are listed too on a myDAQ system | `app.md 18`, `featured-apps.md` |
| Tile | click launches the program (one window per program); hover/selection frame with a **star** (add to Favorites) | no right-click menu | `app.md 18` (favorites **MISSING** -> `app.md 18.1`) |
| Add Item (Instruments & Apps) | expands into a card `Add Item` with `Browse` (pick a program/file to add as a tile) and `Find More Online` (web page with downloadable apps); `<<` collapses | | `app.md 18.1` (user tools) |
| Favorites | tiles starred elsewhere; empty by default | | `app.md 18.1` |
| Labs | empty on this system (lab documents installed by courseware) | | `app.md 18.1` |
| My Files | `Add Item` card with `Browse` (adds a document/file shortcut) | | `app.md 18.1` (recent sessions + pinned files) |
| Resources | tiles: NI ELVIS II Series User Manual, NI ELVIS II Series Specifications, NI myDAQ User Guide, NI myDAQ Specifications, NI ELVISmx Help, How to Configure Your Hardware, Measurement & Automation Explorer, Discussion Forum, What is LabVIEW?, Using NI ELVISmx with NI Multisim, Add Item | documents and web links | `app.md 18.1` (Help > Resources menu) |

## 2. Digital Multimeter (`DMM.exe`)

Window `Digital Multimeter - NI ELVISmx`, 412 x 508 px, fixed size.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | readout | cyan text; `0.0 V` at launch; overload text is `+Over` (and `-Over`); resistance units are written `Ohms`, `kOhms`, `MOhms` | `dmm.md 2`, `3` |
| Display | readout resolution seen | 60V: 1 decimal (`0.0 V`); resistance 20Mohm `0.01 MOhms`, 2Mohm `0.011 MOhms`, 200Kohm `10.7 kOhms`, 20Kohm `8.91 kOhms`/`13.01 kOhms` | `dmm.md 2` |
| Display | % FS bar | 0-100 % of the range; full on overload; empty before the first reading; tooltip `Percent Full-Scale` | `dmm.md 3` |
| Display | Null value | small box at the top of the display, `Null value: <v> <unit>` (range unit, e.g. mV), only while Null Offset is on | `dmm.md 3`, `4.4` |
| Display | Diode status | Diode function: the main readout itself shows `OPEN` (large) for an open circuit | `dmm.md 4.5` (updated) |
| Display | Continuity status | Continuity function: main readout `+Over` plus a secondary `OPEN` text at the right of the display | `dmm.md 4.6` (updated) |
| Display | Toggle View button (top right) | shrinks the window to the display only (412 x 186); click again restores | `app.md 6.5` |
| Measurement Settings | 9 function buttons | DC Voltage, AC Voltage, DC Current, AC Current, Resistance, (Capacitance, Inductance: always disabled), Diode, Continuity; tooltips = names | `dmm.md 2` |
| Measurement Settings | Mode | `Auto`, `Specify Range` (default Specify Range). **Mode is global**: it is kept when the function changes. Continuity forces `Auto` (Specify Range item disabled). Diode offers both | `dmm.md 3` (updated, **MISSING** before) |
| Measurement Settings | Range | lists **smallest first**: DC V `200mV, 2V, 20V, 60V`; AC V `200mV, 2V, 20V`; DC A and AC A `20mA, 200mA, 1A`; Resistance `200ohm, 2Kohm, 20Kohm, 200Kohm, 2Mohm, 20Mohm`; Diode `2V`; Continuity: shows `No Range` (disabled). In Auto the combo is disabled and shows the range in use (empty until the first reading). **Range resets to the largest one on every function change** | `dmm.md 2`, `3` (updated) |
| Measurement Settings | Null Offset | checkbox, off at launch; enabled for every function including Diode and Continuity; turning it on or off restarts the measurement task; turning off a function clears it (it is off again after a function change) | `dmm.md 4.4` (updated) |
| Measurement Settings | Banana Jack Connections | picture of HI / COM / HI (A) jacks with arrows on the two jacks to use | `dmm.md 3` |
| Instrument Control | Device | list of DAQmx devices as `myDAQ1 (NI myDAQ)`; disabled while running | `app.md 4` |
| Instrument Control | Acquisition Mode | `Run Continuously`, `Run Once` | `dmm.md 3` |
| Instrument Control | Run / Stop / Help | Run starts (button stays pressed), Stop stops, Help opens the help | `dmm.md 4` |
| Start | **no auto-start**: the DMM stays idle at launch until Run (verified: no DAQmx task before Run when started from its executable) | `dmm.md 4.1` (updated) |
| Run Once | one reading, then idle | `dmm.md 4.1` |
| Context menus | none on display, bar, combos, buttons | `app.md 8.3` (MYBENCH adds its own) |
| Log / Print | none | `dmm.md 7` |

## 3. Featured applications

Coverage marks (2026-09-28): **explored** = UI observed; **code** = described from ELVISmx behaviour only
(no launch). Details and sources: [featured-apps.md](featured-apps.md).

| App | Status | What is covered | Coverage |
|---|---|---|---|
| DC Level | explored | controls, range, Start/Stop behaviour | `gen.md` DC mode, `featured-apps.md 2` |
| Data Logger | explored + code | controls, defaults (rate 1000), log file layout (header pair per channel, absolute date-time cells, `E` values, TAB, CRLF, append), path control (`*.lvm`, shared Documents) | `log.md 2, 8`, `featured-apps.md 3` |
| Octave Analyzer | explored (screens) + code | every control, Mode / Time Constant / band index / weighting behaviour, tabs, staircase display, Settled | `dsa.md 14`, `featured-apps.md 4` |
| Audio Equalizer | code only | panel elements, defaults, ranges, three-band tone control | `aeq.md 1`, `featured-apps.md 5` |
| 8-Channel Oscilloscope | out of scope | mapped to the Oscilloscope | `scope.md`, `featured-apps.md 6` |
| Digital Waveform Viewer | out of scope | mapped to the Digital I/O timeline (ELVIS II only) | `dio.md 4`, `featured-apps.md 7` |

## 4. Oscilloscope (`Scope.exe`)

Window `Oscilloscope - NI ELVISmx`, 883 x 605 px, fixed. Screens: `ni-local/screens/explore/scope/`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | graph 10 x 10 div, no numbers; tooltip `Scope Graph`; **no right-click menu, no graph palette** | | `scope.md 2.1`, `app.md 8.3` |
| Display | trigger level line | red horizontal line at Level while Type = Edge | `scope.md 3.6` (was **MISSING**) |
| Display | `Sample Rate:` | `50.00 kS/s`, `200.00 kS/s` (2 decimals) | `scope.md 3.6` |
| Display | `*** Waiting for Trigger ***` | shown while an Edge record has no trigger | `scope.md 3.6`, `4.5` (was **MISSING**) |
| Display | Timeout LED | lit when no trigger | `scope.md 4.5` |
| Display | measurement rows | `CH0 Meas: RMS Freq Vp-p`, `CH1 Meas: ...`, `Cursors: C1 C2 dT` row; 3 decimals ≥ 1, 2 below | `scope.md 3.6` (format corrected) |
| Display | Toggle View | window shrinks to the display (500 x 536) and back | `app.md 6.5` |
| Basic > Channel N | Source | `AI 0`, `AI 1`, separator, `AudioInput Left`, `AudioInput Right`; choosing audio on one channel switches the other to audio | `scope.md 3.1`, `4.1` (rule was **MISSING**) |
| Basic > Channel N | Enabled | live | `scope.md 3.1` |
| Basic > Channel N | Probe `1x`, Coupling `DC`/`AC` | always grayed; AC for audio | `scope.md 3.1` |
| Basic > Channel N | Volts/Div knob + ring | AI 5 V ... 10 mV; audio 1 V ... 10 mV; coerced on switch | `scope.md 3.1` (audio list was **MISSING**) |
| Basic > Channel N | Vertical Position knob + numeric | ±5, coerced | `scope.md 3.1` |
| Basic > Timebase | Time/Div knob + ring | 200 ms ... 5 µs, 15 values | `scope.md 3.2` (list corrected) |
| Basic > Trigger | Type | `Immediate`, `Digital` (disabled), `Edge` | `scope.md 3.3` |
| Basic > Trigger | Source | `TRIG`, `SYNC` (disabled), `Chan 0 Source`, `Chan 1 Source` (disabled while CH 1 off) | `scope.md 3.3` |
| Basic > Trigger | Slope toggle, Level (±100 V, coerced), Horizontal Position slider + numeric | enabled only for Edge; live | `scope.md 3.3` (±100 was different) |
| Advanced | per channel Vertical Offset (V) knob + numeric, 20MHz Filter Enabled | all grayed | `scope.md 3.5` |
| Bottom strip | Cursors On, C1 / C2 combos (`CH 0`, `CH 1`; defaults CH 0 / CH 1) | combos disabled while off | `scope.md 3.4`, `4.10` |
| Bottom strip | Display Measurements CH 0 / CH1 | CH1 disabled while CH 1 off | `scope.md 3.4` |
| Bottom strip | Graph Properties | dialog, see below | `scope.md 4.9` |
| Graph Properties > Plots | plot selector (`Channel 0 (Real)`, `Channel 1 (Real)`, grayed Simulated), line style (5), width (6), point style (17), interpolation (6), Line / Point-Fill colors (color picker), Fill to (`<none>`, `zero`, `-Infinity`, `+Infinity`), Background color (`Black`, `White`) | OK / Cancel / Reset to Default | `scope.md 4.9` (Background list, palettes sizes were **MISSING**) |
| Graph Properties > Cursors | cursor `C1`/`C2`, line style, width, point style, cursor style (7), Cursor color, Linked plot | | `scope.md 4.9` |
| Graph Properties > Measurements | channel; RMS Voltage, Frequency, Voltage Peak-to-Peak; C1 Volts, C2 Volts, delta X-Value | checkboxes | `scope.md 4.9` |
| Cursors | dashed vertical lines with `C1`/`C2` label at the trace, start at the left edge, draggable | | `scope.md 4.10` |
| Instrument Control | Device (disabled while running), Acquisition Mode (`Run Continuously`, `Run Once`), Autoscale, Run, Stop, Print, Log, Help | Print/Log only idle after a record | `scope.md 3.4` |
| Print dialog | Printer / File (PNG), Ink Saver, User name, Computer name, Time stamp, Portrait/Landscape, copies; page with settings table | | `scope.md 8` (was **MISSING**) |
| Log dialog | `Save Log File`, TXT / TDMS; TXT = LabVIEW waveform export with absolute timestamps | | `scope.md 8` (format was **MISSING**; TDMS not replicated) |
| Run Once + Edge, no trigger | one acquisition, then waits (Run pressed) until Stop | | `scope.md 4.5` |

## 5. Function Generator (`Fgen.exe`)

Window `Function Generator - NI ELVISmx`, 439 x 490 px, fixed. Screens: `explore/fgen/`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | readout | `OFF`; while generating the frequency with 4 decimals (`1.0000 kHz`, `100.0000 Hz`, `1.2345 kHz`) | `gen.md 4.1.5` (format was different) |
| Waveform Settings | Sine / Triangle / Square buttons | exclusive, tooltips = names | `gen.md 4.1.2` |
| Waveform Settings | Frequency knob (log, `200m`...`20k`) + numeric | 0.2 Hz ... 20 kHz coerced; 5 significant digits accepted | `gen.md 4.1.2` |
| Waveform Settings | Amplitude knob `0.0`...`10.0` + numeric Vpp | 0 ... 10 coerced | `gen.md 4.1.2` |
| Waveform Settings | DC Offset knob `-5.0`...`5.0` + numeric V | ±5 coerced | `gen.md 4.1.2` |
| Waveform Settings | Duty Cycle % | 0 ... 100 coerced; enabled only for Square | `gen.md 4.1.2` (range was 1-99) |
| Waveform Settings | Modulation Type | `None`, grayed | `gen.md 4.1.2` |
| Sweep Settings | Start / Stop Frequency, Step, Step Interval | 0.2 Hz ... 20 kHz; Step ≥ 0; Interval 1 ... 300 000 ms | `gen.md 4.1.2` (limits were different) |
| Instrument Control | Device, Signal Route (`AO0`, `AO1`), Manual Mode (grayed), Run, Sweep, Stop, Help | Stop grayed while idle; Device + Sweep grayed while running; route live | `gen.md 4.1.2`, `4.1.7` |
| Sweep | sequence Start, +Step, ..., Stop if on a step, then `OFF` | all controls grayed during a sweep except Stop/Help | `gen.md 4.1.6` |
| Stop | output 0 V | | `gen.md 4.1.7` |
| Context menus / Log / Print | none | | - |

## 6. Arbitrary Waveform Generator (`Arb.exe`)

Window `Arbitrary Waveform Generator - NI ELVISmx`, 583 x 545 px, fixed. Screens: `explore/arb/`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | `Update Rate :` readout | `OFF`; `1.00 kS/s` while running | `gen.md 4.2.2` |
| Display | `Error` red flag | top right while an error dialog is pending | `gen.md 4.2.2` (banner) |
| Display | graph `Amplitude (V)` vs `Time (s)`; `Display` ring under the X title: `Time (s)`, `Frequency (Hz)` (Y `Magnitude (dB)`, 0 ... rate/2) | shows the enabled waveforms (AO 0 yellow, AO 1 white) only while generating | `gen.md 4.2.5` |
| Display | `Waveform Points :` | 0 until Run, then the length | `gen.md 4.2.2` |
| Display | graph palette: cursor tool, zoom tool (zoom rectangle, zoom X, zoom Y, zoom to fit, zoom in/out about a point), X and Y autoscale lock buttons (locked at launch); no right-click menu | | `app.md 8.3` |
| Waveform Settings | Output Channel per row: row 1 `AO 0` / `AudioOutput Left`, row 2 `AO 1` / `AudioOutput Right`; `Enabled`; Waveform Name (`< empty >`); folder button; Gain | | `gen.md 4.2.2` |
| Folder button | dialog `Select Waveform File to Load...`, pattern `*.wdt`, opens in the examples folder (4 example files) | | `gen.md 4.2.3` |
| Gain | no limit (50 accepted -> DAQmx error dialog) | | `gen.md 4.2.2` |
| Timing | Update Rate (0.1 S/s ... 200 kS/s coerced), Trigger Source `Immediate` grayed | | `gen.md 4.2.2` |
| Waveform Editor | `Click to Launch` button opens the Waveform Editor program | | `gen.md 4.3` (Edit button) |
| Instrument Control | Device, Generation Mode (`Run Continuously`, `Run Once`), Run, Stop, Help | | `gen.md 4.2.2` |
| Errors | different lengths on both rows -> DAQmx error dialog, nothing generated; the dialog can hide behind the window | | `gen.md 4.2.4` |
| Run Once | plays once, outputs back to 0 V, `OFF` | | `gen.md 4.2.4` (option) |

## 7. Waveform Editor (`WaveformEditor.exe`)

Window `Waveform Editor - <name>`, 622 x 495 px, **resizable**, menu bar. Screens: `explore/weditor/`.

| Group | Control / item | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| File menu | New Ctrl+N, Open Ctrl+O, Close Ctrl+W, Save Ctrl+S, Save As..., Exit Ctrl+Q | Save As: `*.wdt` only (rendered data); Open: `*.wfm;*.txt;*.wdt;*.bin;...` -> wizard | `gen.md 4.3.8` (Exit **MISSING** -> added via Close) |
| Edit menu | Undo Ctrl+Z, Cut, Copy, Paste, Clear, Select All Ctrl+A, Import from File..., Scale Selection..., Settings..., Info... Ctrl+I | | `gen.md 4.3.8` |
| Help menu | Show Context Help Ctrl+H, Online Reference..., About Waveform Editor... | | `gen.md 4.3.8` (context help: tooltips + help drawer) |
| Palette | X/Y autoscale locks, X/Y format menus (Format, Precision, Mapping Mode, Visible Scale, Visible Scale Label, Grid Color), zoom menu (6 tools), pointer, pan | | `gen.md 4.3.7` |
| Plot Style legend | Plot Visible, Common Plots, Color, Line Style, Line Width, Anti-Aliased, Bar Plots, Fill Base Line, Interpolation, Point Style, X Scale, Y Scale, Export (to Clipboard; Excel/DIAdem grayed) | | `gen.md 4.3.7` (was partly **MISSING**) |
| View | `Waveform`, `Segment`, `Component` | | `gen.md 4.3.7` |
| X Axis | `Time`, `Frequency` | | `gen.md 4.3.7` |
| Sample Rate | 10000.0 | in the current Units | `gen.md 4.3.7` |
| Units | `Hz`, `kHz`, `MHz` - also switches time to sec / ms / µs | | `gen.md 4.3.7` (semantics were **MISSING**) |
| Bottom band | Index ◀ n ▶, segment list, New Segment, New Component, Function (`+`, `--`, `x`, `/`, `FM`), Library / Expression / Sketch | | `gen.md 4.3.7` |
| Library pane | `Function Library:` 21 functions with their parameter sets | | `gen.md 4.3.4` (rewritten) |
| Expression pane | `Expression:` `f(x)=`, X Range From / To, Cycles | | `gen.md 4.3.5` |
| Sketch pane | `Freehand Sketch:` Name, Cycles, Samples, Import..., Draw | | `gen.md 4.3.6` |
| Segment pane | Name, Duration (sec) | | `gen.md 4.3.7` |
| Draw mode | Mouse Position (x, y), pencil and line tools, list/buttons grayed | | `gen.md 4.3.7` |
| Settings dialog | Plot / Selection / Grid / Cursor Color, Max Points 500000, Animate Graphs, Show Cursors | | `gen.md 4.3.8` (Max Points, Animate were **MISSING**) |
| Scale Selection | Time Axis Multiply by, Voltage axis Multiply by | | `gen.md 4.3.8` |
| Info | file path + Description | | `gen.md 4.3.8` |
| Open Waveform File Wizard | preview (by sample index), OK / Cancel -> new segment with one Sketch | | `gen.md 4.3.9` |
| Context menus | none | | - |

## 8. Bode Analyzer (`Bode.exe`)

Window `Bode Analyzer - NI ELVISmx`, 761 x 602 px, fixed. Screens: `explore/bode/`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | `Connections: AI 0 - AO 0   AI 1 - Signal`; Toggle View button | | `bode.md 9`, `app.md 6.5` |
| Display | Gain plot (`Gain (dB)` / `Gain (Linear)`), Phase plot (`Phase (deg)`), log frequency axes; yellow line + hollow circle markers; no palette, no right-click menu | | `bode.md 3.4` |
| Display | readouts `Frequency (Hz)`, `Phase (deg)`, `Gain`, `Gain (dB)` (2 decimals; `Inf` before data) | | `bode.md 3.6` |
| Measurement Settings | Stimulus `AI 0`, Response `AI 1` (grayed) | | `bode.md 2` |
| Measurement Settings | Start / Stop Frequency 1 Hz ... 20 kHz; Steps ≥ 1 (per decade); Peak Amplitude 0 ... 5 V; Op-Amp Signal Polarity `Normal` / `Inverted` | coerced; polarity applies to the next sweep | `bode.md 2` (limits and polarity rule were different) |
| Graph Settings | Mapping `Linear` / `Logarithmic` (live), Autoscale (off -> fixed -30...5 / -360...360) | | `bode.md 3.4` |
| Cursor Settings | Cursors On; `Cursor (Real)` (grayed); Position Left / Right diamonds | red crosshair on both plots, starts at the first point | `bode.md 3.6` |
| Instrument Control | Device, Run, Stop, Print, Log, Help | Device/Print/Log grayed during a sweep | `bode.md 2` |
| Log | TXT: date/time, `Amplitude: 2.00 V`, `Freq (Hz)`, `Gain (Linear|dB)`, `Phase (deg)` rows | | `bode.md 7` (was **MISSING**) |
| Print | parameter table (Device, Start, Stop, Steps / decade, Peak Amplitude, Op-Amp Polarity, Graph Mapping, Cursor Linked Plot) | | `bode.md 7` |
| Stop | keeps the measured points; AO 0 to 0 V | | `bode.md 4` |

## 9. Dynamic Signal Analyzer (`DSA.exe`)

Window `Dynamic Signal Analyzer - NI ELVISmx`, 847 x 664 px, fixed. Screens: `explore/dsa/`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | `Detected Fundamental Frequency (Hz)`, `Fundamental Frequency Power (Vrms^2|Vpk^2)` (linear power whatever the Units) | | `dsa.md 3.4` (units rule was different) |
| Display | spectrum `Magnitude (<unit>)` vs `Frequency (Hz)`; display ring `Power Spectrum` / `Power Spectral Density` under it; `THD (%)`, `SINAD (dB)` | | `dsa.md 3.2` |
| Display | time plot `Amplitude (V)` vs `Time (s)` with palette (cursor, zoom, X/Y autoscale locks); `Vpk (V)` = half peak-to-peak; `Sample Rate: 80.00 kS/s` | | `dsa.md 3.4`, `3.5` |
| Display | cursor readouts `d<unit>` (e.g. `ddBm 4.47`) and `df (Hz)` when Cursors On | | `dsa.md 3.7` |
| Input Settings | Source `AI 0`, `AI 1`, `AudioInput Left`, `AudioInput Right`; Voltage Range `+/- 10V`, `+/- 2V` | | `dsa.md 2` |
| FFT Settings | Frequency Span 405 ... 100000; Resolution 200 ... 3200; Window (10 windows) | | `dsa.md 2` (limits corrected) |
| Averaging | Mode `None`/`Vector`/`RMS`/`Peak Hold`; Weighting `Linear`/`Exponential`; # of Averages | | `dsa.md 2` |
| Trigger Settings | Type `Immediate`, Source `TRIG`, Slope, Level - all grayed | | `dsa.md 2` |
| Frequency Display | Units `Linear`/`dB`/`dBm` (Linear = V²); Mode `RMS`/`Peak` (grayed for dBm); Averaging `Restart` (only running + Linear weighting) | | `dsa.md 2` (enable rules were different) |
| Scale Settings | Scale `Auto`/`Manual`; Maximum/Minimum (10 / -120 for dB, 3 decimals) | | `dsa.md 2` |
| Cursor Settings | Cursors On; C1/C2 `Real` (grayed); Cursor Select `C 1`/`C 2`; Left/Right diamonds | spectrum only | `dsa.md 2` |
| Instrument Control | Device, Acquisition Mode (`Run Continuously`, `Run Once`), Run, Stop, Print, Log, Help | Print/Log idle only | `dsa.md 2` |
| Log | TXT: time record (waveform export) + `FFT waveform`, `Units:`, frequency/value rows | | `dsa.md 13` (was **MISSING**) |

## 10. Digital Writer (`DigWriter.exe`)

Window `Digital Writer - NI ELVISmx`, 351 x 536 px, fixed. Screens: `explore/dio/0*`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | `Numeric Value` with radix prefix (menu `Decimal`, `Hex`, `Octal`, `Binary`, `SI Notation`); 8 `Line States` LEDs 7...0 (off navy, on cyan) | | `dio.md 3.2` |
| Configuration | Lines to Write `0 - 3`, `4 - 7`, `0 - 7` | grayed while running | `dio.md 3.2` |
| Configuration | Pattern `Manual`, `Ramp (0-15)` / `Ramp (0-255)`, `Alternating 1/0's`, `Walking 1's` | grayed while running | `dio.md 3.2` (name `Alternating 1/0's` was **MISSING**) |
| Configuration | Manual Pattern: 8 slide switches (lines outside the group grayed), Pattern number (radix prefix), Action `Toggle`, `Rotate`, `Shift`, Direction `Left`/`Right` | only in Manual | `dio.md 3.2` |
| Instrument Control | Device, Generation Mode (`Run Continuously`, `Run Once`), Run, Stop, Help | | `dio.md 3.3` |
| Timing | sequences step every 100 ms; Stop holds outputs | | `dio.md 3.2` |

## 11. Digital Reader (`DigReader.exe`)

Window `Digital Reader - NI ELVISmx`, 357 x 366 px, fixed. Screens: `explore/dio/1*`.

| Group | Control / indicator | Values / behaviour (verified) | MYBENCH |
|---|---|---|---|
| Display | `Numeric Value` (radix prefix menu), 8 LEDs 7...0, Toggle View button | right-justified value (`x 5` for lines 4 and 6 on `4 - 7`) | `dio.md 3.1` |
| Configuration | Lines to Read `0 - 3`, `4 - 7` (default), `0 - 7` | grayed while running | `dio.md 3.1` |
| Instrument Control | Device, Acquisition Mode (`Run Continuously`, `Run Once`), Run, Stop, Help | Stop grayed while idle | `dio.md 3.1` |
| Polling | back to back, no pause | | `dio.md 3.1` (MYBENCH 100 ms) |

## 12. Not explored

- Featured apps: Audio Equalizer never launched (defaults of the routing combos, plots); Data Logger
  real log file, browse dialog and Help panel; Octave Analyzer filter class, dB reference; DC Level
  DAQmx calls. (8-Channel Oscilloscope and Digital Waveform Viewer: out of scope.)
- DMM: Help window content beyond the first page; behaviour with a real signal (continuity tone,
  real overload values) - impossible with the simulated device.
- Scope: TDMS log format; Help; C1/C2 combos while running with CH 1 disabled; Graph Properties
  `Reset to Default` effect; Print to a real printer.
- FGEN: arrow increments of each numeric (only typed values tested); Help.
- ARB: Run Once end state in the UI (only the trace was read); audio output rows in use; TDMS n/a.
- Waveform Editor: Import from File wizards (text, binary, sound), Expression error display, Undo,
  Cut/Copy/Paste, Segment view / Component view rendering, Frequency X axis, Animate Graphs.
- Bode: TDMS log; behaviour when Start > Stop.
- DSA: Run Once with averaging; Peak Hold behaviour; audio sources; Print output page.
- Digital Writer/Reader: Run Once of sequences; Toggle View; radix changes on the numeric display.
- Launcher: tile hover star (favorites) not clicked; Resources tiles not opened.
