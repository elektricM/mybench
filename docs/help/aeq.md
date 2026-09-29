# Audio Equalizer

The Audio Equalizer plays an input through a three-band tone control to an output in real time:
connect a music player to **AUDIO IN** and headphones or speakers to **AUDIO OUT**, press **Run**
and shape the sound with the Bass, Midtone and Treble faders. It is MYBENCH's version of the NI
"Audio Equalizer" featured application.

## Quick start

1. Connect the source to AUDIO IN and the headphones to AUDIO OUT (3.5 mm jacks).
2. Press **Run**. The meters show the input and output levels.
3. Move **Bass**, **Midtone** and **Treble** (±12 dB each) and **Volume**. Double-click a fader to
   reset it; **Flat** sets the three bands to 0 dB, **Bypass** compares with the unprocessed
   sound.

## Bands

| Band | Frequencies | Filter |
|---|---|---|
| Bass | below 320 Hz | 4th-order Bessel low-pass |
| Midtone | 320 Hz – 2.56 kHz | 4th-order Bessel band-pass |
| Treble | 2.56 kHz – 20.84 kHz | 4th-order Bessel band-pass |

The output is the sum of the three bands, each multiplied by its gain, times the volume. The top
plot shows the resulting tone response (and each band's curve): even with all bands at 0 dB it
is not perfectly flat around the band edges, like the original.

## Inputs and outputs

- **Input (L/R)**: AI 0, AI 1, AudioInput Left/Right or None; **Output (L/R)**: AO 0, AO 1,
  AudioOutput Left/Right or None. Inputs must be both analog inputs or both audio inputs (they
  share the converters); the same holds for the outputs. Choosing one kind for a side moves the
  other side to the same kind (AI 0 ↔ AudioInput Left, AI 1 ↔ AudioInput Right, and likewise for the
  outputs) with a short notice, as the Oscilloscope does, so a mixed setting can never be run. With a
  single input, both outputs play it. If another instrument holds the analog input or output, it is
  paused with a reason and can be resumed afterwards.
- Levels are limited to ±2 V; louder output is clipped and the **Clip** light turns red: lower
  the volume or the gains.
- **Sample Rate** 50 kS/s (as the original), 100 or 200 kS/s. **Output Buffer** is the silence
  queued before the first sound: the delay between input and output is about the buffer plus
  10 ms. Short buffers react faster but may drop out on a busy computer (a dropout restarts the
  stream and is counted in the display header).

## Displays

- **Spectrum**: input (gray) and output (blue) in 1/24-octave bands, in dBV, for the **L** or
  **R** channel. *Spectrum Averaging* (Display card) smooths it (10 ms, 125 ms, 1 s). The analysis window grows
  from 0.3 s to about 1 s (at most 65536 points, so it stays shorter at 200 kS/s) as the history builds up, so the lowest bands sharpen a second after Run.
  *Auto-scale* switches for both graphs are in the Display card (on by default).
- **Time**: the input and output waveforms over 100 ms (as the NI app) or, with *Time Span* set to
  20 ms, steadied on the input's rising zero crossings.
- **Meters**: RMS bar, peak line and held peak in dB below full scale (2 V); amber from −12 dB,
  red from −3 dB.

## With the simulated device

The simulator wires the audio outputs back to the audio inputs (and AO 0 to AI 0/AI 1), so the
equalizer would only hear its own output. Click **Use AI 0 / AI 1** in the hint to process the
simulator's test signals (a 1 kHz sine and a 200 Hz square wave) instead.

**Export ▸ Export data** saves the current spectrum and tone response with the settings.
