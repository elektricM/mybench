# Waveform Generator

The Waveform Generator drives the myDAQ outputs. Each of its two channels can be switched **Off**,
used as a **Function** generator (sine, triangle or square wave with adjustable frequency, amplitude,
offset and duty cycle, plus frequency sweeps), or play an **Arbitrary** waveform that you load from a
file or draw in the built-in **Waveform Editor**.

Both channels can run at the same time and are independent: you can change or stop one without
disturbing the other. All changes you make while a channel runs reach the output within about a
tenth of a second, without interrupting the signal.

## Connections

- **AO 0** and **AO 1** screw terminals, referenced to **AGND**: up to ±10 V. This is the default
  output pair.
- **AUDIO OUT** jack (left and right): up to ±2 V, AC-coupled (no DC). Select the audio pair in the
  **Output** card to use it.
- You cannot mix an AO terminal and an audio output: the two channels always use the same pair.
- To look at the signal, wire AO 0 to **AI 0+** and AGND to **AI 0-** and open the Oscilloscope, or
  connect AUDIO OUT to AUDIO IN.
- The outputs can deliver only a few milliamps: use a buffer amplifier for low-impedance loads.

## Quick start: function generator

1. On the **AO 0** card, set **Mode** to **Function** (the default).
2. Pick **Sine**, **Triangle** or **Square**, then set **Frequency**, **Amplitude** (peak-to-peak) and
   **DC Offset** with the knobs or by typing (`1.5k` means 1500 Hz).
