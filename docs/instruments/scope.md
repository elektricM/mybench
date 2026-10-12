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
| Source | combo | `AI 0`, `AI 1`, separator line (not selectable), `AudioInput Left`, `AudioInput Right` (verified in ELVISmx UI) | `AI 0` / `AI 1` | always | yes (next record) - see 4.1 for the AI/audio rule |
| Enabled | checkbox | on/off | on / off | always | yes (next record; verified) |
| Probe | combo | `1x` (ELVIS: `10x`) | `1x` | never (grayed, verified) | - |
| Coupling | combo | `DC`, `AC` (ELVIS: `GND`) | follows Source: `DC` for AI, `AC` for audio (verified) | never (value set automatically) | - |
| Scale Volts/Div | ring knob + combo | AI sources: `5 V`, `2 V`, `1 V`, `500 mV`, `200 mV`, `100 mV`, `50 mV`, `20 mV`, `10 mV`; audio sources: `1 V` ... `10 mV` only (verified in ELVISmx UI). The drop-down lists the largest value first. Switching a channel to audio coerces a larger value to `1 V` (verified) | `1 V` | always | yes |
| Vertical Position (Div) | knob + numeric | -5 ... +5 div, increment 0.1 (as observed in ELVISmx); typed values are coerced (`10` -> `5`, verified in UI); shown without trailing zeros (`0`, `-1.5`) | 0 | always | yes (display only, see 4.6) |

The controls of a disabled channel stay enabled (verified), so the channel can be prepared before
it is enabled. Every control has a tooltip equal to its name (ELVISmx); no right-click menus.

### 3.2 Timebase

| Control | Type | Values | Default |
|---|---|---|---|
| Time/Div | ring knob + combo | `5 µs`, `10 µs`, `20 µs`, `50 µs`, `100 µs`, `200 µs`, `500 µs`, `1 ms`, `2 ms`, `5 ms`, `10 ms`, `20 ms`, `50 ms`, `100 ms`, `200 ms` (15 values; the drop-down lists `200 ms` first; ELVISmx writes `us`) - **verified in ELVISmx UI** | `5 ms` |

Live while running (the next record uses the new timebase; verified). **[MYBENCH extension, may]**
`1 µs`, `2 µs` and `500 ms` ... `10 s` (slow timebases with a rolling display) may be added after
the ELVISmx list; they are marked as extensions in the UI and never used by default.

### 3.3 Trigger

| Control | Type | Values | Default | Enabled when |
|---|---|---|---|---|
| Type | combo | `Immediate`, `Digital` (item disabled: ELVIS only), `Edge` (verified) | `Immediate` | always |
| Source | combo | `TRIG`, `SYNC` (items always disabled: ELVIS only), `Chan 0 Source`, `Chan 1 Source` (verified) | `TRIG` (shown grayed while Immediate) | Type = Edge |
| Slope | toggle button | Positive (rising, icon `slope-rising`) / Negative (falling); click toggles (verified) | Positive | Type = Edge |
| Level (V) | numeric | **-100 ... +100 V** (typed `150` is coerced to `100`, verified in the ELVISmx UI), increment 0.1 V, shown without trailing zeros | 0 | Type = Edge |
| Horizontal Position (%) | slider + numeric | 0 ... 100 %, increment 1 % | 50 | Type = Edge |

Rules (verified in ELVISmx UI):
- When Type becomes `Edge`, Source becomes `Chan 0 Source` (the TRIG/SYNC default is replaced).
- `Chan N Source` is only selectable while channel N is enabled (`Chan 1 Source` is grayed while
  CH 1 is disabled). If the selected trigger channel gets disabled, Source switches to the other
  channel's source; if no channel is enabled nothing can run.
- The trigger source is *the signal of that channel* (e.g. `Chan 1 Source` with CH 1 = AI 0 triggers
  on AI 0), after the channel's coupling.
