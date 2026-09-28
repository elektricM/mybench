# Counter/Timer (`ctr`)

Read [app.md](app.md) first (shell, run bar, inspector cards, ResourceArbiter, export). Hardware
facts: [`docs/device.md`](../device.md) ("Counters / timers"), API: `core/include/mydaq/device.h`
(`CounterInputTask`, `CounterOutputTask`, FREQOUT).

**[MYBENCH extension]**: ELVISmx has no counter instrument. This panel exposes the myDAQ's
counter/timer `ctr0` and its FREQOUT generator, which NI-DAQmx offers but no ELVISmx program uses.

## 1. Purpose

- Measure digital signals on the DIO screw terminals: frequency, period, pulse width,
  semi-period, two-edge separation, edge counting and quadrature encoders (linear/angular).
- Generate a pulse train (PWM) with `ctr0`.
- Generate a square wave with FREQOUT, alone or while `ctr0` measures (self-test: jumper DIO 4 to
  DIO 1 and measure it).

## 2. Functions and terminals

`ctr0` does one thing at a time (**Counter** card: `Measure`, `Pulse Train`, `Off`); FREQOUT
(**FREQOUT** card, header switch) runs independently in the same run. Default terminals (fixed on
the myDAQ, `device.h`):

| Function | DIO lines (role) |
|---|---|
| Frequency, Period, Pulse Width, Semi-Period | DIO 1 (input) |
| Two-Edge Separation | DIO 2 (first edge), DIO 1 (second edge) |
| Edge Count | DIO 0 (edges); + DIO 1 (count reset) when *Reset on DIO 1*; + DIO 2 (direction) when *External* |
| Linear / Angular Encoder | DIO 0 (A), DIO 2 (B); + DIO 1 (Z) when *Z Index* is on. Two-pulse decoding: DIO 0 counts up, DIO 2 down |
| Pulse Train | DIO 3 |
| FREQOUT | DIO 4 |

Terminal names shown on the pins: DIO 0 = CTR 0 SOURCE, DIO 1 = CTR 0 GATE, DIO 2 = CTR 0 AUX,
DIO 3 = CTR 0 OUT, DIO 4 = FREQOUT.

**Resources**: `Counter` when `ctr0` is used, `FrequencyOutput` when FREQOUT is on, and
`DigitalLines` with exactly the mask of the table (e.g. Frequency = `0x02`, Edge Count external +
reset = `0x07`, Pulse Train + FREQOUT = `0x18`). The mask follows the settings (declared when idle,
refreshed at every start). Starting pauses whoever holds a needed line (e.g. Digital I/O).

## 3. Panel

Display: header `<function> · DIO lines · Timebase`; Measure: big readout (value + unit, secondary
quantity: period for Frequency, frequency for Period, edge rate for Edge Count, velocity/rpm for
encoders), statistics row (Min, Max, Mean, Std Dev, Readings/Intervals), trend chart (time axis,
follows the latest value, mean line), pin map. Pulse Train: actual frequency and duty cycle
readouts (period, high and low time), a diagram of the first pulses (idle level, initial delay,
finite end), pin map. `Off` + FREQOUT: big FREQOUT readout. A FREQOUT chip is shown beside a
running counter function. The pin map shows each DIO line's role, colour-coded (input green, pulse
amber, FREQOUT cyan), lines held by another instrument, and conflicts ("held by DIO" + a warning
line "Run pauses it").

### 3.1 Measure settings

| Control | Values | Default | Live |
|---|---|---|---|
| Measurement | Edge Count, Frequency, Period, Pulse Width, Semi-Period, Two-Edge Separation, Linear Encoder, Angular Encoder | Frequency | restart |
| Active edge (Starting edge for Pulse Width: Rising = high time; Edges to Count; First Edge) | Rising, Falling (hidden for Semi-Period and encoders) | Rising | restart |
| Second Edge (Two-Edge Separation) | Rising, Falling | Falling | restart |
| Count Direction | Up, Down, External (DIO 2 high = up) | Up | restart |
| Initial Count | 0 … 2^32 − 1 | 0 | restart |
| Reset on DIO 1 | off/on (rising edge sets the count to 0) | off | restart |
| Decoding (encoders) | X1, X2, X4, 2-Pulse | X4 | restart |
| Distance / Pulse, Units (linear) | > 0; free unit text (8 chars) | 1, `mm` | restart / display |
| Pulses / Revolution (angular) | 1 … 2^24 | 1024 | restart |
| Z Index (DIO 1), Z Index Value | off/on; position loaded at the index | off, 0 | restart |
| Expected Range Minimum/Maximum | Frequency: Hz; other interval measurements: s | 1 Hz … 1 MHz; 100 ns … 10 s | restart |
| Sampling | On Demand, Buffered (interval measurements only) | On Demand | restart |
| Update Interval (on demand) | 10 ms … 10 s | 100 ms | restart |
| Samples (Single, buffered) | 1 … 10^6 | 1000 | next run |

