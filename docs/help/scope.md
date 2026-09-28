# Oscilloscope

The Oscilloscope shows one or two voltages versus time. It reads the myDAQ analog inputs **AI 0** and
**AI 1** (±10 V or ±2 V, up to 200 000 samples per second per channel) or the **AUDIO IN** jack (left
and right, ±2 V). It measures RMS, frequency and peak-to-peak automatically (and more on request),
triggers on a rising or falling edge, and has two cursors, a math trace, XY mode, reference traces
and a roll mode for slow signals.

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
2. Press **Run**. The CH 0 trace (green) appears, with RMS, Freq and Vp-p under the graph.
3. Turn **Time/Div** until a few periods are visible and **Volts/Div** until the trace fills the
   screen — or press **Autoscale**.
4. For a stable picture of a repetitive signal, set Trigger **Type** to *Edge* and drag the **T**
   marker on the right edge of the graph to a level the signal crosses.

## The display

- The graph has 10 × 10 divisions and no axis numbers: read the scales from the channel chips at the
  bottom of the graph (`CH 0 1 V/div`) and the header (`5 ms/div`, `Sample Rate: 50.00 kS/s`). Click
  a chip to switch a channel on or off.
- Markers on the edges can be dragged: **0 / 1 / M** on the left move a trace (Vertical Position),
  **T** on the right sets the trigger level, **T** at the top sets the Horizontal Position.
- Mouse wheel over the graph changes Time/Div (up = zoom in); Shift + wheel changes Volts/Div of
  the channel whose zero line is nearest the pointer. Ctrl/⌘ + drag zooms into a region and
  **Reset zoom** or a double-click returns to the full screen. The trigger level line is red.
- Under the graph: the cursor line (when Cursors On), one measurement line per channel in the
  channel's color, the math line, and the **Timeout** LED.
- `*** Waiting for Trigger ***` appears in the header while the Edge trigger finds no crossing.

## Controls

### Channel 0 Settings / Channel 1 Settings

- **Enabled** (switch in the card header) – acquires and shows the channel. CH 0 is green, CH 1 cyan.
  The settings of a disabled channel can still be changed.
- **Source** – AI 0, AI 1, AudioInput Left or AudioInput Right. If both channels use the same
  source it is acquired once and shown twice.
- **Probe** – always 1x on the myDAQ. **Coupling** – set by the source: DC for AI inputs, AC for the
  audio inputs (the average of the record is removed).
- **Scale Volts/Div** – knob and list, 10 mV to 5 V per division for AI inputs, 10 mV to 1 V for
  audio. The input range follows: ±10 V from 500 mV/div, ±2 V below (finer resolution).
- **Vertical Position (Div)** – −5 to +5 divisions. It only moves the trace; data, measurements and
  cursor values are unchanged.

### Timebase

- **Time/Div** – 5 µs to 200 ms per division (10 divisions across). The sample rate and the number of
  points are chosen as ELVISmx does and shown under the list: full speed (200 kS/s) up to 1 ms/div,
  then about 2500 points per screen (e.g. 50 kS/s at 5 ms/div). At 5 µs/div only 11 samples fit on
  the screen: the myDAQ cannot sample faster.
- **Roll mode** (from 50 ms/div) – the trace scrolls continuously from right to left like a chart
  recorder, without waiting for a full screen. The trigger is not used in roll mode.

### Trigger

- **Type** – *Immediate*: each screen is shown as acquired. *Edge*: each screen is aligned on a
  crossing of the trigger level; choosing Edge selects *Chan 0 Source*. (*Digital* exists only on NI ELVIS.)
- **Source** – *Chan 0 Source* or *Chan 1 Source*: the signal of that channel (it must be enabled).
- **Slope** – rising or falling edge.
- **Level (V)** – the voltage the signal must cross (drag the **T** marker on the right).
- **Horizontal Position (%)** – where the trigger point sits on the screen: 0 % left edge, 50 % the
  middle (you see what happened before and after it), 100 % the right edge.
- The myDAQ triggers in software on a record twice as long as the screen: a crossing must occur within
  about one screen. Use a slower Time/Div if the trace does not trigger.

### Timeout LED

Lights when the Edge trigger found no crossing in the last acquisition (level outside the signal,
wrong slope or source). As in ELVISmx the graph is then cleared until the next triggered record;
Advanced Settings can keep the last trace (dimmed) or show untriggered records instead.

### Measurements

For each enabled channel ticked in **Display Measurements**:

- **RMS** – root-mean-square voltage of the whole screen (includes the DC part on AI inputs).
- **Freq** – frequency of the strongest tone (FFT). It is shown only when that tone is larger than
  0.2 division, otherwise `?`. For square or pulse signals choose *Edge counting* in Advanced Settings.
- **Vp-p** – highest minus lowest voltage on the screen.

Values are written like ELVISmx: `707.11 mV`, `1.000 kHz`, `?` when undefined. **Graph Properties ›
Measurements** hides items and adds Mean, Minimum, Maximum, Amplitude (high − low), Period, Frequency
(counter), Duty Cycle, Rise and Fall Time (10–90 %) and the positive and negative pulse widths.