- All trigger controls are live while running (verified: Type, Level, Slope, Horizontal Position).
- MYBENCH may limit the Level SiField to ±100 V as ELVISmx; values beyond the input range simply
  never trigger.

### 3.4 Other controls

| Control | Type | Values | Default | Enabled when |
|---|---|---|---|---|
| Cursors On | checkbox | | off | always (also while running, verified) |
| C1 | combo | `CH 0`, `CH 1` | `CH 0` | Cursors On |
| C2 | combo | `CH 0`, `CH 1` | `CH 1` (verified) | Cursors On |
| Display Measurements CH 0 | checkbox | | on | CH 0 enabled |
| Display Measurements CH 1 | checkbox | | on | CH 1 enabled (disabled-but-checked at launch; enabled as soon as CH 1 is enabled, verified) |
| Graph Properties | button | opens dialog (4.9) | | always |
| Autoscale | button | one-shot action (4.8) | | a device is selected; also while running |
| Acquisition Mode | run bar Run (continuous) / Single (once) | `Run Continuously`, `Run Once` | Run Continuously | always (app.md) |
| Device | combo | DAQmx devices (`myDAQ1 (NI myDAQ)`) | first device | idle only (disabled while running, verified) |
| Print | button | Print dialog (8) | | **idle and at least one record acquired** (disabled at launch and while running, verified) |
| Log | button | Save Log File dialog (8) | | idle and at least one record acquired (verified) |
| Help | button | help window at the Scope page | | always |
| Toggle View | icon button top-right of the display | shrinks the window to the display area only; click again restores (verified) | | always |

MYBENCH mapping: Print = `Export image/PDF...`, Log = `Export data...`, both available any time a
record exists (also while running, app.md 10); Toggle View = Focus mode.

### 3.5 Advanced Settings tab

Verified in ELVISmx UI: on a myDAQ the tab shows, per channel (`Channel 0 Settings` green,
`Channel 1 Settings` cyan), a `Vertical Offset (V)` knob + numeric (0) and a `20MHz Filter` /
`Enabled` checkbox, **all grayed** (ELVIS II+ hardware only). Nothing else.

MYBENCH keeps these two items disabled (tooltip "NI ELVIS II+ only") and adds, per channel:

| Control | Type | Values | Default | Notes |
|---|---|---|---|---|
| Input Range **[MYBENCH extension]** | combo | `Auto`, `±10 V`, `±2 V` | `Auto` | Audio sources: fixed `±2 V`, disabled. Auto = the ELVISmx rule of 4.3. |

and a `Trigger` group with `Hysteresis (div)` **[MYBENCH extension]**: numeric 0.00 ... 2.00 div,
increment 0.05, **default 0.00** (ELVISmx has no hysteresis, as observed in ELVISmx; see 4.4).

### 3.6 Indicators

| Indicator | Content | Before first acquisition |
|---|---|---|
| Sample Rate | actual AI rate per channel (`AiTask::sampleRate()`), yellow label `Sample Rate:` + value with 2 decimals and SI prefix (`50.00 kS/s`, `200.00 kS/s`; verified) | empty |
| Trigger status | `*** Waiting for Trigger ***` (yellow, left of the sample rate) while an Edge acquisition finds no trigger; hidden otherwise (verified) | hidden |
| Traces | last displayed record per enabled channel (CH 0 green, CH 1 cyan; 10 x 10 division graticule, no axis numbers). Trigger level shown as a red horizontal line across the graph while Type = Edge (verified: the center line turns red at level 0, moves with Level) | none |
| Measurement rows | `CH0 Meas:  RMS: <v>  Freq: <v>  Vp-p: <v>` in CH 0 color, same for CH 1 in CH 1 color; cursor row `Cursors:  C1: <v>  C2: <v>  dT: <v>` (yellow) above them when Cursors On (verified) | |
| Timeout LED | see 4.5 | off |
| RMS | RMS of the displayed record (after coupling), DC included | `?` |
| Freq | see 4.7 | `?` |
| Vp-p | max - min of the record | `0.00 V` |
| C1, C2 | linked channel's voltage at the cursor (true volts, independent of Vertical Position); `0.00 V` when the linked channel is disabled (verified) | `0.00 V` |
| dT | t(C2) - t(C1), signed, seconds | |
| 1/dT **[MYBENCH extension]** | 1/|dT| in Hz, appended to the cursor line | |

