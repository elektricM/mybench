# Spectrum Analyzer (`dsa`)

Read [app.md](app.md) first (application shell, common conventions, run bar, inspector, SiField,
plots, cursors, export, notifications, resource arbiter) and [README.md](README.md) (hardware
constraints). This file is the complete **functional** specification (sections 2-8 are binding);
section 9 (ELVISmx reference layout) is informative only.

## 1. Purpose

A single-channel FFT spectrum analyzer (ELVISmx "Dynamic Signal Analyzer"). It acquires blocks from
AI 0, AI 1 or an audio input, computes the windowed power spectrum (or power spectral density),
averages successive spectra (RMS, vector or peak hold; linear or exponential weighting), shows the
spectrum in dB, dBm or linear units with RMS or peak scaling, shows the time-domain block, and
measures the fundamental frequency and its power, THD, SINAD and the signal's peak-to-peak level.
Cursors read values on either plot.

Reference screenshot: `ni-local/screens/DSA.png` (ELVISmx, idle at launch).

## MYBENCH design notes

Mapping to the MYBENCH panel (UI.md / app.md) and enhancements (which must not change the default
behaviour of sections 2-8). Panel name "Spectrum Analyzer", id `dsa`.

- **Display area**: the spectrum plot (large, top; `Magnitude (<unit>)` vs `Frequency (Hz)`,
  analyzer colors) with the readouts `Detected Fundamental Frequency (Hz)` and
  `Fundamental Frequency Power (<unit>)` above it; under it the Display Mode selector
  (`Power Spectrum` / `Power Spectral Density`) and the `THD (%)` / `SINAD (dB)` readouts; then the
  time-domain plot (small, `Amplitude (V)` vs `Time (s)`) with `Vpk (V)` and `Sample Rate`
  readouts. Readout labels/values in the theme's secondary (yellow) color, as ELVISmx.
- **Inspector sections**: *Input Settings*, *FFT Settings*, *Averaging*, *Trigger Settings*
  (collapsed by default, all disabled, note "Immediate only on NI myDAQ"), *Frequency Display*,
  *Scale Settings*, *Cursor Settings*. Device is global.
- **Run bar**: Run/Stop (Run Continuously), Single (Run Once), `Restart` (averaging restart, the
  ELVISmx "Averaging: Restart" button), status `Averaging k/N`, `Averaging complete`, `Running`.
- **Dashboard tile**: mini spectrum, fundamental frequency and THD.
- **Enhancements**:
  - *Harmonic markers*: markers on the fundamental (F) and harmonics H2..H10 that fall inside the
    span, labelled with frequency and level (absolute and dBc relative to F); a *Harmonics* table
    (order, frequency, level, dBc).
  - *Peak table / marker search*: `Peak`, `Next peak`, `Peak left/right` actions for the selected
    cursor.
  - *Spectrogram / waterfall*: optional view below the spectrum showing the history of displayed
    spectra (time vs frequency, color = magnitude in the current units, history 50-1000 spectra,
    color map and range follow Scale Settings).
  - *Log-frequency axis* option for the spectrum plot (linear by default, as ELVISmx).
  - Mouse zoom/pan on both plots; hover readout of the nearest bin.

## 2. Controls

