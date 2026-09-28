# Waveform Generator (`gen`)

Read [app.md](app.md) first (application shell and common conventions: run bar, inspector cards,
SiField, knobs, plots, cursors, export, notifications, resource arbiter) and [README.md](README.md)
(hardware constraints). This file is the complete **functional** specification of the MYBENCH
waveform generator, which replaces three ELVISmx programs: the **Function Generator** (FGEN), the
**Arbitrary Waveform Generator** (ARB) and the **Waveform Editor**. Every control, value list,
default, rule and algorithm of sections 3 and 4 must exist in MYBENCH. The "ELVISmx reference
layout" subsections are informative only.

## 1. Purpose

Generates signals on the myDAQ analog outputs AO 0 and AO 1 (±10 V, screw terminals) or on the
audio outputs AudioOutput Left/Right (±2 V, AUDIO OUT jack). Each of the two channels works
independently in one of three modes:

- **Off**: the channel outputs its safe level (0 V).
- **Function**: sine, triangle or square wave with frequency, peak-to-peak amplitude, DC offset and
  duty cycle, adjustable in real time, plus a single linear frequency sweep (the ELVISmx FGEN).
- **Arbitrary**: a user waveform loaded from a file or made in the built-in Waveform Editor, played
  at an update rate with a gain, looped or once (the ELVISmx ARB + Waveform Editor).

Both channels can run at the same time (e.g. a function on AO 0 and an arbitrary waveform on AO 1),
something ELVISmx cannot do on a myDAQ. The myDAQ has no hardware function generator: all signals
are synthesized in software and streamed to the hardware-timed AO (section 3).

Reference screenshots (ELVISmx): `ni-local/screens/Fgen.png`, `Arb.png`, `WaveformEditor.png`.

## 2. MYBENCH design notes

How the functions map to the MYBENCH panel (UI.md / app.md) and which enhancements are added.
Enhancements must not change the default behaviour of sections 3-4.

- **Panel layout**: display area on top, inspector (cards) beside or below it, run bar at the bottom.
- **Display area**: a live **preview plot** of both outputs (what is being or would be generated):
  CH 0 green (`trace0`), CH 1 cyan (`trace1`), analyzer-style axes `Amplitude (V)` / `Time (s)`.
  Function channels show 2-3 periods of the current settings; Arbitrary channels show one pass of the
  loaded waveform after gain and clipping. A **Display** selector (`Time` / `Frequency`, the ARB's
  Display ring) switches the preview to an RMS amplitude spectrum (4.2.5). Above the plot, one big
  cyan readout per channel, in its color accent: `OFF` when the channel is not generating, the
  generated frequency (Function, 4.1.5) or the update rate (Arbitrary, 4.2.4) while generating.
  Small readouts per channel: shape/waveform name, `Waveform Points`, sweep progress (`step 3/10`).
- **Inspector cards**:
  - *Output* card: output pair selector `AO 0 / AO 1` (default) or `AudioOutput Left / Right`
    (the two kinds can never be mixed in one task). Device selection is in the app top bar.
  - Two *channel cards*, titled `AO 0` (green accent) and `AO 1` (cyan accent), or
    `AudioOutput Left` / `AudioOutput Right` when the audio pair is selected. Each card has a
    **Mode** segmented control (`Off` / `Function` / `Arbitrary`), an **Output** on/off toggle with a
    state LED (the per-channel Run/Stop), and the mode's sections:
    - Function: *Waveform Settings* (shape buttons, Frequency knob + SiField, Amplitude knob +
      SiField, DC Offset knob + SiField, Duty Cycle, Modulation Type) and *Sweep Settings* (Start
      Frequency, Stop Frequency, Step, Step Interval, **Sweep** button).
    - Arbitrary: *Waveform Settings* (waveform name, Open, Recent, Library, Clear, **Edit** which
      opens the editor for this channel, Gain) and *Timing and Triggering Settings* (Update Rate,
      Trigger Source, Generation Mode).
  - Defaults: pair AO; `AO 0` Mode = Function (so the panel opens as a ready function generator,
    like the ELVISmx FGEN with Signal Route AO 0); `AO 1` Mode = Off.
- **Run bar**: `Run` (starts every channel whose Mode is not Off), `Stop` (stops everything,
  including sweeps), `Single` (plays the Arbitrary channels once, 5.2), status text per channel
  (`AO 0: 100.00 Hz sine · AO 1: off`) and an underflow/fallback indicator.
- **Waveform Editor**: embedded; it opens as a drawer or a full editor view bound to a channel's
  Arbitrary waveform (`Edit` button), and can also be opened standalone (nav/command palette,
  "New waveform") to create files. Several editor documents may be open. Saving is optional: the
  channel can use the edited document directly (4.3.8 "Use in channel").
- **Dashboard tile**: two mini previews, per channel mode icon + big value (frequency or rate) +
  output LED, run state.
- **Controls**: all numerics are `SiField`s (typed SI prefixes, per-digit tuning); knobs are kept for
  Frequency (logarithmic), Amplitude and DC Offset as in ELVISmx.
- **Enhancements** (optional per release, all off/neutral by default):
  - *Live preview* of both outputs (above) - recommended for the first release.
  - *Phase offset between channels*: per Function channel `Phase (deg)` (-180...180, default 0)
    relative to the channel's accumulator, and an `Align phases` action that restarts both Function
    accumulators together at the next block (3.4).
  - *Burst*: in Function mode, a `Burst` button outputs N complete cycles (N = 1...10 000, default 10)
    then returns to the DC offset level, with a repeat period option.
  - *Software modulation*: since synthesis is software, Modulation Type may offer `AM` / `FM` (ELVIS
    features) with a modulation frequency and depth; default `None` and disabled unless enabled.
  - *Waveform library and recent files*: built-in presets (the 21 library functions as ready
    documents, 4.3.4), a user folder, the last 10 files.
  - *Draggable sketch editing*: vertex handles in Line mode, freehand painting, drag directly on the
    editor plot (4.3.6).
  - *Export*: the current channel waveform (Function: an integer number of periods at the task rate;
    Arbitrary: the rendered waveform) to `.csv` or `.wav`; editor export (4.3.8).
  - Audio pair in Function mode (amplitude limited to 4.00 Vpp, DC Offset forced to 0 because the
    audio outputs are AC-coupled).

## 3. Output engine (normative)

### 3.1 One streaming task for both channels

- Both channels of the selected pair run in **one** AO task: `AoConfig{ channels = [{AO0, 10}, {AO1,
  10}] (or [{AudioOutLeft, 2}, {AudioOutRight, 2}]), sampleRate = fs, mode = Continuous,
  regenerate = false }` (streaming).
- Task rate `fs = coerceAoRate(maxAoRate())` (200 kS/s on the myDAQ), fixed for the life of the task.
  All synthesis uses the actual rate `AoTask::sampleRate()`.
- The task exists while at least one channel generates: it is created when the first channel starts
  and destroyed when the last one stops. A channel that is Off/stopped while the other runs outputs
  its safe level **inside the stream** (0 V; a finished Run Once holds its last sample, 4.2.4), so
  one channel can start, stop or change mode without disturbing the other.
- After the task is destroyed, each AO channel is set with `Device::aoWriteStatic(ch, level)`: 0 V,
  or the held last sample of a finished Run Once. Audio outputs get no static write (AC-coupled).
  Errors of the static write (e.g. `ResourceBusy` because another instrument took AO) are ignored
  (logged).
- Switching the output pair (AO <-> audio) while generating stops the task, recreates it on the new
  pair and restarts the channels that were generating (toast "Output moved to <pair>").
- Starting one channel while nothing runs also drives the other channel of the pair (to 0 V).

### 3.2 Blocks, lead and latency

- The worker produces blocks of `B = round(0.010 * fs)` samples per channel (10 ms, 2000 samples at
  200 kS/s) from the latest settings snapshot.
- Pre-fill: before `start()`, write at least 100 ms of samples (`write()` before `start()`).
- Steady state: the worker keeps the **lead** (samples written minus samples already played,
  estimated as `written - elapsed * fs` from a steady clock started at `start()`, refined with
  `spaceAvailable()` when the backend reports it) near 100 ms (never below 50 ms, never above
  200 ms): it writes a block, then sleeps while the lead is above target. `write(block, 1000 ms)`.
- Consequence: a live change becomes audible/visible on the output after about 100-120 ms
  (lead + one block), with no reload glitch.

### 3.3 Underflow fallback

- `DeviceError::Underflow` (the computer did not keep up): the engine recreates the task at
  `fs = coerceAoRate(100 kS/s)`, restarts all generating channels (Function phases restart, Arbitrary
  playback continues from its current position) and shows a banner "Generator output was
  interrupted (computer too slow); continuing at 100 kS/s." A second underflow at 100 kS/s stops all
  channels with an error banner. The reduced rate stays until the task is next created from idle.
- If the backend does not support streaming (`NotSupported` for `regenerate = false`), use the
  regenerate fallback of 3.6.

### 3.4 Function channels: NCO synthesis

- Per channel a 64-bit phase accumulator `φ` (2^64 = one period) with increment
  `inc = round(f / fs * 2^64)`; the generated frequency is `inc * fs / 2^64` (relative error below
  1e-12, i.e. exact for display purposes).
- Sample value: `u = φ / 2^64` (+ `Phase/360` enhancement, modulo 1),
  `y = O + (Vpp / 2) * dsp::waveformShape(shape, u, d)`; then `φ += inc`.
- Changes at block boundaries: frequency, shape and duty take effect at the first sample of the next
  block, **phase-continuous** (the accumulator is not reset); amplitude and offset are ramped
  linearly over that block (no click). `Align phases` sets both Function accumulators to 0 at the
  same block boundary.
