# Bode Analyzer (`bode`)

Read [app.md](app.md) first (application shell, common conventions, run bar, inspector, SiField,
plots, cursors, export, notifications, resource arbiter) and [README.md](README.md) (hardware
constraints). This file is the complete **functional** specification (sections 2-8 are binding);
section 9 (ELVISmx reference layout) is informative only.

## 1. Purpose

Measures the frequency response (gain and phase versus frequency) of a passive or active linear
circuit. It drives the circuit with a sine wave on AO 0, measures the stimulus on AI 0 and the
circuit's output on AI 1, steps the frequency logarithmically from Start to Stop with a given number
of points per decade, and plots gain (dB or linear) and phase (degrees). The polarity of the
response can be inverted so that inverting amplifiers show 0° instead of ±180°. Replaces the ELVISmx
Bode Analyzer.

Reference screenshot: `ni-local/screens/Bode.png` (ELVISmx, idle at launch).

## MYBENCH design notes

Mapping to the MYBENCH panel (UI.md / app.md) and enhancements (which must not change the default
behaviour of sections 2-8):

- **Display area**: two vertically stacked plots sharing a logarithmic frequency axis
  (`Frequency (Hz)`): *Gain* (`Gain (dB)` or `Gain`) on top, *Phase* (`Phase (deg)`) below, analyzer
  colors (cyan axes and major grid, dark-cyan minor grid, black background in the dark theme). The
  connection hint `Connections: AI 0 - AO 0   AI 1 - Signal` sits above the gain plot. A readout row
  under the plots shows `Frequency (Hz)`, `Phase (deg)`, `Gain`, `Gain (dB)` (yellow in ELVISmx;
  theme "secondary readout" color). The cursor, when on, is one vertical line spanning both plots.
- **Inspector sections**: *Measurement Settings* (stimulus/response channels, Start, Stop, Steps,
  Peak Amplitude, Op-Amp Signal Polarity), *Graph Settings* (Mapping, Autoscale), *Cursor Settings*
  (Cursors On, Position Left/Right). Device is global (top bar).
- **Run bar**: Run (starts a sweep) / Stop toggle; no Single (a sweep is always single); status
  `Point k/K · <frequency>` with a progress bar, `Done`, `Stopped`, `Paused`; estimated remaining
  time.
- **Dashboard tile**: miniature gain curve, the -3 dB frequency (if found) and run state/progress.
- **Enhancements**:
  - *Overlay / compare*: after each sweep, `Keep as reference` stores the curve (name, color); stored
    curves are listed in a *References* card (show/hide, rename, delete, export) and drawn under the
    live sweep in muted colors. Up to 8 references; saved in the session.
  - *Markers*: `-3 dB` marker = the frequencies where the gain crosses (maximum gain of the sweep -
    3 dB), interpolated linearly in log-frequency between points; *unity-gain / 0 dB crossover* and
    *phase margin* (180° + phase at the 0 dB crossover) and *gain margin* (-gain in dB at the -180°
    phase crossover) for loop-gain measurements; each marker shows its value in a label and can be
    toggled in a *Markers* card.
  - *Phase unwrap* toggle (default off = ELVISmx wrapped phase): removes 360° jumps between
    consecutive points.
  - *Averages per point* (1-16, default 1): repeat the acquisition at each frequency and average the
    complex response.
  - *Re-measure at cursor*: measure only the cursor's frequency again (replaces that point).
  - Hover tooltip on the plots with the nearest point's values.

## 2. Controls