### Cursors Settings

- **Cursors On** (switch) – two vertical cursors C1 and C2 on the graph. Drag them, or click one and
  use ← → to nudge it. They snap to the samples of their channel.
- **C1 / C2** – the trace each cursor reads (CH 0, CH 1, or Math).
- The cursor line shows each cursor's voltage, **dT** (time between the cursors), **1/dT** and
  **ΔV**. A cursor on a disabled channel reads 0.00 V.

### Math

Switch it on to draw an amber trace computed from the displayed record: **CH 0 + CH 1**,
**CH 0 − CH 1**, **CH 0 × CH 1**, **−CH 0**, **−CH 1** (with its own scale and position), or the
**FFT** of CH 0 or CH 1: a magnitude spectrum in dBV under the graph with the strongest peak marked.
For averaging, windows and distortion measurements use the Spectrum Analyzer.

### Reference Traces

**Save** freezes CH 0, CH 1 or Math as REF A or REF B (violet, dashed), drawn with the scale and
position it had. The eye button hides it, the arrows save it to or load it from a CSV/TSV file
(time in the first column, volts in the second), the bin clears it.

### Display

- **Mode** – *YT* (voltage versus time) or *XY* (CH 1 vertically versus CH 0 horizontally, with each
  channel's Volts/Div and position), e.g. for Lissajous figures or I–V curves.
- **Persistence** – previous traces fade out over the chosen time (phosphor look).

### Graph Properties

- **Plots** – for CH 0, CH 1, Math, REF A and REF B: line color, width (none … 5 px, default 1),
  line style (solid, dash, dot, dash-dot, dash-dot-dot), point style (ten markers) and its Point /
  Fill color, interpolation (points, linear and four step modes), fill (to zero, −∞ or +∞);
  graph background (theme, black, white).
- **Cursors** – color, line style and width, cursor style (vertical line, full crosshair, small
  crosshair, none), point style and linked trace.
- **Measurements** – which readouts are shown per channel, and C1, C2, dT for the cursor line.
- **Reset to Default** restores the selected trace or cursor; **Reset all to Default** the whole card.

### Advanced Settings

- **Input Range** – *Auto* (from Volts/Div, as ELVISmx) or force ±10 V / ±2 V. Audio is always ±2 V.
- **Vertical Offset** and **20MHz Filter** exist only on NI ELVIS II+ (shown disabled).
- **Hysteresis (div)** – 0 = ELVISmx. Larger values make the trigger ignore noise around the level.
- **No trigger** – *Clear (ELVISmx)*, *Keep last* (the last triggered trace stays, dimmed) or *Auto*
  (untriggered records are shown).
- **Record length** – *Standard (2500 points)* as ELVISmx, or *High resolution* (up to 100 000
  points per screen, 200 kS/s up to 50 ms/div) for zooming and FFT.
- **Frequency method** – *Tone* (ELVISmx) or *Edge counting*.
- **Autoscale sets the trigger level** – with Edge trigger, Autoscale also moves the level to the
  middle of the signal.

### Run bar

- **Run / Stop** – acquire continuously (ELVISmx *Run Continuously*), or stop.
- **Single** – one screen, then stop (ELVISmx *Run Once*). With the Edge trigger it waits until a
  crossing is found (or you press Stop).
- **Autoscale** – measures the enabled channels on their widest range and sets Volts/Div and Time/Div
  (about five periods on screen). It works while running or stopped; stopped, it only changes the
  settings. Vertical Position and the trigger are not changed.
- The **log** button on the right saves the displayed record as a text file in the ELVISmx *Log*
  layout (tab-separated, absolute timestamps).

### Export

- **Export data** (header menu, Ctrl+L) saves the displayed record as CSV/TSV: time and the voltage of
  each enabled channel (and Math), with the settings in the header lines.
- **Export image / PDF** (Ctrl+P) saves a picture of the display (the ELVISmx *Print*).
- **Focus mode** shows the display alone (the ELVISmx *Toggle View*).

## Tips

- Use ±2 V signals with Volts/Div of 200 mV or less for the best resolution.
- Combine the Oscilloscope with the Generator: connect AO 0 to AI 0+ and AGND to AI 0- to see the
  generated signal.
- The Oscilloscope shares the analog inputs with the Spectrum Analyzer and the Bode Analyzer. If you
  start one of them, the Oscilloscope pauses; press **Resume** in its banner to continue.

## Troubleshooting

- **Flat line at the top or bottom**: the signal is outside the input range or the screen; press
  Autoscale.
- **Trace jumps around**: use the Edge trigger and set Level inside the signal's range.
- **Graph empty, Timeout LED lit**: no edge crossed the level; move the T marker into the signal or
  choose a slower Time/Div.
- **Freq shows `?`**: the tone is smaller than 0.2 division (increase the sensitivity) or less than
  one period is on screen (increase Time/Div).
