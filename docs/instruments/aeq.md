# Audio Equalizer (`aeq`)

Read [app.md](app.md) first. Behaviour matches the ELVISmx featured application. MYBENCH version of the ELVISmx **Audio
Equalizer** featured app: live three-band tone control from an input to an output.

## 1. ELVISmx parity

| ELVISmx | Values / default | MYBENCH |
|---|---|---|
| Input (L) / Input (R) | ai0, ai1, audioInputLeft, audioInputRight, none | **Input** card, same list; default AudioInput Left / Right |
| Output (L) / Output (R) | ao0, ao1, audioOutputLeft, audioOutputRight, none | **Output** card; default AudioOutput Left / Right |
| Bass / Midtone / Treble | −12 … +12 dB, step 1; saved default 1 dB (ticks at −10 / 0 / +10) | faders + dB fields (0.1 dB entry), default +1 dB each |
| Volume | 0 (end caption `Min`) … 1 (`Max`); saved 0.4965, 0.5 in a second copy of the value | fader + slider in %, default 50 % |
| Filters | IIR Bessel, order 4: lowpass 320 Hz; bandpass 320–2560 Hz; bandpass 2560–20 840 Hz | same (§2) |
| Output | Σ 10^(dB/20) × band, × Volume | same |
| AI/AO ±2 V, AO 50 kS/s continuous without regeneration, pre-fill 0.25 s / 0.35 s | | ±2 V; AI and AO at the same rate (50 kS/s default, 100 / 200 kS/s); **Output Buffer** 50 / 100 / 250 (default) / 350 ms |
| "Time" graph | autoscale | **Time** view: 20 ms of input and output, triggered on the input's rising zero crossing |
| "Frequency" graph | 1/24-octave, 25 Hz … 25 kHz, exponential averaging (10 ms) | **Spectrum** view: 1/24-octave bands 25 Hz … min(25 kHz, 0.48 fs), dBV, input and output; averaging 10 ms / 125 ms (default) / 1 s |

## 2. Signal processing

- Bessel analog prototype of order 4 normalized to −3 dB at 1 rad/s; lowpass: bilinear transform
  with the cutoff prewarped; bandpass: lowpass-to-bandpass transform (8 poles) with both edges
  prewarped, so each band is −3 dB at its edges and 0 dB at DC / its geometric center. Realized
  as cascaded second-order sections (double precision, denormals flushed).
- Each output channel runs its own three filters on its side's input; with one input selected it
  feeds both outputs. Gain, volume and bypass changes are ramped over one 10 ms block (no clicks).
- **Bypass** [MYBENCH]: output = volume × input. **Flat** [MYBENCH]: all bands 0 dB.
- The bands are not complementary: at 0 dB the sum dips around the crossovers (Bessel phase), as
  in ELVISmx; the **tone response** plot shows it.
- Output samples beyond ±2 V are clipped (Clip LED for 1 s).

## 3. Streaming

One continuous AI task (inputs, unique channels) and one streaming AO task (outputs) at the same
coerced rate. Start: AO primed with `buffer` seconds of silence, AO then AI started. Loop: read
10 ms blocks (timeout 200 ms), process, write. Input-to-output delay ≈ buffer + 10 ms. An AO
underflow or AI overflow restarts both streams (dropout counter in the header, warning toast at
most every 5 s). Stop: tasks destroyed; AO 0 / AO 1 set to 0 V when used.

Routing rules (checked before Run, shown in red): at least one input and one output; inputs are
either both AI or both audio (shared converters), likewise outputs; Output L ≠ Output R.
Resources: `AnalogInput | AnalogOutput`. Routing, rate and buffer changes restart; gains,
volume, bypass and display settings are live. Single does not exist.

## 4. Panel

Display: header (gains or Bypass; rate, latency, dropouts); tone response plot (log f, ±18 dB,
band regions, per-band curves with their gains, total curve, volume not included); spectrum or
time plot (L/R selector, legend, hover readout); console with Bass, Midtone, Treble and Volume
faders (drag, wheel, arrows, double-click resets) and level meters In L/R, Out L/R (dBFS, RMS
bar, peak line, 1.5 s peak hold, zones −12 / −3 dBFS) and Clip LED. On the simulator, a hint
explains that the audio outputs feed the audio inputs (and AO 0 feeds AI 0/AI 1), offering
**Use AI 0 / AI 1** (their test signals). Inspector: Input, Output, Equalizer (fields, volume,
bypass), Streaming (rate, buffer, delay), Display (averaging, show input).

## 5. Export and tile

Export data: band centers with input/output spectrum (dBV) and the tone response, with the
settings as metadata. Tile: gains, volume, tone curve, output meters.

## 6. Checks (automated: `instruments/aeq/tests`)

Prototype −3 dB at 1 rad/s; lowpass −3.01 dB at 320 Hz; bandpasses −3 dB at their edges and 0 dB
at center (50 and 200 kS/s), stable; processed sine amplitude = |H(f)|; ramped gain changes;
octave-band power conservation; meters; routing validation; streaming AI 0/AI 1 → AudioOutput
with output level = input level + |H(1 kHz)|, live bypass at 25 % = −12 dB, no dropouts.