Measurement formatting (verified in ELVISmx UI): engineering notation with SI prefix and
unit; **3 decimals when the mantissa is ≥ 1, 2 decimals below 1**: `6.861 V`, `20.053 Hz`,
`19.992 V`, `-299.39 mV`, `707.11 mV`, `0.00 V`, `25.00 us` (MYBENCH writes `µs`); `?` when the
value is undefined (NaN/infinite: empty data, no detectable tone for Freq).

## 4. Behaviour and algorithms

### 4.1 Channel set and validation (at Run and on every live change)

- Enabled channels in order CH 0, CH 1. At least one must be enabled ("Enable at least one channel.").
- If both enabled channels use the same Source, the source is acquired once and shown twice
  (verified with NI I/O Trace: CH 0 and CH 1 both `AudioInput Left` gave a one-channel task).
- AI and audio sources can never be mixed. **ELVISmx behaviour (verified in UI):** choosing an audio
  source on one channel switches the other channel's Source to an audio input as well (observed:
  CH 0 -> `AudioInput Left` changed CH 1 from `AI 1` to `AudioInput Left`), and choosing an AI source
  switches the other channel back to AI. MYBENCH does the same: the other channel follows to the
  same kind (audio: the matching `AudioInput Right`/`Left` partner if the user never chose one,
  else its last audio choice; AI: its last AI choice, default `AI 1`/`AI 0`), with a small toast
  "CH 1 switched to AudioInput Right (AI and audio inputs cannot be mixed)". No error is needed.
- Audio sources force Coupling = AC and limit Volts/Div to 1 V ... 10 mV; AI sources force DC.

### 4.2 Record, sample rate and acquisition

ELVISmx rule (as observed in ELVISmx, and with NI I/O Trace): for
Time/Div = τ, screen time `Ts = 10 τ`, `Rmax = maxAiRate()` (200 kS/s):
- if `Rmax × Ts ≤ 2500`: rate `= Rmax`, `samples = Rmax × Ts`;
- else rate `= 2500 / Ts` and `samples = 2500` (the rate never drops below 1500 S/s on the myDAQ
  list except at 200 ms/div: 1250 S/s < 1500, so rate 2000 S/s and samples = 2000 × Ts = 4000);
- record length `N = samples + 1` (first and last sample on the two screen edges); sample i at
  `t = i / fs`; `fs = coerceAiRate(rate)` and the Sample Rate indicator shows the actual rate.

| Time/Div | rate | N | | Time/Div | rate | N |
|---|---|---|---|---|---|---|
| 5 µs | 200 kS/s | 11 | | 2 ms | 125 kS/s | 2501 |
| 10 µs | 200 kS/s | 21 | | 5 ms | 50 kS/s | 2501 (verified) |
| 20 µs | 200 kS/s | 41 | | 10 ms | 25 kS/s | 2501 |
| 50 µs | 200 kS/s | 101 | | 20 ms | 12.5 kS/s | 2501 |
| 100 µs | 200 kS/s | 201 | | 50 ms | 5 kS/s | 2501 |
| 200 µs | 200 kS/s | 401 | | 100 ms | 2.5 kS/s | 2501 |
| 500 µs | 200 kS/s | 1001 | | 200 ms | 2 kS/s | 4001 |
| 1 ms | 200 kS/s | 2001 (verified) | | | | |

**[MYBENCH extension]** `Record length` in the Advanced tab: `Standard (2500 points)` (default, the
ELVISmx rule above) or `High resolution` (up to 100 000 samples per screen: rate
`min(Rmax, 100 000 / Ts)`), useful for zooming and for FFT-based measurements.

