# Waveform Generator

The Waveform Generator drives the myDAQ outputs. It combines four tools in one panel: a
**function generator** (sine, square, triangle, frequency sweeps), an **arbitrary waveform
generator** (play a waveform from a file, the library or the editor), a **DC level** source, and
the **Waveform Editor**, where you build your own waveforms.

Each of the two channels has its own card on the right of the display. Both channels can run at the
same time, and you can start, stop or change one without disturbing the other.

## Connections

- **AO 0** and **AO 1** screw terminals, referenced to **AGND**: up to ±10 V. This is the default
  output pair.
- **AUDIO OUT** jack (left and right): up to ±2 V, AC coupled (no DC). Choose **Audio out** in the
  bar at the top to use it. The two channels always use the same pair.
- To look at a signal, wire AO 0 to **AI 0+** and AGND to **AI 0-** and open the Oscilloscope beside
  the generator (or connect AUDIO OUT to AUDIO IN).
- The outputs deliver only a few milliamps: use a buffer amplifier for low-impedance loads.

## Quick start: function generator

1. On the **AO 0** card, pick **Sine**, **Square** or **Triangle**.
2. Turn the **Frequency**, **Amplitude** (peak to peak) and **Offset** knobs, or click a value and
   type it (`1.5k` means 1500 Hz); **−** / **+** step it.
3. Press **Run**. The display shows the output; its legend (`AO 0`) has a filled swatch while the
   output is on.
4. Press **Stop**: the output returns to 0 V.

## Quick start: arbitrary waveform

1. On a channel card, pick **Arb** (Arbitrary). The card then shows everything about the waveform.
2. Open a file with the folder button (`.mbw` editor document, `.wav`, `.csv`/`.txt`, raw `.bin`, or,
   on a best-effort basis, an NI `.wdt` waveform or `.wfm` editor document), pick a shape with the
   book button (**Library**), or press **Open in Editor** to edit it (or draw a new one) in the
   Waveform Editor.
3. Set **Gain** and **Update Rate** (a file that stores its sample rate sets it for you).
4. Choose **Continuous** or **Once**, then press **Run** (or **Single** to play every Arbitrary
   channel once; Single appears while an Arbitrary channel has a waveform).

## The bar at the top

- **Run / Stop** starts every channel that has a waveform picked (when no output is on yet; a card's
  switch starts or stops that channel alone); below it, `Generating · 200 kS/s` or `Stopped`. **Stop** stops everything: outputs go to 0 V, including a
  channel that holds the end of a Run Once (the header then offers to release it).
- **Single** plays every Arbitrary channel once (shown only while an Arbitrary channel has a
  waveform).
- **Output**: **AO 0 / AO 1** | **Audio out**, the output pair. Changing it while running moves the
  signals.
- **⋯** also holds **Export AO 0 / AO 1 Waveform…** (CSV or WAV), **Waveform Editor**, **Align
  Phases** (restart both Function channels at phase 0 on the same sample) and **Sweep the Function
  channels** (every stopped Function channel), besides Export Image, Print and Reset.

## The display

- **One plot for both outputs**, as on a bench generator: AO 0 in orange, AO 1 in magenta, on one
  time axis. The axis spans two periods of the slower function (or one pass of the longest
  waveform), so the phase between the channels is visible; a much faster channel shows as a band.
  A channel whose output is off is drawn faded; an Off channel is not drawn. Switching a channel on
  or off never resizes the display.
- Above the plot: the legend (filled swatch = output on, `Off` = no waveform picked) and **Time** |
  **Frequency** (volts against time, or the spectrum in dBV: the harmonics of a function, the FFT
  of a waveform).
- DC levels are flat lines with a handle you can drag. When every channel in use is DC, the plot is
  a 60 s chart of the levels. At high frequencies the plot shows the real output samples as steps
  (for example 10 samples per period at 20 kHz).
- In a narrow pane (for example beside the Oscilloscope) the knobs fold away: each parameter keeps
  its typeable value with **−** / **+**, and the tools show icons only.

## Channel cards

- The **switch** in the card header turns that output on or off (off = 0 V). It is the only output
  switch; the card stays open while the output is off so you can prepare the next signal.
- The waveform pictures pick what the channel produces: **Sine**, **Square**, **Triangle** (function
  generator), **DC** (not on the AC-coupled audio outputs) or **Arb** (arbitrary). Click the picked
  one again to set the channel **Off**: the card shrinks to its pictures and reads `Off`, and Run
  leaves it out. Switching an Off channel on brings back its last waveform.
- Every knob change applies live while the channel generates (phase-continuous, no restart).
  Double-click a knob to return to its default.

### Sine, Square, Triangle

- **Frequency** 0.2 Hz to 20 kHz (logarithmic knob). **"Generates …"** under it appears when the
  frequency the hardware can produce differs from the one you asked for.
- **Amplitude** 0 to 10 Vpp and **Offset** -5 to +5 V (the audio outputs: up to 4 Vpp, no offset).
- **Duty Cycle** 0 to 100 % (Square only) and **Phase** in degrees (useful for two channels at the
  same frequency, e.g. 90° for quadrature).