- A channel start sets `φ = 0` (the waveform starts at its phase-0 point, e.g. a rising zero
  crossing of the sine). Channels started by the same Run press start on the same sample.
- Sweep steps (4.1.6) change `inc` at exact sample positions (not only at block boundaries).
- No band-limiting: square and triangle are sampled directly, so at high frequencies the square's
  edges jitter by up to one task sample (5 µs at 200 kS/s) - as with any DDS generator.

### 3.5 Arbitrary channels: phase-accumulator player

- Per channel a position `P` (fixed-point or double, in waveform samples) with increment
  `R / fs`, where `R` is the channel's Update Rate (1 S/s ... fs; higher values are coerced to fs).
- Sample value: zero-order hold `w[floor(P)]` (default, like a DAC updated at R) or, **[MYBENCH
  extension]** `Interpolation = Linear`, `w[i] + (w[i+1] - w[i]) * frac(P)` (wrapping to `w[0]` in
  Continuous mode). `w = gain * waveform`, clipped to the output range (4.2.4).
- Continuous: `P` wraps modulo the waveform length N. Once: when `P >= N` the channel is *Done* and
  outputs `w[N-1]` from then on.
- Update instants are quantized to the task sample grid (jitter up to 1/fs = 5 µs); the average
  update rate is exactly R. This keeps ELVISmx's per-waveform Update Rate while both channels share
  one task rate, and the two channels may use different update rates and lengths.
- Gain, Update Rate and a new waveform take effect at the next block (4.2.6).

### 3.6 Regenerate fallback (backends without streaming)

`dsp::planPeriodicBuffer` + `dsp::generatePeriodic` produce a regenerated buffer holding whole
periods (Function mode, one channel or two channels whose frequencies share a buffer plan);
Arbitrary channels use their waveform repeated to a common length (both repeated to
`lcm(N0, N1)`, cap 4 000 000 samples, else error "The two waveforms have incompatible lengths");
the task uses `regenerate = true`, rate = update rate. Every change is applied by `stop()`,
`write()`, `start()` (a few ms glitch). Sweeps use one reload per step (Step Interval >= 50 ms).
This path exists for robustness; the streaming engine is the reference behaviour.

## 4. Functional specification

### 4.1 Function mode (ELVISmx Function Generator)

A function generator on one analog output: sine, triangle or square wave with frequency,
peak-to-peak amplitude, DC offset and (square) duty cycle, adjustable in real time while generating,
plus a single linear frequency sweep. In MYBENCH every channel card in Function mode is one such
generator; ELVISmx's `Signal Route` (`AO 0` / `AO 1`) is the choice of the channel card.

#### 4.1.1 ELVISmx reference layout (informative, not binding)

ELVISmx window `Function Generator`, about 420 x 480 px, fixed.

```
+----------------------------------------------------------+
| [badge]                                             OFF  |  display (black), ~76 px
+----------------------------------------------------------+
| Waveform Settings                                        |
| [~]    Frequency          Amplitude        DC Offset     |
|        (big knob)          (knob)            (knob)      |
| [/\]                     0.0    10.0      -5.0   5.0     |
|      200m     20k        [1.00 ^v] Vpp    [0.00 ^v] V    |
| [_|]  [100    ^v] Hz     Duty Cycle       Modulation Type|
|                          [50 ^v] %        [None v]       |
+----------------------------------------------------------+
| Sweep Settings                                           |
| Start Frequency  Stop Frequency  Step        Step Interval|
| [100 ^v] Hz      [1k ^v] Hz      [100 ^v] Hz [1000 ^v] ms |
+----------------------------------------------------------+
| Instrument Control                                       |
| Device                    Signal Route                   |
| [myDAQ1 (NI myDAQ) v]     [AO0             v]            |
|                           Run   Sweep   Stop  |  Help    |
| [ ] Manual Mode           [->]  [~~~]   [■]   |  [?]     |
+----------------------------------------------------------+
```

- Display: black strip; one big bold cyan readout, right aligned: `OFF` or the frequency.
- Waveform Settings: three stacked square toggle buttons (exclusive): Sine (green icon), Triangle
  (dark red), Square (dark blue); the checked one has a light-blue face. Frequency: large knob with
  end labels `200m` / `20k`, numeric below with unit `Hz`. Amplitude: small knob `0.0` / `10.0`,
  numeric `Vpp`. DC Offset: small knob `-5.0` / `5.0`, numeric `V`. Duty Cycle numeric `%` (under
  Amplitude, grayed unless Square). Modulation Type combo `None` (grayed).
- Sweep Settings: one row of four labelled numerics with units.
- Instrument Control: Device, Signal Route (`AO0`), Manual Mode checkbox (grayed), Run, Sweep (chirp
  icon), Stop (grayed while idle), Help. No Print/Log.

#### 4.1.2 Controls and indicators

| Control | Type | Values / range | Format, increment | Default | Enabled when | Live while running |
|---|---|---|---|---|---|---|
| Sine / Triangle / Square | exclusive buttons | 3 shapes | | Sine | always | yes |
| Frequency | log knob (end labels `200m` / `20k`) + SiField | 0.2 Hz ... 20 kHz (typed values coerced: `50000` -> `20k`, `0.01` -> `200m`; verified in ELVISmx UI) | up to 5 significant digits with SI suffix, trailing zeros trimmed (`100`, `1.2345k`, `20k`, `200m`; verified); arrow increment = 1 % of the value's decade (MYBENCH; ELVISmx code: 10); knob values rounded to 3 significant digits | 100 Hz | not during a sweep | yes (phase-continuous) |
| Amplitude | linear knob (`0.0` ... `10.0`) + SiField | 0.00 ... 10.00 Vpp (coerced, verified) | 2 decimals; increment 0.01 (ELVISmx code) | 1.00 Vpp | not during a sweep | yes (ramped over one block) |
| DC Offset | linear knob (`-5.0` ... `5.0`) + SiField | -5.00 ... +5.00 V (coerced, verified) | 2 decimals; increment 0.02 (ELVISmx code) | 0.00 V | not during a sweep (audio pair: forced 0, disabled) | yes (ramped) |
| Duty Cycle | SiField | **0 ... 100 %** (coerced, verified; 0 and 100 give a constant level) | integer; increment 1 | 50 % | Square selected and not sweeping (grayed otherwise, verified) | yes |
| Modulation Type | combo | `None` (ELVIS: AM, FM; see enhancement) | | None | never (grayed, verified) | - |
| Start Frequency | SiField | 0.2 Hz ... 20 kHz (coerced, verified) | 2 decimals with SI suffix trimmed (`100`, `1k`, `200m`) | 100 Hz | not sweeping (grayed during a sweep, verified) | - |
| Stop Frequency | SiField | 0.2 Hz ... 20 kHz | as Start | 1 kHz (`1k`) | not sweeping | - |
| Step | SiField | **0** ... 20 kHz (coerced at 0, verified); a step of 0 is refused at Sweep by MYBENCH ("Step must be greater than 0 Hz") | 2 decimals (`123.46`) | 100 Hz | not sweeping | - |
| Step Interval | SiField (ms) | **1 ... 300 000 ms** (coerced, verified in UI and code) | integer; increment 1 | 1000 ms | not sweeping | - |
| Output toggle (Run/Stop of the channel) | toggle + LED | on/off | | off | Mode = Function | - |
| Sweep | button | start a single sweep on this channel | | | channel not generating | - |
| Manual Mode | (not shown) | ELVIS II front-panel knobs | | off | never | - |

| Indicator | Content |
|---|---|
| Channel readout | `OFF` when not generating; generated frequency while running or sweeping (4.1.5) |
| Preview | 2-3 periods of the current settings |

The amplitude/offset limits guarantee `|offset| + amplitude/2 <= 10 V` (the AO range), so no
clipping check is needed in Function mode.

#### 4.1.3 Waveform definition

With amplitude `Vpp`, offset `O`, frequency `f`, duty `d` (fraction), phase 0 at the start of each
period (the shapes of `dsp::waveformShape`):
- Sine: `O + (Vpp/2) sin(2π f t)`.
- Triangle: starts at `O` rising, peak `O + Vpp/2` at 1/4 period, `O - Vpp/2` at 3/4 period
  (symmetric, linear segments).
- Square: `O + Vpp/2` for the first `d` of each period, `O - Vpp/2` for the rest.

Amplitude 0 outputs the DC offset only.

#### 4.1.4 Generation and live changes

- Output toggle on (or run bar Run): the channel starts in the engine (3.1, 3.4) with `φ = 0`.
- Any change of shape, frequency, amplitude, offset or duty while running is sent as a new
  snapshot and applied at the next block (3.4): phase-continuous, no glitch, latency ~0.1 s. While a
  knob is dragged, snapshots are coalesced (at most one per block).
- Square duty resolution: the duty cycle is exact on average; each edge is placed on the nearest task
  sample (at 20 kHz and 200 kS/s, 10 samples per period, so a single period can only show multiples
  of 10 %, alternating around the requested value).

#### 4.1.5 Frequency display

While generating, the channel readout shows the frequency actually generated (the NCO frequency,
3.4) with **4 decimals**, SI prefix and unit, as ELVISmx (verified in ELVISmx UI): `100.0000 Hz`,
`1.0000 kHz`, `1.2345 kHz`, `20.0000 kHz`, `200.0000 mHz`. During a sweep it shows the current step's frequency. Not generating:
`OFF`. (ELVIS hardware measures its output with a counter and shows dashes when that measurement
times out; the myDAQ has no readback path, so MYBENCH shows the exact synthesized frequency and never
shows dashes.)

#### 4.1.6 Sweep

