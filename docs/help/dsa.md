# Spectrum Analyzer

The Spectrum Analyzer (a "dynamic signal analyzer") shows which frequencies a signal contains. It
records blocks of samples from one input — **AI 0**, **AI 1** or the **AUDIO IN** jack — computes their
spectrum with an FFT, averages successive spectra, and measures the fundamental frequency, its power,
the total harmonic distortion (THD) and SINAD. The time-domain signal is shown underneath.

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

## Controls

### Input Settings

- **Source Channel** – AI 0, AI 1, AudioInput Left or AudioInput Right.
- **Voltage Range** – ±10 V or ±2 V for AI inputs (±2 V gives better resolution for small signals);
  audio inputs are always ±2 V.

### FFT Settings

- **Frequency Span** – the spectrum covers 0 Hz up to this frequency. The sample rate is twice the
  span (shown as **Sample Rate**), up to 200 kS/s, so the span can be at most 100 kHz.
- **Resolution (lines)** – number of frequency lines in the span. More lines = finer resolution
  (span / lines Hz per line) but longer records: 400 lines at 40 kHz take 10 ms, 6400 lines at 100 Hz
  take 64 s.
- **Window** – shapes each record before the FFT. *None* is exact for signals that fit the record
  exactly; *Hanning* is a good general choice; *Flat Top* gives the most accurate peak amplitudes;
  *Blackman-Harris* variants show small components next to big ones.

### Averaging

- **Mode** – *None* (latest spectrum), *RMS* (averages power: smooths the noise), *Vector* (averages
  the complex spectra: only useful for signals synchronised with the acquisition), *Peak hold* (keeps
  the maximum of each line).
- **Weighting** – *Linear*: averages the first N spectra, then holds the result until you restart.
  *Exponential*: a running average where each new spectrum counts 1/N.
- **# of Averages** – N.
- **Restart** (run bar) – starts the averaging again. Changing a setting also restarts it.

### Trigger Settings

Not available on the myDAQ: acquisition always starts immediately.

### Frequency Display

- **Units** – *dB* (decibels relative to 1 V), *dBm* (relative to 1 mW in 50 Ω), *Linear* (volts).
- **Mode** – *RMS* or *Peak*: a sine of 1 V peak reads 0.707 Vrms (-3 dBVrms) or 1 Vpk (0 dBVpk).
- **Display Mode** (under the spectrum) – *Power Spectrum* or *Power Spectral Density* (power per
  hertz, for noise measurements).

### Scale Settings

- **Scale** – *Auto* fits the spectrum; *Manual* uses **Maximum** and **Minimum**.

### Cursor Settings

- **Cursors On** – shows cursors C1 and C2; **C1 / C2** choose the plot each one is on (spectrum or
  time); **Cursor Select** chooses which cursor **Position Left / Right** moves one point at a time.
  The readout shows both positions and their difference.

### Measurements

- **Detected Fundamental Frequency (Hz)** – frequency of the strongest peak, refined between lines.
- **Fundamental Frequency Power** – power of that peak in the selected units.
- **THD (%)** – total harmonic distortion: power of all harmonics relative to the fundamental.
- **SINAD (dB)** – signal to noise-and-distortion ratio.
- **Vpk (V)** – difference between the highest and lowest sample of the latest record.

### Run bar

**Run / Stop** (continuous), **Single** (acquires enough records to complete the averaging, then
stops), **Restart** (averaging).

### Export

**Export data** saves the displayed spectrum and the latest time record as CSV; **Export image**
saves a picture.

## Tips

- Harmonic markers label the fundamental and its harmonics with their level relative to the
  fundamental (dBc).
- Use the spectrogram view to watch a spectrum change over time.
- Look at the Generator's output: connect AO 0 to AI 0 and compare sine, triangle and square
  harmonics.
- The analyzer shares the analog inputs with the Oscilloscope and the Bode Analyzer; starting one of
  them pauses it.

## Troubleshooting

- **Peaks are wide or have "skirts"**: choose a window (Hanning, Blackman-Harris).
- **Spectrum frozen**: Linear weighting finished its average — press Restart, or use Exponential.
- **Components above the span appear at wrong frequencies**: signals above half the sample rate fold
  back (aliasing); increase the span or filter the signal.
