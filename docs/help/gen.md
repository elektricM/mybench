# Waveform Generator

The Waveform Generator drives the myDAQ outputs. It combines four tools in one panel: a
**function generator** (sine, triangle, square, frequency sweeps), an **arbitrary waveform
generator** (play a waveform from a file or from the editor), a **DC level** source, and the
**Waveform Editor**, where you build your own waveforms.

Each of the two channels is set to **Off**, **Function**, **Arbitrary** or **DC**. Both channels can
run at the same time, and you can start, stop or change one without disturbing the other.

## Connections

- **AO 0** and **AO 1** screw terminals, referenced to **AGND**: up to ±10 V. This is the default
  output pair.
- **AUDIO OUT** jack (left and right): up to ±2 V, AC coupled (no DC). Choose
  `AudioOutput L / R` under **Output Channel** to use it. The two channels always use the same pair.
- To look at a signal, wire AO 0 to **AI 0+** and AGND to **AI 0-** and open the Oscilloscope (or
  connect AUDIO OUT to AUDIO IN).
- The outputs deliver only a few milliamps: use a buffer amplifier for low-impedance loads.

## Quick start: function generator

1. On the **AO 0** card, **Mode** is **Function** by default.
2. Pick **Sine**, **Triangle** or **Square**, then set **Frequency**, **Amplitude** (peak to peak)
   and **DC Offset** with the knobs or by typing (`1.5k` means 1500 Hz).
