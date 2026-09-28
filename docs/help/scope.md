# Oscilloscope

The Oscilloscope shows one or two voltages versus time. It reads the myDAQ analog inputs **AI 0** and
**AI 1** (±10 V or ±2 V, up to 200 000 samples per second per channel) or the **AUDIO IN** jack (left
and right, ±2 V). It measures RMS, frequency and peak-to-peak automatically, can trigger on a rising
or falling edge, and has two cursors for time and voltage readings.

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

Audio inputs are AC-coupled: they block DC and very low frequencies. AI and audio inputs cannot be
used at the same time.

## Quick start

1. Connect your signal to AI 0+ / AI 0- (and AGND).
2. Press **Run**. The CH 0 trace (green) appears.
3. Turn **Time/Div** until a few periods are visible and **Volts/Div** until the trace fills the
   screen — or press **Autoscale**.
4. For a stable picture of a repetitive signal, set Trigger **Type** to *Edge* and adjust **Level**.

## Controls

### Channel 0 Settings / Channel 1 Settings

- **Source** – AI 0, AI 1, AudioInput Left or AudioInput Right. Both channels must use the same kind
  of input (both AI or both audio).
- **Enabled** – shows and acquires the channel. CH 0 is green, CH 1 is cyan.
- **Probe** – always 1x on the myDAQ.
- **Coupling** – set automatically: DC for AI inputs, AC for audio inputs (the average is removed).
- **Scale Volts/Div** – vertical sensitivity, 10 mV to 5 V per division (the screen is 10 divisions
  high). Small scales also select the more sensitive ±2 V input range automatically.
- **Vertical Position (Div)** – moves the trace up or down on screen. It does not change the data or
  the measurements.

### Timebase

- **Time/Div** – horizontal scale, 1 µs to 500 ms per division (10 divisions across). The sample rate
  is chosen automatically and shown in the display. At the fastest settings only a few samples fit on
  the screen, because the myDAQ samples at most 200 000 times per second.

### Trigger

- **Type** – *Immediate*: each screen starts right away (the picture may roll). *Edge*: each screen
  is aligned on a crossing of the trigger level.
- **Source** – which channel's signal is used for the edge (Chan 0 Source or Chan 1 Source). The
  channel must be enabled.
- **Slope** – trigger on a rising (positive) or falling (negative) edge.
- **Level (V)** – the voltage the signal must cross.
- **Horizontal Position (%)** – where the trigger point sits on the screen, from 0 % (left edge) to
  100 % (right edge); 50 % puts it in the middle so you see what happened before and after it.
- You can also drag the trigger markers on the graph to change the level and position.
- The myDAQ triggers in software: at least one edge must occur within about one screen. Use a slower
  Time/Div if the trace does not trigger.

### Timeout LED

Lights when no trigger was found (the level is outside the signal, or the wrong slope/source). The
last triggered trace stays on screen until the next trigger.

### Measurements

For each enabled channel (tick it in **Display Measurements**):
- **RMS** – root-mean-square voltage of everything on screen (includes the DC part on AI inputs).
- **Freq** – frequency, from the time between successive crossings of the middle level. Needs at
  least one full period on screen, otherwise `?`.
- **Vp-p** – difference between the highest and lowest voltage on screen.

### Cursors Settings

- **Cursors On** – shows two vertical cursors C1 and C2 that you drag along the time axis.
- **C1 / C2** – the channel each cursor reads.
- The readout shows each cursor's voltage and **dT**, the time between the cursors (and 1/dT).

### Graph Properties

Change line style, width, points, colors and fill of each trace, the cursor style and color, and
which measurements are displayed.

### Advanced

- **Input Range** – *Auto* (chosen from Volts/Div and position), or force ±10 V / ±2 V.
- **Trigger hysteresis** – how far the signal must move away from the level before a new edge is
  accepted; increase it for noisy signals.

### Run bar

- **Run / Stop** – acquire continuously, or stop. **Single** – acquire one screen (with Edge
  trigger: waits up to 10 s for a trigger).
- **Autoscale** – adjusts Volts/Div, Vertical Position and Time/Div to the current signals (and the
  trigger level to the middle of the signal when Edge is used).

### Export

- **Export data** saves the last screen as a CSV table (time and voltage of each channel).
- **Export image** saves a picture of the display.

## Tips

- Use ±2 V signals with Volts/Div of 200 mV or less for the best resolution.
- Combine the Oscilloscope with the Generator: connect AO 0 to AI 0+ and AGND to AI 0- to see the
  generated signal.
- The Oscilloscope shares the analog inputs with the Spectrum Analyzer and the Bode Analyzer. If you
  start one of them, the Oscilloscope pauses; press **Resume** in its banner to continue.

## Troubleshooting

- **Flat line at the top or bottom**: the signal is outside the input range or the screen; press
  Autoscale.
- **Trace jumps around**: use Edge trigger; set Level inside the signal's range.
- **"AI channels cannot be used with AudioInput channels"**: choose the same kind of source for both
  channels.
- **Freq shows `?`**: fewer than one period on screen; increase Time/Div.