- There is no modulation or front-panel manual mode: those belong to the NI ELVIS II hardware.

### DC

**Level** from -10 to +10 V. When only DC channels run, MYBENCH writes the level directly (no sample
clock) and each change applies at once; next to a function or waveform the level is part of the
running output. Stop returns to 0 V. From anywhere, the command palette (⌘K / Ctrl+K) offers
**Set AO 0 to … V** and **Set AO 1 to … V**.

### Arbitrary

- The waveform name (hover it for the file and its samples), the folder button (**Open…**), the book
  button (**Library** shapes, one cycle each, and **Clear Waveform**) and **Open in Editor** (edit
  this waveform in the Waveform Editor, or draw a new one; **‹ Generator** brings you back).
- **Gain** (-10 to 10, multiplies the waveform; samples beyond ±10 V, ±2 V audio, are clipped and the
  card tells how many) and **Update Rate** (0.1 S/s to 200 kS/s).
- **Continuous** / **Once** (Once plays one pass and holds the last value until you switch the
  output off or press Stop; **Then 0 V** returns to 0 V instead, the ELVISmx behaviour) and **Hold**
  / **Linear** interpolation.
- With both channels Arbitrary, the AO 1 card offers **Same rate as AO 0** (one shared Update Rate,
  as in ELVISmx).
- The generator always starts immediately (the myDAQ has no generator trigger input).

## Tools

The tool buttons below the cards open a floating panel over the display (× or Esc closes it).

- **Sweep**: per Function channel, **Start**, **Stop**, **Step** and **Step Interval** (1 ms to
  300 s) define a single linear sweep; **Start Sweep** runs it. Stop is included when it falls
  exactly on a step; each step lasts exactly the Step Interval. The card shows the step, a bar the
  progress; the channel stops (0 V) after the last step. The frequency is locked during the sweep;
  amplitude, offset and duty stay live.
- **Presets**: ready-made setups (1 kHz sine, TTL clock, quadrature pair, sweep, ±5 V reference,
  ELVISmx defaults), your own saved setups (the bin deletes one) and **Save Current Settings…**.

The tool palette stays at the bottom of the right column; the cards above it scroll when they do
not fit.

Files: `.mbw` documents are rendered at their own sample rate; `.wav` uses the first channel with
full scale = 1 V; text files take the first column (the second when the first is a `Time` column)
and read the rate from a `# sampleRate=` line or the time column; `.bin` is 16-bit signed
little-endian with ±5 V full scale. `.wdt` waveforms and `.wfm` documents of the NI Waveform Editor
are read on a best-effort basis (their layout was worked out without NI sample files: check the
preview). The Open dialog starts in the folder of the last waveform you opened.

## Sharing the outputs

If another instrument (the Bode Analyzer) needs the outputs, the generator pauses; **Resume**
restarts the channels (a sweep in progress is not resumed). The engine always streams at the
device rate (phase-continuous changes, exact frequencies, linear interpolation); outputs that are
only DC levels are written directly.

## Waveform Editor

A waveform is a chain of **segments** played one after the other. Each segment has a **Duration**
and **components**, combined from top to bottom with the **Function** operator: `+`, `−`, `×`, `/`,
or **FM** (the result so far speeds up or slows down the new component: +0.5 means 50 % faster). An
empty segment is 0 V. The document's **Sample Rate** fixes how many points each second has.

- **Units** is the document's unit system: **Hz** (seconds, S/s), **kHz** (milliseconds, kS/s) or
  **MHz** (microseconds, MS/s). As in ELVISmx, switching keeps the numbers and changes their meaning:
  a 10 s segment becomes 10 ms, 10000.0 S/s becomes 10000.0 kS/s, 1 Hz becomes 1 kHz; the shape and
  the number of samples stay the same. Undo reverts it.
- **Layout**: the waveform plot on the left with the segment list under it; on the right the
  **Document** card, the **Segment** card (the selection) and the tools. The bar above the plot has
  **‹ Generator**, Undo / Redo, **Zoom to fit**, New / Open / Save, and **⋯** with every File and
  Edit action (New, Open, Close, Save, Save As, Import, Export, Undo, Redo, Cut, Copy, Paste, Clear,
  Select All, Scale Selection, Info, Settings, Help).
- The list shows segments (`1: name (10 sec)`) and their components. Click to select; drag a row to
  move it, or use the arrows (Alt+↑/↓). **Index** steps through segments; clicking the plot selects
  the segment under the pointer. **New Segment** (1 s) inserts after the selection; **New
  Component** inserts the library function you chose last (a sine at first).
- **Document** card: name, **Points**, **Duration**, **Sample Rate** (knob or type a value),
  **Hz / kHz / MHz**, **Use in AO 0 / AO 1** and **Live update**.