- Sweep (from a non-generating channel) generates, with the current shape, amplitude, offset and
  duty, the frequencies `f_k = Start + k * Step * sgn(Stop - Start)` for `k = 0, 1, ...` while `f_k`
  has not passed Stop (Stop is included when it falls on a step: 1e-9 relative tolerance). Start =
  Stop gives a single step. Each frequency lasts exactly `round(StepInterval * fs / 1000)` task
  samples; frequency changes are phase-continuous and sample-exact (3.4).
- The readout follows the current step (and the progress `step k/n`). Verified in ELVISmx UI
  (100 Hz -> 1 kHz, Step 300 Hz, 1000 ms: `100.0000 Hz`, `400`, `700`, `1.0000 kHz`, then `OFF`):
  during a sweep **every control is grayed** in ELVISmx (waveform buttons, Frequency, Amplitude,
  DC Offset, Duty Cycle, the Sweep Settings, Signal Route, Device, Run, Sweep) except Stop and Help;
  the Frequency control keeps its own value (it is not updated by the sweep). MYBENCH grays the
  Frequency and Sweep Settings and **[MYBENCH extension]** keeps shape, amplitude, offset and duty
  live.
- ELVISmx step timing (verified with NI I/O Trace): the sweep streams 0.25 s blocks, so a step lasts
  Step Interval rounded up to a multiple of 250 ms. MYBENCH steps exactly at Step Interval
  (sample-exact; documented improvement).
- After the last step the channel stops (single sweep): its output goes to the safe level and the
  readout shows `OFF` (if it was the last generating channel, the task is destroyed, 3.1). The
  channel Output toggle, the run bar Stop, or a mode change abort the sweep at any time.
- A sweep is not resumed after an arbiter pause (6): the channel reports "Sweep aborted".

#### 4.1.7 Stop and safe state

ELVISmx (verified with NI I/O Trace and code): Stop and the end of a sweep write a 0 V signal on the
routed output and clear the task, so the output rests at **0 V**. Verified UI states: while
generating (Run) the Device combo and the Sweep button are grayed, Stop is enabled; Signal Route,
the waveform settings and the Sweep Settings stay editable and apply live (a route change moves the
signal to the other output at once); `OFF` returns on Stop.

Channel stop (Output off, run bar Stop, end of sweep, panel close): the channel outputs 0 V inside
the stream; when no channel generates any more, the task is destroyed and `aoWriteStatic(ch, 0.0)`
is written (3.1). The readout shows `OFF`.

#### 4.1.8 Differences from ELVISmx (Function Generator)

- ELVIS-only: Manual Mode (front-panel knobs) and Modulation Type (AM/FM) are absent/disabled;
  Signal Route choices are AO 0/AO 1 (ELVIS: prototyping board / FGEN BNC), mapped to the channel
  cards.
- The ELVIS feature list mentions "8-bit amplitude/offset" (ELVIS hardware DDS); on the myDAQ
  amplitude and offset are part of the 16-bit AO samples.
- Frequency readback is computed, not measured (4.1.5).
- Live changes are glitch-free (streaming NCO) where ELVISmx reloads its buffer.
- Two independent function generators (AO 0 and AO 1) instead of one.

### 4.2 Arbitrary mode (ELVISmx Arbitrary Waveform Generator)

Plays a user-defined waveform on a channel: loaded from a file (MYBENCH `.mbw` made with the
Waveform Editor, WAV, CSV/TXT) with a gain, at an update rate (samples per second), looped or once.

#### 4.2.1 ELVISmx reference layout (informative, not binding)

ELVISmx window `Arbitrary Waveform Generator`, about 564 x 535 px, fixed.

```
+--------------------------------------------------------------------------+
| [badge]           Update Rate :      OFF                                 |  display (black) ~250 px
|  1.0 ┐+--------------------------------------------------------------+   |
| Ampl.||                 dense grid, no data                          |   |
| (V)  ||                                                              |   |
| -1.0 ┘+--------------------------------------------------------------+   |
|        0   0.1  0.2  0.3  0.4  0.5  0.6  0.7  0.8  0.9  1             |
|  Waveform Points :  0        Time (s) ▼ Display                 [+][Q]  |
|                                                                 [X][Y]  |
+--------------------------------------------------------------------------+
| Waveform Settings                                                        |
|  Output Channel             Waveform Name                        Gain    |
|  [AO 0        v] [ ]Enabled [          < empty >          ] [📂] [1.00^v]|
|  [AO 1        v] [ ]Enabled [          < empty >          ] [📂] [1.00^v]|
+--------------------------------------------------------------------------+
| Timing and Triggering Settings           | Waveform Editor               |
|  Update Rate     Trigger Source          |   [✎]  Click to Launch        |
|  [1.0k ^v] S/s   [Immediate v]           |                               |
+--------------------------------------------------------------------------+
| Instrument Control                                                       |
|  Device               Generation Mode           Run    Stop  |  Help     |
+--------------------------------------------------------------------------+
```

- Display: cyan `Update Rate :` label and a big bold cyan readout (`OFF` or the actual rate);
  analyzer-style graph (`Amplitude (V)` -1...1, `Time (s)` 0...1 at launch, dense dark-cyan grid
  with brighter major lines); `Waveform Points : 0` bottom-left; the axis title works as the
  `Display` ring (Time/Frequency); graph palette bottom-right (cursor tool, zoom, X and Y autoscale;
  a green dot marks the active tool and the locked autoscales - all on at launch). No Toggle View.
- Waveform Settings: per row Output Channel combo, Enabled checkbox (off), read-only name field
  `< empty >`, folder browse button, Gain `1.00`.
- Timing: Update Rate `1.0k` S/s; Trigger Source `Immediate` (grayed). Waveform Editor: button +
  `Click to Launch`. Instrument Control: Device, Generation Mode (`Run Continuously`), Run, Stop,
  Help; no Print/Log.

#### 4.2.2 Controls and indicators (per Arbitrary channel card unless noted)

| Control | Type | Values | Default | Enabled when | Live while running |
|---|---|---|---|---|---|
| Output pair (Output card; ELVISmx "Output Channel") | segmented | `AO 0 / AO 1`, `AudioOutput Left / Right` | AO | always | yes: task restarts on the new pair (3.1) |
| Mode = Arbitrary + Output toggle (ELVISmx "Enabled") | segmented + toggle | | Off | | - |
| Waveform Name | read-only label | `< empty >` or file/document name (tooltip: full path, sample count, file sample rate) | `< empty >` | always | - |
| Open (ELVISmx folder button) | button | file dialog (4.2.3) | | always | yes: new waveform (4.2.6) |
| Recent / Library **[MYBENCH extension]** | menus | last 10 files; built-in presets | | always | yes |
| Clear waveform | button / context menu (+ `Delete` on the name) | | | a waveform is loaded and the channel is not generating | no |
| Edit | button | opens the Waveform Editor on this channel's document (a copy of the loaded `.mbw`; for WAV/CSV a new document with one Sketch component holding the samples) | | always | yes (4.3.8 "Use in channel") |
| Gain | SiField | -10.00 ... +10.00, increment 0.01, 2 decimals. (ELVISmx has **no limit**: gain 50 was accepted and the generation then failed with a DAQmx "value out of range" error dialog - verified in ELVISmx UI; MYBENCH coerces and clips, 4.2.4) | 1.00 | always | yes (next block; ELVISmx rewrites the buffer, verified) |
| Update Rate | SiField (S/s) | **0.1 S/s** ... task rate (200 kS/s): ELVISmx accepted `0.1` (shown `100.0m`) and coerced `900k` to `200.0k` (verified); display with SI prefix and one decimal (`1.0k`, `5.0k`, `200.0k`, `100.0m`); arrows step through the 1-2-5 sequence to the next entry above/below | 1000 (`1.0k`) | always | yes (ELVISmx restarts the task at the new rate, verified) |
| Link update rates **[MYBENCH extension]** | toggle (Output card) | on: both Arbitrary channels share one Update Rate (ELVISmx behaviour); off: per channel | on | always | yes |
| Interpolation **[MYBENCH extension]** | combo | `Hold` (zero-order), `Linear` | Hold | always | yes |
| Trigger Source | combo | `Immediate` | Immediate | never (the myDAQ has no ARB trigger input) | - |
| Generation Mode | combo | `Run Continuously`, `Run Once` (verified) | Run Continuously | channel not generating | no |

| Indicator | Content |
|---|---|
| Channel readout (ELVISmx "Update Rate") | `OFF` when the channel is not generating; the update rate with 2 decimals and SI prefix (`1.00 kS/s`, `5.00 kS/s`, `200.00 kS/s`, verified) while generating; `Done` after a Run Once finished (MYBENCH; ELVISmx shows `OFF` again) |
| Error flag | ELVISmx shows a small red `Error` box at the top right of the display while an error dialog is pending (verified); MYBENCH uses its error banner |
| Waveform Points | number of samples of the loaded waveform (`0` when nothing is loaded) |
| Preview | the gain-applied, clipped waveform (4.2.5) |

With linked rates, changing Update Rate on one card changes it on both; loading a file into one
channel sets the shared rate (4.2.3).

#### 4.2.3 Loading waveforms

Open shows the file dialog (last waveform directory) with filters `Waveforms (*.mbw *.wav *.csv
*.txt)`, `MYBENCH waveform (*.mbw)`, `WAV audio (*.wav)`, `Text data (*.csv *.txt)`,
`All files (*)`.

