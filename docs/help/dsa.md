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

## Layout

- **Instrument bar** (top): **Run / Stop** (with the state under the verb), **Single** (stopped:
  acquires enough records to complete the averaging, then stops; running: stops after the current
  record or average), the **Span** and **Resolution** knobs, and **Spectrum | Octave** (in a narrow
  pane this choice moves to an **Analysis** card at the top of the right column).
- **Display** (left): the spectrum, optionally the spectrogram and the time record under it. Under
  the spectrum, while they apply: the averaging progress `Avg n/N` and **Restart**.
- **Right column** (the same fixed width in every instrument): the **Input** card (source and
  range); the tools — **Window**, **Averaging**, **Display**, **Cursors**, **Export** (in Octave
  mode: **Octave**, **Export**) — stay at its bottom. A tool opens a panel that floats over the
  display; click the tool again or × to close it.
- **Measurement cards** (bottom): Fundamental, Fundamental power, THD, SINAD, SNR, Peak, and the
  cursor card (C1, C2 and their difference) while the cursors are shown; in Octave mode Overall level
  and Loudest band. The result cards show min / max / mean / σ over the last 100 results of the run.

Settings that do not apply are hidden (for example *RMS / Peak* with dBm, the weighting without
RMS or Vector averaging, Minimum / Maximum with an automatic scale).

## Display

- **Spectrum**: magnitude against frequency, 0 Hz to the span. The strongest component is marked
  **F**; its harmonics **H2 … H10** are marked with their level relative to the fundamental (dBc) —
  weak ones that hide in the noise get a small unlabelled marker.
- **Spectrogram** (Display tool): the history of the spectra, newest at the top; the colours follow
  the spectrum's scale.
- **Time record** (Display tool): the latest record. With a window selected it is shown multiplied
  by the window (scaled so the average level is kept), which shows what the FFT actually sees.
- Hovering over a plot shows the values under the mouse; the mouse wheel zooms, dragging pans and a
  double-click restores the view. The magnitude axis stays inside the vertical scale (automatic or
  Manual): it can be zoomed in, never panned or zoomed out into an empty region; the frequency axis
  stays inside 0 Hz to the span with a minimum zoom of a few hundred bins.

## Controls

### Instrument bar

- **Span** – the spectrum covers 0 Hz up to this frequency, in 1-2-5 steps up to the source's
  maximum (100 kHz for AI). The sample rate is twice the span (shown under the knob). Turn the knob,
  use − / +, or click the value and type it ("20k").
- **Resolution** – 200, 400, 800, 1600 or 3200 lines in the span. More lines give a finer line
  spacing (df = span / lines) but longer records; df and the record length are shown under the knob.
- **Spectrum | Octave** – the FFT analyzer, or the 1/3-octave band display (see *1/3 Octave*).

### Input card

The source as picture buttons — **AI 0**, **AI 1** (a signal into a terminal) and **Audio L**,
**Audio R** (the audio jack) — and for AI inputs the range **±10 V | ±2 V** (±2 V gives better
resolution for small signals). Audio inputs have a fixed ±2 V range, shown on the card instead of
the range buttons. The spectrum and time record follow the source's colour.

### Window tool

Shapes each record before the FFT. *None* is exact for signals that fit the record exactly;
*Hanning* is a good general choice; *Flat Top* gives the most accurate peak amplitudes; the
*Blackman-Harris* variants show small components next to big ones.

### Averaging tool

- **Mode** (picture buttons) – *None* (latest spectrum), *Vector* (averages the complex spectra: only
  useful for signals synchronised with the acquisition), *RMS* (averages power: smooths the noise),
  *Peak Hold* (keeps the maximum of each line).
- **Weighting** (RMS and Vector only) – *Linear*: averages the first N spectra, then holds the result
  until you restart. *Exponential*: a running average where each new spectrum counts 1/N.
- **Averages** – N (knob). While the average fills, `Avg n/N` shows under the spectrum.
- **Restart** (under the spectrum, shown while running with Linear weighting) starts the averaging
  again. Changing the window or an averaging setting also restarts it; changing the source, range,
  span or lines restarts the acquisition.

### Display tool

- **Units** – *V* (linear power in V²), *dB* (relative to 1 V) or *dBm* (1 mW in 50 Ω), and for V
  and dB *RMS* or *Peak*: a sine of 1 V peak reads 0.5 Vrms² (−3.01 dBVrms) or 1 Vpk² (0 dBVpk).
- **Spectrum** – *Power* (power spectrum) or *Density* (power spectral density, for noise).
- **Vertical scale** – *Auto* fits the spectrum; *Manual* shows Minimum … Maximum.
- Logarithmic frequency axis, **Max hold** / **Min hold**, the **Time record**, the **Spectrogram**
  and its depth.

These only change the display, also of a stopped result.

### Cursors tool

Every marker and cursor control is here, once:

- **Harmonic markers** – the F and H2 … H10 markers on the spectrum.
- **Harmonics and peaks table** – the table beside the spectrum.
- **Cursors C1 · C2** – two cursors (C1 red, C2 blue); drag them on the plot (they snap to lines or
  samples).
- **Selected cursor** – the one that ‹ › (one line or sample) and the peak buttons move.
- **Cursor plots** (with the time record shown) – the plot each cursor is on.
- **Peak search** – *Highest*, *Next lower*, nearest peak *Left* / *Right*.

The cursor card at the bottom shows C1 and C2 (position and level) and their difference (Δx, Δy;
on the time record also 1/Δx).

## Measurements

- **Detected Fundamental Frequency** – the strongest peak, refined between lines.
- **Fundamental power** – power of that peak summed over ±3 lines. It is always a linear power, in
  Vrms² (Vpk² when Mode is Peak), and does not follow the Units setting. (The ELVISmx-style text
  log writes these units the ELVISmx way, `Vrms^2`.)
- **THD** – total harmonic distortion: power of harmonics 2 to 10 relative to the fundamental.
- **SINAD** – signal to noise-and-distortion ratio.
- **SNR** – signal to noise ratio: like SINAD, but harmonics 2 to 10 are not counted as noise.
- **Peak** (Vpk) – half the difference between the highest and the lowest sample of the latest record.

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
- **Display** – band levels in dBV as bars; the cards show the overall level (sum of the bands
  shown) and the loudest band; a **Settled** light above the bars: the lowest bands' filters need a
  moment after a start or a change before their levels are valid. **Restart** (beside it, while
  running) restarts the band averaging.

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
- **Spectrum frozen**: Linear weighting finished its average — press Restart (under the spectrum), or
  use Exponential.
- **Components above the span appear at wrong frequencies**: signals above half the sample rate fold
  back (aliasing); increase the span or filter the signal.
