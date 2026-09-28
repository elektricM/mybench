# Oscilloscope (`scope`)

Read [app.md](app.md) first (application shell, common conventions: run bar, inspector, SiField,
knobs, plots, cursors, export, notifications, resource arbiter) and [README.md](README.md) (hardware
constraints). This file is the complete **functional** specification: every control, value list,
default, rule and algorithm in sections 3-9 must exist in MYBENCH. Section 2 (ELVISmx reference
layout) is informative only.

## 1. Purpose

A two-channel oscilloscope on the myDAQ analog inputs (AI 0, AI 1) or the audio inputs (AudioInput
Left/Right). It acquires and displays one or two traces, continuously or once, with software edge
triggering (level, slope, horizontal position), per-channel volts/div and vertical position, a
timebase knob, automatic RMS / frequency / peak-to-peak measurements, two cursors, autoscale,
image export and data logging. Replaces the ELVISmx Oscilloscope.

Reference screenshot: `ni-local/screens/Scope.png` (ELVISmx Basic Settings tab at launch).

## MYBENCH design notes

How the functions map to the MYBENCH panel (UI.md / app.md) and which enhancements are added.
Enhancements must not change the default behaviour of sections 3-9.

- **Display area**: the scope graticule (10 x 10 divisions, no axis numbers by default; theme scope
  colors: green grid, bright-green center axes with minor ticks), traces CH 0 green / CH 1 cyan,
  the measurement rows (per channel, in the channel color), the cursor readout row, the sample-rate
  readout, the Timeout LED (also mirrored in the run bar status). Per-channel scale badges in the
  corners (`CH0 1 V/div`, `5 ms/div`, `200 kS/s`) replace ELVISmx's knob-reading habit.
- **Inspector sections** (cards with ELVISmx names): *Channel 0 Settings* (green accent),
  *Channel 1 Settings* (cyan accent), *Timebase*, *Trigger*, *Cursors Settings*, *Display
  Measurements*, *Graph Properties* (trace style, cursors style, measurement selection: the content
  of ELVISmx's Graph Properties dialog, as a collapsible card or a popover), *Advanced* (the
  Advanced Settings tab content of 3.5). Knobs are kept for Volts/Div, Vertical Position and
  Time/Div, each paired with its combo/SiField. `Autoscale` is a button in the run bar (and in the
  command palette).
- **Run bar**: Run/Stop toggle (Run Continuously), Single (Run Once), Autoscale, status text
  (`Running`, `Waiting for trigger`, `Stopped`) and the Timeout LED.
- **Dashboard tile**: miniature trace of both channels, CH 0 frequency and Vp-p, run state.
- **Direct manipulation** (UI.md enhancement): drag the trigger-level marker (right edge) vertically
  to set Level (V); drag the trigger-position marker (top edge) horizontally to set Horizontal
  Position (%); drag a channel's zero marker (left edge) to set its Vertical Position; mouse wheel
  over the graph changes Time/Div, Shift+wheel changes the hovered channel's Volts/Div. Markers:
  channel zero triangles in channel color, trigger markers in the trigger source's color.
- **Enhancements** (optional per release, all off by default):
  - *More measurements* (selectable per channel in Graph Properties): Mean, Min, Max, Amplitude
    (high - low), Period, Duty cycle (`dsp::dutyCycle`), Rise time and Fall time (10-90 %), Positive
    and negative pulse width; formatted like 3.6.
  - *XY mode*: CH 0 on X, CH 1 on Y (both channels required), same Volts/Div and positions.
  - *Math trace* (amber): CH0 + CH1, CH0 - CH1, CH0 x CH1, -CH0, -CH1, with its own Volts/Div and
    position; not available to the trigger.
  - *Persistence*: overlay of the last N records (N = 2..64 or infinite) with fading.
  - *Reference traces* (violet): freeze the current trace as REF A/B for comparison.
  - Sub-sample `sin(x)/x` interpolation when fewer than ~50 samples are on screen.

## 2. ELVISmx reference layout (informative, not binding)

- ELVISmx window `Oscilloscope`, about 864 x 595 px, fixed.
- Two columns: left ~55 % = display area + a strip of three small groups; right ~45 % = tab widget
  (`Basic Settings`, `Advanced Settings`) above the Instrument Control group. The right column has a
  white background (tab page color `colors::tabPage`); the bottom-left strip is `colors::panel`.

