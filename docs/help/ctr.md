# Counter/Timer

The Counter/Timer turns the myDAQ's digital terminals into a frequency counter, a timer, an
event counter, an encoder reader and a pulse generator. It uses the myDAQ's counter (ctr0) and
its separate square-wave generator (FREQOUT). There is no equivalent in the classic NI programs.

## Connections

All signals use the DIO screw terminals (3.3 V logic, 5 V tolerant inputs; connect the signal
ground to **DGND**). The **Terminals** strip at the bottom of the display always shows which lines
the current settings use:

| Terminal | Counter signal | Used by |
|---|---|---|
| DIO 0 | CTR 0 SOURCE | Edge Count (edges), encoder A (two-pulse: up) |
| DIO 1 | CTR 0 GATE | Frequency, Period, Pulse Width, Semi-Period (input); Two-Edge second edge; count reset; encoder Z |
| DIO 2 | CTR 0 AUX | Two-Edge first edge; count direction; encoder B (two-pulse: down) |
| DIO 3 | CTR 0 OUT | Pulse train output |
| DIO 4 | FREQOUT | Square-wave output |

A line used by the Digital I/O panel is marked on the strip; pressing Run pauses Digital I/O so
the two never drive the same line.

## Quick start

- **Measure a frequency**: connect the signal to DIO 1, keep *Measure · Frequency*, press **Run**.
- **Generate PWM**: choose *Pulse Train*, set frequency and duty cycle, press **Run**, take the
  signal from DIO 3. Frequency and duty change while it runs.
- **Self-test**: turn on **FREQOUT**, put a wire from DIO 4 to DIO 1 and measure its frequency.

## Measure

- **Measurement**: *Frequency* and *Period* (between two active edges), *Pulse Width* (Rising
  measures the high time, Falling the low time), *Semi-Period* (time between any two edges),
  *Two-Edge Separation* (from an edge on DIO 2 to an edge on DIO 1), *Edge Count*, *Linear* and
  *Angular Encoder*.
- **Edge Count**: count up, down, or *External* (DIO 2 high counts up, low counts down); an
  initial count; *Reset on DIO 1* sets the count to 0 on each rising edge there. Counting down
  below zero shows negative numbers. The secondary line shows the edge rate.
- **Encoders**: quadrature A/B on DIO 0/DIO 2 with X1, X2 or X4 decoding, or *2-Pulse* (separate
  up and down pulses). Linear: distance per pulse and a unit; angular: pulses per revolution
  (degrees). *Z Index* on DIO 1 reloads the *Z Index Value* once per turn. The secondary line
  shows velocity or rpm.
- **Expected Range**: the slowest and fastest values you expect. MYBENCH picks the counter's
  timebase from it (100 MHz, 20 MHz or 100 kHz) and shows its resolution and the longest interval
  it can time.
- **Sampling**: *On Demand* takes the latest measurement every *Update Interval*; *Buffered*
  measures every single period (best for statistics and jitter; Single measures the number of
  *Samples* and stops). Edge counts and encoders are always on demand.
- The big readout shows the value with its unit; below it Min, Max, Mean, Std Dev and the number
  of readings, and a trend chart (drag or scroll to look back). **Reset stats** (or **Reset
  count**) starts over. The **No input** light means no complete measurement has arrived yet.

## Pulse Train (DIO 3)

Enter *Frequency & Duty* or *High & Low Time*, the *Idle State* (level before, between runs and
after), an *Initial Delay*, and *Continuous* or *Finite* (a number of pulses, after which the run
stops by itself). The counter makes frequencies of 100 MHz ÷ n: the actual frequency and duty
cycle are shown, and the diagram draws the first pulses.

## FREQOUT (DIO 4)

A square wave of 20 MHz, 10 MHz or 100 kHz divided by 1 to 16, so from 20 MHz down to 6.25 kHz
in fixed steps. Type any frequency (it snaps to the nearest one) or pick it from **Available**.
FREQOUT runs together with a measurement or a pulse train, or alone (*Counter: Off*).

## Export

**Export ▸ Export data** saves the trend (time and value) with the settings and statistics.