| Control | Type | Values / range | Default | Enabled when | Change while running |
|---|---|---|---|---|---|
| Source Channel | combo | `AI 0`, `AI 1`, `AudioInput Left`, `AudioInput Right` | `AI 0` | always | restarts acquisition + averaging |
| Voltage Range | combo | `+/- 10V`, `+/- 2V` | `+/- 10V` | AI sources (audio: shows `+/- 2V`, disabled) | restarts acquisition + averaging |
| Frequency Span | numeric (Hz) | **405 Hz ... `maxAiRate()/2` (100 kHz)** - typed `5` -> `405`, `500000` -> `100000` (verified in ELVISmx UI); integer display (`40000`), increment 1 (ELVISmx) / 1 % of the decade (MYBENCH arrows) | 40000 | always | restarts acquisition + averaging |
| Resolution (lines) | combo | **200, 400, 800, 1600, 3200** (verified in UI and code) | 400 | always | restarts acquisition + averaging |
| Window | combo | `None`, `Hanning`, `Hamming`, `Blackman-Harris`, `Exact Blackman`, `Blackman`, `Flat Top`, `4 Term B-Harris`, `7 Term B-Harris`, `Low Sidelobe` (`dsp::windowTypes()` / `windowName()`) | `None` | always | restarts averaging |
| Averaging Mode | combo | `None`, `Vector`, `RMS`, `Peak Hold` (verified) | `RMS` | always | restarts averaging |
| Weighting | combo | `Linear`, `Exponential` | `Exponential` | Mode = Vector or RMS | restarts averaging |
| # of Averages | numeric | integer 1 ... 1000, increment 1 (ELVISmx accepts 0 ... 99999, verified; MYBENCH coerces 0 to 1) | 5 | Mode = Vector, RMS or Peak Hold (Peak Hold: used only by Run Once, 3.6) | restarts averaging |
| Trigger Type | combo | `Immediate` | `Immediate` | never (myDAQ: immediate only) | - |
| Trigger Source | combo | `TRIG` | `TRIG` | never | - |
| Trigger Slope | toggle | rising | rising | never | - |
| Trigger Level (V) | numeric | 0 | 0 | never | - |
| Units | combo | `Linear`, `dB`, `dBm` (list order verified) | `dB` | always | display only |
| Mode (Frequency Display) | combo | `RMS`, `Peak` | `RMS` | Units ≠ dBm (grayed for dBm, verified) | display only |
| Averaging Restart | button | restart the averaging | | **running and Weighting = Linear** (verified: grayed with Exponential, enabled as soon as Linear is chosen while running) | |
| Display Mode | ring under the spectrum | `Power Spectrum`, `Power Spectral Density` | `Power Spectrum` | always | display only |
| Scale | combo | `Auto`, `Manual` | `Auto` | always | display only |
| Maximum | numeric | any; 3 decimals; shows `0.000` grayed in Auto | dB/dBm: 10.000, Linear: 1.0 (verified 10 / -120 for dBm) | Scale = Manual | display only |
| Minimum | numeric | any, < Maximum | dB/dBm: -120.000, Linear: 0.0 | Scale = Manual | display only |
| Cursors On | checkbox | | off | always | |
| C1, C2 | combo | ELVISmx: `Real` (plot binding), **always grayed** - the cursors live on the spectrum only (verified). MYBENCH: `Spectrum` (default), `Time` **[MYBENCH extension]** | `Spectrum` | Cursors On | |
| Cursor Select | combo | `C 1`, `C 2` (verified) | `C 1` | Cursors On | |
| Position Left / Right | diamond buttons | move the selected cursor by one bin (verified: 10 clicks = 1000 Hz at df = 100 Hz) | | Cursors On | |
| Run / Single / Stop | run bar | Run Continuously / Run Once | Run Continuously | app.md | |
| Log / Print (ELVISmx) | Export data / Export image | | | a spectrum exists, also while running (data, app.md 10.1); any time (image) | |

When Units change and Scale is Manual, Maximum/Minimum are reset to the defaults of the new units.
Frequency Span values above `maxAiRate()/2` are coerced.

## 3. Behaviour and algorithms

### 3.1 Acquisition parameters

- Sample rate `fs = coerceAiRate(2 * Frequency Span)` (never above `maxAiRate()`); `Sample Rate`
  shows `fs` as `formatEng(fs, "S/s", 3)` (`80 kS/s` at the defaults).
- Block length `N = 2 * lines` samples; bin spacing `df = fs / N` (= span / lines when fs is exact);
  bins `0 .. N/2` cover 0 ... fs/2 ≈ span. The spectrum plot's X axis is 0 ... Frequency Span.