- **Segment** card, for the selection:
  - **Segment**: Name, Duration (knob, in the document's time unit), Gain.
  - **Component**: pictures for Sine, Square, Triangle, Ramp (sawtooth), DC, Noise, **f(x)** and
    **Sketch**; **Combine** `+ − × /` or **FM** with the components above (not for the first one);
    then the function's parameters as knob fields.
  - **Library** (the **Functions** tool lists all 21 functions of the NI editor with their NI
    defaults): DC Level, Sine, Square
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
  - **Expression** (f(x)): `f(x) =` a formula of `x`, for example `sin(2*pi*x) + 0.3*sin(6*pi*x)`. **X
    Range** From/To is spread over the segment and repeated **Cycles** times. Operators `+ - * / ^`,
    `pi`, `e`, and `abs acos asin atan ceil cos cosh exp floor ln log max min mod sign sin sinh sqrt
    tan tanh`. The preview and any mistake (in red, with the character position) follow your typing.
  - **Sketch**: Name, Cycles, Samples, **Import…** (replace the values from a file) and **Draw**.
- **Tools** (below the cards; each opens a floating panel, × closes it):
  - **Draw**: paints one cycle of the selected sketch (a new sketch component when none is
    selected) with **Freehand**, or **Line** to click points, drag them, and right-click to delete
    one. While drawing, the palette on the plot shows the pointer position, the list and the
    structure buttons are grayed, and the Y range stays fixed; press **Done** (or Draw) to finish.
  - **Functions**: all 21 library functions, f(x) and Sketch for the selected component (a new
    component when a segment is selected).
  - **Import**: from a file as new segments (text wizard showing the detected delimiter, column,
    start line and count; WAV channel; binary), or into the selected sketch.
  - **Export**: the rendered waveform as CSV, 32-bit float WAV, 16-bit binary, or `.wdt` in
    MYBENCH's reading of the LabVIEW layout (MYBENCH reads it back; NI software was not available to
    confirm it opens it); Save As; Copy Data (tab-separated Time / Amplitude on the clipboard).
  - **Display**: **Waveform / Segment / Component** view, **Time / Frequency** (power spectrum in
    dBVrms), **Line / Line + Points / Points**, line width, step interpolation, **Plot visible**,
    **Trace style…** (color, line style, points, interpolation, fill), Copy data, two **Cursors**
    with ΔX, ΔY and 1/ΔX, and **Settings…** (plot, selection, grid, cursor and background colors,
    Max Points, Animate Graphs, Show Cursors).
  - **Scale**: stretch the selection in time and scale it in amplitude.
  - **Info** (⌘I): the file path and the description stored with the document.
- **Plot**: hover it for the plot toolbar (pointer, zoom menu, pan, autoscale X / Y and their locks);
  right-click for the same, Trace style…, Cursors, Copy data, and image / data export (the rendered
  waveform). Autoscale is locked by default, so the view follows every edit; zooming or panning
  unlocks it and your zoom is kept while you edit; **Zoom to fit** locks it again.
- Files: Open (⌘O; `.mbw`, an NI `.wfm` document with its segments, or any supported file as a
  one-sketch document), New (⌘N), Close (⌘W), Save (⌘S) / Save As (⇧⌘S, `.mbw`). New, Open and
  Close ask before discarding unsaved changes, and so does the pencil (Edit) of a channel card when
  the editor holds another modified waveform.
- Keys: Undo / Redo (every edit, ⌘Z / ⇧⌘Z), Cut, Copy, Paste, Clear (Del), Select All (⌘A), this
  page (⌘H). They work while the editor has the focus (click in the list, the plot or a field of its
  cards); elsewhere ⌘N / ⌘O / ⌘S keep their session meaning.
- **Max Points** (Settings, 500 000 by default as in ELVISmx, up to 10 000 000): an edit that would
  make the waveform longer is refused with a message. A waveform already above the limit (a large
  channel file) stays editable as long as it does not grow.
- **Use in AO 0 / AO 1** (Document card) loads the document into that channel (Arbitrary mode; its Update Rate follows
  the document). With **Live update**, every later edit reaches the channel, also while it plays.

## Tips

- Keep at least 10 samples per period (Update Rate or editor Sample Rate at least 10 times the
  highest frequency).
- Knob turning while generating is glitch-free (phase-continuous, no restart).
- The audio outputs block DC: offsets and very slow waveforms fade away.
- Run Function on AO 0 and Arbitrary on AO 1 and view both on the Oscilloscope to compare them.

## Troubleshooting

- **"Generator paused - AO in use by …"**: another instrument needs the analog outputs. Stop it and
  press **Resume**.
- **"Output was interrupted… continuing at 100 kS/s"**: the computer could not keep up with the
  stream; close heavy programs. A second interruption stops the
  generator ("The generator was stopped").
- **Clipping warning**: reduce Gain or the waveform's amplitude.
- **"no waveform loaded"**: an Arbitrary channel needs a waveform before it can start.
- **"The waveform would have … samples (max …)"**: raise **Max Points** in the editor's Settings.
- **No signal on the audio jack**: select **Audio out** in the bar at the top.