Each acquisition cycle (verified with NI I/O Trace):
- ELVISmx creates a fresh finite AI task for **every record**: (a throw-away task on the enabled
  channels only to read the maximum sample-clock rate, then) the acquisition task with the enabled
  channels, sample clock at the rate above, finite, `N` samples per channel (**`2N` when Type =
  Edge**), one read of all samples (timeout 10 s), then the task is cleared. MYBENCH may keep the
  task between cycles (restart only), since the result is identical.
- **Immediate**: the record is displayed as is. Horizontal Position has no effect.
- **Edge** (as observed in ELVISmx): read `2N`; `pre = N × H / 100` (H = Horizontal Position);
  scan the trigger channel's samples from index `pre` to `pre + N - 1` for the first crossing
  (4.4); if found at index `k`, the displayed record of every channel is samples `[k - pre, k - pre + N)`,
  so the trigger sample sits at H % of the screen (no sub-sample interpolation in ELVISmx;
  **[MYBENCH extension, may]** sub-sample alignment by interpolation). If none: no trigger (4.5).
  A trigger edge must therefore occur within about one screen.
- Run Continuously: cycles repeat back to back (observed ~5 records/s on the emulated test PC; the
  real rate depends on the PC); MYBENCH posts at most **30 records per second**. Run Once: one
  displayed record, then idle (Edge: see 4.5).
- A new record replaces the previous one entirely (no persistence / averaging).

### 4.3 Input range (per channel)

ELVISmx rule (as observed in ELVISmx and with NI I/O Trace): the range depends only on Volts/Div:
`5 V`, `2 V`, `1 V`, `500 mV` per div -> **±10 V**; `200 mV` ... `10 mV` per div -> **±2 V**. Vertical
Position is not taken into account. Audio sources: always ±2 V. AI channels are configured
differential; audio inputs referenced single-ended (RSE). MYBENCH Input Range `Auto` = this rule;
`±10 V` / `±2 V` force the range. A range change recreates the task at the next cycle. Signals
beyond the range clip in hardware.

### 4.4 Software edge trigger

- Applied to the trigger source channel's samples (after coupling, i.e. mean-removed for audio).
- ELVISmx (as observed in ELVISmx): rising = `previous < Level ≤ sample`; falling = `previous > Level ≥
  sample`; the first sample of the search window never triggers; **no hysteresis**.
- **[MYBENCH extension]** Hysteresis (Advanced tab, default 0 = ELVISmx behaviour): with h > 0 the
  signal must first be below `Level - h` (rising) / above `Level + h` (falling) before a crossing
  counts, which rejects noise retriggering. `h = Hysteresis(div) × Volts/Div(source channel)`.

### 4.5 Timeout LED and "no trigger" handling (verified in ELVISmx UI)

- Immediate trigger: the LED is always off.
- Edge, Run Continuously: when a record contains no trigger, **the graph is cleared** (no trace),
  the measurements show `?` / `0.00 V`, the Timeout LED lights and `*** Waiting for Trigger ***`
  appears at the top of the display. Acquisition continues; the first triggered record is displayed
  and clears the LED and the message. (Observed with Level 15 V on a ±10 V signal and with an
  audio signal that never reached the level.)
- Edge, Run Once: ELVISmx makes **one** acquisition; without a trigger it shows the empty graph, the
  LED and the waiting message, and stays in the running state (Run button pressed, Print/Log
  disabled) without acquiring again until the user presses Stop (observed > 60 s; NI I/O Trace
  shows a single read). MYBENCH: Single keeps acquiring until a trigger is found or the user
  presses Stop (a more useful "single-shot arm"); the status shows `Waiting for trigger`.
- **[MYBENCH extension]** `Keep last trace on timeout` (Advanced tab, default off = ELVISmx):
  the last triggered record stays on screen, drawn dimmed, while waiting.
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