| Format | Parsing | Samples (V) | Sample rate carried |
|---|---|---|---|
| `.mbw` (MYBENCH waveform, JSON, 4.3.10) | shared document parser | rendered with the shared renderer at the file's `sampleRate` (4.3.3) | `sampleRate` |
| `.wav` | RIFF/WAVE; `fmt ` format 1 (PCM: 8-bit unsigned, 16/24/32-bit signed) or 3 (IEEE float 32/64-bit), or 0xFFFE extensible with a PCM/float subformat; `data` chunk; other chunks skipped | first channel only; full scale (±1.0) maps to ±1.0 V | header rate |
| `.csv`, `.txt` | UTF-8. Lines starting with `#` are comments; `# sampleRate=<r>` or `# Sample rate: <r> S/s` (MYBENCH export header), case-insensitive, gives the rate. Fields split on tab, `;` or `,` (the first of these present in the first data line), else on whitespace. The first non-comment line is a header if any field is non-numeric. Values come from the first column, **except** when the header names it `Time...`: then the second column is used and, without a rate comment, the rate is `1 / (t1 - t0)`. Lines whose chosen field is not a number are skipped. | chosen column, volts | comment or time column, else none |

- Decimal separator `.` (a `,` inside a field is accepted when the delimiter is not a comma).
- 1 ... 10 000 000 samples; NaN/inf samples are replaced by 0 with a notice.
- On success: the name shows the file name, preview and Waveform Points update. If the file carries a
  sample rate, **Update Rate is set to it** (toast "Update Rate set to <r> S/s from <file>"); with
  linked rates and the other channel holding a waveform made for another rate, the toast adds "AO 0
  and AO 1 were made for different sample rates; both play at the Update Rate."
- On failure: toast/banner "Could not load <file name>: <reason>." (unsupported WAV encoding, no
  numeric data, not a MYBENCH waveform, too many samples (max 10 000 000), I/O error, render error).
  The channel keeps its previous waveform.
- Loading never starts the output (ELVISmx: loading does not tick Enabled; verified).
- ELVISmx (verified): the preview graph stays empty after loading/enabling; it shows the enabled
  channels' waveforms (after gain; AO 0 yellow, AO 1 white) only **while generating**, and is cleared
  on Stop; `Waveform Points` is `0` until Run. MYBENCH previews at once (improvement).
- ELVISmx ships four example `.wdt` files in the public Documents `National Instruments\Waveforms`
  folder (a 1 V sine and a 2 V square of 1000 points, a 2 V triangle and a sine+square of 2000
  points) and opens the file dialog there. **[MYBENCH, should]** ship equivalent example waveforms
  (our own `.mbw` documents) in the Library menu.