3. Press **Run** (or the card's **Output** switch). The big readout shows the generated frequency.
4. Turn the knobs while it runs: the output follows smoothly.
5. Press **Stop**: the output returns to 0 V and the readout shows `OFF`.

## Quick start: arbitrary waveform

1. On a channel card, set **Mode** to **Arbitrary**.
2. Press **Open** and choose a waveform (`.mbw` from the editor, a `.wav` sound file or a `.csv`/`.txt`
   list of values), or press **Edit** to create one in the Waveform Editor.
3. Check **Update Rate** (samples per second; a file that stores its own rate sets it for you) and
   **Gain**.
4. Choose **Run Continuously** (loops) or **Run Once** (plays once), then press **Run** or the
   card's **Output** switch.

## Controls

### Output card

- **Output pair**: `AO 0 / AO 1` or `AudioOutput Left / Right`. Changing it while running moves the
  running signals to the new outputs.
- **Link update rates**: when on (default), both Arbitrary channels share one Update Rate.

### Channel cards (AO 0 in green, AO 1 in cyan)

- **Mode**: Off, Function or Arbitrary. Changing the mode stops that channel.
- **Output** switch and LED: starts or stops this channel only. Green = generating, amber = paused or
  finished a Run Once and holding its last value.
- The big readout shows `OFF`, the frequency (Function) or the update rate (Arbitrary).

### Function mode: Waveform Settings

- **Sine / Triangle / Square**: waveform shape.
- **Frequency**: 0.2 Hz to 20 kHz (logarithmic knob).
- **Amplitude**: 0 to 10 V peak-to-peak.
- **DC Offset**: -5 to +5 V, added to the waveform.
- **Duty Cycle**: 1 to 99 %, the fraction of each period at the high level (square only).
- **Modulation Type**: always None on the myDAQ.

### Function mode: Sweep Settings

- **Start Frequency**, **Stop Frequency**, **Step** and **Step Interval** (ms) define a sweep that
  goes from Start towards Stop in equal steps, holding each frequency for the step interval.
- **Sweep** runs the sweep once; the channel stops after the last step. Shape, amplitude, offset and
  duty stay adjustable during the sweep; the frequency settings are locked until it ends.

### Arbitrary mode: Waveform Settings

- **Waveform Name**: the loaded file, or `< empty >`.
- **Open**, **Recent**, **Library**: load a waveform. **Clear** unloads it.
- **Edit**: opens the Waveform Editor on this channel's waveform.
- **Gain**: multiplies the waveform (-10 to 10). Values beyond the output range (±10 V on AO,
  ±2 V on audio) are clipped and you get a warning.

### Arbitrary mode: Timing and Triggering Settings

- **Update Rate**: how many waveform samples are output per second (1 S/s to 200 kS/s). A waveform of
  1000 samples at 1 kS/s lasts one second.
- **Trigger Source**: always Immediate (the myDAQ has no trigger input for the generator).
- **Generation Mode**: **Run Continuously** loops the waveform; **Run Once** plays it once and then
  holds the last value.

### Run bar

- **Run** starts every channel that is not Off. **Stop** stops everything and sets the outputs to 0 V.
- **Single** plays all Arbitrary channels once.

### Preview

The plot shows what each channel generates (green = AO 0, cyan = AO 1). The **Display** selector
switches between **Time** and **Frequency** (amplitude spectrum).

## Waveform Editor

A waveform is a chain of **segments** played one after another. Each segment has a **Duration** and
contains **components** that are combined with the **Function** operator (`+`, `-`, `x`, `/`, or `FM`,
which uses the previous result to modulate the frequency of the new component). A segment with no
component is 0 V.

- **New Segment** adds a segment after the selected one; **New Component** adds a component to the
  selected segment. Select items in the list; **Index** with its arrows steps through segments.
- **Library** components: 21 ready-made shapes (Sine, Square, Triangle, Sawtooth, three kinds of
  noise, rising and falling exponentials, Hanning, Haversine, linear and logarithmic sweeps,
  Multitone, Pulse, Ramp, Circle, DC Level, sin x/x, Trapezoid, Stairstep) with **Amplitude**,
  **Offset**, **Freq** and **Phase**, plus extra settings for some shapes (duty cycle, stop frequency,
  number of tones or steps...). Shapes repeat at Freq: set Freq = 1 / Duration for exactly one shape
  per segment.
- **Expression** components: type a formula of `x`, e.g. `sin(2*pi*x) + 0.3*sin(6*pi*x)`. **X Range**
  From/To is spread across the segment and repeated **Cycles** times. Operators `+ - * / ^`, constants
  `pi` and `e`, functions `abs atan ceil cos exp floor ln log max min mod sin sqrt tan` (and `asin
  acos sinh cosh tanh sign`). Mistakes are shown in red with the character position.
- **Sketch** components: press **Draw**, then paint with the **Freehand** pencil or place and drag
  points with the **Line** tool. **Samples** sets the resolution, **Cycles** how often the sketch
  repeats in the segment. **Import...** fills a sketch from a text, WAV, MYBENCH or binary file.
- **View** shows the whole waveform, the selected segment or the selected component. **X Axis**
  switches to a frequency view (power spectrum in dB). **Sample Rate** sets how many points per second
  the waveform has; **Units** chooses Hz, kHz or MHz for frequency values.
- **Edit** commands: Undo/Redo, Cut/Copy/Paste, Clear, Select All, Import from File, Scale Selection
  (stretch in time or scale in amplitude), Info (description), Settings (cursors and colors).
- **Save** stores the document as a `.mbw` file; **Use in channel** sends it straight to a generator
  channel; **Export** writes CSV or WAV.

## Tips

- For clean signals keep at least 10 samples per period (Update Rate or editor Sample Rate at least 10
  times the highest frequency); below 2 samples per period the waveform aliases.
- At high frequencies the square wave's edges move by up to 5 µs from period to period: the output is
  updated 200 000 times per second.
- The audio outputs block DC: offsets and very slow waveforms fade away.
- To compare two signals, run Function on AO 0 and Arbitrary on AO 1 and view both on the Oscilloscope.
- Save your editor work: `.mbw` files keep every segment and component editable.

## Troubleshooting

- **"Generator paused - AO in use by Bode"**: the Bode Analyzer needs the analog outputs. Stop it and
  press **Resume**.
- **"Output was interrupted... continuing at 100 kS/s"**: the computer could not keep up; close other
  heavy programs. Waveforms still play at their update rates.
- **Clipping warning**: reduce Gain or the waveform's amplitude so it stays within ±10 V (±2 V audio).
- **No signal on the audio jack**: select the `AudioOutput Left / Right` pair in the Output card.
- **Could not load a file**: use `.mbw`, PCM or float `.wav`, or a text file with one number per line
  (a `Time` column is skipped automatically).