- **Timebase**: the expected range picks the timebase as the device does: the fastest of
  100 MHz / 20 MHz / 100 kHz whose 32-bit count covers the longest expected interval (a period of
  the minimum frequency, or the maximum time). Shown with its resolution and longest interval.
- **On Demand**: a reading of the latest completed measurement every update interval (statistics
  are over readings). **Buffered**: every period/interval is measured once (exact statistics,
  jitter); Run = continuous, Single = the given number of intervals. Counting measurements have
  no buffered mode on the myDAQ (no sample clock): the option is disabled.
- Edge counts are followed as a signed count (counting down past 0 gives −1, not 4294967295).
- No input edge yet: the run bar's **No input** LED lights (on-demand timeouts, or > 2 s without
  a buffered value).
- **Reset stats** / **Reset count** (run bar): clears statistics and trend; for counting
  measurements it also restarts the count from the initial value.

### 3.2 Pulse Train (DIO 3)

| Control | Values | Default | Live |
|---|---|---|---|
| Define By | Frequency & Duty, High & Low Time | Frequency & Duty | display |
| Frequency | 100 MHz / 2^32 (0.023 Hz) … 25 MHz | 1 kHz | live (continuous) |
| Duty Cycle | 0.01 … 99.99 % | 50 % | live (continuous) |
| High Time / Low Time | ≥ 20 ns each (sets frequency and duty) | 500 µs / 500 µs | live (continuous) |
| Idle State | Low, High | Low | restart |
| Initial Delay | 0 … 100 s | 0 | restart |
| Generation | Continuous, Finite | Continuous | restart |
| Pulses (finite) | 1 … 10^9 | 100 | restart |

Frequency is coerced to 100 MHz / n (n ≥ 4) and the duty cycle so that high and low last ≥ 2
ticks; the actual values (and n) are shown. A finite train ends the run after the last pulse
(status `Done`). Single is hidden in this mode.

### 3.3 FREQOUT (DIO 4)

Square wave of 20 MHz, 10 MHz or 100 kHz divided by 1 … 16 (40 distinct frequencies, 20 MHz …
6.25 kHz). Frequency field (coerced to the nearest), **Available** list, actual value and divider
("100 kHz ÷ 8"). Frequency changes apply live; switching FREQOUT on/off restarts the run.

## 4. Run semantics

Run starts FREQOUT (if on) and the `ctr0` function; Stop ends both (output lines back to idle,
lines released). Single (Measure only): one reading, or N buffered intervals. Nothing to run
(`Off` and FREQOUT off): validation message. Settings that need a new task restart a running
panel.

## 5. Export

Export data: the trend (Time from the first value in s, value in its unit) with metadata
(function, terminals, sampling, timebase, statistics, FREQOUT).

## 6. Tile

Current value (or pulse/FREQOUT frequency), mini trend, terminals.

## 7. Checks against the simulator (automated: `instruments/ctr/tests`)

- DIO 1 clock 1 kHz: Frequency reads 1000 Hz, lines `0x02`; export columns `Time (s),Frequency (Hz)`.
- FREQOUT jumpered to DIO 1: measured value follows FREQOUT live (12.5 kHz, then 1.25 MHz).
- Buffered Period, Single 50: mean 1 ms, σ ≈ 0.
- Edge Count external direction low: count goes negative.
- Encoder at 1 rev/s: angle and rpm readings.
- Pulse train: live frequency/duty update; finite train ends by itself.
- Terminal masks equal the device's `dioLinesUsed()` for every configuration.

## 8. Open questions

- The timebase rule mirrors the simulator; the real backend may choose differently (then the
  display should read it from the task).
- Real-hardware buffered rates above ~1 MHz of intervals are untested.