- Block duration `N / fs = lines / span` (400 lines at 40 kHz: 10 ms; 6400 lines at 10 Hz: 640 s).
- AI task: one channel (`Source Channel`, range from Voltage Range, audio ±2 V), rate fs.
  - Run Continuously: `SampleMode::Continuous`; the worker reads contiguous blocks of N samples
    (`read(N, timeout = N/fs*1000 + 2000 ms)`, in chunks of at most 100 ms for a responsive Stop).
    Blocks do not overlap.
  - Run Once: `SampleMode::Finite` with `samplesPerChannel = N * blocksNeeded` (3.6), or repeated
    finite blocks.

### 3.2 Spectrum of a block

1. `spec = dsp::complexSpectrum(block, fs, window)` (window-compensated, one-sided: a sine of RMS
   amplitude A reads A at its bin whatever the window).
2. `averager.add(spec)` (`dsp::SpectrumAverager`, 3.3); `P = averager.result()` (powers in Vrms²
   per bin).
3. Display values: `dsp::formatSpectrum(P, {mode, unit, density, 50 Ω})` where `mode` = Frequency
   Display Mode (RMS/Peak), `unit` = Units, `density` = Display Mode is Power Spectral Density.
   - dB: `10 log10(P / 1 V²)` (dBVrms, or dBVpk with Peak = power doubled for AC bins).
   - dBm: `10 log10(P / 50 Ω / 1 mW)` (= dBVrms + 13.01 dB).
   - Linear: `sqrt(P)` in Vrms (or Vpk).
   - PSD: power divided by `ENBW * df` (per Hz), then the same unit rules (V/√Hz, dBV/√Hz, dBm/Hz).
   - Zero power floors at -400 dB (never -inf on the plot).
4. Spectrum plot Y title: `Magnitude (<unit>)` with unit text:

   | Units | Mode RMS | Mode Peak | PSD, RMS | PSD, Peak |
   |---|---|---|---|---|
   | dB | `dBVrms` | `dBVpk` | `dBVrms/√Hz` | `dBVpk/√Hz` |
   | dBm | `dBm` | `dBm` | `dBm/Hz` | `dBm/Hz` |
   | Linear | `Vrms^2` | `Vpk^2` | `Vrms^2/Hz` | `Vpk^2/Hz` |

   ELVISmx (verified): Linear shows **power** (`Magnitude (Vpk^2)`), not volts; the PSD view lowers
   the curve by `10 log10(df)` but keeps the power-spectrum axis title (`Magnitude (dBm)`).
   MYBENCH: Linear = power as ELVISmx (**[MYBENCH extension]** `Linear (V)` amplitude option) and
   PSD titles with `/Hz` / `/√Hz` as in the table.

### 3.3 Averaging (`dsp::SpectrumAverager`)

- `None`: the latest block only.
- `RMS`: average of the power spectra (reduces the variance of noise, not its level).
- `Vector`: average of the complex spectra (reduces noise that is not phase-locked to the block
  start; useful only for signals synchronous with the acquisition — with the myDAQ's immediate
  trigger most signals average towards zero, which is expected).
- `Peak hold`: per-bin maximum of the powers since the last restart; Weighting ignored.
- Weighting `Linear`: equal-weight mean of the first `# of Averages` blocks; when complete, the
  result is held (acquisition continues, the display stays on the completed average, status
  `Averaging complete`) until Restart or a setting change. `Exponential`: equal weights while
  filling, then each new block has weight `1/N` (continuous running average).
- Restart (button, or any change of Source, Range, Span, Lines, Window, Mode, Weighting, # of
  Averages) clears the averager. Status shows `Averaging k/N` while filling.

### 3.4 Measurements (on the averaged spectrum `P`)

- ELVISmx (verified in UI and code): the harmonic analyzer uses harmonics 2 ... 10; the
  fundamental power is estimated over 7 bins around the peak and shown as a **linear power** -
  `Fundamental Frequency Power (Vrms^2)` in RMS mode, `(Vpk^2)` (value doubled) in Peak mode -
  **whatever the Units** (observed `47.17` Vrms² with Units = dB for a 10 V-peak sine; the caption
  does not follow dB/dBm). MYBENCH shows the same linear value by default and **[MYBENCH extension]**
  offers `Power in display units`.