Computed per shown channel on the displayed record (N samples, after coupling), for channels whose
Display Measurements box is checked and whose channel is enabled (as observed in ELVISmx):
- RMS: `sqrt(mean(x^2))` over the whole record (rectangular window, DC included).
- Freq: frequency of the dominant tone found by an FFT-based single-tone estimate on the record
  (`dsp::estimateTone`: windowed FFT peak refined by interpolation); shown only when the detected
  tone amplitude is **greater than 0.2 × Volts/Div** of that channel, otherwise `?`.
  **[MYBENCH extension]** option `Frequency method: Tone (ELVISmx) / Edge counting` (mid-level
  crossings with 10 % hysteresis, `dsp::frequency`) for square/pulse signals.
- Vp-p: `max - min` of the record.

Graph Properties > Measurements can hide individual items (4.9).

### 4.8 Autoscale

One-shot action on the button (works idle or running; verified idle). ELVISmx (M/H confidence; UI: verified that it acquires by itself and changes Volts/Div):
1. A separate acquisition per enabled channel on the ±10 V range (audio ±2 V). NI I/O Trace
   observed one finite task per channel with 2001 samples at 200 kS/s (the current timebase's
   record, 1 ms/div); an alternative observation gives 16 000 samples at up to 50 kS/s with retries. MYBENCH:
   one acquisition per channel of `max(N, 16 000)` samples at the current record's rate.
2. Tone detection on that block (frequency and tone amplitude) and `A = (max - min) / 2 × 2.2`.
3. Volts/Div = the entry nearest to `A × 3 / 10` in the channel's list (observed: a ±10 V ramp gave
   `5 V`); the signal is not re-centered (Vertical Position unchanged).
