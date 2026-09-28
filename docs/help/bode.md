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
4. Press **Run**. Points appear one by one; the run bar shows the progress.

## Controls

### Measurement Settings

- **Stimulus Channel** / **Response Channel** – fixed to AI 0 and AI 1 on the myDAQ.
- **Start Frequency**, **Stop Frequency** – the sweep range (Start must not be above Stop).
- **Steps (per decade)** – number of points in each factor-of-ten of frequency. With 5 steps from
  100 Hz to 10 kHz you get 11 points (100, 158, 251, 398, 631, 1000 Hz, ...). If Stop does not fall
  exactly on a step, the sweep ends at the last step below it.
- **Peak Amplitude** – peak voltage of the test sine (default 2 V). Use a large amplitude for passive
  circuits (better signal-to-noise); use a small one for amplifiers with high gain so their output
  does not saturate.
- **Op-Amp Signal Polarity** – *Inverted* flips the sign of the response so an inverting amplifier
  shows about 0° instead of ±180°. It only changes the calculation, not the measurement, so you can
  switch it after a sweep.

### Graph Settings

- **Mapping** – *Logarithmic* shows gain in dB (20·log10 of the ratio); *Linear* shows the plain ratio.
  The phase plot is not affected.
- **Autoscale** – fits the vertical axes to the data. Turn it off to keep your own zoom.

### Cursor Settings

- **Cursors On** – shows a cursor on both plots at the same frequency point; the readouts then show
  that point's values.
- **Position Left / Right** – moves the cursor one measured point at a time. You can also drag it.

### Readouts

- **Frequency (Hz)**, **Phase (deg)**, **Gain**, **Gain (dB)** – the latest measured point during a
  sweep, or the point under the cursor.

### Run bar

- **Run** starts a sweep; **Stop** ends it early (the points measured so far are kept). A sweep always
  runs once from Start to Stop.

### Markers and references

- **-3 dB markers** show where the gain has dropped 3 dB below its maximum (the cut-off frequency of
  a filter). **Phase margin / gain margin** markers help with feedback-loop measurements.
- **Keep as reference** stores a finished sweep so you can compare it with the next one.

### Export

- **Export data** saves frequency, gain, gain (dB) and phase for every point as CSV.
- **Export image** saves a picture of the plots.

## How it measures

At each frequency the analyzer outputs the sine wave, waits for the circuit to settle, records both
inputs for a few periods and fits a sine of that exact frequency to each input. Gain is the ratio of
the fitted amplitudes, phase the difference of the fitted phases. Because the stimulus is measured on
AI 0 rather than assumed, output-level errors cancel out. Low frequencies take longer (a 1 Hz point
needs several seconds).

## Tips

- If the response is small, the analyzer automatically switches AI 1 to the sensitive ±2 V range.
- A point drawn as a hollow red dot was clipped (the response exceeded ±10 V): reduce the Peak
  Amplitude.
- The Bode Analyzer uses AO 0 and the analog inputs, so the Generator, the Oscilloscope and the
  Spectrum Analyzer pause while it runs.

## Troubleshooting

- **"No stimulus signal on AI 0"**: AO 0 is not connected to AI 0+, or AI 0- is not on AGND.
- **Gain around 0 dB and phase 0° everywhere**: AI 1 is probably connected to the stimulus instead of
  the circuit output.
- **Noisy results at high gain losses**: increase the Peak Amplitude (if the circuit allows it).