| Control | Type | Values / range | Format, increment | Default | Enabled when |
|---|---|---|---|---|---|
| Stimulus Channel | combo | `AI 0` only | | `AI 0` | never (fixed on myDAQ) |
| Response Channel | combo | `AI 1` only | | `AI 1` | never (fixed on myDAQ) |
| Start Frequency | numeric (Hz) | 1 Hz ... 20 kHz (typed `0.1` -> `1.00`, `900k` -> `20.00k`; verified in ELVISmx UI) | 2 decimals + SI suffix (`100.00`, `1.50k`, `10.00k`); increment 1 (ELVISmx) / 1 % of the decade (MYBENCH arrows) | 100 Hz (`100.00`) | idle (MYBENCH; ELVISmx leaves them editable during a sweep, the sweep keeps its values) |
| Stop Frequency | numeric (Hz) | 1 Hz ... 20 kHz | same | 10 kHz (`10.00k`) | idle |
| Steps (per decade) | numeric | integer ≥ 1 (`0` -> `1`; `99999` accepted - verified); MYBENCH caps at 1000 | increment 1 | 5 | idle |
| Peak Amplitude | numeric (V) | **0.00 ... 5.00 V** (typed `20` -> `5.00`, `-3` -> `0.00`; verified in UI and code) | 2 decimals; increment 0.01 | 2.00 | idle |
| Op-Amp Signal Polarity | combo | `Normal`, `Inverted` | | `Normal` | always; ELVISmx applies it to the **next** sweep only (verified: changing it after a sweep leaves the graphs and readouts unchanged). **[MYBENCH extension]** re-computes the displayed data immediately (3.5) |
| Mapping | combo | `Linear`, `Logarithmic` (list order verified) | | `Logarithmic` | always; redraws the existing data at once (verified) |
| Autoscale | checkbox | on/off | | on | always |
| Cursors On | checkbox | on/off | | off | always |
| Cursor selector | combo | `Cursor (Real)` (ELVISmx: always grayed on a myDAQ, verified; its other item is for simulated data) | | | never |
| Position Left / Right | two diamond buttons | move the cursor by one measured point (verified: three Right clicks from 100 Hz -> 398.11 Hz at 5 points/decade) | | | Cursors On and data exist |
| Run / Stop | run bar | | | | app.md |
| Log / Print (ELVISmx) | Export data / Export image | | | | ELVISmx: idle with at least one point (grayed at launch and during a sweep, verified); MYBENCH: also during a sweep (data), any time (image) |
| Device (ELVISmx) | top-bar device | | | | ELVISmx grays it during a sweep |

The Measurement Settings are locked while a sweep runs (the sweep uses the values at Run).

## 3. Behaviour and algorithms

### 3.1 Frequency points

