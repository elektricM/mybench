# Spectrum Analyzer

The Spectrum Analyzer (a "dynamic signal analyzer") shows which frequencies a signal contains. It
records blocks of samples from one input — **AI 0**, **AI 1** or the **AUDIO IN** jack — computes their
spectrum with an FFT, averages successive spectra, and measures the fundamental frequency, its power,
the total harmonic distortion (THD) and SINAD. The time-domain signal is shown underneath. A second
analysis mode, **1/3 Octave**, shows the signal's level in third-octave bands like a sound level
meter.

## Connections

| Signal | myDAQ terminals |
|---|---|
| AI 0 or AI 1 | **AI x+** / **AI x-**, reference to **AGND** |
| Audio | **AUDIO IN** jack (Left or Right) |

## Quick start

1. Connect the signal to AI 0+ / AI 0- (and AGND).
2. Press **Run**. The spectrum appears; the strongest peak is reported as the fundamental.
3. Set **Frequency Span** a little above the highest frequency you care about, and raise
   **Resolution (lines)** for finer frequency detail.

## Display

- **Readouts** (top): *Detected Fundamental Frequency (Hz)*, *Fundamental Frequency Power* (always
  a linear power in Vrms² or Vpk², whatever the Units), *THD (%)*, *SINAD (dB)*, *Vpk (V)* and
  *df* (line spacing).
- **Spectrum**: magnitude against frequency, 0 Hz to the span. The strongest component is marked
  **F**; its harmonics **H2 … H10** are marked with their level relative to the fundamental (dBc) —
  weak ones that hide in the noise get a small unlabelled marker.
- **Power Spectrum / Power Spectral Density** (under the spectrum): the display mode.
- **Spectrogram** (optional): the history of the spectra, newest at the top; the colours follow the
  spectrum's scale.
- **Time**: the latest record. With a window selected it is shown multiplied by the window (scaled
  so the average level is kept), which shows what the FFT actually sees.
- Hovering over a plot shows the values under the mouse; the mouse wheel zooms, dragging pans and a
  double-click restores the view.

## Controls

### Analysis

**Spectrum (FFT)** is the classic analyzer described below; **1/3 Octave** switches to the band
display (see *1/3 Octave* at the end).

### Input Settings

- **Source Channel** – AI 0, AI 1, AudioInput Left or AudioInput Right.
- **Voltage Range** – ±10 V or ±2 V for AI inputs (±2 V gives better resolution for small signals);
  audio inputs are always ±2 V.

### FFT Settings

- **Frequency Span** – the spectrum covers 0 Hz up to this frequency (at least 405 Hz: smaller
  values are raised to 405 Hz). The sample rate is twice the
  span (shown as **Sample Rate** in the display header), up to 200 kS/s, so the span can be at most
  100 kHz.
- **Resolution (lines)** – 200, 400, 800, 1600 or 3200 lines in the span. More lines give a finer
  resolution (span / lines Hz per line) but longer records: 400 lines at 40 kHz take 10 ms, 3200
  lines at 100 Hz take 32 s. The card shows the sample rate, line spacing and record length.
- **Window** – shapes each record before the FFT. *None* is exact for signals that fit the record
  exactly; *Hanning* is a good general choice; *Flat Top* gives the most accurate peak amplitudes;
  the *Blackman-Harris* variants show small components next to big ones.

### Averaging

- **Mode** – *None* (latest spectrum), *RMS* (averages power: smooths the noise), *Vector* (averages
  the complex spectra: only useful for signals synchronised with the acquisition, otherwise they
  average away), *Peak Hold* (keeps the maximum of each line).
- **Weighting** (RMS and Vector) – *Linear*: averages the first N spectra, then holds the result
  (`Averaging complete`) until you restart. *Exponential*: a running average where each new spectrum
  counts 1/N.
- **# of Averages** – N.
- **Restart** (card and run bar) – starts the averaging again. It is available only while running
  with RMS or Vector averaging and *Linear* weighting (grayed with Exponential weighting, None and
  Peak Hold). Changing the window or an averaging
  setting also restarts it; changing the source, range, span or lines restarts the acquisition.

### Trigger Settings

Shown as on the myDAQ: disabled. The acquisition always starts immediately (the trigger inputs exist
only on NI ELVIS).

### Frequency Display

- **Units** – *Linear* (power in V², axis `Vrms^2` or `Vpk^2`), *dB* (decibels relative to 1 V) or
  *dBm* (relative to 1 mW in 50 Ω).