## 7. Open questions

- Which pre-fill and rate pairing ELVISmx uses on the myDAQ: see 8.3 (two candidates; MYBENCH runs
  both tasks at one rate).
- The display weighting index (3) of the ELVISmx spectrum: 8.2. MYBENCH shows unweighted dBV.
- Never launched: 8.5 lists what a manual session must confirm.

## 8. ELVISmx reference (not launched)

Source tags: **FP** = the panel as ELVISmx shows it; **VI** = ELVISmx application behaviour (defaults,
filter configuration, call order); **SVT** = NI Sound and Vibration library behaviour. Confidence H / M / L. All wording is ours.

### 8.1 Panel

| Element | ELVISmx | Source, conf. |
|---|---|---|
| Device | `Select Device` combo, grayed while running; `No supported hardware` message when none | FP, VI, H |
| Routing | four combos `Input (L)`, `Input (R)`, `Output (L)`, `Output (R)`; lists are filled at run time from the device (myDAQ: `ai0`, `ai1`, `audioInputLeft`, `audioInputRight`, `none` / `ao0`, `ao1`, `audioOutputLeft`, `audioOutputRight`, `none`); selected defaults not read | FP, VI constants H (lists); defaults unknown |
| Tone sliders | `Bass`, `Midtone`, `Treble`: -12 ... +12 dB, step 1, labeled ticks -10 / 0 / 10, **saved value +1 dB each** | FP, VI, H |
| Volume | slider 0 ... 1, end captions `Min` / `Max`; saved 0.4965 (FP) and 0.5 (second constant) | FP, VI, H (M which one shows first) |
| Graphs | tab pair `Time` / `Frequency`, each with its own `Auto-scale graph` checkbox (both on) | FP, H |
| Time graph | X 0 ... 0.1 s (grid 0.02 s), Y at rest +-1 mV then autoscaled, SI-formatted ticks, two plots (blue and red) | FP, H (which plot is input/output: L) |
| Frequency graph | X log 25 Hz ... 25 kHz (grid marks 250, 2500), Y from -140 in 20 dB steps, autoscale | FP, H |
| Run | Start, Stop, Help >> (Introduction / How to Use) | FP, H |
| Not present | level meters, bypass, clip indicator, tone-response plot, output-buffer setting: these are MYBENCH additions | FP, H |

### 8.2 Signal path and constants

- Two DAQmx tasks: AI (voltage, **+-2 V**) and AO (voltage, **+-2 V**), virtual channel names
  `Audio Input` / `Audio Output`. AO sample clock 50 kS/s, continuous, **regeneration off**, buffer
  1 000 000 samples. AI reads **10 000 samples** per channel per loop (0.2 s at 50 kS/s), timeout
  10 s. AO is pre-filled before start with `delay x rate` samples. (VI, H)
- Filters (three configured NI filter blocks): IIR **Bessel order 4**; Bass = lowpass
  **320 Hz**; Midtone = bandpass **320 ... 2560 Hz**; Treble = bandpass **2560 ... 20 840 Hz**.
  Band gain 10^(dB/20); sum of the bands; times Volume; written to AO. (VI, H; summation order M)
- Type-conversion blocks only convert between waveform and dynamic-data types. (H)
- Frequency display: NI 1/24-octave analysis, **25 Hz ... 25 000 Hz**, averaging type
  Exponential with **Custom 10 ms**, equal-confidence 1 dB (unused), dB on, **weighting value 3**.
  The value 3 is the same enumeration as the Octave Analyzer's weighting ring (Linear, A, B, C) *if*
  the ring is copied straight, which would mean **C-weighting**; not resolved (L).
- Time graph shows the waveform block; 100 ms of it fits the axis (M).

### 8.3 Two run configurations

The VI reads back the actual sample-clock rate and compares it with **99 000 Hz** in three places.
Two constant sets exist: (A) AI = AO = 50 kS/s, AI-to-AO delay **0.25 s**; (B) AI **100 kS/s**,
AO **50 kS/s**, delay **0.35 s**. Most likely B is tried first and A is the fallback when the
read-back AI rate is below 99 kS/s (audio inputs?), but the wiring was not traced (L).
MYBENCH: single rate for both tasks, default 50 kS/s, buffer 250 ms (audit aeq #5).

### 8.4 MYBENCH compared

Matches (H): sliders and ranges, Bessel bands and corner frequencies, +-2 V, AO no-regeneration,
1/24-octave 25 Hz ... 25 kHz display with an exponential average. Differences: Time view 20 ms
vs ELVISmx 100 ms window; no per-graph autoscale checkboxes; default gain +1 dB is right, Volume
default 50 % is right; MYBENCH adds meters, bypass, tone-response plot, buffer setting.

### 8.5 To confirm in a manual session

Defaults of the four routing combos on a myDAQ; which rate/delay configuration runs for
AI, for audio, and mixed; what the two time-graph plots and the frequency plot show (input,
output, or both); weighting on the spectrum; behaviour when Input and Output use the same
channel type; audio-out latency measured input-to-output.