- `.wdt` (LabVIEW waveform files written by NI's editor) is not supported (open question 1).

#### 4.2.4 Playback

- `w = gain * waveform`. **Clipping**: the output range is ±10 V (AO) or ±2 V (audio); samples beyond
  it are clipped; a toast "AO <n> exceeds the ±<r> V output range after gain; <k> samples are
  clipped." appears when the channel starts or gain/waveform change (generation continues). The
  preview shows the clipped waveform.
- Output toggle on / run bar Run: the channel starts in the engine's player (3.5) at `P = 0`, in its
  Generation Mode:
  - Run Continuously: loops until stopped.
  - Run Once: plays the waveform once, then *Done*: holds its last sample (readout `Done`, output
    LED amber) until the channel is stopped, restarted or its mode changes. A Done channel does not
    keep the task alive on its own: when every channel is Off or Done, the task is destroyed and the
    held values are written statically (3.1); the panel returns to idle.
- Two channels may have different lengths and update rates: each loops (or plays once)
  independently. (ELVISmx, with one buffer for both channels, needs equal lengths: verified in
  ELVISmx UI, enabling a 1000-point and a 2000-point file and pressing Run gives a DAQmx error dialog
  "number of samples to write must be the same for every channel" and nothing is generated; the
  regenerate fallback 3.6 keeps that rule.)
- ELVISmx Run Once (verified with NI I/O Trace): a finite task of the waveform length plays once,
  then both AO 0 and AO 1 are written to **0 V** statically; the readout returns to `OFF`. MYBENCH
  default: hold the last sample (`Done`); **[MYBENCH option]** `Return to 0 V after Run Once`
  (ELVISmx behaviour).
- A waveform shorter than 2 samples is treated as a 2-sample repetition (DC).

#### 4.2.5 Preview (Display ring)

- `Time`: for every channel in Arbitrary mode with a waveform, one pass of `w` against
  `t = i / R`; channels whose output is off are drawn at 40 % brightness. Autoscale on: X spans the
  longest waveform, Y the data (a flat waveform gets a ±1 V window). With nothing loaded the axes show
  0...1 s and -1...1 V.
- `Frequency`: one-sided RMS amplitude spectrum of each such `w`
  (`dsp::complexSpectrum(w, R, WindowType::None)`, bin magnitude in Vrms, DC bin = mean), X
  `Frequency (Hz)` 0...R/2, Y `Amplitude (Vrms)`, linear.
- Function channels are drawn in the Time view only (2-3 periods); in the Frequency view they show a
  line at f (and harmonics for triangle/square, computed from one period) **[MYBENCH extension]**.
- Cursor tool, zoom and autoscale locks as in app.md; no cursor readouts (ELVISmx has none here).

#### 4.2.6 Live changes (Arbitrary)

| Change while generating | Effect |
|---|---|
| Gain | next block; position kept (no restart) |
| New waveform (Open, Recent, Library, editor "Use in channel") | Continuous: at the next block, playback restarts at `P = 0` of the new waveform; Once: playback restarts from the beginning |
| Update Rate | next block; position kept |
| Interpolation | next block |
| Output pair | task restart (3.1); playback restarts at `P = 0` |
| Generation Mode, Clear | disabled while generating |

#### 4.2.7 Stop and safe state

Channel stop (Output off, run bar Stop, mode change, panel close): the channel outputs 0 V inside
the stream; when no channel generates any more the task is destroyed and 0 V is written statically
to AO 0/AO 1 (3.1). A user stop always gives 0 V (a Done channel's held value is kept only until a
stop).

#### 4.2.8 Differences from ELVISmx (ARB)

- ELVISmx loads LabVIEW waveform files (`.wdt`); MYBENCH uses `.mbw`, WAV and CSV/TXT.
- The ELVIS II version offers PFI start triggers; on the myDAQ (and in MYBENCH) the trigger is always
  Immediate.
- The ELVISmx help lists a Clear action with no visible button; MYBENCH shows it on the card.
- Per-channel Update Rate (optional), independent lengths, glitch-free live changes, preview of
  function channels, recent files and library, Edit-in-place.
- ELVISmx's per-row Output Channel combos (`AO 0`/`AudioOutput Left`, `AO 1`/`AudioOutput Right`)
  become the single output-pair selector (the two rows could never mix kinds anyway).

### 4.3 Waveform Editor

Creates and edits arbitrary waveforms: a waveform is a sequence of **segments**; each segment has a
duration and zero or more **components** combined with arithmetic operators; a component is a
**Library** function (21 primitives with amplitude/offset/frequency/phase), a mathematical
**Expression** of x, or a freehand **Sketch**. Documents are saved as `.mbw` files and played by
Arbitrary channels. No device is involved: the editor has no Run/Stop.

#### 4.3.1 ELVISmx reference layout (informative, not binding)

ELVISmx window `Waveform Editor - Untitled`, about 603 x 485 px, **resizable**, menu bar
`File Edit Help`.

```
+-----------------------------------------------------------------------+
| File  Edit  Help                                                      |
+--------------+--------------------------------------------------------+
| [X lock][X auto][x.xx v] [zoom v][+]                              1   |
| [Y lock][Y auto][y.yy v] [pan]    |  graph: selected segment's    0.8 |
|                                   |  time span yellow (#FFFF99),  ... |
| [Plot Style ~~ ]                  |  other spans white, grid #CCCCCC, |
|      View                         |  trace blue #0000FF               |
| [Waveform   v]                    |                               -1  |
|     X Axis                        | 0.0  1.0  2.0 ... 9.0  10.0       |
| [Time       v]                    |                              sec  |
|   Sample Rate                     |                                   |
| [10000.0  ^v]                     |                                   |
|     Units                         |                                   |
| [Hz         v]                    |                                   |
+-------------------------+----------+----------------------------------+
| [◀][ 1 ][▶] Index       | Function |                                  |
| +---------------+ [New     ]| [+ v]   |  Name:     [                ] |
| | 1: (10 sec)   | [Segment ]|         |                                 |
| |               | [New     ]|( )Library|  Duration: [10.0000 ^v] sec    |
| |               | [Component]( )Expression                             |
| +---------------+           |( )Sketch |                                |
+-------------------------+----------+----------------------------------+
```

At launch: one segment `1: (10 sec)` selected (graph fully yellow, flat 0 V trace, X 0.0...10.0
`sec`, Y -1...1), Name empty, Duration `10.0000`, Function combo and component-type radios grayed,
Sample Rate `10000.0`, Units `Hz`, View `Waveform`, X Axis `Time`. Parameter panes for each
selection type: see 4.3.7.

#### 4.3.2 Document model

- **Document**: name, info (description text), sample rate `fs_doc` (S/s, default 10000.0), units
  (`Hz`, `kHz`, `MHz`: display units for frequencies, default `Hz`), list of >= 1 segments.
- **Segment**: name (may be empty), duration D (s, default 10.0000 for the first segment of a new
  document, 1.0000 for segments added with New Segment), gain (default 1; set by Scale Selection),
  list of components (may be empty).
- **Component**: operator (`+`, `-`, `x`, `/`, `FM`; the first component's operator is ignored and
  shown as `+`), type (Library, Expression, Sketch) and the type's parameters (4.3.4-4.3.6).
- New document = one empty 10 s segment, `fs_doc` 10000, units Hz, name `Untitled`.

#### 4.3.3 Rendering rules (shared renderer, normative)

The renderer is a UI-free C++ library in the gen instrument (`instruments/gen/`), used by the editor
preview, by Arbitrary channels and by tests: `parse(json) -> Document`, `serialize(Document)`,
`render(Document, fs) -> samples + warnings`.

1. Segment k gets `N_k = round(D_k * fs)` samples (at least 1); sample n has local time `t = n / fs`
   (from the segment start). The waveform is the concatenation of all segments. Total samples are
   limited to `Max Points` (Settings, default 500 000 as ELVISmx; hard cap 10 000 000): an edit that
   would exceed it (duration, sample rate, import) is refused with a status message "The waveform
   would have <N> samples (max <Max Points>)."
2. An empty segment is 0 V.
3. Components are evaluated left to right: `acc = c_1`, then `acc = acc op_k c_k`:
   - `+`, `-`, `x` (multiply), `/`: sample-wise; for `/`, samples with `|c_k| < 1e-12` give 0 and a
     warning "Division by zero in segment <k>: <n> samples set to 0".
   - `FM`: component k is evaluated on a warped time `τ(t) = sum over previous samples of
     (1 + acc(t_n)) / fs` (τ(0) = 0) instead of t, i.e. its instantaneous rate is scaled by
     `1 + acc` (acc in volts read as a fractional deviation: 0.5 = +50 %); then `acc = c_k(τ)`
     (the carrier replaces the accumulated signal). Negative `1 + acc` runs the carrier backwards.
4. The segment result is multiplied by the segment gain.
5. NaN/inf samples (expressions) become 0 with a warning "Expression produced invalid values in
   segment <k> (<n> samples set to 0)".
6. Rendering is deterministic (noise uses the seeded generator of 4.3.4), so a file renders to the
   same samples everywhere.
7. Arbitrary channels render at the document's `fs_doc` and play at their Update Rate (4.2).

#### 4.3.4 Library components (21 functions)

The function list, the order, the parameter names and the defaults below are **verified in the
ELVISmx UI** (each function selected in the `Function Library:` combo, defaults read from the
parameter pane; screens `ni-local/screens/explore/weditor/07-*`, `08-*`) and match the code study
(algorithms/gen.md 3.4). The formulas are MYBENCH definitions that reproduce the shapes observed with
the default parameters on a 10 s segment (NI's exact formulas are not public). All numerics have
4 decimals; Amplitude/Offset/Level/Start/End/Stop in V (range -1000 ... 1000, increment 0.1),
frequencies in the document Units (`Freq (Hz)`, `Start (Hz)`, `Stop (Hz)`; 0 ... 10 MHz stored in
Hz, increment 1 unit), Phase in degrees (-360 ... 360), percentages 0 ... 100.

Notation: `t` local time in the segment (or τ under FM), `D` segment duration, `v = t / D`
(0 ... 1 across the segment), for periodic functions `u = frac(f t + p/360)`.

| # | Function (combo name, id) | Parameters (default) | Shape (`y`) |
|---|---|---|---|
| 1 | DC Level (`dcLevel`) | Offset (1) | `Offset` |
| 2 | Sine (`sine`) | Amplitude (1), Offset (0), Freq (1), Phase (0) | `O + A sin(2πu)` |
| 3 | Square (`square`) | Amplitude (1), Offset (0), Freq (1), Phase (0), Duty Cycle % (50) | `O + A` if `u < d` else `O - A` |
| 4 | Triangle (`triangle`) | Amplitude (1), Offset (0), Freq (1), Phase (0) | `O + A tri(u)`, tri = `4u` (u < 1/4), `2 - 4u` (< 3/4), `4u - 4` |
| 5 | Sawtooth (`sawtooth`) | Amplitude (1), Offset (0), Freq (1), Phase (0) | `O + A saw(u)`, saw = `2u` (u < 1/2), `2u - 2`: rises from 0, jumps +1 -> -1 at half period (observed) |
| 6 | Uniform Noise (`uniformNoise`) | Amplitude (1), Offset (0) | `O + A U`, U uniform in [-1, 1) |
| 7 | Triangular Noise (`triangleNoise`) | Amplitude (1), Offset (0) | `O + A (U1 + U2 - 1)` with U1, U2 in [0, 1) |
| 8 | Gaussian Noise (`gaussianNoise`) | **Level** (1) = standard deviation, Offset (0) | `O + Level N(0,1)` |
| 9 | Rising Exp (`risingExponential`) | Amplitude (1), Offset (0), Freq (1), **Tau** (5) | per period `O + A (e^(Tau u) - 1) / (e^Tau - 1)` (0 -> A, steep at the end of each period; observed) |
| 10 | Falling Exp (`fallingExponential`) | Amplitude (1), Offset (0), Freq (1), Tau (5) | per period `O + A (e^(-Tau u) - e^(-Tau)) / (1 - e^(-Tau))` (A -> 0, steep at the start) |
| 11 | Hanning (`hanning`) | Amplitude (1), Offset (0) | **one bump over the whole segment**: `O + A (0.5 - 0.5 cos(2πv))` |
| 12 | Ramp (`ramp`) | **Start** (-1), **End** (1) | linear `Start + (End - Start) v` across the segment |
| 13 | Lin Sweep (`linearSweep`) | Amplitude (1), Offset (0), **Start (Hz)** (0.1), **Stop (Hz)** (1) | `O + A sin(2π (f1 t + (f2 - f1) t² / (2D)))` |
| 14 | Log Sweep (`logSweep`) | Amplitude (1), Offset (0), Start (Hz) (0.1), Stop (Hz) (1) | `O + A sin(2π f1 D / ln(r) (r^v - 1))`, `r = f2/f1` (r = 1: sine) |
| 15 | Sin x/x (`sinc`) | Amplitude (1), Offset (0), Freq (1) | `O + A sinc(2 f (t - D/2))`, `sinc(z) = sin(πz)/(πz)`: main lobe centred in the segment (observed), zero crossings every 1/(2f) |
| 16 | Multitone (`multitone`) | Amplitude (1), Offset (0), Start (Hz) (0.1), Stop (Hz) (1) | sum of equal-amplitude tones from Start to Stop (spacing 1/D, Schroeder phases), scaled so the peak equals A, + O |
| 17 | Trapezoid (`trapezoid`) | Amplitude (1), **Delay %** (20), **Rise %** (20), **Hold %** (20), **Fall %** (20) | over the whole segment: 0 until Delay, linear rise to A over Rise, A for Hold, linear fall to 0 over Fall, 0 afterwards (percentages of D, coerced so their sum ≤ 100) |
| 18 | Stairstep (`stairstep`) | **Start** (-1), **Stop** (1), **Steps** (10) | `Steps` equal levels from Start to Stop across the segment: `Start + (Stop - Start) floor(v Steps) / (Steps - 1)` |
| 19 | Circle (`circle`) | Amplitude (1), Offset (0), Freq (1) | one arch per period spanning -A ... +A: `O + A (2 sqrt(1 - (2u - 1)²) - 1)` (observed arches) |
| 20 | Haversine (`haversine`) | **Start** (0), **End** (1) | half cosine across the segment from Start to End: `(Start + End)/2 + (Start - End)/2 cos(πv)` |
| 21 | Pulse (`pulse`) | Amplitude (1), Delay % (20), Rise % (20), Hold % (20), Fall % (20) | like Trapezoid but with raised-cosine (smooth) rise and fall edges (observed) |

Seeds: the noise functions have no Seed parameter in ELVISmx (a new random sequence at every
render). **[MYBENCH extension]** a `Seed` field (default 1, 0 = random each render) is shown under
the noise parameters so files render identically everywhere.

Noise generator (normative for reproducibility): xoshiro256** seeded through SplitMix64 from
`seed`; uniform `U = (next() >> 11) * 2^-53` in [0, 1). Each noise component starts its own generator
from its seed at the start of its segment.

List names in the `Function Library:` combo, in this order (verified): DC Level, Sine, Square,
Triangle, Sawtooth, Uniform Noise, Triangular Noise, Gaussian Noise, Rising Exp, Falling Exp,
Hanning, Ramp, Lin Sweep, Log Sweep, Sin x/x, Multitone, Trapezoid, Stairstep, Circle, Haversine,
Pulse. Segment-list rows use the same names.

A new component is a Library component. ELVISmx (observed) gives it the **function last chosen in
the combo** (e.g. `Pulse` after Pulse was used) with that function's defaults; the first component
of a session is `Sine`. MYBENCH: the same rule. A frequency above `fs_doc / 2` shows the alias
warning next to the frequency field.

#### 4.3.5 Expression components

- Parameters: `Expression:` text after the fixed prefix `f(x)=` (default `sin(2*pi*x)`), group
  `X Range` with `From` (0.0000) and `To` (1.0000), `Cycles` (1.0000, > 0). All numerics 4 decimals.
- Evaluation: `x(t) = From + (To - From) * frac(Cycles * t / D)`; the interval [From, To) is mapped
  across the segment and repeated Cycles times; the component value is `gain * f(x)` (gain from
  Scale Selection, default 1).
- Grammar: numbers (`1`, `2.5`, `1e-3`), variable `x`, constants `pi`, `e`; binary `+ - * / ^`
  (`^` right-associative and binding tighter than unary minus: `-x^2 = -(x^2)`), unary `+ -`,
  parentheses; functions (radians) `abs atan ceil cos exp floor ln log(base 10) max(a,b) min(a,b)
  mod(a,b)` (remainder of a/b with the quotient rounded toward -infinity) `sin sqrt tan`, and
  **[MYBENCH extension]** `asin acos sinh cosh tanh sign`. Names are case-insensitive; no implicit
  multiplication. (The ELVISmx help writes the multiply operator as `x`, which clashes with the
  variable; MYBENCH uses `*`.)
- Errors are shown in red under the field with the 1-based character position in parentheses, e.g.
  `Unknown function 'sn' (1)`, `Missing ')' (12)`, `Unexpected end of expression (11)`. While the
  expression is invalid the component renders as 0 V and the list shows it with a warning icon.
- Invalid values (sqrt(-1), ln(0)...) become 0 with the rendering warning of 4.3.3.

#### 4.3.6 Sketch components

- Parameters: title `Freehand Sketch:`, `Name` (text), `Cycles` (1.000, 3 decimals, > 0),
  `Samples` S (1000, integer 2 ... 1 000 000), buttons `Import...` and `Draw` (toggle).
- Data: S values `v[0..S-1]` spanning one cycle (new sketch: all 0). Value at local time t:
  `q = frac(Cycles * t / D) * S`, `i = floor(q)`, linear interpolation between `v[i]` and `v[i+1]`
  (the last interval holds `v[S-1]`), times the component gain.
- Changing Samples resamples the existing values linearly to the new count.
- **Draw mode** (Draw pressed): View switches to Component, the plot shows one cycle of the sketch
  (X 0 ... D/Cycles s, Y range frozen at its current extent), and two tool buttons appear on the
  palette: **Freehand** (pencil) and **Line** (straight line).
  - Freehand: while the mouse button is held, each pointer position sets the sample under it; the
    samples between two successive pointer positions are filled by linear interpolation.
  - Line: clicking adds a vertex; the sketch is the polyline through the vertices (sorted by x, the
    first and last vertices pinned at x = 0 and x = end); dragging a vertex moves it (draggable
    handles); right-click on a vertex deletes it. The vertex list is stored so the line stays
    editable; switching to Freehand rasterizes it.
  - Each stroke or vertex edit is one undo step. Pressing Draw again leaves draw mode (View returns
    to its previous value).
  - Zoom (palette) works in draw mode for precise drawing, as in ELVISmx.
- `Import...` replaces the sketch's values with data from a file (wizards 4.3.9); Samples becomes the
  number of imported values, Cycles is unchanged.

#### 4.3.7 Controls

Left column (ELVISmx) / editor toolbar (MYBENCH):

| Control | Type | Values | Default | Notes |
|---|---|---|---|---|
| X autoscale / Y autoscale | buttons + lock switches | once / continuous | both locked on | a flat waveform gets a ±1 V window around its value |
| X format / Y format | menus (verified items) | `Format ▸` (Decimal, Scientific, SI, Hexadecimal, Octal, Binary), `Precision ▸` (0...6 digits), `Mapping Mode ▸` (Linear, Logarithmic), `Visible Scale` (check), `Visible Scale Label` (check), `Grid Color ▸` | Decimal, 1 (X) / 1 (Y) digits, Linear, scale visible | |
| Zoom | tool + menu | zoom rectangle, zoom X, zoom Y, undo zoom, zoom in about point, zoom out about point | | |
| Standard (+) | tool | normal pointer: click on the plot selects the segment under the pointer **[MYBENCH extension]**, drags cursors | active | |
| Pan | tool | drag to scroll | | |
| Plot Style | legend button + menu (verified items, in order) | `Plot Visible` (check), `Common Plots ▸` (6 presets: line, points, line + points, filled, bars, stepped bars), `Color ▸`, `Line Style ▸`, `Line Width ▸`, `Anti-Aliased` (check), `Bar Plots ▸` (none, 5 vertical and 5 horizontal bar styles), `Fill Base Line ▸` (None, Zero, -Infinity, +Infinity), `Interpolation ▸`, `Point Style ▸`, `X Scale ▸`, `Y Scale ▸`, `Export ▸` (`Export Data To Clipboard`; `Export Data To Excel` and `Export Data To DIAdem` grayed) | blue line, 1 px, linear, anti-aliased | MYBENCH: the same items except Bar Plots (**[MYBENCH, may]**); `Export ▸ Copy data` (clipboard, tab-separated) and `Export data...` (CSV) |
| View | combo | `Waveform` (whole waveform, absolute time), `Segment` (the selected component's or segment's segment only, local time 0...D), `Component` (the selected component alone, before combination, local time; falls back to Segment when no component is selected) | Waveform | |
| X Axis | combo | `Time`, `Frequency` | Time | Frequency: power spectrum of the viewed part, Hann window (`dsp::powerSpectrum` + `formatSpectrum` Db): Y `Power (dBVrms)` floored at -200 dB, X `Frequency (<units>)` |
| Sample Rate | SiField (samples per second **in the current Units**, 1 decimal) | 1.0 ... 1 000 000.0; arrows step through the 1-2-5 sequence | 10000.0 | re-renders; durations are kept, sample counts change |
| Units | combo | `Hz`, `kHz`, `MHz` | Hz | **unit system of the whole document** (verified in ELVISmx UI): Hz ↔ seconds (`sec`), kHz ↔ milliseconds (`ms`), MHz ↔ microseconds (`µs`). The numbers stay the same and are reinterpreted: Sample Rate 10000.0 means 10 kS/s in Hz, 10 MS/s in kHz; Duration 10.0000 means 10 s in Hz, 10 ms in kHz; frequency labels read `Freq (kHz)`; the time axis title changes to `ms`. Opening a waveform file may select kHz/MHz automatically so the numbers stay readable (observed). Storage (`.mbw`) keeps SI values plus the chosen unit |

Plot: white plot area, the selected segment's time span filled `#FFFF99` (the whole plot when the
only segment is selected, as at launch), grid `#CCCCCC`, trace `#0000FF`, black axis labels, X title
`sec` at the bottom right (Time view). Colors are configurable in Settings (4.3.8).

Bottom band:

| Control | Type | Behaviour |
|---|---|---|
| Index | ◀ button, integer SiField, ▶ button, label `Index` | 1 ... number of segments; selects that segment (its header row) |
| Segment list | list | rows `k: <name> (<duration> sec)` (name omitted when empty, duration in shortest form: `10 sec`, `0.5 sec`) followed by the segment's components indented (`    Sine`, `    + Square`, `    x Expr: sin(2*pi*x)`, `    FM Sketch 1`; the first component has no operator prefix). Single selection: a header row selects the segment, a component row selects that component. Drag and drop reorders components within/between segments and segments **[MYBENCH extension]**. |
| New Segment | button | inserts an empty segment (1.0000 s, no name) after the current segment and selects it |
| New Component | button | inserts a new component (Library Sine) right after the selected component, or at the end of the selected segment's list when a header is selected; selects it |
| Function | combo `+`, `-`, `x`, `/`, `FM` | operator of the selected component; enabled only when a component other than the first of its segment is selected |
| Library / Expression / Sketch | radio buttons | type of the selected component; enabled only when a component is selected; changing type replaces the component's parameters with the new type's defaults (operator kept; undoable) |

Parameter pane (right), depending on the selection:
- Segment: `Name:` text field, `Duration:` SiField (sec, 4 decimals, 0.0001 ... 100 000 s, increment
  0.1) with unit `sec`, and **[MYBENCH extension]** `Gain:` (shown when not 1).
- Library: `Function Library:` combo, Amplitude, Offset, Freq (<units>), Phase, extras (4.3.4).
- Expression: `Expression:` / `f(x)=` field, X Range From/To, Cycles, error text (4.3.5).
- Sketch: `Freehand Sketch:` Name, Cycles, Samples, Import..., Draw (4.3.6).
- Select All: the pane shows the whole waveform's duration and sample count (read-only).

Every edit re-renders the preview immediately (debounced to 50 ms for text/drag input; rendering of
large documents runs in a worker and shows a progress indicator beyond 200 ms).

Cursors (Settings > Show Cursors): a cursor on the plot, dragged horizontally, labelled with
`<x> sec, <y> V` of the point it touches (observed: `0 sec, 0 V` at the left edge), snapping to
samples; **[MYBENCH extension]** a second cursor and a readout `dX`, `dY`, `1/dX` in the status line.

Draw mode (verified): pressing `Draw` replaces the graph palette with a `Mouse Position` indicator
(`x: <time>`, `y: <voltage>`) and the two tools (pencil = freehand, line), hides View, keeps
X Axis / Sample Rate / Units, and grays the segment list, Index, New Segment, New Component and the
component type radios until Draw is released. The Y range stays fixed while drawing.

No right-click menus exist on the editor graph or controls (verified); everything is in the menus,
the palette and the Plot Style legend.

#### 4.3.8 Commands (ELVISmx File / Edit / Help menus)

Verified menu contents (ELVISmx): **File** `New` (Ctrl+N), `Open` (Ctrl+O), `Close` (Ctrl+W),
`Save` (Ctrl+S), `Save As...`, `Exit` (Ctrl+Q); **Edit** `Undo` (Ctrl+Z), `Cut` (Ctrl+X), `Copy`
(Ctrl+C), `Paste` (Ctrl+V), `Clear`, `Select All` (Ctrl+A), `Import from File...`, `Scale
Selection...`, `Settings...`, `Info...` (Ctrl+I); **Help** `Show Context Help` (Ctrl+H: floating
context-help window describing the control under the pointer), `Online Reference...`, `About Waveform
Editor...`. ELVISmx closed a modified document without asking (observed); MYBENCH asks.

File formats (verified): `Save`/`Save As` offer only `*.wdt` - the **rendered** waveform (e.g. a
100 000-sample document gave a 800 631-byte file); the segment/component structure is **not** kept:
re-opening it (Open dialog pattern `*.wfm;*.txt;*.wdt;*.bin;...`) shows the `Open Waveform File
Wizard` (preview by sample index, OK/Cancel) and creates one segment named after the file holding
one Sketch component with all the samples. MYBENCH saves `.mbw` (structure kept) and exports rendered
data to CSV/WAV (below); LabVIEW `.wdt` datalogs are not read or written (open question 1).

File:
- `New`: new Untitled document (unsaved-changes prompt: Save / Discard / Cancel).
- `Open...`: `.mbw` documents (and WAV/CSV/TXT/BIN, which open as a new document with one segment
  holding a Sketch component of the data, duration = samples / rate).
- `Save` (Ctrl/⌘S), `Save As...` (`.mbw`). The title/tab shows the document name and a modified
  marker (`Waveform Editor - Untitled*`).
- `Export...` **[MYBENCH extension]**: rendered waveform to `.csv` (header `# sampleRate=<fs>`, one
  value per line) or `.wav` (float32 mono at `fs_doc`, full scale 1 V; values beyond ±1 V are
  clipped with a warning).
- `Use in channel` / "Send to AO 0 / AO 1" **[MYBENCH extension]**: loads the rendered document into a
  channel's Arbitrary waveform (the editor opened from a channel's Edit button does this on every
  change when "Live" is on, else on click); Update Rate follows `fs_doc` as for a file load.
