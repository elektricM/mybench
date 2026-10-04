# Oscilloscope

The Oscilloscope shows one or two voltages over time. It reads the myDAQ analog inputs **AI 0** and
**AI 1** (±10 V or ±2 V, up to 200 000 samples per second per channel) or the **AUDIO IN** jack (left
and right, ±2 V). It measures RMS, frequency and peak-to-peak automatically (and more on request),
triggers on a rising or falling edge, and has two time cursors, two level cursors, a math trace, an
FFT view, XY mode, reference traces and a roll mode for slow signals.

## Connections

| Signal | myDAQ terminals |
|---|---|
| Channel on AI 0 | **AI 0+** and **AI 0-** (differential) |
| Channel on AI 1 | **AI 1+** and **AI 1-** |
| Ground reference | **AGND** |
| Audio | **AUDIO IN** 3.5 mm jack (left = tip, right = ring) |

The analog inputs are differential: the instrument shows the voltage between AI x+ and AI x-. For a
signal referenced to your circuit's ground, connect AI x- (and the circuit ground) to AGND. If you
measure a floating source such as a battery, connect one side of it to AGND.

The audio inputs are AC-coupled: they block DC and very low frequencies. AI and audio inputs share
the converters, so both channels always use the same kind of input: choosing an audio input on one
channel moves the other channel to an audio input too (and back to AI when you choose AI again).

## Quick start

1. Connect your signal to AI 0+ / AI 0- (and AGND).
2. Press **Run** (or Space). The CH0 trace appears, with its RMS, frequency and peak-to-peak as
   cards under the graph.
3. Press **Autoscale** — or turn the **Scale** knob (CH 0 card) and the **Timebase** knob by hand.
4. For a stable picture of a repetitive signal, choose **Auto** or **Normal** next to the trigger in
   the bar and turn the **Trigger level** knob (or drag the **T** marker on the right edge of the
   graph) to a level the signal crosses.

## The pane

- **Bar** (top): **Run / Stop** with the state under it — *Running*, *Triggered*, *Waiting for
  trigger* (Normal, no edge within the timeout), *Timeout — free running* (Auto) —, **Single**, the
  **Timebase** knob (with the sample rate and record length under it), the **Trigger level** knob
  (colored like its source channel, with source and slope) and the trigger mode **Auto / Normal /
  None**, the view **YT / XY / FFT**, **Autoscale** and **⋯** (Export Data…, Export Image…, Print…,
  Trace Style…, Show Measurements, Center Cursors, Reset Settings…, Help).
- **Display**: the graph has 10 × 10 divisions and no axis numbers. The line at the top left sums up
  the timebase and the trigger (`5 ms/div · Edge ↑ CH0 0.00 V`), the legend under the graph the scale
  of every shown trace (`CH0 1 V/div`).
- **Right column**: a card per channel (switch, **Scale** and **Position** knobs, source, input
  range, and what is in effect: coupling, range, differential), the **Math** and **References**
  cards, and the tool palette — **Measure, Cursors, Math, FFT, Refs, Display, Trigger, Export**. A
  tool opens a panel over the right part of the graph; click the tool again or × (Esc) to close it.
- **Measurement cards** under the display: the value large, with min / max / mean / σ over the last
  100 records (or all records, or none: Measure › Statistics). **+ Add** opens the Measure panel.
- Every knob can be turned (drag, wheel), stepped with − / +, or typed (`2m`, `500u`, `1.5k`).
  In a narrow pane (split screen) the knobs give way to their values, and the view and trigger mode
  move to the top of the right column.

## The graph

- Drag the markers on the edges: **0 / 1 / M** on the left move a trace (its Position), **T** on the
  right sets the trigger level, **T** at the top the trigger position.
- The mouse wheel over the graph changes Time/div (up = faster); Shift + wheel changes the Scale of
  the channel whose zero line is nearest the pointer.
- Hover: a crosshair and a tooltip show the value of every trace at the pointer.
- The tools at the top right (shown on hover) and the right-click menu zoom, pan, autoscale the
  view, reset it, show the cursors, copy the image and export the data. A double-click fits the view.

## Settings

Where they are: Channel 0 / 1 → the channel cards; Timebase → the bar (record length: Display tool;
roll: Trigger tool); Trigger → level and mode in the bar, the rest in the Trigger tool (None in the
bar = the Immediate type, Normal + *Clear the graph while waiting* = Clear); Math → the Math card and
tool; FFT → View FFT in the bar and the FFT tool; Cursors, References, Display → their tools;
Analysis → Frequency method in the Measure tool, Autoscale option in the Display tool.

### Channel 0 and Channel 1