`K = floor(Steps * log10(Stop / Start) + 1e-9) + 1` points, `f_k = Start * 10^(k / Steps)` for
`k = 0 .. K-1`. The last point may be below Stop ("depending on Steps the sweep may not reach the
stop frequency"). Start = Stop gives one point. Start > Stop is an error (5).

Default example: 100 Hz ... 10 kHz, 5 per decade -> 11 points: 100, 158.5, 251.2, 398.1, 631.0,
1000, 1585, 2512, 3981, 6310, 10000 Hz.

### 3.2 Per-point measurement (worker)

For each `f_k`:
1. **Stimulus**: `plan = dsp::planPeriodicBuffer(f_k, maxAoRate(), coerceAoRate, 100 000, 2, 1e-6)`;
   `buffer = dsp::generatePeriodic({Sine, f_k, amplitude = Peak Amplitude, offset 0, phase 0}, plan)`.
   If `plan.sampleRate` differs from the current AO task's rate, destroy and recreate the AO task
   (`AO 0`, range 10, Continuous, regenerate = true); otherwise `stop()`, `write(buffer)`, `start()`.
   The point's frequency is the exact generated frequency `f = plan.frequency`.
2. **Settling**: wait `max(3 / f, 20 ms)`, capped at 3 s (plus 50 ms extra before the first point),
   so the circuit reaches steady state.
3. **Acquisition**: finite AI task with AI 0 (stimulus) and AI 1 (response), rate
   `fs = coerceAiRate(min(maxAiRate(), max(1000, 100 f)))`, duration `max(4 / f, 0.1 s)`,
   `N = ceil(duration * fs)` samples (at most 200 000). Ranges: stimulus ±2 V when Peak Amplitude
   <= 1.8 V, else ±10 V; response adaptive (3.3). Recreate the AI task only when the rate or the
   ranges change; `start()` + `read(N, timeout)`.
4. **Tone fit** at the known frequency (exact for any record length, immune to DC and harmonics):
   `t0 = dsp::fitSine(ai0, fs, f)`, `t1 = dsp::fitSine(ai1, fs, f)`.
5. **Response**: `H = (t1.amplitude / t0.amplitude) * exp(j (t1.phase - t0.phase - 2π f skew))`
   with `skew = AiTask::channelSkew()` (AI 1 is sampled `skew` seconds after AI 0; without the
   correction its phase would read too high by `360 f skew` degrees). `Inverted` polarity:
   `H = -H`.
6. **Point values**: `Gain = |H|`, `Gain (dB) = 20 log10 |H|`, `Phase (deg) = arg H` in degrees,
   wrapped to (-180, 180].
7. Append the point, update both plots and the readout row (last point), then the next frequency.

Checks at each point:
- *No stimulus*: if `t0.amplitude < max(0.05 * Peak Amplitude, 5 mV)` -> stop the sweep with
  "No stimulus signal on AI 0. Connect AO 0 to AI 0+ (and AI 0- to AGND), then run again."
- *Clipping*: a channel is clipped when any sample reaches `0.98 * range`. Stimulus clipped at ±2 V
  -> redo the point at ±10 V. Response: see 3.3.

### 3.3 Response input range

The response starts at ±10 V for the first point. After each point: if the response peak
(`|offset| + amplitude`) is below 1.8 V the next point uses ±2 V; if a point measured at ±2 V is
clipped it is re-measured immediately at ±10 V. A point clipped at ±10 V is kept, marked *clipped*
(drawn with a hollow red marker) and a toast "Response clipped at <f> Hz — reduce the Peak
Amplitude" is shown once per sweep.

### 3.4 Plots

- X: logarithmic, from Start to Stop (fixed, so the sweep grows left to right), decade labels
  (`100`, `1000`, `10000`) and 2..9 minor lines.
- Gain plot: Mapping `Logarithmic` -> dB values, title `Gain (dB)`; `Linear` -> gain ratio, title
  `Gain (Linear)` (verified). Phase plot: degrees, title `Phase (deg)`, linear Y.
- Points are drawn as small hollow circle markers joined by lines, **yellow** in ELVISmx for both
  plots (verified); MYBENCH uses the theme's `trace0`/`displayYellow` (option `ELVISmx colors`);
  references use other colors.
- Run clears both plots (verified).
- Autoscale on: both Y axes fit the data (with 5 % margin, nice tick steps) after every point.
  Autoscale off (verified in ELVISmx): at Run the axes jump to fixed ranges - Gain -30 ... +5
  (whatever the Mapping), Phase -360 ... +360 with 90° steps - and stay there. MYBENCH: the same
  fixed ranges, plus zoom/pan and axis-limit editing (app.md plot conventions). Before any data
  ELVISmx shows placeholder ranges (Gain -0.5 ... 0.1 dB, Phase -3.5 ... 1.5); MYBENCH: Gain (dB)
  -40 ... +10, Gain 0 ... 2, Phase -180 ... +180.

### 3.5 Polarity and Mapping after the fact

Mapping only changes the display. Op-Amp Signal Polarity may be changed after (or during) a sweep:
MYBENCH stores the non-inverted complex response of each point and recomputes the displayed gain and
phase immediately ("the inversion does not affect the acquired data").

### 3.6 Readouts and cursor

- `Frequency (Hz)`, `Phase (deg)`, `Gain`, `Gain (dB)`: values of the most recent point during a
  sweep; with Cursors On, the values of the point under the cursor (verified). Format: 2 decimals
  (`10000.00`, `214.04`, `1.08`, `0.64`); before any data `0.00` and `Gain (dB)` = `Inf` (as ELVISmx,
  verified; MYBENCH may show `-Inf`, the mathematically correct value).
- Cursors On shows one **red full crosshair** on each plot at the same point, initially the **first**
  point (verified). Dragging snaps to the nearest measured point; `Position Left`/`Right` move it by
  one point (clamped). The cursor stays on its point index during a new sweep.
- Phase values may exceed ±180° slightly: ELVISmx wraps the phase difference to [-180, 180] and
  then adds the inter-channel skew correction (observed `214.04` on the simulator); MYBENCH applies
  the skew correction before wrapping, so its phase stays in (-180, 180].

### 3.7 End of sweep

After the last point: stop and destroy the AO task, `aoWriteStatic(AO 0, 0.0)` (safe state 0 V),
destroy the AI task, release the resources, status `Done`.

## 4. Run / Stop / pause semantics

- Run: validate (5), acquire `AO` and `AI` from the ResourceArbiter (it pauses their holders, e.g.
  the Generator and the Scope), clear the plots (references stay), sweep.
- Stop: finish or abort the current acquisition (reads in chunks with 100 ms timeouts), then the
  end-of-sweep cleanup of 3.7; points measured so far are kept and can be exported.
- Paused by the arbiter (another instrument takes AI or AO): same cleanup as Stop, banner "Bode
  Analyzer paused — <AI|AO> in use by <instrument>" with `Resume`; Resume re-acquires the resources
  and continues with the next point (data kept).

## 5. Errors and notifications

Banners/toasts (app.md), not blocking dialogs.

| Situation | Message |
|---|---|
| Start > Stop | "Start Frequency must not be higher than Stop Frequency." (Run refused) |
| no stimulus | see 3.2 |
| response clipped | see 3.3 (warning, sweep continues) |
| AO/AI busy outside MYBENCH | "The analog <input|output> of <device> is busy (used by another program?)." |
| device errors | app.md table; the sweep stops, data kept |

## 6. Resources and interactions

- Holds `AO` (AO 0) **and** `AI` (AI 0 + AI 1) for the whole sweep: the Generator, the Scope and the
  Spectrum Analyzer are paused while it runs. The Multimeter and DIO keep running.
- AO 1 is idle during the sweep (the single AO task only drives AO 0).
- Safe state: AO 0 at 0 V.

## 7. Export (ELVISmx Log and Print)

- ELVISmx **Log** (verified): same `Save Log File` dialog as the Scope (TXT / TDMS). The TXT file
  holds: line 1 date and time (tab-separated), line 2 `Amplitude: <A> V`, a header `Freq (Hz)`,
  `Gain (Linear)` or `Gain (dB)` (per Mapping), `Phase (deg)`, then one row per measured point with
  3 decimals in fixed-width columns (points measured before a Stop are included).
- ELVISmx **Print** (verified): the Scope's Print dialog; the page shows the header (computer, user,
  time stamp), a legend `CH0 Real`, both plots (ink saver), the readout row and a parameter table:
  Device, Start Frequency, Stop Frequency, Steps / decade, Peak Amplitude, Op-Amp Polarity, Graph
  Mapping, Cursor Linked Plot.