```
+--------------------------------------------+---------------------------------------------+
| [badge]              Sample Rate: 200 kS/s [tv]| [Basic Settings][Advanced Settings]         |
|  +--------------------------------------+  | +Channel 0 Settings [■]-+ +Channel 1 Settings [■]+
|  |            10 x 10 div grid          |  | | Source [AI 0      v]  | | Source [AI 1      v] |
|  |                                      |  | | [x] Enabled           | | [ ] Enabled          |
|  |                                      |  | | Probe [1x v] Coupling [DC v]| (same)          |
|  |                                      |  | | Scale      Vertical   | |                      |
|  |                                      |  | | Volts/Div  Position(Div)                     |
|  |                                      |  | |  (knob)     (knob)    | |                      |
|  +--------------------------------------+  | | [1 V v]     [0   ^v]  | |                      |
|                               (o) Timeout  | +-----------------------+ +----------------------+
|  CH0 Meas:  RMS: ?     Freq: ?   Vp-p: 0.00 V| +Timebase-+ +Trigger-----------------------+ |
|  (CH1 Meas line, cursor line)              | | Time/Div| | Type [Immediate v]  Slope [/]  | |
+--------------------------------------------+ |  (knob) | | Source [TRIG v]     Level (V)  | |
| Cursors Settings | Display Measurements | Graph Controls | [5 ms v]| | [===|====]  [0   ^v]        | |
| [ ]Cursors On C1[CH 0v]| [x]CH 0 [x]CH1 | [Graph      |  +---------+ | Horizontal Position (%) [50] | |
|               C2[CH 1v]|                | Properties] |              +------------------------------+ |
+--------------------------------------------+ +Instrument Control-------------------------+ |
                                             | Device [myDAQ v]   Acquisition Mode [Run Continuously v]
                                             | [Autoscale] Run [->] Stop [■] | Print | Log | Help |
                                             +---------------------------------------------+
```

### 2.1 Display area (black, left, ~480 x 490 px)

- Badge top-left; Toggle View button top-right.
- `Sample Rate:` label in yellow (`colors::displayYellow`) at the top right of the display, followed
  by the actual per-channel rate (`formatEng(rate, "S/s", 4)`, e.g. `200 kS/s`, `20 kS/s`); empty
  before the first acquisition.