- The **switch** in the section header turns the channel on or off. Settings of a channel that is
  off can still be changed.
- **Source** – AI 0, AI 1, Audio Left or Audio Right. If both channels use the same source it is
  acquired once and shown twice.
- **Scale** – 10 mV to 5 V per division for AI inputs, 10 mV to 1 V for audio. Use ‹ › , the arrow
  keys, the wheel, or click the value for the list.
- **Position** – −5 to +5 divisions. It only moves the trace; data, measurements and cursor values
  are unchanged. Drag the label to scrub the value.
- **Input range** (AI inputs only) – *Auto* uses ±10 V from 500 mV/div and ±2 V below (finer
  resolution); or force ±10 V / ±2 V.
- The line under the section states the coupling (DC for AI, AC for audio, whose average is removed)
  and the range in use. They follow the source; they are not settings.

### Timebase

- **Time/div** – 5 µs to 200 ms per division (10 divisions across). The sample rate and the number of
  samples are shown under the section: full speed (200 kS/s) up to 1 ms/div, then about 2 500
  samples per screen (e.g. 50 kS/s at 5 ms/div). At 5 µs/div only 11 samples fit on the screen: the
  myDAQ cannot sample faster.
- **Position** (Edge trigger only) – where the trigger point sits on the screen: 0 % left edge, 50 %
  the middle (you see what happened before and after it), 100 % the right edge.
- **Roll** (from 50 ms/div) – the trace scrolls continuously from right to left like a chart
  recorder, without waiting for a full screen. Roll mode does not use the trigger.
- **Record length** – *Normal* (up to 2 501 samples per screen) or *Long* (up to 100 000 samples,
  200 kS/s up to 50 ms/div) for zooming and the FFT.

### Trigger

- **Type** – *Immediate*: every screen is shown as acquired. *Edge*: every screen is aligned on a
  crossing of the trigger level. With Immediate the other trigger settings are hidden.
- **Source** – *CH0* or *CH1*: the signal of that channel (a channel that is off cannot be chosen).
- **Slope** – rising or falling edge.
- **Level** – the voltage the signal must cross, −100 to 100 V (or drag the **T** marker).
- **Mode** – what happens when no edge arrives within the timeout:
  - *Auto* shows the untriggered signal, so the trace never freezes;
  - *Normal* keeps the last triggered trace (faded) and waits;
  - *Clear* empties the graph and waits (as ELVISmx).
- **Timeout** – 100 ms to 10 s, or *Never*. Nothing changes on screen before it has passed, so rare
  events no longer replace the trace with a waiting message.
- **Holdoff** – 0 to 10 s minimum time between two shown triggers (steadies bursts).
- **Hysteresis** – 0 to 2 divisions of the source channel; the signal must first move this far away
  from the level, which makes noise around the level harmless. 0 = as ELVISmx.
- The trigger is found in software. Every acquisition holds two screens; while no edge turns up the
  search window grows (up to about a quarter of a second of samples) so that rare events are caught
  even on fast time bases. The edge is placed with sub-sample accuracy: the crossing sits exactly at
  the trigger position on every screen (no jitter, even at 5 µs/div).

### Math

The switch turns on a trace computed from the shown record: **CH0 + CH1**, **CH0 − CH1**,
**CH0 × CH1**, **−CH0** or **−CH1**, with its own **Scale** and **Position**. The section tells you
when a channel it needs is off. The math trace is not available to the trigger.

### FFT

**Source** *Off*, *CH0* or *CH1* opens the spectrum of that channel under the graph: the magnitude of
the shown record (Hann window, dBV RMS) with the strongest peak marked. Zoom and pan it like any plot;
the zoom stays until you reset it. For averaging, other windows and distortion use the Spectrum
Analyzer.

### Cursors

- The **switch** in the section header shows two vertical cursors, C1 and C2. Drag them, or click one
  and use ← → to move it sample by sample. They snap to the samples of their trace.
- **C1 trace / C2 trace** – the trace each cursor follows (CH0, CH1 or Math).
- **Style** – a line, a full crosshair, a small crosshair, or hidden (the readings stay).
- **Level cursors** – two horizontal lines H1 and H2 on the **Level trace**; drag them at the right
  edge of the graph.
- The readings are rows of the measurement table (see below). **⋯ › Center Cursors** brings both
  cursors back to 25 % and 75 % of the screen.

### References

**Save** (the disk button) keeps CH0, CH1 or Math of the shown record as REF A or REF B, drawn with
the scale and position it had. The eye hides or shows it, the arrows export it to or import it from a
CSV/TSV file (time in the first column, volts in the second), the bin clears it.

### Display

