# Featured Applications and Instrument Launcher - what they do and how MYBENCH provides them

The ELVISmx Instrument Launcher shows, after the instruments, six small "Featured Applications"
(separate LabVIEW programs). This file describes each one in our own words (from the running
programs on a simulated NI myDAQ) and proposes how MYBENCH offers the same
capability. Status per app: **explored** (UI observed), **partly explored**, or **not explored**
(not launched); 8-Channel Oscilloscope and Digital Waveform Viewer are **out of
scope** (already mapped to Scope / Digital I/O). Screens: `ni-local/screens/explore/<app>/` (gitignored).

Note: specs [log.md](log.md) (Data Logger) and [aeq.md](aeq.md) (Audio Equalizer) were added by
specs written separately; where they exist they are the binding specs and the proposals below are
input to them.

Common to all featured apps (observed on DC Level and Data Logger): a fixed window with a blue
title band (app name), `Select Device` combo (`No Supported Device Found` without a myDAQ/ELVIS),
buttons `Start`, `Stop` (grayed while idle), `Help >>` which widens the window with a side panel of
two vertical tabs `Introduction` / `How to Use` (static text); the device combo is grayed while
running. They accept NI myDAQ, NI ELVIS II and II+ only.

## 1. Instrument Launcher (`niiql.exe`) - explored

- Resizable window, left sidebar with five pages: **Instruments & Apps** (tile grid of all ELVISmx
  instruments, including ELVIS-only ones, followed by the six featured apps and an `Add Item` tile),
  **Favorites** (tiles starred with the star shown on a hovered/selected tile; empty by default),
  **Labs** (courseware-installed lab documents; empty here), **My Files** (user-added file
  shortcuts, `Add Item` -> `Browse`), **Resources** (document/web tiles: ELVIS II and myDAQ manuals
  and specifications, ELVISmx Help, How to Configure Your Hardware, Measurement & Automation
  Explorer, Discussion Forum, What is LabVIEW?, Using ELVISmx with Multisim, `Add Item`).
- `Add Item` on Instruments & Apps opens an inline card with `Browse` (add any program as a tile)
  and `Find More Online` (web page of downloadable apps). No right-click menus.
- Clicking a tile starts the program (one window each).

**MYBENCH proposal** (`app.md 18`): nav rail + Home tiles replace the Instruments page (one tile per
MYBENCH panel, including the new featured-app panels below). Add to Home: a **Favorites** strip
(pin/star any panel or tool), a **Resources** menu under Help (myDAQ user guide and specifications
links, MYBENCH help pages, project web site/forum) and **My Files** = recent sessions and exported
files (open with the system). `Add Item`/external program tiles and Labs are not replicated
(MYBENCH is not a launcher for other programs); a "Tools" menu may run user-configured external
commands later.

## 2. DC Level - explored

What it does: sets a constant voltage on AO 0 or AO 1.
- Controls: Select Device; `Channel Settings` `AO 0` / `AO 1`; `Voltage` group `Level` numeric +
  horizontal slider -10.00 ... 10.00 (typed 15 is coerced to 10); a `Voltage (V)` chart -10 ... 10
  (stayed empty on the simulator); Start / Stop / Help >>. Device and channel are grayed while
  running; Level stays live.
- Behaviour: Start writes the level as a single static sample; every change writes the new
  value; Stop writes 0 V and clears the task.

**MYBENCH proposal**: a **DC** mode in the Waveform Generator channel cards (Mode `Off` /
`Function` / `Arbitrary` / **`DC`**) with a Level SiField + slider (-10 ... 10 V, 0.01 V steps),
written with `aoWriteStatic` when the generator is otherwise idle or inside the stream otherwise;
Stop -> 0 V. Also reachable from the command palette ("Set AO 0 to ... V"). A tiny history chart is
optional.

## 3. Data Logger - explored (UI)

What it does: slow continuous logging of AI voltages to a chart and a text file.
- Observed: channel list with LED toggles `ai0` (on by default) and `ai1` (off) plus six grayed
  rows (ELVIS channels); `Sampling Rate (S/s)` starts at **1000** (the saved control value is 10, the
  app overrides it from the `SamplingRate` key of its ini, default 1000), coerced to **1 ... 1000**;
  `Minimum (V)` -10 / `Maximum (V)` 10; strip chart `Voltage (V)` vs absolute `Time (s)` with
  `Auto-scale graph` checkbox; `Log` button (enabled only while running; caption `Logging...`
  while on) and `File path for saved data` with a browse button; Start / Stop / Help >>. Running
  greys the device, channels, rate and limits; each channel gets its own trace color (ai0 blue,
  ai1 red observed).