- **Mode** – *RMS* or *Peak*: a sine of 1 V peak reads 0.5 Vrms² (−3.01 dBVrms) or 1 Vpk² (0 dBVpk).
  Grayed for dBm, where it has no effect.
- **Display Mode** – *Power Spectrum* or *Power Spectral Density* (power per hertz, for noise).

These only change the display, also of a stopped result.

### Scale Settings

- **Scale** – *Auto* fits the spectrum (in dB the view never reaches more than 160 dB below the top);
  *Manual* uses **Maximum** and **Minimum**. Changing the units resets them to 10 / −120 (dB, dBm) or
  1 / 0 (Linear).

### Cursor Settings

- **Cursors On** – shows cursors C1 and C2.
- **C1 / C2** – the plot each cursor is on: *Spectrum* or *Time*.
- **Cursor Select** – the cursor that **Position** ‹ › and the peak buttons move.
- **Position** ‹ › – one line (spectrum) or one sample (time) to the left or right. You can also
  drag the cursors; they snap to lines or samples.
- **Peak search** – *Peak* (highest peak), *Next* (next lower peak), ◂ / ▸ (nearest peak to the
  left or right).

The readout row under the plots shows both cursors and their difference (Δx, Δy, and 1/Δx on the
time plot).

### View

Harmonic markers, a **Harmonics and peaks** table beside the spectrum, **Max hold** / **Min hold**
curves (per-line maximum and minimum of the displayed spectrum since the last restart), a
logarithmic frequency axis, and the **Spectrogram** with its history length.

### Run bar

**Run / Stop** (continuous), **Single** (acquires enough records to complete the averaging, then
stops; pressed while running, the run ends once the average is complete), **Restart** (averaging).

## Measurements

- **Detected Fundamental Frequency** – the strongest peak, refined between lines.
- **Fundamental Frequency Power** – power of that peak summed over ±3 lines. It is always a linear
  power, in Vrms² (Vpk² when Mode is Peak), and does not follow the Units setting.
- **THD** – total harmonic distortion: power of harmonics 2 to 10 relative to the fundamental.
- **SINAD** – signal to noise-and-distortion ratio.
- **Vpk** – half the difference between the highest and the lowest sample of the latest record.

## 1/3 Octave

Measures the level in the standard third-octave bands (like a sound level meter), from the selected
source at 50 kS/s.

- **Mode (time weighting)** – how fast the band levels follow the signal: *Slow* (1 s), *Fast*
  (125 ms), *Impulse* (35 ms rising, 1.5 s falling) or *Custom* with **Time Constant** (0 to
  10 000 ms).
- **Low Band / High Band** – the range of bands shown, chosen from the 31 standard bands
  20 Hz … 20 kHz (band 0 = 20 Hz … band 30 = 20 kHz; default all of them).
- **Weighting** – *Linear* (none), or the *A*, *B* and *C* frequency weightings of sound level meters,
  applied to the band levels.
- **Display** – band levels in dBV as bars, the overall level (sum of the bands shown), the loudest
  band, and a **Settled** light: the lowest bands' filters need a moment after a start or a change
  before their levels are valid.

## Export

The panel's **Export** menu saves the displayed spectrum (and the hold curves when shown) with the
latest time record as CSV or TSV — in 1/3 Octave mode the band levels — an image of the display, or a
PDF with the image and the settings.

Choose **ELVISmx-style text log (\*.txt)** for the layout of the NI Spectrum Analyzer log: the time
record as a waveform block (`waveform`, `Source signal`, `t0`, `delta t`, then an absolute time and a
value per sample), a blank line, `FFT waveform`, `Units: <unit>` and one `frequency<TAB>value` row per
line exactly as displayed. In 1/3 Octave mode the second block lists the band centres and levels.

## Tips

- Look at the Generator's output: connect AO 0 to AI 0 and compare sine, triangle and square
  harmonics.
- The analyzer shares the analog inputs with the Oscilloscope and the Bode Analyzer; starting one of
  them pauses it (press **Resume** afterwards).
- If the computer cannot keep up, "Samples were lost; averaging restarted." appears and the analyzer
  carries on; after three losses within 10 s it stops.

## Troubleshooting

- **Peaks are wide or have "skirts"**: choose a window (Hanning, Blackman-Harris).
- **Spectrum frozen**: Linear weighting finished its average — press Restart, or use Exponential.
- **Components above the span appear at wrong frequencies**: signals above half the sample rate fold
  back (aliasing); increase the span or filter the signal.