- **Graph** ~460 x 340 px: black plot area, thin gray frame, 10 horizontal x 10 vertical divisions.
  Grid lines `colors::scopeGrid` (#009900). The center horizontal and vertical axes are drawn in
  `colors::scopeAxis` (#64FF00) with 5 minor ticks per division (short dashes across the axis).
  **No axis numbers** (every channel has its own scale). Traces: CH 0 `colors::trace0` (green
  #64FF00), CH 1 `colors::trace1` (cyan #11CBFF), 1 px, linear interpolation (Graph Properties can
  change this).
- **Timeout LED** under the graph, right-aligned: round LED (~14 px) with label `Timeout` to its
  right in green. Off = dark green (`colors::ledGreenOff`), on = bright green (`colors::ledGreenOn`).
- **Measurement lines** under the LED, left-aligned, 12 px text:
  - `CH0 Meas:` then `RMS: <v>`, `Freq: <v>`, `Vp-p: <v>` at fixed tab positions, in CH 0 color.
  - Second line `CH1 Meas: ...` in CH 1 color, when CH 1 is shown.
  - Cursor line (when Cursors On): `C1: <v>   C2: <v>   dT: <v>` (C1 text in C1's color, C2 text in
    C2's color, dT in white).
  - Hidden lines leave no gap (the lines stack from the top).
- (MYBENCH markers and status text: see MYBENCH design notes; rejected live settings are reported
  with a transient notification, app.md.)

### 2.2 Bottom-left strip (gray, ~60 px high), three groups side by side

- **Cursors Settings**: `Cursors On` checkbox; to its right two stacked rows `C1 [CH 0 v]` and
  `C2 [CH 1 v]` (combos disabled while Cursors On is off).
- **Display Measurements**: checkboxes `CH 0` and `CH 1`.
- **Graph Controls**: push button `Graph Properties` (two-line text).

### 2.3 Basic Settings tab

Two channel groups side by side on the first row; `Timebase` (narrow) and `Trigger` (wide) groups
on the second row.

- **Channel 0 Settings** / **Channel 1 Settings** group. The title is followed by a small filled
  square (~14 px) in the channel's trace color (green / cyan). Contents, top to bottom:
  - `Source` label, combo full width.
  - `Enabled` checkbox.
  - `Probe` combo (left) and `Coupling` combo (right), labels above, both always disabled.
  - Two knobs side by side with two-line labels above: `Scale` / `Volts/Div` (ring knob) and
    `Vertical` / `Position (Div)` (continuous knob). Below each knob: the Volts/Div ring (combo) and
    the Vertical Position numeric.
- **Timebase** group: label `Time/Div`, a larger ring knob, the Time/Div ring (combo) below it.
- **Trigger** group, two columns:
  - Left: `Type` combo; `Source` combo; `Horizontal Position (%)` label and a horizontal slider
    (blue thumb) under it.
  - Right: `Slope` toggle button (icon `slope-rising` / `slope-falling`, ~26 x 20 px); `Level (V)`
    numeric; the Horizontal Position numeric aligned with the slider.

### 2.4 Advanced Settings tab

See 3.5 (MYBENCH content; ELVISmx's version of this tab is for ELVIS II+ hardware).

### 2.5 Instrument Control group (right column, bottom)

`Device` combo (left), `Acquisition Mode` combo (right). Button row: `Autoscale` (text push button,
~65 x 28 px, no label above), `Run`, `Stop`, separator, `Print`, separator, `Log`, separator,
`Help` (labels above the icon buttons).

## 3. Controls and indicators

### 3.1 Channel settings (identical for CH 0 and CH 1)

| Control | Type | Values | Default CH 0 / CH 1 | Enabled when | Live while running |
|---|---|---|---|---|---|
| Source | combo | `AI 0`, `AI 1`, `AudioInput Left`, `AudioInput Right` | `AI 0` / `AI 1` | always | yes (task restarts); rejected if it would mix AI and audio with the other enabled channel |
| Enabled | checkbox | on/off | on / off | always | yes (task restarts) |
| Probe | combo | `1x` | `1x` | never (myDAQ has no probe setting; 10x is ELVIS only) | - |
| Coupling | combo | `DC`, `AC` | follows Source: `DC` for AI, `AC` for audio | never (value set automatically) | - |
| Scale Volts/Div | ring knob + combo | `10 mV`, `20 mV`, `50 mV`, `100 mV`, `200 mV`, `500 mV`, `1 V`, `2 V`, `5 V` | `1 V` | always | yes |
| Vertical Position (Div) | knob + numeric | -5.00 ... +5.00 div, increment 0.1, display up to 2 decimals (`0`, `-1.5`) | 0 | always | yes (display only, see 4.6) |

The controls of a disabled channel stay enabled (screenshot), so the channel can be prepared before
it is enabled.

### 3.2 Timebase

| Control | Type | Values | Default |
|---|---|---|---|
| Time/Div | ring knob + combo | `1 µs`, `2 µs`, `5 µs`, `10 µs`, `20 µs`, `50 µs`, `100 µs`, `200 µs`, `500 µs`, `1 ms`, `2 ms`, `5 ms`, `10 ms`, `20 ms`, `50 ms`, `100 ms`, `200 ms`, `500 ms` | `5 ms` |

Live while running (the next record uses the new timebase).

### 3.3 Trigger

| Control | Type | Values | Default | Enabled when |
|---|---|---|---|---|
| Type | combo | `Immediate`, `Digital` (item disabled: ELVIS only), `Edge` | `Immediate` | always |
| Source | combo | `TRIG`, `SYNC` (items disabled: ELVIS only), `Chan 0 Source`, `Chan 1 Source` | `TRIG` (shown grayed) | Type = Edge |
| Slope | toggle button | Positive (rising, icon `slope-rising`) / Negative (falling) | Positive | Type = Edge |
| Level (V) | numeric | -10.000 ... +10.000 V, increment 0.1 V | 0 | Type = Edge |
| Horizontal Position (%) | slider + numeric | 0 ... 100 %, increment 1 % | 50 | Type = Edge |

Rules:
- When Type becomes `Edge` and Source is `TRIG`/`SYNC`, Source becomes `Chan 0 Source` (or
  `Chan 1 Source` if CH 0 is disabled).
- `Chan N Source` is only selectable while channel N is enabled (the item is disabled otherwise). If
  the selected trigger channel gets disabled, Source switches to the other channel's source; if no
  channel is enabled nothing can run.
- The trigger source is *the signal of that channel* (e.g. `Chan 1 Source` with CH 1 = AI 0 triggers
  on AI 0), after the channel's coupling.
- All trigger controls are live while running.

### 3.4 Other controls

| Control | Type | Values | Default | Enabled when |
|---|---|---|---|---|
| Cursors On | checkbox | | off | always |
| C1 | combo | `CH 0`, `CH 1` | `CH 0` | Cursors On |
| C2 | combo | `CH 0`, `CH 1` | `CH 1` (screenshot; ELVISmx help says CH 0) | Cursors On |
| Display Measurements CH 0 | checkbox | | on | CH 0 enabled |
| Display Measurements CH 1 | checkbox | | on | CH 1 enabled (disabled-but-checked at launch) |
| Graph Properties | button | opens dialog (4.9) | | always |
| Autoscale | button | one-shot action (4.8) | | a device is selected; also while running |
| Acquisition Mode | run bar Run (continuous) / Single (once) | Run Continuously, Run Once | Run Continuously | always (app.md) |
| Log / Print (ELVISmx) | Export data / Export image actions | | | a completed record exists, also while running (data, app.md 10.1); any time (image) |

### 3.5 Advanced Settings tab (MYBENCH content)

ELVISmx's Advanced Settings tab holds ELVIS II+ oscilloscope-hardware settings (vertical offset,
20 MHz filter) that the myDAQ does not have. MYBENCH shows, for each channel, a group
`Channel 0 Settings` / `Channel 1 Settings` with:

| Control | Type | Values | Default | Notes |
|---|---|---|---|---|
| Input Range **[MYBENCH extension]** | combo | `Auto`, `±10 V`, `±2 V` | `Auto` | Audio sources: fixed `±2 V`, disabled. Auto = rule of 4.3. |
| Vertical Offset (V) | numeric | 0 | 0 | always disabled, tooltip "NI ELVIS II+ only" |
| 20 MHz Filter | checkbox | off | off | always disabled, tooltip "NI ELVIS II+ only" |

and a `Trigger` group with `Hysteresis (div)` **[MYBENCH extension]**: numeric 0.00 ... 2.00 div,
increment 0.05, default 0.10 (see 4.4).

### 3.6 Indicators

| Indicator | Content | Before first acquisition |
|---|---|---|
| Sample Rate | actual AI rate per channel (`AiTask::sampleRate()`), `formatEng(rate, "S/s", 4)` e.g. `200 kS/s` | empty |
| Traces | last displayed record per enabled channel (CH 0 green, CH 1 cyan; 10 x 10 division graticule, no axis numbers) | none |
| Measurement rows | `CH0 Meas:  RMS: <v>  Freq: <v>  Vp-p: <v>` in CH 0 color, same for CH 1; cursor row `C1: <v>  C2: <v>  dT: <v>` | |
| Timeout LED | see 4.5 | off |
| RMS | `dsp::rms` of the displayed record (after coupling) | `?` |
| Freq | `dsp::frequency` (mid-reference crossings, 10 % hysteresis) | `?` |
| Vp-p | `dsp::peakToPeak` | `0.00 V` |
| C1, C2 | linked channel's voltage at the cursor (linear interpolation between samples; true volts, independent of Vertical Position) | `?` |
| dT | t(C2) - t(C1), signed, seconds | |
| 1/dT **[MYBENCH extension]** | 1/|dT| in Hz, appended to the cursor line | |

Measurement formatting: 3 significant digits with SI prefix and unit, trailing zeros kept
(`707 mV`, `1.00 kHz`, `2.00 V`, `0.00 V` for zero); `?` when undefined (NaN: empty data, no complete
period for Freq, disabled linked channel for cursors).

## 4. Behaviour and algorithms

### 4.1 Channel set and validation (at Run and on every live change)

- Enabled channels in order CH 0, CH 1. At least one must be enabled ("Enable at least one channel.").
- If both enabled channels use the same Source, the source is acquired once and shown twice.
- Enabled sources must be all AI (AI 0/AI 1) or all audio: otherwise at Run "AI channels cannot be
  used with AudioInput channels. Select the same kind of source for both channels." While running,
  such a change is rejected: the combo reverts and a transient notification (app.md) shows the same
  message.
- Audio sources force Coupling = AC; AI sources force DC.

### 4.2 Record, sample rate and acquisition

For Time/Div = τ:
- Screen time `Ts = 10 τ`.
- Requested rate `r = min(maxAiRate(), 100 000 / Ts)`; actual rate `fs = coerceAiRate(r)` (never above
  the max); the Sample Rate indicator shows `fs` (the task's `sampleRate()`).
- Record length `N = floor(Ts * fs + 1e-9) + 1` samples, sample i at `t = i / fs` (the last sample sits on
  the right edge when Ts*fs is an integer). N >= 2.

Examples at 200 kS/s max: 5 ms/div -> 200 kS/s, N = 10 001; 500 ms/div -> 20 kS/s, N = 100 001;
1 µs/div -> 200 kS/s, N = 3 (only three samples on screen: the myDAQ cannot do better).

Each acquisition cycle (worker):
- **Immediate**: finite AI task (`SampleMode::Finite`, `samplesPerChannel = N`); `start()`,
  `read(N, timeout = Ts*1000 + 2000 ms)`. The record is displayed as is. Horizontal Position has no
  effect (trigger point = first sample).
- **Edge**: finite AI task with `samplesPerChannel = M = 2N`. Let `pre = round(H/100 * (N-1))`
  (H = Horizontal Position) and `post = N - 1 - pre`. Search the trigger source's samples with
  `dsp::findEdge` (level, slope, hysteresis, 4.4) for the first armed crossing at fractional index
  `k` with `pre <= k <= M - 1 - post`. If found, the displayed record is samples
  `[floor(k) - pre, floor(k) - pre + N - 1]` of every channel, and its time axis is shifted by the
  fractional part so the interpolated crossing lands exactly at H % of the screen
  (`t_i = (i - frac(k)) / fs`; the drawing clips the tiny overhang at the edges). If no crossing
  qualifies, the cycle has *no trigger* (4.5).
  The ELVISmx limitation follows from this design: a trigger edge must occur within about one screen,
  i.e. at least one period of a repetitive signal must fit on the screen.
- The task object is reused between cycles (`start()` begins a new finite acquisition) and recreated
  only when channels, ranges or rate change.
- Run Continuously: cycles repeat back to back, but the worker posts at most **30 records per
  second** (sleep to throttle at fast timebases). Run Once: exactly one displayed record, then idle
  (Edge: see 4.5).
- A new record replaces the previous one entirely (no persistence / averaging).

**[MYBENCH, may]** An implementation may use one continuous AI task and a ring buffer instead of
repeated finite tasks, provided the display is identical (same record, same trigger placement,
same timeout rules); this can reduce the dead time between records at slow timebases.

### 4.3 Input range (per channel)

Audio sources: ±2 V always. AI sources, Input Range = Auto: the visible voltage window is
`[(-5 - pos) * Vdiv, (5 - pos) * Vdiv]`; use ±2 V when both ends are within ±2.0 V, otherwise ±10 V.
Input Range = ±10 V or ±2 V forces that range. A range change recreates the task at the next cycle.
Signals beyond the range clip in hardware (the trace flattens at about ±range).

### 4.4 Software edge trigger

- `dsp::EdgeTrigger{ level = Level (V), slope, hysteresis }` applied to the trigger source channel's
  samples (after coupling, i.e. mean-removed for audio).
- Hysteresis in volts = `Hysteresis(div) * VoltsPerDiv(source channel)`, minimum 1 mV (default
  0.1 div = 100 mV at 1 V/div). Rising: the signal must first be below `level - hysteresis`, then the
  first sample pair crossing `level` upwards triggers; falling is symmetric. This rejects noise
  retriggering near the level.

### 4.5 Timeout LED and "no trigger" handling

- Immediate trigger: the LED is always off.
- Edge, Run Continuously: when a cycle ends without a trigger, the displayed traces are **not
  updated** (the previous record stays; empty graph if none yet) and the Timeout LED turns on. The
  worker keeps acquiring; the first triggered record turns the LED off and is displayed.
  Measurements keep describing the displayed record.
- Edge, Run Once: cycles repeat until a trigger is found or 10 s have elapsed (at least one full
  cycle is always attempted). On success the record is displayed and the instrument stops with the
  LED off; otherwise it stops with the LED on and the graph unchanged. No error notification (it is not an
  error).
- A `DeviceError::Timeout` from `read()` is a device problem, not a missing trigger: error message.

### 4.6 Drawing, vertical position, coupling

- Screen coordinates in divisions: `x = t / τ` (0..10), `y = v / Vdiv + pos` (-5..+5, 0 at the
  center). Vertical Position only moves the trace; it never changes the data, measurements, cursor
  values or the log.
- AC coupling (audio): the record mean is subtracted before trigger search, drawing, measurements,
  cursors and log (the hardware is already AC-coupled; this removes the residual offset).
- CH 1 samples are drawn at `t + channelSkew()` when the task reports a skew, so phase relations on
  screen are correct.
- Decimate with `dsp::minMaxDecimate` when a trace has more samples than plot pixels.
- Values beyond ±5 div are clipped at the plot frame.

### 4.7 Measurements

Computed per shown channel on the displayed record (N samples, after coupling), in the worker or on
arrival, for channels whose Display Measurements box is checked and whose channel is enabled:
- RMS: `sqrt(mean(x^2))` over the whole record (DC included for DC coupling).
- Freq: reciprocal of the average period between adjacent crossings, in the same direction, of the
  mid-reference level `(max + min) / 2` with 10 % of Vp-p hysteresis (`dsp::frequency(x, fs, 0.1)`).
  Needs at least one complete period on screen, else `?`.
- Vp-p: `max - min` of the record.

Graph Properties > Measurements can hide individual items (4.9).

### 4.8 Autoscale

One-shot action on the button (works idle or running). It uses the most recent record; if the
instrument is idle and no record exists, it first performs one Immediate acquisition (without
changing the trigger settings) and then applies the result. For each enabled channel:
1. `vmax`, `vmin` of the record (after coupling); `c = (vmax + vmin) / 2`, `a = vmax - vmin`.
2. Volts/Div = the smallest list entry with `a / Vdiv <= 8` (signal fills at most 8 of the 10
   divisions); for a flat signal (`a` < 1 mV) the smallest entry with `|c| / Vdiv <= 4`.
3. Vertical Position = `-c / Vdiv` rounded to 0.1 div and clamped to ±5 (the signal is centered).
Then Time/Div: take `f = dsp::frequency` of the trigger source channel (Edge) or of the first enabled
channel (Immediate). If `f` is defined, Time/Div = the smallest entry with `10 * τ >= 2 / f` (2 to 5
periods on screen); if not, Time/Div is unchanged. If Type = Edge, Level (V) is set to the trigger
source's `c` (mid level) so that triggering works. Slope, Type, Source and Horizontal Position are
not changed. While running, the new settings apply from the next cycle.

### 4.9 Graph Properties dialog

Non-modal dialog `Graph Properties`, three tabs, changes apply immediately, `Close` button,
**[MYBENCH extension]** `Reset` button (restores defaults of the current tab).

- **Plots**: `Channel` selector (`CH 0`, `CH 1`); `Line Style` (Solid, Dash, Dot, Dash Dot,
  Dash Dot Dot); `Line Width` (1, 2, 3, 4, 5 px); `Point Style` (None, Circle, Filled Circle, Square,
  Filled Square, Diamond, Cross, X); `Plot Interpolation` (None = points only, Linear, Step);
  `Line Color` (color button); `Point/Fill Color` (color button); `Fill to` (None, Zero, -Infinity,
  +Infinity: area between the trace and the chosen baseline filled with Point/Fill Color at 50 %
  alpha); `Background color` (graph background, default black). Defaults: solid, 1 px, no points,
  linear, trace color, trace color, None, black.
- **Cursors** (controls enabled only when Cursors On): `Cursor` selector (`C1`, `C2`); `Line Style`;
  `Line Width`; `Point Style` (marker drawn where the cursor meets its plot; default Filled Circle);
  `Cursor Style` (Vertical Line, Full Crosshair, Small Crosshair; default Vertical Line);
  `Cursor Color` (defaults C1 yellow, C2 magenta); `Linked plot` (`CH 0`, `CH 1`: the same setting as
  the C1/C2 combos of the Cursors Settings group, kept in sync).
- **Measurements**: `Channel` selector (`CH 0`, `CH 1`); `Channel measurements` checkboxes `RMS`,
  `Freq`, `Vp-p` (all on); `Cursor Measurements` checkboxes `C1`, `C2`, `dT` (all on; enabled only
  when Cursors On) controlling which items appear in the cursor line.

### 4.10 Cursors

- Cursors On shows two vertical cursors C1 and C2, initially at 2.5 div and 7.5 div (25 % / 75 % of
  the screen) the first time; afterwards they keep their time positions.
- Drag horizontally with the mouse (hit area ±4 px). The cursor snaps to the nearest sample time of
  its linked channel's record. Arrow keys move the focused cursor by one sample **[MYBENCH extension]**.
- Readouts as in 3.6, updated on every new record and while dragging.

### 4.11 Live changes summary

| Change while running | Effect |
|---|---|
| Vertical Position, Display Measurements, cursors, Graph Properties | immediate re-render of the current record |
| Volts/Div | immediate re-render; the next cycle may change the input range |
| Source, Enabled, Time/Div, Input Range | next cycle (task recreated when needed); the old record stays until the new one arrives |
| Trigger settings | next cycle |
| Acquisition Mode -> Run Once | current cycle completes, then stop |
| Device (top bar) | the instrument stops on the old device (app.md); press Run again |

## 5. Run / Stop semantics

- Run: validate (4.1), acquire resource `AI` from the ResourceArbiter (which pauses the current AI
  holder, see 7), create the task, start cycling. Export data exports the last completed record
  (also while running, app.md 10.1).
- Stop: the worker finishes or aborts the current read (use a short read timeout loop, e.g. read in
  chunks with 100 ms timeouts, so Stop reacts within ~100 ms even at 500 ms/div), destroys the task,
  releases `AI`. The last record stays displayed and can be exported.
- Paused by the arbiter: same as Stop, plus the banner "Oscilloscope paused — AI in use by
  <instrument>" with `Resume` (Resume = Run with the current settings, which in turn pauses the
  other holder).
- Run Once ends by itself (4.2, 4.5).

## 6. Errors and notifications

Shown as banners/toasts (app.md), never blocking dialogs.

| Situation | Message |
|---|---|
| no channel enabled | "Enable at least one channel." |
| AI and audio mixed | "AI channels cannot be used with AudioInput channels. Select the same kind of source for both channels." |
| AI held by Spectrum/Bode when Run is pressed | no error: the holder is paused by the arbiter (its banner: "<instrument> paused — AI in use by Oscilloscope") |
| AI busy outside MYBENCH (`ResourceBusy` with a free arbiter) | "The analog input of <device> is busy (used by another program?)." |
| device errors | app.md table (Overflow cannot happen with finite tasks) |
| no trigger | Timeout LED only, no message |

## 7. Resources and interactions

- Holds `AI` while running (AI 0/1 or audio: the ADCs are shared). Starting the Spectrum Analyzer or
  the Bode Analyzer pauses the Scope, and starting the Scope pauses them (ResourceArbiter, app.md).
- Runs together with the Generator (typical: AO 0 wired to AI 0), the Multimeter and DIO.
- Safe state: none (input only).

## 8. Export (ELVISmx Log and Print)

- **Export data** (ELVISmx Log; whenever a completed record exists): `DataTable` with the common header
  (app.md) and these comments

  ```
  # Sample rate: <fs> S/s
  # Time/Div: <τ>
  # CH 0: <Source>, <Volts/Div>/div, position <pos> div, <DC|AC> coupling, range ±<r> V   (or "CH 0: disabled")
  # CH 1: ...
  # Trigger: Immediate | Edge, <Chan N Source>, <rising|falling>, level <L> V, hysteresis <h> V, horizontal position <H> %
  # Trigger time: <t> s from the first sample   (Edge only)
  ```

  columns `Time (s)`, then `CH 0 (V)` and/or `CH 1 (V)` for enabled channels (volts after coupling,
  without vertical position). Time = `i / fs` from the first displayed sample. `sampleRate = fs`.
- **Export image** (ELVISmx Print): PNG/SVG/PDF of the panel's display area or of the whole panel
  (app.md).

## 9. Device API mapping

| Feature | Calls |
|---|---|
| rate planning | `Device::maxAiRate()`, `Device::coerceAiRate(r)` |
| acquisition | `createAiTask(AiConfig{ channels = [{AI0/AI1/AudioIn*, range}], sampleRate = fs, mode = Finite, samplesPerChannel = N or 2N })`; per cycle `AiTask::start()`, `AiTask::read(n, timeoutMs)` (in chunks for responsiveness); `AiTask::stop()` on Stop; task destroyed on Stop/config change |
| actual rate | `AiTask::sampleRate()` |
| skew | `AiTask::channelSkew()` (CH 1 drawn shifted) |
| channel names | `mydaq::toString(AiChannel)` gives `AI 0`, `AI 1`, `AudioInput Left`, `AudioInput Right` |
| DSP | `dsp::findEdge`, `dsp::rms`, `dsp::frequency`, `dsp::peakToPeak`, `dsp::mean`, `dsp::minMaxDecimate`, `dsp::formatEng` |

No AO/DMM/DIO calls.

## 10. Differences from ELVISmx

- Removed/disabled ELVIS-only features: Digital trigger type, TRIG/SYNC sources, 10x probe, GND
  coupling, ELVIS II+ Advanced Settings (shown disabled).
- ELVIS II disables Horizontal Position when both channels are enabled (hardware trigger limitation);
  the myDAQ software trigger has no such limit and MYBENCH never disables it for that reason.
- Extensions: Input Range and trigger hysteresis (Advanced tab), channel/trigger markers, 1/dT,
  status text, graph context menu.

## 11. Checks against the simulator

- Defaults + Run: CH 0 = AI 0 test signal (1 kHz, 1 V peak): at 5 ms/div 50 periods on screen,
  `Sample Rate: 200 kS/s`, RMS ≈ `707 mV`, Freq `1.00 kHz`, Vp-p ≈ `2.00 V`.
- Time/Div 500 µs, Edge, Chan 0 Source, rising, 0 V, 50 %: a rising zero crossing sits exactly at
  the screen center and the trace is stable.
- Enable CH 1 (AI 1, 200 Hz ±0.5 V square, 20 % duty): RMS `500 mV`, Freq `200 Hz`, Vp-p `1.00 V`.
- Level 3 V with a 1 V signal: Timeout LED on, trace frozen; back to 0 V: LED off.
- CH 0 = AudioInput Left while CH 1 = AI 1 enabled -> Run shows the mixing error.
- Autoscale on AI 0 (2 V p-p, centered on 0 V): 500 mV/div (smallest entry with 2 V / Vdiv <= 8),
  position 0, Time/Div 200 µs (smallest τ with 10 τ >= 2 / 1 kHz).
- Run FGEN on AO 0 (sim loops AO 0 -> AI 0): the FGEN waveform appears on CH 0.

## 12. Open questions / assumptions

1. **Volts/Div and Time/Div lists** are inferred from the knob pointer angles in the screenshot
   (1 V at index 6 of 9 -> 10 mV...5 V; 5 ms at index 11 of 18 -> 1 µs...500 ms, assuming 270-degree
   knobs). If the real lists differ, only the tables in 3.1/3.2 change. Slower timebases (1 s ... 10 s)
   could be added later (with a rolling display).
2. **Sample rate policy** (max 100 000 samples per screen, max rate otherwise) is ours; ELVISmx only
   shows the resulting rate.
3. **Missing trigger**: we behave like a "normal" trigger mode (keep the last trace, LED on). ELVISmx
   may display untriggered data instead; an `Auto` behaviour could be added as an option.
4. **Vertical Position range** (±5 div) and increment (0.1) are assumptions; the screenshot only shows 0.
5. **Advanced Settings tab** content on a myDAQ is not documented (the help describes ELVIS II+ items
   only). We keep the ELVIS items disabled and add Input Range + trigger hysteresis. A screenshot of
   the real tab would settle it.
6. **C2 default** is `CH 1` (screenshot) although the help says CH 0.
7. **Autoscale algorithm** is ours (the help does not describe the button).
8. **Instrument Control placement**: in the screenshot the group sits below the tab widget in the
   white right column; we keep it outside the tabs so Run/Stop stay visible on both tabs.
9. **Log content** uses the processed (coupled) data and time from the first displayed sample.