- `Close`: closes the editor view (unsaved-changes prompt).

Edit:
- `Undo` / `Redo` **[MYBENCH extension: full multi-level undo/redo of every edit, QUndoStack in the
  controller]**. (ELVISmx's Undo only deletes the last added segment or component.)
- `Cut`, `Copy`, `Paste`: the selected component or segment (clipboard MIME
  `application/x-mybench-waveform+json`, plus the JSON as text). Paste inserts a component after the
  selected component (or at the end of the selected segment), a segment after the current segment.
- `Clear` (Delete): deletes the selected component or segment; the document always keeps at least
  one segment (clearing the only segment removes its components).
- `Select All`: selects all segments (Copy copies the whole waveform, Scale Selection applies to all,
  Clear resets to one empty 10 s segment after confirmation).
- `Import from File...`: inserts after the current segment a new segment holding a Sketch component
  of the imported data (wizards 4.3.9); duration = samples / (file rate or `fs_doc`).
- `Scale Selection...`: dialog `Scale Selection` with two groups, `Time Axis` `Multiply by:` and
  `Voltage axis` `Multiply by:` (both 1.000, 3 decimals, verified), OK / Cancel. MYBENCH labels:
  `Time scale factor` (default 1.0, > 0) and `Amplitude scale factor` (default 1.0). Segment(s): duration x time factor and every Library frequency (Freq, Stop
  Freq) / time factor, so the content is stretched without changing its shape; gain x amplitude
  factor. Component: Library - Freq / time factor, A and O x amplitude factor; Expression - Cycles /
  time factor, gain x amplitude factor; Sketch - Cycles / time factor, values x amplitude factor.
- `Info...`: dialog `Information`: the document's file path (read-only, empty for Untitled) and a
  multi-line `Description:` (verified), OK / Cancel.
- `Settings...`: dialog `Waveform Editor Settings` (verified): `Plot Color` (blue), `Selection
  Color` (light yellow), `Grid Color` (light gray), `Cursor Color` (black), `Max Points` (500000:
  the largest number of samples a document may render), `Animate Graphs` (off: animates the plot
  when the selection changes), `Show Cursors` (off); OK / Cancel. Saved as user preferences.
  MYBENCH: same items plus plot background; `Max Points` default 500 000, adjustable up to
  10 000 000 (4.3.3).

Help: `Waveform Editor Help` (help drawer, `docs/help/gen.md` editor section), `About MYBENCH`.

#### 4.3.9 Import wizards

- **Text File Wizard** (`.txt`, `.csv`, other text): `Delimiter` checkboxes `Tab` (default on),
  `Semicolon`, `Comma`, `Space`, `Other` + one-character field; `Column` (1-based, default 1);
  `Start Line` (1-based, default the first line whose chosen column is numeric); `No. Samples`
  (default all remaining numeric values); a live `Waveform Preview` plot (sample index on X); `OK`,
  `Cancel`. Lines whose chosen field is not numeric are skipped (not counted).
- **Waveform File Wizard** (`.mbw`, `.wav`): `Waveform File Preview` plot, **[MYBENCH extension]**
  channel selector for multi-channel WAV, `OK`, `Cancel`. An `.mbw` is rendered at its own rate.
- **Binary files** (`.bin`): no header, 16-bit signed little-endian integers, ±5 V full scale
  (`v = raw * 5 / 32768`); a small dialog shows these assumptions and a preview before OK.

#### 4.3.10 `.mbw` file format (normative)

UTF-8 JSON, one object. Frequencies in Hz, times in s, voltages in V, phases in degrees, percentages
0...100. Readers ignore unknown keys; a `version` greater than 1 is refused ("This waveform was made
by a newer MYBENCH").

```json
{
  "format": "mybench-waveform",
  "version": 1,
  "name": "Test burst",
  "info": "free description text",
  "sampleRate": 10000.0,
  "units": "Hz",
  "segments": [
    {
      "name": "",
      "duration": 10.0,
      "gain": 1.0,
      "components": [
        { "type": "library", "op": "+", "function": "sine",
          "amplitude": 1.0, "offset": 0.0, "frequency": 1.0, "phase": 0.0,
          "params": { } },
        { "type": "expression", "op": "*", "expression": "sin(2*pi*x)",
          "from": 0.0, "to": 1.0, "cycles": 1.0, "gain": 1.0 },
        { "type": "sketch", "op": "fm", "name": "Sketch 1", "cycles": 1.0, "gain": 1.0,
          "samples": [0.0, 0.25, 0.5],
          "vertices": [[0.0, 0.0], [1.0, 0.5]] }
      ]
    }
  ]
}
```

- `op`: `"+"`, `"-"`, `"*"`, `"/"`, `"fm"`. `function`: the ids of 4.3.4. `params` keys:
  `dutyCycle`, `seed`, `rate`, `stopFrequency`, `tones`, `zeroCrossings`, `rise`, `high`, `fall`,
  `steps` (only those the function uses; missing keys take the defaults).
- Sketch values: `samples` (JSON numbers) or, for more than 10 000 values, `samplesBase64`
  (little-endian float32, base64); readers accept both. `vertices` (optional): Line-mode vertices as
  `[position 0...1 within the cycle, value]`.
- The file does not contain rendered samples; consumers render with the shared renderer.

#### 4.3.11 Differences from ELVISmx (Waveform Editor)

- File format `.mbw` (JSON) instead of LabVIEW files; formulas of the library functions are MYBENCH
  definitions; `*` is the multiply operator in expressions.
- Full undo/redo, drag-and-drop reordering, segment gain, export, Use in channel, click-to-select on
  the plot, configurable colors persisted as preferences.
- The "waveform animation" feature mentioned in the ELVISmx help is not replicated.

## 5. Run / Stop semantics (panel)

### 5.1 Channel states

`Off` (Mode Off) · `Ready` (Mode Function/Arbitrary, not generating) · `Running` · `Sweeping`
(Function) · `Done` (Arbitrary Run Once finished, holding its last sample) · `Paused` (arbiter, 6).
The channel LED: off (Off/Ready), green (Running/Sweeping), amber (Done/Paused).

### 5.2 Actions

- **Channel Output toggle**: Ready -> Running (Function: continuous; Arbitrary: its Generation
  Mode). Running/Sweeping/Done -> Ready (stop, 0 V).
- **Sweep** (Function card): Ready -> Sweeping (4.1.6).
- **Run bar Run**: every Ready channel whose mode is not Off starts as with its Output toggle;
  channels started by one Run press start on the same task sample.
- **Run bar Single**: every Arbitrary channel with a waveform starts in Run Once (whatever its
  Generation Mode); Function channels are not started. Disabled when no Arbitrary channel has a
  waveform.
- **Run bar Stop** (and `Stop all` in the top bar): every channel -> Ready; the task is destroyed,
  outputs 0 V.
- **Mode change** while generating: stops the channel (0 V), switches mode; the channel stays Ready.
- Panel close / device change: all channels stop (safe state).
- Validation at start: Arbitrary without waveform -> the channel stays Ready, inline card error and
  toast "AO <n>: no waveform loaded". Function channels are always valid (coercion).
- The panel is "running" (nav rail indicator, dashboard) while any channel is Running/Sweeping.

## 6. Resources and interactions

- The generator holds resource `AO` while its task exists (any channel Running/Sweeping). One AO
  task drives AO 0, AO 1 and the audio outputs, so the Bode Analyzer (which uses AO 0) cannot run at
  the same time.
- Per UI.md, when another instrument needs AO (Bode Run) the **ResourceArbiter pauses the
  generator**: the engine stops and destroys the task (no static write: the new holder owns the
  outputs), channels become `Paused` (sweeps are aborted), and a banner shows "Generator paused -
  AO in use by Bode" with a `Resume` button. Resume (or the other instrument stopping, if
  "auto-resume" is set in app.md) re-acquires AO (pausing the other holder if it still runs) and
  restarts the paused channels: Function channels with φ = 0, Arbitrary channels from `P = 0`.
- Conversely, starting the generator while Bode holds AO pauses Bode (app.md arbiter rules).
- Works together with the Oscilloscope/Spectrum Analyzer (AI; e.g. AO 0 wired to AI 0+, AGND to
  AI 0-, or AUDIO OUT to AUDIO IN), the DMM (AC V can measure the output) and Digital I/O.
- The top bar resource chip shows `AO: Generator`.

## 7. Errors and notifications

All messages are non-blocking toasts/banners (app.md); none is modal.

| Situation | Notification |
|---|---|
| AO taken by another instrument | banner "Generator paused - AO in use by <instrument>" + Resume (6) |
| Underflow at 200 kS/s | banner "Generator output was interrupted (computer too slow); continuing at 100 kS/s." (3.3) |
| Underflow at 100 kS/s | error banner "The output ran out of data because the computer could not keep up. The generator was stopped." |
| Arbitrary channel without waveform | toast "AO <n>: no waveform loaded" + inline card error |
| load failure | toast "Could not load <file>: <reason>." |
| clipping | toast (4.2.4), preview shows the clipped waveform |
| file rate adopted | toast (4.2.3) |
| incompatible lengths (regenerate fallback only) | toast "The two waveforms have incompatible lengths (<N0> and <N1> samples)..." |
| editor render warnings / limits | editor status line (4.3.3), not toasts |
| device errors (`NotConnected`, `UsbError`, ...) | error banner with the README message text; all channels stop |

## 8. Export / logging

- No Log/Print in ELVISmx for these instruments. Plot context menus (app.md) export the preview
  image/data.
- Enhancement exports: channel waveform (2), editor Export (4.3.8).
- Settings (modes, all parameters, loaded file paths, editor documents that are unsaved are stored
  inline) persist in the session file (`.mybench`) like every instrument.

## 9. Device API mapping

| Feature | Calls |
|---|---|
| task rate | `Device::maxAoRate()`, `Device::coerceAoRate()`; `AoTask::sampleRate()` for synthesis |
| streaming task | `createAoTask(AoConfig{ {AO0,10},{AO1,10} or {AudioOutLeft,2},{AudioOutRight,2}; fs; Continuous; regenerate = false })`, pre-fill `write(block)` before `start()`, then `write(block, 1000)` in the worker loop, `spaceAvailable()` as a lead hint, `stop()`, destroy |
| underflow | `DeviceError::Underflow` from `write()` -> fallback 3.3 |
| safe state | `Device::aoWriteStatic(AoChannel::AO0/AO1, level)` after the task is destroyed |
| regenerate fallback | `dsp::planPeriodicBuffer`, `dsp::generatePeriodic`, `AoConfig{..., regenerate = true}`, `stop()`/`write()`/`start()` |
| shapes | `dsp::waveformShape` |
| spectra (preview, editor) | `dsp::complexSpectrum`, `dsp::powerSpectrum`, `dsp::formatSpectrum` |
| numbers | `dsp::formatEng`, `dsp::parseEng` (SiField) |
| channel names | `mydaq::toString(AoChannel)`: `AO 0`, `AO 1`, `AudioOutput Left`, `AudioOutput Right` |
| waveform documents | gen renderer library (`parse`, `serialize`, `render`) |

No AI/DMM/DIO calls.

## 10. Checks against the simulator

Sim wiring: AO 0 -> AI 0 directly and -> AI 1 through a 1 kHz RC low-pass; AudioOutput L/R ->
AudioInput L/R (AC coupled, 5 Hz high-pass); AO 1 not connected. Use the Oscilloscope on AI 0/AI 1.

Function mode
- Defaults, Run: AO 0 readout `100.00 Hz`; the Scope on AI 0 shows a 1 Vpp 100 Hz sine; AI 1 almost
  the same (1 kHz low-pass).
- Drag Frequency from 100 Hz to 5 kHz while running: the Scope trace changes continuously with no
  flat gaps or jumps (phase-continuous), about 0.1 s behind the knob.
- Square, 10 kHz, duty 30 %: average duty 30 % on the Scope (single periods at 20 samples/period show
  30 % exactly).
- Amplitude 10 Vpp, offset 5 V: output spans 0 ... 10 V.
- Sweep 100 Hz -> 1 kHz, step 100 Hz, 1000 ms: ten steps of exactly 1.000 s (measure with the Scope
  at 200 ms/div), then `OFF` and 0 V.

Arbitrary mode / editor
- Editor: Library Sine, Freq 1000 Hz, segment 0.01 s, Sample Rate 100000 -> Use in channel AO 0
  (or save and Open on the AO 0 card, Mode Arbitrary): Waveform Points `1000`, Update Rate `100.0k`;
  Output on: readout `100.0 kS/s`, the Scope on AI 0 shows a 1 kHz 2 Vpp sine (AO 1 is not wired in
  the sim, so loopback checks use AO 0).
- Gain 2.00 while running: 4 Vpp, no restart glitch; Gain 20 is coerced to 10.00 and a 1 V waveform
  clips at 10 V with the clipping toast.
- Output pair = AudioOutput: cards become `AudioOutput Left/Right`; the Scope on AudioInput Left shows
  the waveform (DC offsets decay: AC coupling).
- AO 0 Function 1 kHz + AO 1 Arbitrary running together; stop AO 1 alone: AO 0 continues unchanged.
- Run Once of a 0 -> 1 V ramp (Ramp function, one 1 s segment): the output rises for 1 s, then holds
  1 V (`Done`, amber LED); Stop -> 0 V.
- Editor expression `sin(2*pi*x) + 0.3*sin(6*pi*x)`, X Range 0...1, Cycles 5 over a 1 s segment: 5
  periods; typing `sin(2*pi*x` shows `Missing ')' (11)` in red.
- Editor: Linear Sweep 1 -> 20 Hz over 10 s at 1000 S/s: frequency visibly increases; X Axis =
  Frequency shows energy spread from 1 to 20 Hz.
- Save, reopen: identical render (bitwise, including Uniform Noise with the same seed).

Arbiter
- Generator running, start Bode: banner "Generator paused - AO in use by Bode"; after Bode, Resume:
  channels restart.

## 11. Open questions / assumptions

1. **`.wdt` files** (LabVIEW waveform datalog; the only format the ELVISmx ARB loads and the only
   format its Waveform Editor saves - verified) are not supported; supporting them needs a
   clean-room study of sample files (four example files exist in the ELVISmx install, kept in
   `ni-local/` only). CSV/WAV cover data exchange.
2. **Streaming at 200 kS/s** for two channels over USB is assumed feasible (single-channel continuous
   AO was verified; two-channel AO is still an open hardware question in HANDOFF.md). The 100 kS/s
   fallback and the regenerate path (3.6) cover failures.
3. **Frequency readback** is computed (NCO), not measured - as ELVISmx on a myDAQ, which displays the
   requested/stepped frequency (verified).
4. **Sweep end** - resolved: single sweep, Stop included when it falls on a step, then `OFF` and
   0 V (verified in ELVISmx UI and trace).
5. **Step Interval** - resolved: 1 ... 300 000 ms accepted (verified); ELVISmx steps are multiples of
   250 ms in practice, MYBENCH steps exactly.
6. **Increments**: ELVISmx code gives Amplitude 0.01, Offset 0.02, Frequency 10, Duty 1 (0-100 %,
   verified range); MYBENCH keeps SiField per-digit tuning plus these arrow increments.
7. **Safe state** - resolved: ELVISmx FGEN Stop and sweep end drive 0 V; ARB Stop, errors and the
   end of Run Once write 0 V to AO 0 and AO 1 (verified with NI I/O Trace). MYBENCH holds the last
   sample after Run Once by default (option for ELVISmx behaviour).
8. **Update Rate** shared by both channels in ELVISmx (verified: one task); MYBENCH links rates by
   default. ELVISmx left the Update Rate at `1.0k` when the example files were loaded (their own rate is
   not known); MYBENCH adopts the
   file's rate when it carries one (toast).
9. **Library formulas** - parameter sets, defaults and order verified (4.3.4); exact NI formulas
   unknown, MYBENCH formulas reproduce the observed shapes.
10. **FM operator** semantics are ours; ELVISmx lists `+`, `--`, `x`, `/`, `FM` (verified).
11. **New Segment duration** 1 s (first segment 10 s) - from the code study; not re-checked in the UI.
12. **Editor Frequency view** shows a Hann-windowed power spectrum in dBVrms; the ELVISmx ARB
   Frequency display is an FFT power spectrum in dB (`Magnitude (dB)`, verified).
13. **WAV scaling**: full scale = 1 V (gain 2 fills the audio range, 10 the AO range).
14. **Default modes** (AO 0 Function, AO 1 Off) and **Single** semantics (Arbitrary channels only) are
    MYBENCH panel decisions.
15. **Resume after preemption** restarts Function phases and Arbitrary playback from the beginning;
    sweeps are not resumed.
16. **Controls during a sweep**: ELVISmx grays everything but Stop/Help; MYBENCH keeps shape,
    amplitude, offset and duty live (extension).

## 12. Verified DAQmx usage (ELVISmx, observed with NI I/O Trace)

Simulated NI myDAQ, ELVISmx 25.8; in our own words.

Function Generator
- Every edit of a numeric while **idle** creates an AO task on the routed channel and clears it
  again (about one per Enter; used to validate against the device) - no output change.
- Run: one AO voltage task on `ao0` or `ao1` (±10 V, default terminal configuration); the maximum
  sample clock rate is read (200 kS/s); the buffer is **one period**; `use only onboard memory` is
  set when the period has fewer than 200 samples; timing is taken from the waveform (continuous,
  regeneration); one write with auto-start. Observed: 1 kHz -> 200 samples at 5 µs; 1234.5 Hz ->
  162 samples at 5 µs (onboard memory only).
- Any live change (frequency, shape, amplitude, offset, duty, route) clears the task and creates a
  new one with the new buffer (not phase-continuous).
- Sweep: regeneration not allowed, continuous, 100 kS/s (frequencies below 5 kHz), writes of 25 000
  samples (0.25 s) with a 10 s timeout; 100 -> 1000 Hz in 300 Hz steps of 1 s took 20 writes.
- Stop / end of sweep: a task on the channel at 2 kS/s writes 10 000 zero samples, then is
  cleared (output 0 V).

Arbitrary Waveform Generator
- While idle, every change of Enabled, file or Update Rate re-creates (without starting) an AO task
  with the enabled channels (`ao0`, `ao1` or `ao0:1`), **RSE**, ±10 V, sample clock at the Update
  Rate, continuous (Run Continuously) or finite (Run Once) with the waveform length.
- Run: the gain-scaled waveform(s) are written in one multi-channel write, the task is started and
  the actual rate is read back for the readout; a gain change rewrites the buffer; an Update Rate
  change recreates the task.
- Stop, the end of Run Once and errors: the task is cleared and AO 0 and AO 1 each get a static
  single-sample write of 0 V.