- Continuous AI task, buffer 10 x rate, 1-second blocks; a producer/consumer queue writes the
  file (commands: write header, write data, reset error, stop).
- **File format (details and confidence in [log.md 8](log.md))**: plain TAB-separated
  text, CR LF lines, no comment lines. One header row written when `Log` is pressed: per logged
  channel the pair `Time`, `<device>/ai<n>` (for example `Time<TAB>myDAQ1/ai0`). Then one data
  row per sample, appended once per second: per channel its own time cell (absolute local date and
  time, `M/D/YYYY  HH:MM:SS.ffffff`, two spaces between date and time, region-dependent date)
  followed by the voltage as `d.dddddd` `E` exponent (`5.174413E+0`). The file is always appended
  to, never replaced. The path control browses `*.lvm` starting in `C:\Users\Public\Documents`;
  an empty/invalid path refuses to log with an error dialog. Format is the same library writer
  used by the Scope/DSA `Log` files (real samples of those confirm the cell formats).
- Still unknown (needs one real file, listed in log.md 8.4): exact header row text and trailing
  tab, dialog title/file types/default name, extension handling, repeat-`Log` behaviour.
- Not observed: `Help >>` text.

**MYBENCH proposal**: a new **Data Logger** panel (`log`, exists): channels AI 0, AI 1 (and
optionally the DMM reading), rate 1 ... 1000 S/s with Min/Max, strip chart with time window and
autoscale, Start/Stop, `Record to file` (native layout with header lines, or the
**ELVISmx-compatible layout** of log.md 8.2), file size/duration indicator. Holds resource `AI`.

## 4. Octave Analyzer - explored (screens)

What it does: real-time third-octave band spectrum of one AI channel (or several) with exponential
time averaging and A/B/C weighting. Full detail (every control, value list, default, range,
readout, differences from MYBENCH) is in [dsa.md 14](dsa.md); summary with sources and confidence:

- Window (fixed size, blue title band, Help >>): `Physical Channel` group: `Select Device` combo,
  `Channel(s)` combo (`<dev>/ai0`, `ai1`, `audioInputLeft`, `audioInputRight`, `dmm`; `dmm` is
  refused at Start), `Minimum (V)` / `Maximum (V)` (-10 / 10); `Exponential Averaging` group:
  `Mode` (list below) and `Time Constant (ms)`; `Frequency` group: `Low Band
  (Hz)`, `High Band (Hz)`, `Weighting`; two tabs `Time` and `Frequency`; Start / Stop / Help >>.
  (screens 01-04, H)
- `Mode` list: **Slow, Fast (default), Impulse, Custom**; `Time Constant (ms)` (default 125, range
  0 ... 10000 coerced, step 1) is only editable in Custom; otherwise it is grayed and keeps its last value (125 at start, 0 after a Custom entry of 0). (03, 05, 06, 09, H)
- **Low Band / High Band are not frequencies.** They are indices into the 31 nominal third-octave
  bands 20 Hz ... 20 kHz (0 = 20 Hz, 5 = 63 Hz, 20 = 2 kHz, 30 = 20 kHz), defaults **0 and 30**
  (full range). Values above 30 are clamped in use (High 1000 shows all bands; Low 100 leaves the
  single 20 kHz band). The field caption `(Hz)` is misleading. (screens 10-15, H)
- `Weighting`: **Linear, A-weighting, B-weighting, C-weighting** (default Linear), applied to the
  band powers; live while running (like Low/High Band and Mode). (04, 12, H)
- Running greys: device, channel, Minimum, Maximum. Mode/Time constant/Low/High/Weighting stay
  editable. `Stop` becomes active, `Start` grays. (09, H)
- `Time` tab: graph `Voltage (V)` vs `Time (s)`, X axis in absolute clock time (`hh:mm:ss AM/PM`,
  span unresolved: one 20 ms block is likely, L), a `Settled` indicator under the graph (text
  `Settled`/`Not settled`). `Frequency` tab:
  band power as a staircase (one flat step per band), X log 20 Hz ... 20 kHz (grid at 20, 200,
  2000, 20000), Y autoscaled, axis title `Band Power (dB ref 20u Pa)` (fixed text). (09, 10, H)
- Engine: 50 kS/s continuous, 1000-sample reads (20 ms), SVT third-octave analysis with the
  exponential averaging parameters, then SVT octave weighting; an overrun error `-200279` (cannot
  keep up) appeared on the slow test VM (explorer checkpoint note, M). (constants H)