- **Detected Fundamental Frequency (Hz)** and **Fundamental Frequency Power**:
  `pk = dsp::findPeak(P, span = 3)` (largest bin outside the DC lobe, refined by the power-weighted
  centroid of the lines around it; power = sum of those lines / ENBW). Frequency formatted with 2
  decimals (`1000.00`). Power shown in the current Units and Mode (non-density even in PSD display):
  dB -> `10 log10(power)` dBVrms (dBVpk), dBm -> dBm, Linear -> the power itself in `Vrms^2`
  (`Vpk^2`); the indicator's label carries the unit, e.g. `Fundamental Frequency Power (Vrms^2)`.
- **THD (%)**: `dsp::analyzeHarmonics(P)`: harmonic powers integrated over the window main lobe at
  `k * f0` for every harmonic k >= 2 below the span (fs/2); `THD = 100 * sqrt(sum P_k / P_1)`.
- **SINAD (dB)**: `10 log10(total / (total - fundamental))`, total excluding the DC lobe.
- **Vpk (V)**: `(max - min) / 2` of the latest time block (verified: `9.96` for a ±10 V sine;
  code: peak-to-peak amplitude divided by 2).
- No signal / only DC: frequency and power `0.00`, THD and SINAD `0.00` (never NaN on screen).

### 3.5 Plots and scale

- Spectrum: X linear 0 ... Frequency Span (Hz), Y per Scale:
  - `Auto`: fit to the displayed data; in dB/dBm the bottom is limited to `top - 160 dB` so the
    -400 dB floor does not flatten the view; nice steps (10 dB in dB units).
  - `Manual`: Maximum / Minimum.
- Time: the latest block, X 0 ... (N-1)/fs, Y autoscaled (graph palette X/Y autoscale locks, both on
  by default).
- Trace colors: theme `trace0` (CH 0 green) for both plots.

### 3.6 Run modes

- Run Continuously: acquire and process blocks until Stop; the display updates after every block
  (at most 30 updates/s; blocks faster than that are still averaged).
- Run Once: acquire the number of blocks needed to complete the average — 1 for `None`, `# of
  Averages` for `RMS`/`Vector` (either weighting) and for `Peak hold` — display the result, stop.

### 3.7 Cursors

Two cursors C1, C2, each attached to a plot by its combo (`Spectrum` or `Time`), initially at 25 % /
75 % of the plot's X range. Dragging snaps to bins (spectrum) or samples (time). `Cursor Select`
chooses which cursor `Position Left/Right` moves (one bin/sample per click). Readout row in the
display (cursor colors): `C1: <x> <x-unit>, <y> <y-unit>   C2: ...   Δx: ..., Δy: ...` (Δ only when
both cursors are on the same plot).

## 4. Run / Stop / pause semantics

- Run/Single: acquire `AI` from the ResourceArbiter (it pauses the Scope or the Bode Analyzer if they
  hold it), create the task, run.
- Stop: finish/abort the current read, destroy the task, release `AI`; the last spectrum, time block
  and measurements stay displayed.
- Paused by the arbiter: as Stop plus banner "Spectrum Analyzer paused — AI in use by <instrument>"
  with Resume (restarts acquisition; averaging restarts).

## 5. Errors and notifications

Banners/toasts (app.md).

| Situation | Message |
|---|---|
| `Overflow` (continuous) | warning toast "Samples were lost; averaging restarted." — the task is restarted automatically; after 3 overflows within 10 s: stop with the app.md Overflow error |
| AI busy outside MYBENCH | "The analog input of <device> is busy (used by another program?)." |
| device errors | app.md table |

## 6. Resources and interactions

