# ELVISmx UI inventory (explored) and MYBENCH coverage

Exhaustive checklist of every user-visible capability of the NI ELVISmx programs for the NI myDAQ,
built by driving the real programs (ELVISmx + NI-DAQmx 25.8 on Windows 10, with a **simulated NI
myDAQ** named `myDAQ1` imported into DAQmx) and watching their NI-DAQmx calls with NI I/O Trace.
Everything is written in our own words; control names and value lists are reused so that the
MYBENCH specs can keep them. NI screenshots of this exploration are kept locally in
`ni-local/screens/explore/<app>/` (gitignored, never committed).

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

## 3. DC Level (Featured Application, `DCLevel.exe`)

See [featured-apps.md](featured-apps.md) for the full description and proposal.

| Group | Control / indicator | Values / behaviour | MYBENCH |
|---|---|---|---|
| Window | `DC Level Output`, ~656 x 519, blue header band | | `featured-apps.md` |
| Physical Channel | Select Device | device list; `No Supported Device Found` without a myDAQ | `featured-apps.md` |
| Physical Channel | Channel Settings | `AO 0` (list) | `featured-apps.md` |
| Voltage | Level | numeric + horizontal slider -10.00 ... 10.00 V (ticks -10, -5, 0, 5, 10) | `featured-apps.md` |
| Graph | Voltage (V) chart, Y -10 ... 10 | output level history | `featured-apps.md` |
| Buttons | Start, Stop, Help >> | | `featured-apps.md` |