**MYBENCH proposal (already built as the Octave mode of `dsa`)**: see dsa.md 14.4 for the delta.

## 5. Audio Equalizer - not launched; spec in [aeq.md 8](aeq.md)

What it does: live three-band tone control from an input to an output.
- Panel (from the front panel resources): `Select Device`; combos `Input (L)`, `Input (R)`,
  `Output (L)`, `Output (R)` (lists built at run time: `ai0`, `ai1`, `audioInputLeft/Right`, `none`;
  `ao0`, `ao1`, `audioOutputLeft/Right`, `none`); sliders `Bass`, `Midtone`, `Treble` -12 ... +12
  (step 1, ticks -10/0/10; saved value **+1** each) and `Volume` 0 ... 1 with end captions `Min` /
  `Max` (saved 0.4965, on-load 0.5); tabs `Time` and `Frequency`, each with its own `Auto-scale
  graph` checkbox (both on); Start / Stop / Help >>. **No level meters, no bypass, no clip
  indicator exist in ELVISmx** (those are MYBENCH additions). (FP resources, H)
- Time graph: X 0 ... 0.1 s (grid 0.02 s), Y starts at +-1 mV (autoscale on), two plots (colors blue
  0x065FA3 and red 0xFF4242). Frequency graph: X log 25 Hz ... 25 kHz (grid 250, 2500), Y from -140
  step 20 upward, autoscale on, 1/24-octave band levels. (FP, H; which plot is input vs output: L)
- Engine: AI +-2 V and AO +-2 V; AO 50 kS/s continuous, regeneration off, buffer 1 000 000; AI
  reads 10 000 samples. Two configurations selected by testing the actual sample-clock rate
  against 99 kS/s: (A) 50 kS/s with a 0.25 s AI-to-AO delay, (B) AI at 100 kS/s, AO 50 kS/s, delay
  0.35 s; which one applies to which channel type is not settled (L). Channels get the names
  `Audio Input` / `Audio Output`. Filters: three Bessel order-4 IIR filters (lowpass 320 Hz;
  bandpass 320-2560 Hz; bandpass 2560-20 840 Hz); band gain 10^(dB/20); volume last. Frequency
  display: 1/24-octave, 25 Hz ... 25 kHz, exponential averaging Custom 10 ms, weighting index 3. (H
  constants, M roles)

**MYBENCH proposal**: `aeq` panel (exists). Differences and unknowns: aeq.md 8.

## 6. 8-Channel Oscilloscope - out of scope (mapped to Scope)

(Status: covered by the `scope` instrument; not launched.)

What it does: a simple scope with a channel list (2 channels on a myDAQ), Time/Div 50 µs ... 50 ms,
one Volts/Div for all channels (20 mV ... 2 V), immediate / software analog edge trigger, full-rate
acquisition, autoscale. On a myDAQ it offers nothing beyond the Oscilloscope.

**MYBENCH proposal**: no separate panel; the Oscilloscope covers it. Two useful ideas are taken
into the Scope as options: `Full-rate acquisition` (the Scope's `High resolution` record mode,
scope.md 4.2) and a `Same scale for all channels` lock.

## 7. Digital Waveform Viewer - out of scope (mapped to Digital I/O)

(Status: ELVIS II only; the timeline of `dio` covers it; not launched.)

What it does (ELVIS II only): hardware-timed capture of the 8 DIO lines together with one AI
channel at 100 kS/s for 0.1 ... 5 s, then browsing (Time/Div, Navigate To, cursors, value
representation Decimal/Hex/Binary). The launcher hides it on a myDAQ, which has no hardware-timed
digital input.

**MYBENCH proposal**: the Digital I/O panel's software-timed **Timeline** (dio.md 4) already covers
the viewing part at ms resolution; add Decimal/Hex/Binary value display at the cursors and an
optional analog trace (AI 0 via the arbiter) aligned on the same time axis. Hardware-timed capture
is not possible on the myDAQ.

## Not explored / to do

- Data Logger: real log file, browse dialog, `Help >>` panel (manual session, log.md 8.4).
- Octave Analyzer: filter class (ANSI S1.11 or IEC 61260) and the dB reference of the band values,
  band level accuracy with a known sine (manual session).
- Audio Equalizer: never launched; defaults of the four channel combos, which of the two rate/delay
  configurations applies, what the two plots show, weighting index meaning (manual session).
- DC Level: chart behaviour and DAQmx calls not captured.
