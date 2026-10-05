# Audio Equalizer

The Audio Equalizer plays an input through a three-band tone control to an output in real time:
connect a music player to **AUDIO IN** and headphones or speakers to **AUDIO OUT**, press **Run**
and shape the sound with the Bass, Midtone and Treble faders. It is MYBENCH's version of the NI
"Audio Equalizer" featured application.

## Quick start

1. Connect the source to AUDIO IN and the headphones to AUDIO OUT (3.5 mm jacks).
2. Press **Run**. The meters show the input and output levels.
3. Move **Bass**, **Midtone** and **Treble** (±12 dB each) and **Volume** — drag, scroll, use the
   arrow keys, or click the value above a fader and type a number (dB, or % for the volume).
   Double-click a fader to reset it; **Flat** sets the three bands to 0 dB, **Bypass** (highlighted
   while on) compares with the unprocessed sound.

## Layout

- **Bar**: Run / Stop (with the stream state), **Bypass**, **Flat**.
- **Display**: the tone response (top), the spectrum or the waveforms with the three band regions,
  and the console — the faders are the only place where the bands and the volume are set — with the
  IN and OUT level meters and the Clip light.
- **Right column**: the **Input** and **Output** cards, then the tools **Streaming** and **Display**
  (at the bottom), which open a small panel over the display (click again to close).
- Colours: **Bass** red, **Midtone** yellow, **Treble** violet (low to high, as in the light
  spectrum) on the faders, the response curves and the band regions.

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

- **Input** card: first pick the input kind, **Audio in** or **AI 0 / AI 1**, then for each channel
  of the equalizer (**L**, **R**) where it comes from: **In L**, **In R** (or **AI 0**, **AI 1**) or
  **Off**. The **Output** card does the same with **Audio out** (**Out L**, **Out R**) or **AO 0 /
  AO 1**. The two inputs (and the two outputs) share the converters, so they are always
  of one kind: a mixed setting cannot be chosen. Switching the kind sets L and R to the kind's two
  channels. With a single input, both outputs play it. If another instrument holds the analog inputs
  or outputs, the arbiter banner says which one; the equalizer pauses and can be resumed afterwards.
- Levels are limited to ±2 V; louder output is clipped and the **Clip** light turns red: lower
  the volume or the gains.
- **Streaming** tool: **Sample rate** 50 kS/s (as the original), 100 or 200 kS/s. **Output buffer**
  is the silence queued before the first sound: the delay between input and output (shown next to
  the title) is about the buffer plus 10 ms. Short buffers react faster but may drop out on a busy
  computer (a dropout restarts the stream; the count appears in this tool).

## Displays

The **Display** tool chooses the view (**Spectrum | Time**), the channel (**L | R**), the spectrum
**Averaging** or the **Time span**, **Autoscale** for the current view (on by default) and
**Show input**.

- **Spectrum**: before (input, gray) and after (output, blue) in 1/24-octave bands, in dBV, over
  the Bass / Midtone / Treble regions. Averaging smooths it (10 ms, 125 ms, 1 s). The analysis window grows
  from 0.3 s to about 1 s (at most 65536 points, so it stays shorter at 200 kS/s) as the history builds up, so the lowest bands sharpen a second after Run.
- **Time**: the input and output waveforms over 100 ms (as the NI app) or, with *Time Span* set to
  20 ms, steadied on the input's rising zero crossings.
- **Meters**: RMS bar, peak line and held peak in dB below full scale (2 V); amber from −12 dB,
  red from −3 dB.

## With the simulated device

The simulator wires the audio outputs back to the audio inputs (and AO 0 to AI 0/AI 1), so the
equalizer would only hear its own output; the Input card then reads *Simulator: the outputs loop
back here*. Choose **AI 0 / AI 1** as input and **Audio out** as output to process the simulator's
test signals (a 1 kHz sine and a 200 Hz square wave) instead.

**Export ▸ Export data** saves the current spectrum and tone response with the settings.
