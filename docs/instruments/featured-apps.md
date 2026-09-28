# Featured Applications and Instrument Launcher - what they do and how MYBENCH provides them

The ELVISmx Instrument Launcher shows, after the instruments, six small "Featured Applications"
(separate LabVIEW programs). This file describes each one in our own words (from the running
programs on a simulated NI myDAQ and from the code study in
[algorithms/featured.md](algorithms/featured.md)) and proposes how MYBENCH offers the same
capability. Status per app: **explored** (UI observed), **partly explored**, or **not explored**
(described from the code study only). Screens: `ni-local/screens/explore/<app>/` (gitignored).

Note: specs [log.md](log.md) (Data Logger) and [aeq.md](aeq.md) (Audio Equalizer) were added by
another agent in parallel; where they exist they are the binding specs and the proposals below are
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
- Behaviour (code): Start writes the level as a single static sample; every change writes the new
  value; Stop writes 0 V and clears the task.

**MYBENCH proposal**: a **DC** mode in the Waveform Generator channel cards (Mode `Off` /
`Function` / `Arbitrary` / **`DC`**) with a Level SiField + slider (-10 ... 10 V, 0.01 V steps),
written with `aoWriteStatic` when the generator is otherwise idle or inside the stream otherwise;
Stop -> 0 V. Also reachable from the command palette ("Set AO 0 to ... V"). A tiny history chart is
optional.

## 3. Data Logger - partly explored

What it does: slow continuous logging of AI voltages to a chart and a text file.
- Observed: channel list with LED toggles `ai0` (on by default) and `ai1` (off) plus six grayed
  rows (ELVIS channels); `Sampling Rate (S/s)` default **1000**, coerced to **1 ... 1000**;
  `Minimum (V)` -10 / `Maximum (V)` 10; strip chart `Voltage (V)` vs absolute `Time (s)` with
  `Auto-scale graph` checkbox; `Log` button (enabled only while running) and `File path for saved
  data` with a browse button; Start / Stop / Help >>. Running greys the device, channels, rate and
  limits; each channel gets its own trace color (ai0 blue, ai1 red observed).
- Code study: continuous task, 1-second blocks, logging writes a tab-separated file with a time
  column and a value column per channel (see algorithms/featured.md 3).
- Not observed: the file dialog, the actual log file, Help text.

**MYBENCH proposal**: a new **Data Logger** panel (`logger`): channels AI 0, AI 1 (and optionally
the DMM reading and DIO lines as extra columns), rate 1 ... 1000 S/s (AI) with Min/Max range per
channel, strip chart with time window and autoscale, Start/Stop, `Record to file` (CSV via
DataExport, append every second, header per app.md 10.1, columns `Time (ISO)`, `Elapsed (s)`,
one per channel), file size/duration indicator. Holds resource `AI` (arbiter rules apply). The
Multimeter's logging card (dmm.md) stays for DMM-only use.

## 4. Octave Analyzer - not explored (code study only)

What it does: real-time 1/3-octave band spectrum of AI channels: 50 kS/s continuous, blocks of
1000 samples, exponential averaging `Slow` / `Fast` (default) / `Impulse` / `Custom` (time constant
ms, default 125), band range Low/High (centre frequencies), weighting `Linear`, `A`, `B`, `C`,
time graph and octave bar graph (20 Hz ... 20 kHz, dB), "Settled?" LED.

**MYBENCH proposal**: an **Octave** view inside the Spectrum Analyzer (`dsa`): Display selector
`Spectrum` / `1/3 Octave` (and `1/1 Octave`); band range, frequency weighting (Z/A/B/C) and
exponential time weighting (Slow/Fast/Impulse/Custom) in an "Octave Settings" card; bar graph in
dB with band labels; uses the same AI acquisition (rate forced to 50 kS/s or the nearest valid).

## 5. Audio Equalizer - not explored (code study only)

What it does: live three-band tone control from an input to an output: inputs L/R (`ai0`, `ai1`,
audio inputs, none), outputs L/R (`ao0`, `ao1`, audio outputs, none), Bass / Midtone / Treble
sliders -12 ... +12 dB (4th-order Bessel bands: low-pass 320 Hz, band-pass 320-2560 Hz, band-pass
2560-20 840 Hz), Volume 0 ... 1, time and 1/24-octave spectrum graphs; AO 50 kS/s streaming with
0.25-0.35 s latency.

**MYBENCH proposal**: an **Audio Equalizer** panel (`eq`) - AI (or audio in) -> DSP -> AO (or audio
out) passthrough using the Generator's streaming engine: three bands with the same corner
frequencies and gains, volume, bypass switch, input/output level meters, latency indicator, and a
live spectrum (reusing the Spectrum Analyzer plot). Holds `AI` and `AO`. Useful for audio labs;
priority lower than the Data Logger.

## 6. 8-Channel Oscilloscope - not explored (code study only)

What it does: a simple scope with a channel list (2 channels on a myDAQ), Time/Div 50 µs ... 50 ms,
one Volts/Div for all channels (20 mV ... 2 V), immediate / software analog edge trigger, full-rate
acquisition, autoscale. On a myDAQ it offers nothing beyond the Oscilloscope.

**MYBENCH proposal**: no separate panel; the Oscilloscope covers it. Two useful ideas are taken
into the Scope as options: `Full-rate acquisition` (the Scope's `High resolution` record mode,
scope.md 4.2) and a `Same scale for all channels` lock.

## 7. Digital Waveform Viewer - not explored (code study only)

What it does (ELVIS II only): hardware-timed capture of the 8 DIO lines together with one AI
channel at 100 kS/s for 0.1 ... 5 s, then browsing (Time/Div, Navigate To, cursors, value
representation Decimal/Hex/Binary). The launcher hides it on a myDAQ, which has no hardware-timed
digital input.

**MYBENCH proposal**: the Digital I/O panel's software-timed **Timeline** (dio.md 4) already covers
the viewing part at ms resolution; add Decimal/Hex/Binary value display at the cursors and an
optional analog trace (AI 0 via the arbiter) aligned on the same time axis. Hardware-timed capture
is not possible on the myDAQ.

## Not explored / to do for a follow-up agent

- Octave Analyzer, Audio Equalizer, 8-Channel Oscilloscope, Digital Waveform Viewer: not launched
  (defaults, value lists and help text unverified).
- Data Logger: log file format, file dialog, `Help >>` panel not checked.
- DC Level: chart behaviour and DAQmx calls not captured.