3. Press **Run** (or the card's switch). The big readout shows the frequency generated, with four
   decimals (`100.0000 Hz`, `1.2345 kHz`).
4. Press **Stop**: the output returns to 0 V and the readout shows `OFF`.

## Quick start: arbitrary waveform

1. Set a channel's **Mode** to **Arbitrary**.
2. **Open** a file (`.mbw` editor document, `.wav`, `.csv`/`.txt`, raw `.bin`, or, on a best-effort
   basis, an NI `.wdt` waveform or `.wfm` editor document), pick one of the 21 **Library** shapes, or
   press the pencil to build one in the Waveform Editor.
3. Check **Update Rate** (a file that stores its sample rate sets it for you) and **Gain**.
4. Choose **Run Continuously** or **Run Once**, then press **Run** (or **Single** to play every
   Arbitrary channel once; Single is grayed while no Arbitrary channel has a waveform).

## The display

- **Generator** / **Waveform Editor** tabs switch between the outputs and the editor.
- One readout per channel: `OFF`, the generated frequency, the update rate (`1.00 kS/s`), the DC
  level, or `Done` after a Run Once (the output then holds the last value). Its switch starts or
  stops that channel.
- Below, a live preview of each output. **Display** switches between **Time** (volts against time)
  and **Frequency** (spectrum in dBV: the harmonics of a function, the FFT of a waveform). A DC channel
  shows a chart of its level; you can drag the level line.
- At high frequencies the preview shows the real output samples as steps (for example 10 samples
  per period at 20 kHz).

## Instrument Control card

- **Output Channel**: `AO 0 / AO 1` or `AudioOutput L / R`. Changing it while running moves the
  signals to the other outputs.
- **Engine**: how the signal is produced.
  - **Auto** (default): a continuous stream computed on the fly at 200 kS/s. Every change of
    frequency, shape, amplitude or offset is smooth and phase-continuous (no restart, no glitch), the
    frequency is exact, **Linear** interpolation works, and one channel starts or stops without
    disturbing the other. When only DC levels run, they are written directly (no sample clock).
  - **Regenerate**: like NI ELVISmx, one period (or the waveform) is loaded into the device and
    repeated whenever one buffer can describe the outputs; every change briefly restarts the output.
    The readout shows the requested frequency; the caption under it shows the buffer length and the
    frequency that buffer really gives (for example 1.2345 kHz requested, 1.2346 kHz actual).
  - **Stream**: always stream, DC levels included.
  The engine in use and its rate appear at the top right of the display.
- **Presets**: ready-made setups (1 kHz sine, TTL clock, quadrature pair, sweep, ±5 V reference,
  ELVISmx defaults) and your own saved setups.
- **Link update rates**: both Arbitrary channels share one Update Rate (as in ELVISmx).
- **Align phases**: restarts both Function channels at phase 0 on the same sample (streaming).

## Function mode

- **Sine / Triangle / Square**, **Frequency** 0.2 Hz to 20 kHz. The frequency knob has **Coarse**
  (whole range) and **Fine** (±25 % around the current value) modes; in the field, arrow keys or the
  mouse wheel change the digit at the cursor.
- **Amplitude** 0 to 10 Vpp and **DC Offset** -5 to +5 V, with a small knob each (the audio outputs:
  up to 4 Vpp, no offset).
- **Duty Cycle** 0 to 100 % (square only), **Phase** in degrees (useful for two channels at the same
  frequency, e.g. 90° for quadrature).
- There is no modulation or front-panel manual mode: those belong to the NI ELVIS II hardware.
- **"Generates …"** under the frequency tells the exact frequency you get. The stream gives the
  requested frequency; with the **Regenerate** engine the buffer rate must be 100 MHz divided by a
  whole number, so some frequencies are slightly different (3 Hz becomes 3.0003 Hz).

### Sweep Settings

- **Start Frequency**, **Stop Frequency**, **Step** and **Step Interval** (1 ms to 300 s) define a
  single linear sweep. Stop is included when it falls exactly on a step. **Sweep** (on the card, or
  in the run bar for every Function channel) starts it; the readout follows the steps and a bar shows
  progress; the channel stops (0 V) after the last step. A sweep starts from a stopped channel:
  **Sweep** is grayed while the channel generates.
- Each step lasts exactly the Step Interval. **Whole 250 ms steps (as ELVISmx)**: each step lasts the
  Step Interval rounded up to a multiple of 250 ms, as ELVISmx does.
- The frequency settings are locked during the sweep; shape, amplitude, offset and duty stay live.

## Arbitrary mode

- The thumbnail shows the loaded waveform and its name (`< empty >` when none). Hover it for the path
  and sample count.
- **Open**, **Library** (the 21 editor shapes as one-cycle waveforms), **Recent** (clock icon),
  **Edit** (pencil: opens it in the Waveform Editor), and in **⋯** **Export waveform** and **Clear**.
- **Gain** (-10 to 10) multiplies the waveform. Samples beyond ±10 V (±2 V audio) are clipped and a
  warning tells how many. (ELVISmx accepts any gain and then fails when it runs; MYBENCH limits it.)
- **Waveform Points**: number of samples, and how long one pass lasts at the Update Rate.
- **Update Rate**: samples per second, 0.1 S/s to 200 kS/s.
- The generator always starts immediately (the myDAQ has no generator trigger input).
- **Generation Mode**: **Run Continuously** loops; **Run Once** plays once and holds the last value
  (`Done`, amber LED) until you switch the channel off or press **Stop**, which return it to 0 V.
- **Return to 0 V after Run Once**: the ELVISmx behaviour: after one pass the output goes back to
  0 V and the readout shows `OFF`.
- **Interpolation**: **Hold** keeps each sample until the next update, like a DAC at the Update Rate;
  **Linear** draws straight lines between samples (it uses the stream, also with the Regenerate
  engine).

Files: `.mbw` documents are rendered at their own sample rate; `.wav` uses the first channel with
full scale = 1 V; text files take the first column (the second when the first is a `Time` column)
and read the rate from a `# sampleRate=` line or the time column; `.bin` is 16-bit signed
little-endian with ±5 V full scale. `.wdt` waveforms and `.wfm` documents of the NI Waveform Editor
are read on a best-effort basis (their layout was worked out without NI sample files: check the
preview). The Open dialog starts in the folder of the last waveform you opened.

## DC mode

The **DC Level** tool: a slider and a field from -10 to +10 V in 0.01 V steps. When only DC channels
run, MYBENCH writes the level directly (no sample clock) and each change applies at once; next to a
function or waveform the level is part of the running output. Stop returns to 0 V. From anywhere,
the command palette (⌘K / Ctrl+K) offers **Set AO 0 to … V** and **Set AO 1 to … V**.

## Run bar

- **Run** starts every channel whose Mode is not Off (with both channels Off, a warning says so).
  **Stop** stops everything: outputs go to 0 V, including a channel that holds the end of a Run
  Once (the run bar then offers to release it).
- **Single** plays every Arbitrary channel once. **Sweep** sweeps every stopped Function channel.
- If another instrument (the Bode Analyzer) needs the outputs, the generator pauses; **Resume**
  restarts the channels (a sweep in progress is not resumed).

## Waveform Editor

A waveform is a chain of **segments** played one after the other. Each segment has a **Duration**
and **components**, combined from top to bottom with the **Function** operator: `+`, `−`, `×`, `/`,
or **FM** (the result so far speeds up or slows down the new component: +0.5 means 50 % faster). An
empty segment is 0 V. The document's **Sample Rate** fixes how many points each second has.

- **Units** is the document's unit system: **Hz** (seconds, S/s), **kHz** (milliseconds, kS/s) or
  **MHz** (microseconds, MS/s). As in ELVISmx, switching keeps the numbers and changes their meaning:
  a 10 s segment becomes 10 ms, 10000.0 S/s becomes 10000.0 kS/s, 1 Hz becomes 1 kHz; the shape and
  the number of samples stay the same. Undo reverts it.
- The list shows segments (`1: name (10 sec)`) and their components. Click to select; drag a row to
  move it, or use the arrows (Alt+↑/↓). **Index** steps through segments; clicking the plot selects
  the segment under the pointer. **New Segment** (1 s) inserts after the selection; **New
  Component** inserts the library function you chose last (a sine at first).
- The inspector shows the selection's parameters:
  - **Segment**: Name, Duration (in the document's time unit), Gain.
  - **Library**: the 21 functions of the NI editor with their NI defaults: DC Level, Sine, Square
    (duty cycle), Triangle, Sawtooth, Uniform / Triangular / Gaussian Noise (Level = standard
    deviation; a **Seed** makes a waveform render the same everywhere, Seed 0 gives new random values
    at every render), Rising / Falling Exponential (Tau sets how steep: the rising one climbs from 0
    to the amplitude, steepest at the end of each period; the falling one mirrors it), Hanning, Ramp,
    Linear / Log Sweep and Multitone (Start and Stop Freq), Sin x/x (main lobe in the middle of the
    segment), Trapezoid and Pulse (Delay, Rise, Hold, Fall in % of the segment, at most 100 % in
    total; Pulse has rounded edges), Stairstep (Start, Stop, Steps), Circle (one arch from −A to +A
    per period), Haversine. Shapes with a frequency repeat at it (Freq = 0 gives one cycle per
    segment); the others span the whole segment. A warning appears when a frequency exceeds half the
    sample rate.
  - **Expression**: `f(x) =` a formula of `x`, for example `sin(2*pi*x) + 0.3*sin(6*pi*x)`. **X
    Range** From/To is spread over the segment and repeated **Cycles** times. Operators `+ - * / ^`,
    `pi`, `e`, and `abs acos asin atan ceil cos cosh exp floor ln log max min mod sign sin sinh sqrt
    tan tanh`. The preview and any mistake (in red, with the character position) follow your typing.
  - **Sketch**: Name, Cycles, Samples, **Import…** (replace the values from a file) and **Draw**:
    paint one cycle with **Freehand**, or use **Line** to click points, drag them, and right-click to
    delete one. While drawing, the palette shows the pointer position, the list and the structure
    buttons are grayed, and the Y range stays fixed; press **Done** (or Draw) to finish.
- **Plot**: hover it for the plot toolbar (pointer, zoom menu, pan, autoscale X / Y and their locks);
  right-click for the same, Trace style…, Cursors, Copy data, and image / data export (the rendered
  waveform). Autoscale is locked by default, so the view follows every edit; zooming or panning
  unlocks it and your zoom is kept while you edit.
- **Display** card: **View** (whole Waveform, the selected Segment, or the selected Component
  alone), **X Axis** (Time, or Frequency: power spectrum in dBVrms), **Plot Style** (Line, Line +
  Points, Points), line width, step interpolation, **Plot visible**, **Trace style…** (color, line
  style, points, interpolation, fill), **Copy data** (the waveform as tab-separated Time / Amplitude
  on the clipboard) and two **Cursors** with ΔX, ΔY and 1/ΔX.
- **File** menu: New (⌘N / Ctrl+N), Open (⌘O; `.mbw`, an NI `.wfm` document with its segments, or
  any supported file as a one-sketch document), Close (⌘W), Save (⌘S) / Save As (⇧⌘S, `.mbw`), Import
  from File (text wizard showing the detected delimiter, column, start line and count; WAV channel;
  binary), Export (CSV, 32-bit float WAV, 16-bit binary, or `.wdt` in MYBENCH's reading of the
  LabVIEW layout: MYBENCH reads it back, NI software was not available to confirm it opens it).
  New, Open and Close ask before discarding unsaved changes, and so does the pencil (Edit) of a
  channel card when the editor holds another modified waveform.
- **Edit** menu: Undo / Redo (every edit, ⌘Z / ⇧⌘Z), Cut, Copy, Paste, Clear (Del), Select All
  (⌘A), Import from File, Scale Selection (stretch in time and scale in amplitude), **Settings…**
  (plot, selection, grid, cursor and background colors, Max Points, Animate Graphs, Show Cursors),
  Info (⌘I: file path and description).
- **Help** menu: this page (⌘H) and About.
- The shortcuts work while the editor has the focus (click in the list, the plot or a field of its
  cards); elsewhere ⌘N / ⌘O / ⌘S keep their session meaning.
- **Max Points** (Settings, 500 000 by default as in ELVISmx, up to 10 000 000): an edit that would
  make the waveform longer is refused with a message. A waveform already above the limit (a large
  channel file) stays editable as long as it does not grow.
- **Use in AO 0 / AO 1** loads the document into that channel (Arbitrary mode; its Update Rate follows
  the document). With **Live update**, every later edit reaches the channel, also while it plays.

## Tips

- Keep at least 10 samples per period (Update Rate or editor Sample Rate at least 10 times the
  highest frequency).
- Knob turning while generating is glitch-free with the default engine; **Regenerate** restarts the
  buffer at each change, like ELVISmx.
- The audio outputs block DC: offsets and very slow waveforms fade away.
- Run Function on AO 0 and Arbitrary on AO 1 and view both on the Oscilloscope to compare them.

## Troubleshooting

- **"Generator paused - AO in use by …"**: another instrument needs the analog outputs. Stop it and
  press **Resume**.
- **"Output was interrupted… continuing at 100 kS/s"**: the computer could not keep up with the
  stream; close heavy programs, or use Engine **Regenerate**. A second interruption stops the
  generator ("The generator was stopped").
- **Clipping warning**: reduce Gain or the waveform's amplitude.
- **"no waveform loaded"**: an Arbitrary channel needs a waveform before it can start.
- **"The waveform would have … samples (max …)"**: raise **Max Points** in the editor's Settings.
- **No signal on the audio jack**: select `AudioOutput L / R` under Output Channel.