- **Mode** – *YT* (voltage over time) or *XY* (CH1 vertically over CH0 horizontally, each with its
  Scale and Position), e.g. for Lissajous figures or I–V curves.
- **Persistence** – earlier traces fade out over the chosen time. Changing a scale, a position, the
  timebase, the trigger, a source or the mode clears the afterglow, so traces of another setting never
  stay on screen.
- **Averaging** – off, or a running mean of the last 2 … 64 triggered records of each channel: less
  noise on repetitive signals. It restarts when a scale, the timebase, a source or the trigger changes.
- **Trace Styles…** – color, line width and style, point markers, interpolation and fill of CH0, CH1,
  Math, REF A, REF B, and color, line and marker of the two cursors. Changes show at once; Cancel
  undoes them. Dark or light plot backgrounds follow the app's appearance and the *Always Use Dark
  Displays* preference.

### Analysis

- **Frequency** – *Tone* finds the strongest tone in the spectrum (shown only when it is larger than
  0.2 division, else `?`); *Edges* counts mid-level crossings, better for square and pulse signals.
- **Autoscale** › *Sets trigger level* – with an Edge trigger, Autoscale also moves the level to the
  middle of the signal.

## Measurements

The **Measure** tool shows every measurement as a picture button: click one to add its cards, click
it again (or × on a card) to remove them. **Cards for** shows the cards of every trace or of one.

- **RMS** – root-mean-square voltage of the whole screen (includes the DC part on AI inputs).
- **Frequency** – see Analysis › Frequency.
- **Peak-to-peak** – highest minus lowest voltage on the screen.
- **Peak** – the largest absolute voltage on the screen (max |v|), e.g. to check a limit.
- Also: Mean, Minimum, Maximum, Amplitude (high − low), Period, Frequency by edge counting, Duty
  cycle, Rise and Fall time (10–90 %), the positive and negative pulse widths, AC RMS (DC removed),
  Crest factor (peak / RMS), Overshoot and Preshoot (% of the amplitude), Slew rate (mean slope of
  the rising edge) and Phase (CH1 relative to CH0, corrected for the delay between the channels; it
  needs both channels and a periodic signal).
- The math trace has RMS and peak-to-peak.
- With the cursors on: **C1** and **C2** (the time of the cursor, then every trace's value there),
  **ΔV** (C2 − C1 per trace), **Δt** and **1/Δt**. Remove the rows you do not need; *Cursor C1 Values*,
  *Cursor C2 Values* and *Cursor Differences* in the **+** list bring them back.
- With the level cursors on: **H1**, **H2** and **ΔH** in the column of their trace.

Values are written with three or four significant digits and the unit (`707.11 mV`, `1.000 kHz`);
`?` means undefined (no record yet, or no tone found). In a short window the table shrinks to one
summary line; **⋯ › Show Measurements** hides it.

## Run, Single, Autoscale

- **Run / Stop** (Space) – acquire continuously, or stop.
- **Single** (Shift+Space) – one screen, then stop. With the Edge trigger it waits until a crossing
  is found (or you press Stop).
- **Autoscale** – measures the enabled channels on their widest range and sets Scale and Time/div
  (about five periods on screen). It works while running or stopped; stopped, it only changes the
  settings. Position and the trigger are not changed (unless *Sets trigger level* is on).

## Export

- **Export Data…** (⋯ or File menu) saves the shown record: *CSV* or *TSV* (time and the voltage of
  each enabled channel and Math, the settings in the header lines), or the *ELVISmx-style text log*
  (`.txt`, tab-separated with absolute time stamps, for lab scripts written for ELVISmx).
- **Export Image…** and **Print…** save or print the display with an optional settings table.

## Tips

- Use ±2 V signals with a Scale of 200 mV/div or less for the best resolution.
- Combine the Oscilloscope with the Waveform Generator: connect AO 0 to AI 0+ and AGND to AI 0- to see
  the generated signal.
- The Oscilloscope shares the analog inputs with the Spectrum Analyzer, the Bode Analyzer and the
  Data Logger. If you start one of them, the Oscilloscope pauses; press **Resume** in its banner to
  continue.

## Troubleshooting

- **Flat line at the top or bottom**: the signal is outside the input range or the screen; press
  Autoscale.
- **Trace jumps around**: use the Edge trigger and set the Level inside the signal's range.
- **"Waiting for trigger"**: no edge crossed the level within the timeout; move the T marker into the
  signal, check the slope and the source, or choose the Auto mode.
- **Frequency shows `?`**: the tone is smaller than 0.2 division (choose a smaller Scale) or less
  than one period is on screen (choose a larger Time/div).
