# Bode Analyzer

The Bode Analyzer measures how a circuit (filter, amplifier, ...) passes sine waves of different
frequencies. It sends a sine wave from **AO 0** into your circuit, measures that stimulus on **AI 0**
and the circuit's output on **AI 1**, and plots the **gain** (output/input, in dB or as a ratio) and
the **phase shift** (degrees) against frequency. Frequencies are spaced logarithmically between the
Start and Stop frequencies (1 Hz to 20 kHz).

## Connections

| From | To |
|---|---|
| **AO 0** | the circuit's input **and** **AI 0+** (stimulus) |
| circuit output | **AI 1+** (response) |
| **AI 0-**, **AI 1-**, circuit ground | **AGND** |

Until the first sweep, the display shows this wiring as a small diagram (AO 0 → circuit → AI 1,
with AI 0 tapped on AO 0).

## Quick start

1. Wire the circuit as above.
2. Set the **Start** and **Stop** knobs around the frequencies of interest (turn, use − / +, or
   click the value and type "100", "20k").
3. Set the **Amplitude** knob (peak voltage of the test sine) and, in the **Sweep** card on the
   right, the **Steps / decade** knob.
4. Press **Sweep** (or Space). Points appear one by one; the Sweep button shows the progress
   ("12/50 · ≈ 20 s left"). A sweep always runs once from Start to Stop.

## Controls

The panel follows the instrument layout: the bar on top, the plots on the left, the **Sweep** card
and the tool palette on the right, the result cards below. A tool (Graph, Markers, Cursor,
References, Export) opens a small panel over the right edge of the plots; click the tool again or
the panel's close button to hide it. In a narrow pane the knobs and the column become compact.

### Instrument bar

**Sweep / Stop** (with the progress under the verb), **Keep as Reference**, and the knobs:

- **Start**, **Stop** – the sweep range, 1 Hz to 20 kHz, on a logarithmic knob. Start must not be
  above Stop (the Start knob turns red with "Start > Stop", and Sweep is refused). The Stop caption
  shows roughly how long the sweep takes.
- **Amplitude** – peak voltage of the test sine on AO 0, 0 to 5 V (default 2 V). Use a large
  amplitude for passive circuits (better signal-to-noise) and a small one for amplifiers with a high
  gain so their output does not saturate.

### Sweep card

- **Steps / decade** – a stepped knob (1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000; a value restored
  from an older session stays selectable): the number of points in each factor-of-ten of frequency.
  With 5 steps from 100 Hz to 10 kHz you get 11 points (100, 158, 251, 398, 631, 1000 Hz, ...), shown
  under the knob. If Stop does not fall exactly on a step, the sweep ends at the last step below it.
- **Standard | Fast**
  - *Standard* works like ELVISmx: at every frequency the analyzer first records both inputs for
    80 ms (16 000 samples at the highest rate) on the ±10 V range to choose the best range for
    each (±2 V for small signals). A default sweep takes a few seconds.
  - *Fast* is a MYBENCH extension: no range pre-measurement. The circuit settles for three periods,
    the ranges adapt from the previous point, and a record that clips is measured again on the
    larger range. Much quicker at mid and high frequencies.
- **Normal | Inverted** (op-amp signal polarity) – *Inverted* flips the sign of the response so an
  inverting amplifier shows about 0° instead of ±180°. It only changes the calculation, not the
  measurement, so you can switch it during or after a sweep.
- **Averages** – measures each frequency 1 to 16 times and averages the results (less noise, longer
  sweep).

Steps, mode, averages and the bar's knobs are locked while a sweep runs.

### Graph tool

- **Gain: dB | Ratio** – the gain axis: dB (20·log10 of the ratio) or the plain ratio. The phase
  plot is not affected.
- **Frequency: Log | Linear** – the frequency axis (logarithmic by default).
- **Autoscale** – fits both vertical axes to the data after every point. Turning it off jumps to
  the fixed ranges of the original: gain -30 … +5 and phase -360° … +360° (90° grid); zoom with
  the mouse wheel, Shift+wheel or drag from there (double-click returns to the full view).
- **Unwrap phase** – removes the 360° jumps between neighbouring points. Off by default: the phase
  is shown between -180° and +180°.

### Markers tool

- **−3 dB** – where the gain is 3 dB below its maximum (the cut-off frequencies of a filter),
  interpolated between points. The gain plot labels them "−3 dB", the phase plot shows the phase
  there.
- **0 dB crossover** – where the gain crosses 0 dB ("0 dB").
- **Gain / phase margin** – for loop-gain measurements: phase margin = 180° + phase at the 0 dB
  crossover ("PM"); gain margin = minus the gain where the phase crosses -180° ("GM").

The plot labels are short; the values are in the result cards.

### Cursor tool

- **Show** – one cursor on both plots at the same measured point (it starts on the first point and
  keeps its place when you run a new sweep); the **Cursor** card then shows that point's values.
- ‹ › – moves the cursor one point to the left or right. You can also drag it; it snaps to the
  measured points.
- **Re-measure** – measures only that frequency again and replaces the point (handy after adjusting
  a component).

### References tool

**Keep as Reference** (here or in the bar) stores the current sweep. References are drawn dashed
under the live sweep so you can compare circuits or component values. Up to 8 are kept; each row
can be renamed (type in its name), shown or hidden, exported to CSV or deleted. They are saved with
the session.

### Result cards

Under the plots: **f−3dB**, **0 dB at f** (unity-gain frequency), **Gain margin**, **GM at f** (the
frequency where the phase crosses −180°, where the gain margin is read), **Phase margin**, **Max
gain** and **Cursor** / **Last point** (frequency · gain · phase of the point under the cursor, or of
the most recent point; "—" before the first point). Hovering over a plot shows the values under the
mouse. The gain trace uses the AI 0 colour, the phase trace the AI 1 colour.

## Export

The **Export** tool (or the pane's menu) saves the data (frequency, gain, gain in dB, phase and a *Clipped* flag
for every point, CSV or TSV, also during a sweep), an image of the plots, or a PDF with the image and
the settings.

## How it measures

At each frequency the analyzer outputs a sine made of whole periods, records 16 000 samples of both
inputs (five periods at low frequencies, up to 200 kS/s at higher ones) and fits a sine of that exact
frequency to each input. Gain is the ratio of the fitted amplitudes, phase the difference of the
fitted phases. The small delay between the sampling of AI 0 and AI 1 is corrected, and because the
stimulus is measured on AI 0 rather than assumed, output-level errors cancel out. Low frequencies
take longer: a 1 Hz point needs about 5 s of data.

## Tips

- A point drawn with a red dot was clipped (the response exceeded ±10 V): reduce the Amplitude.
- The Bode Analyzer uses AO 0 and the analog inputs, so the Generator, the Oscilloscope and the
  Spectrum Analyzer pause while it runs. If another instrument takes them during a sweep, the Bode
  Analyzer pauses; **Resume** continues with the next point. After the sweep AO 0 returns to 0 V.

## Troubleshooting

- **"No stimulus signal on AI 0"**: AO 0 is not connected to AI 0+, AI 0- is not on AGND, or the
  Amplitude is 0 V.
- **Gain around 0 dB and phase 0° everywhere**: AI 1 is probably connected to the stimulus instead of
  the circuit output.
- **Noisy results at large attenuation**: increase the Amplitude (if the circuit allows it) or
  the averages per point.