4. Time/Div = the entry nearest to `0.5 / f_min` (about 5 periods on screen), f_min = lowest detected
   frequency over the channels; unchanged when no tone is found (observed: unchanged on the
   simulator's noisy ramp).
5. Trigger settings and Vertical Position are not changed. While running, the new settings apply
   from the next cycle.
**[MYBENCH extension]** `Autoscale` also sets the trigger level to the signal's mid level when
Type = Edge (option, default off).

### 4.9 Graph Properties dialog (verified in ELVISmx UI)

Modal dialog `Graph Properties`, three tabs, buttons `Reset to Default` (bottom left), `OK`,
`Cancel`; changes apply on OK (MYBENCH: live preview while the dialog is open, `Cancel` reverts).

- **Plots**: plot selector `Channel 0 (Real)`, `Channel 1 (Real)` (ELVISmx also lists grayed
  `Channel 0 (Simulated)`, `Channel 1 (Simulated)`; MYBENCH shows `CH 0`, `CH 1`, plus `Math`/`Ref`
  when enabled); palettes: **Line Style** (solid, dash, dot, dash-dot, dash-dot-dot), **Line Width**
  (none + 5 widths, 1 px default), **Point Style** (`<none>` + 16 marker shapes: open/filled circle,
  open/filled square, small dot, point, cross, plus, x, diamond, ...), **Interpolation** (6 modes:
  points only, step horizontal, linear (default), step vertical, horizontal center step, vertical
  center step); color buttons **Line** and **Point/Fill** (color picker: grayscale ramp, hue ramp,
  user colors, history, `LED On`-style named default); **Fill to**: `<none>`, `zero`, `-Infinity`,
  `+Infinity` (and a grayed `Plot 0`); **Background color**: `Black`, `White`.
- **Cursors**: cursor selector `C1`, `C2`; the same Line Style, Line Width and Point Style palettes
  (defaults: dotted line style, 1 px, no marker - the cursors are drawn dashed); **Cursor Style**
  palette (`<none>`, vertical line, horizontal line, full crosshair, small crosshair, and two dashed
  crosshair variants; default vertical); **Cursor color** (both cursors yellow in the default look -
  MYBENCH keeps its distinct `cursor1`/`cursor2` tokens); **Linked plot**: `Channel 0 (Real)`,
  `Channel 1 (Real)` (same setting as the C1/C2 combos).
- **Measurements**: channel selector `Channel 0`, `Channel 1`; table `Name` / `Display`:
  `RMS Voltage`, `Frequency`, `Voltage Peak-to-Peak` (all checked); group `Cursor Measurements`:
  `C1 Volts`, `C2 Volts`, `delta X-Value` (all checked).

### 4.10 Cursors (verified in ELVISmx UI)

- Cursors On shows two vertical dashed cursors; each carries its name (`C1`, `C2`) as a label at
  the point where it meets its linked trace. On first use both start at the **left edge** of the
  graph (dT a few samples: `25.00 us` at 5 ms/div); afterwards they keep their positions.
  MYBENCH starts them at 25 % / 75 % of the screen instead (more useful) - a documented difference.
- Drag horizontally with the mouse (verified); the cursor snaps to samples of its linked channel.
  Arrow keys move the focused cursor by one sample **[MYBENCH extension]**.
- Readouts as in 3.6, updated on every new record and while dragging; a cursor linked to a
  disabled channel reads `0.00 V`.

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

ELVISmx (verified in UI): **Log** and **Print** are enabled only when idle and after at least one
record was acquired.

- **Log** opens a save dialog (`Save Log File`, default folder: the public Documents\National
  Instruments folder) with the file types `TXT (*.txt)` (default), `TDMS (*.tdms)`, `All Files`.
  The TXT file (observed) is a tab-separated LabVIEW waveform export of the displayed record:
  row 1 `waveform` + one column pair per channel (channel names), row 2 `t0` + absolute start
  date/time per channel, row 3 `delta t` (sample interval, `5.000000E-6`), a blank line, then a
  header `time[0]  Ch0  time[1]  <ch1>` and N rows of *absolute timestamps* and values in
  `%E` format (6 decimals).
- **Print** opens a `Print` dialog: `Export to...` **Printer** (installed printers list) or **File**
  (path + browse, PNG only), `Print Options`: `Print Ink Saver Mode`, `User name`, `Computer name`,
  `Time stamp` (all checked), `Orientation` Portrait / Landscape, `Number of copies`; `OK`,
  `Cancel`. The output page (observed) holds a header (computer name, user name, time stamp), a
  plot legend (`CH0 Real`, `CH1 Real`), the display (white background in ink-saver mode, traces in
  black/gray), the measurement and cursor rows, and a settings table: per channel Source,
  Volts / Div (V), Vertical Position (Div); Device, Time Base (s), C1/C2 Linked Plot, Trigger Type
  & Slope (e.g. `Edge & Falling`), Trigger Source, Trigger Level (V), Horizontal Position (%).

MYBENCH:
- **Export data** (Log; any time a completed record exists): CSV (default) / TSV, **TDMS is not
  provided** (no free writer; CSV opens everywhere) and **[MYBENCH, should]** an `ELVISmx-compatible
  TXT` option reproducing the layout above (for existing lab scripts). Common header (app.md) plus

  ```
  # Sample rate: <fs> S/s
  # Time/Div: <τ>
  # CH 0: <Source>, <Volts/Div>/div, position <pos> div, <DC|AC> coupling, range ±<r> V   (or "CH 0: disabled")
  # CH 1: ...
  # Trigger: Immediate | Edge, <Chan N Source>, <rising|falling>, level <L> V, horizontal position <H> %
  ```

  columns `Time (s)` (relative to the first displayed sample), then `CH 0 (V)` and/or `CH 1 (V)`.
- **Export image / PDF** (Print): PNG/SVG/PDF of the display or the whole panel, with options
  mirroring the Print dialog: `Ink saver` (white background, dark traces), `Include user / computer
  name / time stamp`, `Include settings table` (the table above), orientation (PDF). Direct
  printing: **[MYBENCH, should]** `Print...` through `QPrintDialog` with the same page layout, since
  ELVISmx users print from the instrument.

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
  `Sample Rate: 50.00 kS/s` (2501 points), RMS ≈ `707.11 mV`, Freq `1.000 kHz`, Vp-p ≈ `2.000 V`.
- Time/Div 500 µs, Edge, Chan 0 Source, rising, 0 V, 50 %: a rising zero crossing sits exactly at
  the screen center and the trace is stable.
- Enable CH 1 (AI 1, 200 Hz ±0.5 V square, 20 % duty): RMS `500.00 mV`, Freq `200.000 Hz`, Vp-p
  `1.000 V`.
- Level 3 V with a 1 V signal: Timeout LED on, graph cleared, `*** Waiting for Trigger ***`; back to
  0 V: LED off, trace back.
- CH 0 = AudioInput Left while CH 1 = AI 1 -> CH 1 switches to an audio input (toast).
- Autoscale on AI 0 (2 V p-p, centered on 0 V): A = 2.2 V -> 500 mV/div (nearest to 0.66 V),
  position unchanged, Time/Div 500 µs (nearest to 0.5 / 1 kHz).
- Run FGEN on AO 0 (sim loops AO 0 -> AI 0): the FGEN waveform appears on CH 0.

## 12. Open questions / assumptions

1. **Volts/Div and Time/Div lists** - resolved (verified in ELVISmx UI): 5 µs ... 200 ms
   (15 values), AI 10 mV ... 5 V, audio 10 mV ... 1 V.
2. **Sample rate policy** - resolved: ELVISmx keeps 2500 points per screen (4.2 table); the
   100 000-point mode is a MYBENCH option.
3. **Missing trigger** - resolved: ELVISmx clears the graph and shows `*** Waiting for Trigger ***`
   with the Timeout LED (4.5); keeping the last trace is a MYBENCH option.
4. **Vertical Position range** - resolved: ±5 div, increment 0.1, typed values coerced.
5. **Advanced Settings tab** - resolved: only the grayed ELVIS II+ items.
6. **C2 default** - resolved: `CH 1`.
7. **Autoscale** - partly resolved (4.8): observed acquisition size (2001 samples at the current
   rate) differs from another observation (16 000 samples); both are listed. The exact Volts/Div snapping
   could not be checked with the simulator's synthetic signal.
8. **Instrument Control placement**: kept outside the tabs in MYBENCH (run bar).
9. **Log content** - resolved for TXT (8). TDMS logging is not replicated.
10. **Cursor start position**: ELVISmx starts both cursors at the left edge; MYBENCH uses 25 %/75 %.
11. **Run Once without trigger**: ELVISmx acquires once and then waits for Stop; MYBENCH keeps
    acquiring until triggered (documented difference).
12. **Graph Properties** in ELVISmx is modal with OK/Cancel; MYBENCH previews live and reverts on
    Cancel.

## 13. Verified DAQmx usage (ELVISmx, observed with NI I/O Trace)

Simulated NI myDAQ, ELVISmx 25.8; in our own words.
- Idle: no task; the device list is polled twice per second.
- Per record: a probe task (enabled channels, ±2 V, 1 kS/s finite) is created only to read the
  maximum sample clock rate (200 kS/s) and cleared; then the acquisition task is created:
  one voltage channel per enabled **distinct** source (`ai0`/`ai1` differential ±10 V or ±2 V per
  4.3; audio inputs RSE ±2 V), sample clock at the rate of 4.2, rising edge, **finite**, `N`
  samples (`2N` for Edge); the actual rate is read back for the indicator; one multi-channel
  waveform read of all samples (10 s timeout); the task is cleared. Observed: 5 ms/div ->
  50 kS/s × 2501; 1 ms/div -> 200 kS/s × 2001, Edge 4002; audio Edge 200 kS/s × 4002.
- Stop aborts and clears the task in progress.
- Autoscale (idle): one finite task per enabled channel (±10 V, default terminal configuration,
  current rate and record length), read once, cleared; then the normal loop is not started.
- Log / Print / Graph Properties do not touch the device.
