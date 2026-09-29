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

The display shows the reminder `Connections: AI 0 - AO 0   AI 1 - Signal`.

## Quick start

1. Wire the circuit as above.
2. Set **Start Frequency** and **Stop Frequency** around the frequencies of interest.
3. Choose **Steps** (points per decade) and the **Peak Amplitude** of the test signal.
4. Press **Run** (or Space). Points appear one by one; the run bar shows the progress and an
   estimate of the remaining time. A sweep always runs once from Start to Stop.

## Controls

### Measurement Settings

- A note states the fixed wiring: stimulus on AI 0, response on AI 1 (not settings).
- **Start Frequency**, **Stop Frequency** – the sweep range, 1 Hz to 20 kHz. Start must not be above
  Stop (the field turns red and Run is refused).
- **Steps (per decade)** – number of points in each factor-of-ten of frequency (1 to 1000). With 5
  steps from 100 Hz to 10 kHz you get 11 points (100, 158, 251, 398, 631, 1000 Hz, ...). If Stop does
  not fall exactly on a step, the sweep ends at the last step below it. The card shows how many
  points the settings give and roughly how long the sweep takes.
- **Peak Amplitude** – peak voltage of the test sine, 0 to 5 V (default 2 V). Use a large amplitude
  for passive circuits (better signal-to-noise) and a small one for amplifiers with a high gain so
  their output does not saturate.
- **Op-Amp Signal Polarity** – *Inverted* flips the sign of the response so an inverting amplifier
  shows about 0° instead of ±180°. It only changes the calculation, not the measurement, so you can
  switch it during or after a sweep.

The measurement settings are locked while a sweep runs.

### Graph Settings

- **Mapping** – the gain axis: *Logarithmic* shows the gain in dB (20·log10 of the ratio), *Linear*
  the plain ratio. The phase plot is not affected.
- **Autoscale** – fits both vertical axes to the data after every point. Turning it off jumps to
  the fixed ranges of the original: gain -30 … +5 and phase -360° … +360° (90° grid); zoom with
  the mouse wheel, Shift+wheel or drag from there (double-click returns to the full view).
- **ELVISmx colors** – yellow traces with ring markers and a red cursor, like the original.
- **Frequency axis** – logarithmic (default) or linear.
- **Unwrap phase** – removes the 360° jumps between neighbouring points. Off by default: the phase
  is shown between -180° and +180°.

### Cursor Settings

- **Cursors On** – shows one cursor on both plots at the same measured point (it starts on the
  first point and keeps its place when you run a new sweep); the readouts then show that point's values.
- **Position** ‹ › – moves the cursor one point to the left or right. You can also drag it; it snaps
  to the measured points.
- **Re-measure point at cursor** – measures only that frequency again and replaces the point (handy
  after adjusting a component).

### Readouts

**Frequency (Hz)**, **Phase (deg)**, **Gain**, **Gain (dB)** – the most recent point during a sweep,
or the point under the cursor. Before the first point they read `0.00` and `-Inf`. Hovering over a
plot shows the values under the mouse.

### Sweep

- **Mode**
  - *Standard* works like ELVISmx: at every frequency the analyzer first records both inputs for
    80 ms (16 000 samples at the highest rate) on the ±10 V range to choose the best range for
    each (±2 V for small signals). A default sweep takes a few seconds.
  - *Fast* is a MYBENCH extension: no range pre-measurement. The circuit settles for three periods,
    the ranges adapt from the previous point, and a record that clips is measured again on the
    larger range. Much quicker at mid and high frequencies.
- **Averages per point** – measures each frequency several times and averages the results (less
  noise, longer sweep).

### Markers

- **-3 dB points** – where the gain is 3 dB below its maximum (the cut-off frequencies of a filter),
  interpolated between points and labelled on both plots.
- **Unity gain** – where the gain crosses 0 dB.
- **Gain and phase margins** – for loop-gain measurements: phase margin = 180° + phase at the 0 dB
  crossover; gain margin = minus the gain where the phase crosses -180°.

The card lists the values; the readout row shows a summary.

### References

**Keep as reference** (card or run bar) stores the current sweep. References are drawn dashed under
the live sweep so you can compare circuits or component values. Up to 8 are kept; each can be
renamed, hidden, exported or deleted. They are saved with the session.

## Export

The panel's **Export** menu saves the data (frequency, gain, gain in dB, phase and a *Clipped* flag
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

- A point drawn with a red dot was clipped (the response exceeded ±10 V): reduce the Peak Amplitude.
- The Bode Analyzer uses AO 0 and the analog inputs, so the Generator, the Oscilloscope and the
  Spectrum Analyzer pause while it runs. If another instrument takes them during a sweep, the Bode
  Analyzer pauses; **Resume** continues with the next point. After the sweep AO 0 returns to 0 V.

## Troubleshooting

- **"No stimulus signal on AI 0"**: AO 0 is not connected to AI 0+, AI 0- is not on AGND, or the
  Peak Amplitude is 0 V.
- **Gain around 0 dB and phase 0° everywhere**: AI 1 is probably connected to the stimulus instead of
  the circuit output.
- **Noisy results at large attenuation**: increase the Peak Amplitude (if the circuit allows it) or
  the averages per point.