- Holds `AI` (one channel; audio sources included). Pauses/paused by Scope and Bode.
- Runs with the Generator (e.g. analyse the distortion of the generator's AO 0 wired to AI 0), the
  Multimeter and DIO.
- Safe state: none.

## 7. Export (ELVISmx Log and Print)

- **Export data** (a spectrum exists; while running it exports the latest displayed result):
  common header (app.md) plus
  ```
  # Source: <channel>, range ±<r> V, sample rate <fs> S/s
  # Frequency span: <span> Hz, lines: <lines>, window: <window>
  # Averaging: <mode>, <weighting>, <n> averages (<k> done)
  # Display: <Power Spectrum|Power Spectral Density>, units <unit label>
  # Fundamental: <f> Hz, power <p> <unit>; THD <thd> %; SINAD <s> dB; Vpk <v> V
  ```
  columns `Frequency (Hz)`, `Magnitude (<unit label>)` (as displayed, after averaging), then
  `Time (s)`, `Amplitude (V)` (latest block; different length, cells left empty). `sampleRate = fs`
  for the time columns.
- **Export image** (ELVISmx Print).

## 8. Device API mapping

| Feature | Calls |
|---|---|
| rate | `Device::maxAiRate()`, `Device::coerceAiRate(2 * span)`, `AiTask::sampleRate()` |
| acquisition | `createAiTask(AiConfig{ {src, range} }, fs, Continuous or Finite, N...)`, `AiTask::start/read/stop` |
| spectrum | `dsp::complexSpectrum`, `dsp::SpectrumAverager`, `dsp::formatSpectrum`, `dsp::spectrumUnitLabel` (UI must show the labels of 3.2) |
| measurements | `dsp::findPeak`, `dsp::estimatePeak`, `dsp::analyzeHarmonics`, `dsp::formatPower`, `dsp::peakToPeak` |
| windows | `dsp::windowTypes()`, `dsp::windowName()` |

## 9. ELVISmx reference layout (informative, not binding)

ELVISmx window `Dynamic Signal Analyzer`, about 828 x 654 px, fixed; black display on the left
(~503 x 549 px), gray cursor strip under it, control column on the right (~310 px):

```
+---------------------------------------------+------------------------------------+
| [logo] Detected Fundamental Frequency (Hz) 0.00  [tv] | Input Settings            |
|        Fundamental Frequency Power (Vrms^2)  0.00     | Source Channel [AI 0 v]   Voltage Range [+/- 10V v]
|  Magnitude (dBVrms) 10 .. -110                |-----------------+------------------|
|  spectrum plot, X 0 .. 40000                  | FFT Settings    | Averaging        |
|                                               | Frequency Span  | Mode [RMS v]     |
|  Power Spectrum ▼ Mode     Frequency (Hz)     | [40000 ^v]      | Weighting        |
|                  THD (%) 0.00  SINAD (dB) 0.00| Resolution (lines) [Exponential v] |
|  Amplitude (V) time plot, X 0 .. 0.019987     | [400 v]         | # of Averages    |
|                        [+][zoom]   [X][Y]     | Window [None v] | [5 ^v]           |
|  Vpk (V) 0.00              Sample Rate: 80 kS/s| Trigger Settings (all grayed)      |
+---------------------------------------------+ Type [Immediate] Slope [/]          |
| Cursor Settings                              | Source [TRIG]   Level (V) [0]       |
| [ ] Cursors On C1 [Real v] C2 [Real v]       | Frequency Display                  |
|        Cursor Select [C 1 v]  Position Left <><> Right | Units [dB v] Mode [RMS v] Averaging [Restart] |
+---------------------------------------------+ Scale Settings                      |
                                              | Scale [Auto v] Maximum [0.000] Minimum [0.000] |
                                              | Instrument Control                  |
                                              | Device [...] Acquisition Mode [Run Continuously v] |
                                              | Run Stop | Print | Log | Help       |
                                              +------------------------------------+
```

- Plots: black, cyan axis numbers/titles and grid (#00FFFC). Readouts yellow (#FFFF00 family).
- The time plot has a small graph palette (cursor tool, zoom tool, X and Y autoscale locks).
- At idle the Maximum/Minimum fields show `0.000` grayed (Scale = Auto) and the Restart button is
  grayed.

## 10. Differences from ELVISmx

- Trigger Settings are disabled on the myDAQ (ELVIS II: digital/analog triggers); shown collapsed.
- Source list: myDAQ AI 0/1 and audio inputs (ELVIS: SCOPE CH 0/1, AI 0-7).
- Overflow auto-restart, the unit label table, Auto-scale floor, Run Once block count and all
  enhancements are MYBENCH decisions.

## 11. Checks against the simulator

AI 0 test signal: 1 kHz sine, 1 V peak, 1 % third harmonic, 0.5 mV rms noise.
- Defaults (40 kHz span, 400 lines, None, RMS/Exponential/5): `Sample Rate 80 kS/s`, df = 100 Hz,
  fundamental `1000.00` Hz, power `-3.01` dBVrms (0.5 Vrms²), THD ≈ `1.00` %, SINAD ≈ `40.0` dB,
  Vpk ≈ `1.00`.
- Units Linear: fundamental power `0.50` (Vrms^2), peak line at 0.50 Vrms²; Mode Peak: 1.00 Vpk².
- Units dBm: power ≈ +10.0 dBm.
- Window Hanning: same power (window-compensated), wider peak.
- Source AudioInput Left (440 Hz, 0.5 V peak): Voltage Range shows `+/- 2V` disabled; fundamental
  440 Hz (approx., bin width 100 Hz: estimated by the centroid).
- Start the Scope: the analyzer pauses with a banner.

## 12. Open questions / assumptions

1. **Sample rate = 2 x span**, block length `2 x lines` - resolved (verified with NI I/O Trace:
   ELVISmx reads `2 × lines + 2` samples per record, 802 at 400 lines; MYBENCH analyses `2 × lines`).
2. **Resolution list** - resolved: 200 ... 3200 lines. **Span limits** - resolved: 405 Hz ...
   100 kHz on a myDAQ.
3. **Fundamental power units** - resolved: always linear power (Vrms² / Vpk²), caption per Mode.
4. **Linear weighting** holds the completed average (verified: the display stops changing and
   `Restart` becomes available).
5. **Cursor plot choices** - resolved: ELVISmx cursors are on the spectrum only (attach combos
   grayed); MYBENCH adds `Time`.
6. **Vpk** - resolved: `(max - min) / 2`.
7. **Time-plot X range** 0 ... 0.019987 s at launch is a stale default; running shows 0 ... (N-1)/fs
   (0.009987 s at the defaults, verified).
8. **Linear units show power** in ELVISmx (V²); MYBENCH keeps that default.

## 13. Verified DAQmx usage and Log format (ELVISmx)

- Each edit of Span (and at Run) creates a probe task (AI 0, ±2 V, 1 kS/s finite) only to read the
  maximum sample-clock rate, then clears it.
- Run Continuously: **a new finite task per record**: one voltage channel (`ai0`/`ai1` default
  terminal configuration, or audio), range from Voltage Range, sample clock `2 × Span`, finite
  `2 × lines + 2` samples, reference trigger disabled, read all samples, read back the actual rate,
  clear. About 10 records/s on the emulated test PC.
- Log (verified): `Save Log File` (TXT / TDMS). The TXT file contains the time record as a LabVIEW
  waveform export (`waveform`, `t0` absolute, `delta t`, then absolute timestamp + value rows,
  channel named `Source signal`), a blank line, `FFT waveform`, `Units: <unit>`, then one row per
  line `frequency <TAB> value` exactly as displayed (400 rows 0 ... 39 900 Hz at the defaults).

## 14. Octave Analyzer (ELVISmx featured app) - recovered behaviour

Source tags: **SCR** = screenshots `ni-local/screens/explore/octave/` (numbers 01-16); **FP** = front
panel resources of `NI ELVISmx Octave Analyzer.vi`; **VI** = its constants and call structure; **SVT** =
NI Sound and Vibration library VIs it calls (third-octave analysis, octave weighting). Confidence H / M / L.
Method for the code part: [algorithms/featured.md 4](algorithms/featured.md). All text is ours.

### 14.1 Window and controls

Fixed-size window (no maximize), blue title band `Octave Analyzer`, `Help >>` side panel
(Introduction / How to Use). Left column, top to bottom (SCR 01, H):

| Group | Control | Values | Default | Notes, source, conf. |
|---|---|---|---|---|
| Physical Channel | Select Device | myDAQ / ELVIS II devices found; `No Supported Devices Found` when none | first device | disabled while running. SCR, FP, H |
| | Channel(s) | list `<dev>/ai0`, `<dev>/ai1`, `<dev>/audioInputLeft`, `<dev>/audioInputRight`, `<dev>/dmm`, plus `Browse...` (channel browser) | `ai0` | a channel list of several AI channels is accepted (N-channel analysis); `dmm` or any non-analog-input channel is refused at Start with a message asking for an analog input channel. SCR 02 H; VI M for multi-channel |
| | Minimum (V) / Maximum (V) | -10 ... +10 (coerced) | -10 / 10 | become the AI channel limits; no cross-check between them when idle (5 / 8 accepted); disabled while running. SCR 06, 08a, FP, H |
| Exponential Averaging | Mode | **Slow, Fast, Impulse, Custom** | **Fast** | live while running. SCR 03, H |
| | Time Constant (ms) | 0 ... 10000, step 1; typing 99999 gives 10000 | 125 | enabled only for Custom; with other modes it is grayed and keeps its last value (125 at start; 0 after Custom 0, SCR 09 to 15), also while running. SCR 03, 05, 06, 07b, 08b, H. 0 is accepted in Custom. |
| Frequency | Low Band (Hz) | integer **band index 0 ... 30** (see 14.2), caption says Hz | **0** | live while running, restarts the display |
| | High Band (Hz) | same | **30** | typing 99999 or 1000 is kept in the box but behaves as 30. SCR 05, 11, 13 |
| | Weighting | **Linear, A-weighting, B-weighting, C-weighting** | Linear | live while running. SCR 04, 12, H |
| Run | Start / Stop / Help >> | | | Stop grayed while idle; while running Start is grayed. SCR 01, 09 |

Behaviour while running (SCR 09, H): Select Device, Channel(s), Minimum and Maximum are grayed;
Mode, Time Constant (Custom only), Low/High Band, Weighting and the graph tabs stay usable.
Changing Low/High Band while running re-draws the band set at once (SCR 10-15).

### 14.2 Band set (the important correction)

- The analysis is **1/3 octave** with the **31 ISO nominal bands 20 Hz ... 20 kHz**: 20, 25, 31.5, 40,
  50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1k, 1.25k, 1.6k, 2k, 2.5k, 3.15k, 4k,
  5k, 6.3k, 8k, 10k, 12.5k, 16k, 20k (SVT band label list inside the VI, H).
- `Low Band` / `High Band` select the first and last band **by index into that list**: 0 = 20 Hz,
  5 = 63 Hz, 20 = 2 kHz, 30 = 20 kHz. Evidence: Low 5 / High 20 draws steps from about 63 Hz to
  2 kHz (SCR 15); Low 0 / High 30 draws 20 Hz ... 20 kHz (SCR 10); High 1000 draws the same full
  range (SCR 11) so out-of-range is clamped to 30; Low 100 leaves a single dot at 20 kHz (SCR 13, 14).
  (H.) The number is *not* a frequency although the label says Hz. The VI stores the choice as an
  enumeration of those nominal frequencies (FP/VI type descriptor, H).
- Behaviour when Low > High: not tested (L).
- Filter class of the SVT analysis (ANSI S1.11 vs IEC 61260, both available in the library) is
  not wired by the app; library default applies (L).

### 14.3 Measurement and display

Engine (VI, H unless noted): continuous AI at **50 kS/s**, reads of **1000 samples** per channel
(20 ms cadence; the test VM raised error `-200279` "cannot keep up with hardware acquisition" and
offered `Continue`; explorer checkpoint note, M). Each block goes through the NI third-octave analysis with the
exponential averaging settings {Mode, Time Constant} and the band range, then through the octave
weighting filter (weights applied to band powers at the band centres, M). The analysis reports
whether its filters have settled.

- Time constants (library behaviour, L): Slow 1 s, Fast 125 ms, Impulse 35 ms rise / 1.5 s decay,
  Custom = the field. dB conversion is switched on; the reference is the library default
  `20 uPa` and the axis title says so (`Band Power (dB ref 20u Pa)`, fixed text even though the
  input is volts). Whether the numbers are 10 log(V^2 / (20u)^2) or relative to 1 V^2 is not
  settled; the test signal was not a known sine (L, manual session).
- **Time tab** (SCR 09, 01): graph `Voltage (V)` (ticks -10 ... 10) vs `Time (s)`, X axis in
  absolute clock time (`hh:mm:ss AM/PM`, ticks have no fractional seconds; at rest 01:00:00 ...
  01:00:01 AM, running 05:42:22 ... 05:42:23 PM). **The displayed span is unresolved** (L): the
  graph is fed 1000-sample blocks, so one 20 ms block is likely and the second-only tick labels
  cannot show it; manual step C5. The label under the graph shows `Settled` (indicator caption
  pair `Settled` / `Not settled`, H for the pair, M for position).
- **Frequency tab** (SCR 10-15): band power drawn as a *staircase* (each band a flat step centred on
  its nominal frequency, `Multiplot octave graph`); X log 20 ... 20 000 Hz, grid at 20, 200, 2000,
  20000; Y autoscaled (observed -55 ... -20 dB linear weighting, -110 ... -20 with A); Y title
  `Band Power (dB ref 20u Pa)`. With A weighting the low bands drop by the A curve (about -50 dB
  at 20 Hz) - consistent with weighting applied to band powers (SCR 12, H). Only the bands in
  Low ... High are drawn; the axis stays 20 ... 20 000.
- No cursors, no readout table, no log/print button, no averaging restart, no run-once.

### 14.4 MYBENCH (`dsa`, Octave mode) compared

| Item | ELVISmx | MYBENCH now | Verdict |
|---|---|---|---|
| Band set | 31 ISO bands 20 Hz ... 20 kHz, indices 0..30 | 34 base-10 bands 10 Hz ... 20 kHz (indices 0..33), combos of nominal frequencies, default 20 Hz ... 20 kHz | same default range; extra bands 10 / 12.5 / 16 Hz are an extension (audit dsa #12) |
| Band selector | numeric index box labelled Hz | two combos of nominal frequencies | better; keep |
| Modes / time constants | Slow, Fast (default), Impulse, Custom (0 ... 10000 ms, 125) | same values, Custom only editable in Custom | OK |
| Weighting | Linear, A, B, C | Z, A, B, C (audit dsa #11) | label only |
| Filters | SVT third-octave (class unknown) | 6th-order Butterworth band-pass bank | accuracy vs SVT unverified (audit dsa #13) |
| Channel | one or several AI, audio, refuses dmm | one source, AI 0/1/audio | audit dsa #10 |
| Live changes while running | Mode, TC, bands, weighting | none of these controls is disabled while running | OK |
| Frequency display | staircase, fixed 20 ... 20k axis, Y autoscale, title `Band Power (dB ref 20u Pa)` | bar chart of band level, axis from the band range | cosmetic (audit dsa #14) |
| Time display | absolute-clock axis, span unresolved (probably one 20 ms block) | one 20 ms block, 0 ... record duration | probably equal; audit dsa #15 is conditional on manual step C5 |
| Extras (MYBENCH) | none | Settled LED, overall level, loudest band, Run Once, export | keep |