- **Export data** (at least one point measured; during a sweep it exports the points so far):
  common header (app.md) plus
  ```
  # Stimulus: AI 0 (AO 0), Response: AI 1
  # Start: <Start> Hz, Stop: <Stop> Hz, Steps per decade: <n>, Points: <measured>/<K>
  # Peak amplitude: <A> V, Op-amp signal polarity: <Normal|Inverted>
  ```
  columns `Frequency (Hz)`, `Gain`, `Gain (dB)`, `Phase (deg)` (with the current polarity) and
  **[MYBENCH extension]** `Clipped` (0/1).
- **Export image** (ELVISmx Print): the plots / whole panel as PNG, SVG or PDF.

## 8. Device API mapping

| Feature | Calls |
|---|---|
| stimulus | `dsp::planPeriodicBuffer` (with `maxAoRate`, `coerceAoRate`), `dsp::generatePeriodic`, `createAoTask(AoConfig{ {AO0, 10}, rate, Continuous, regenerate = true })`, `AoTask::write/start/stop` |
| acquisition | `createAiTask(AiConfig{ {AI0, r0}, {AI1, r1} }, fs, Finite, N)`, `AiTask::start`, `AiTask::read(n, timeout)`, `AiTask::sampleRate()`, `AiTask::channelSkew()` |
| analysis | `dsp::fitSine` (3-parameter fit at the known frequency) |
| safe state | `Device::aoWriteStatic(AoChannel::AO0, 0.0)` after destroying the AO task |

## 9. ELVISmx reference layout (informative, not binding)

ELVISmx window `Bode Analyzer`, about 742 x 592 px, fixed; black display on the left (~490 x 550 px),
control column on the right (~235 px):

```
+-------------------------------------------+------------------------------+
| [logo]   Connections: AI 0 - AO 0  AI 1 - Signal [tv] | Measurement Settings    |
|  Gain (dB) plot, log X 100..10000         | Stimulus Channel  Response Ch. |
|                                           | [AI 0 v](gray)    [AI 1 v]    |
|                                           | Start Frequency [100.00 ^v] Hz |
|  Frequency (Hz)                           | Stop Frequency  [10.00k ^v] Hz |
|  Phase (deg) plot, log X                  | Steps [5 ^v] (per decade)      |
|                                           | Peak Amplitude   Op-Amp Signal |
|  Frequency (Hz)                           | [2.00 ^v]        Polarity [Normal v]
| Frequency (Hz) 0.00  Phase (deg) 0.00  Gain 0.00  Gain (dB) Inf |------------|
+-------------------------------------------+ Graph Settings               |
                                            | Mapping [Logarithmic v] [x] Autoscale
                                            | Cursor Settings              |
                                            | [ ] Cursors On   Position    |
                                            | [Cursor (Real) v] Left <><> Right
                                            | Instrument Control           |
                                            | Device [myDAQ1 (NI myDAQ) v] |
                                            | Run Stop | Print | Log | Help|
                                            +------------------------------+
```

