# Audio Equalizer (`aeq`)

Read [app.md](app.md) first. Behaviour matches the ELVISmx featured application. MYBENCH version of the ELVISmx **Audio
Equalizer** featured app: live three-band tone control from an input to an output.

## 1. ELVISmx parity

| ELVISmx | Values / default | MYBENCH |
|---|---|---|
| Input (L) / Input (R) | ai0, ai1, audioInputLeft, audioInputRight, none | **Input** card, same list; default AudioInput Left / Right |
| Output (L) / Output (R) | ao0, ao1, audioOutputLeft, audioOutputRight, none | **Output** card; default AudioOutput Left / Right |
| Bass / Midtone / Treble | −12 … +12 dB, step 1; default 1 dB | faders + dB fields (0.1 dB entry), default +1 dB each |
| Volume | 0 (`Min`) … 1 (`Max`); about 0.5 by default | fader + slider in %, default 50 % |
| Filters | Three bands: low, mid, high (corner frequencies in §2) | same (§2) |
| Output | Σ 10^(dB/20) × band, × Volume | same |
| Streaming, input to output | continuous | ±2 V; AI and AO at the same rate (50 kS/s default, 100 / 200 kS/s); **Output Buffer** 50 / 100 / 250 (default) / 350 ms |
| "Time" graph | autoscale | **Time** view: 20 ms of input and output, triggered on the input's rising zero crossing |
| "Frequency" graph | 1/24-octave, 25 Hz … 25 kHz, averaged | **Spectrum** view: 1/24-octave bands 25 Hz … min(25 kHz, 0.48 fs), dBV, input and output; averaging 10 ms / 125 ms (default) / 1 s |

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

- ELVISmx's exact input-to-output latency and spectrum weighting are not documented; MYBENCH runs both
  tasks at one rate and shows unweighted dBV.