- Plots: black background, cyan axis numbers/titles (#00FFFC), cyan major grid, dark-cyan minor grid
  (#006189), log X with 2..9 minor lines. Connections text cyan bold. Readout row yellow.
- Stimulus/Response combos are grayed (fixed). Print/Log grayed until a sweep completed.

## 10. Differences from ELVISmx

- On ELVIS II the stimulus/response channels are selectable (SCOPE CH 0/1, AI); on the myDAQ they are
  fixed to AI 0 / AI 1 and the stimulus is always AO 0.
- The frequency range (1 Hz ... 20 kHz), settling/record rules, adaptive input range, clipping marks,
  polarity recomputation and all enhancements are MYBENCH decisions.

## 11. Checks against the simulator

Sim wiring: AO 0 -> AI 0 directly, AO 0 -> RC low-pass (1 kHz) -> AI 1.
- Defaults: 11 points; at 100 Hz ≈ -0.04 dB / -5.7°, at 1 kHz ≈ -3.01 dB / -45.0°, at 10 kHz ≈
  -20.04 dB / -84.3°. The -3 dB marker lands at ≈ 1 kHz.
- `Inverted`: phase at 1 kHz ≈ +135°.
- `setChannelSkew(5e-6)`: results unchanged (skew corrected).
- Disconnect the stimulus (sim test hook or AO 0 unused): "No stimulus signal on AI 0 ..." at the
  first point.
- Start the Scope during a sweep: the Bode pauses with a banner; Resume continues at the next point.

## 12. Open questions / assumptions

1. **Frequency limits** - resolved: 1 Hz ... 20 kHz (verified in UI and code).
2. **Cursor selector** - resolved: always grayed on a myDAQ (`Cursor (Real)`); MYBENCH omits it.
3. **Settling time, record length, AI rate and the 5 % stimulus threshold**: ELVISmx does a
   pre-measurement per point (see 13) and measures 16 000 samples; no stimulus check. MYBENCH keeps
   its own settling rule and the "No stimulus" check (extension).
4. **Wrapped phase**: ELVISmx wraps before the skew correction (values slightly beyond ±180 occur);
   MYBENCH wraps after.
5. **Steps / Peak Amplitude** - resolved: Steps ≥ 1 (no practical maximum), Peak Amplitude 0 ... 5 V.
6. **Trace colors** - resolved: yellow lines with hollow point markers; red crosshair cursor.
7. **Polarity after the fact**: ELVISmx applies it to the next sweep only; MYBENCH recomputes
   (extension, documented difference).
8. **Controls during a sweep**: ELVISmx leaves the Measurement Settings editable (the running sweep
   keeps the values it started with); MYBENCH locks them.

## 13. Verified DAQmx usage (ELVISmx, observed with NI I/O Trace)

Simulated NI myDAQ, ELVISmx 25.8; in our own words.
- Idle: editing any numeric writes 0 V to AO 0 (a short task at 2 kS/s writes zeros and is
  cleared).
- For every frequency point `f`:
  1. **Stimulus**: AO task on `ao0` (RSE, ±10 V), one period of the sine written with the timing
     of the waveform, regeneration allowed, continuous, started; samples per period
     `round(200 kS/s / f)` at 5 µs (e.g. 2000 at 100 Hz, 271 at ~740 Hz); `use onboard memory only`
     when the period has fewer than 200 samples.
  2. **Range pre-measurement**: AI 1 then AI 0, each alone, ±10 V, default terminal configuration,
     200 kS/s, finite 16 000 samples (80 ms each); the input ranges of the next step are chosen
     from these blocks.
  3. **Measurement**: one task with AI 1 (response) then AI 0 (stimulus), differential, ranges from
     step 2, finite 16 000 samples per channel at `min(16 000 f, max rate) / 5` (observed 40.02 kS/s
     at every point from 100 Hz up: 0.4 s per record); the AI conversion rate (80.06 kHz) is read to
     correct the inter-channel skew.
  4. The AO task is stopped and cleared; the next point starts.
  About 0.7 s per point on the emulated test PC.
- Stop or end of the sweep: AO 0 is driven to 0 V (2 kS/s task writing zeros, then cleared).
