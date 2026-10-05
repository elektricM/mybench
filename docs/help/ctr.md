# Counter/Timer

The Counter/Timer turns the myDAQ's digital terminals into a frequency counter, a timer, an
event counter, an encoder reader and a pulse generator. It uses the myDAQ's counter (ctr0) and
its separate square-wave generator (FREQOUT). There is no equivalent in the classic NI programs.

## Connections

All signals use the DIO screw terminals (3.3 V logic, 5 V tolerant inputs; connect the signal
ground to **DGND**). The **Connections** tool (at the bottom of the right column) opens the pin
map of the current settings over the display:

| Terminal | Counter signal | Used by |
|---|---|---|
| DIO 0 | CTR 0 SOURCE | Edge Count (edges), encoder A (two-pulse: up) |
| DIO 1 | CTR 0 GATE | Frequency, Period, Pulse Width, Semi-Period (input); Two-Edge second edge; count reset; encoder Z |
| DIO 2 | CTR 0 AUX | Two-Edge first edge; count direction; encoder B (two-pulse: down) |
| DIO 3 | CTR 0 OUT | Pulse train output |
| DIO 4 | FREQOUT | Square-wave output |

A line used by the Digital I/O panel is marked on the pin map; pressing Run pauses Digital I/O so
the two never drive the same line.

## What your device supports

Options the connected device cannot do are not shown. On the NI myDAQ these are counting with an *External* direction and Buffered sampling
(the myDAQ's counter can only be read on demand). Everything else runs on the device: Pulse
Width, Semi-Period (measured as the high and low halves of each period), Two-Edge Separation
(first edge on DIO 2, second on DIO 1), the encoders, the Falling edge, *Initial Count*, *Reset
on DIO 1*, the pulse train's *Idle State High* and *Initial Delay*, frequency and duty changes of
a running pulse train (applied at the next period, without stopping), and FREQOUT from 6.25 kHz
to 20 MHz (20 MHz / n, 10 MHz / n or 100 kHz / n). The myDAQ itself only counts up: MYBENCH
counts down by subtracting the edges from the Initial Count, so counting down from a non-zero
Initial Count together with *Reset on DIO 1* is refused at Run with a message. If a setting saved
earlier needs a hidden option, the Counter card lists it and Run is refused with a warning
instead of failing on the device. The simulator supports everything.

## Quick start

The pane is laid out like a bench instrument: **Run**, **Single** and **Reset** at the top, the
display on the left (big readout, trend, statistics cards), the controls in a column on the
right: the **Counter** card with one picture button per mode, the card of the active mode's
settings, the **Pulse Train** and **FREQOUT** cards, then the tools.

- **Measure a frequency**: connect the signal to DIO 1, click **Frequency**, press **Run**.
- **Generate PWM**: click **Pulse Train**, turn the frequency and duty-cycle knobs (or type a
  value), press **Run**, take the signal from DIO 3. Frequency and duty change while it runs.
- **Self-test**: switch **FREQOUT** on, put a wire from DIO 4 to DIO 1 and measure its frequency.

Knobs: drag or scroll to turn them, double-click to go back to the default, type a value in the
field beside them, or use − / +.

## Measure

- **Mode buttons**: *Frequency* and *Period* (between two active edges), *Pulse Width* (Rising
  measures the high time, Falling the low time), *Semi-Period* (time between any two edges),
  *Two-Edge* separation (from an edge on DIO 2 to an edge on DIO 1), *Edge Count*, *Linear* and
  *Angular* encoder; *Pulse Train* and *FREQOUT Only* (the counter unused) complete the row.
- **Edge Count**: count up, down, or *External* (DIO 2 high counts up, low counts down); an
  initial count; *Reset on DIO 1* sets the count to 0 on each rising edge there. Counting down
  below zero shows negative numbers. The secondary line shows the edge rate.
- **Encoders**: quadrature A/B on DIO 0/DIO 2 with X1, X2 or X4 decoding, or *2-Pulse* (separate
  up and down pulses). Linear: distance per pulse and a unit; angular: pulses per revolution
  (degrees). *Z Index* on DIO 1 reloads the *Z Index Value* once per turn. The secondary line
  shows velocity or rpm.
- **Expected Range**: three buttons, one per counter timebase (100 MHz, 20 MHz, 100 kHz): the
  slower the slowest expected value, the slower the timebase. Frequency: 1 Hz – 1 MHz,
  10 mHz – 1 MHz, 100 µHz – 1 kHz; times: 100 ns – 10 s, 1 µs – 100 s, 100 µs – 10 ks. The
  tooltip shows the timebase's resolution and the longest interval it can time.
- **Acquisition**: *On Demand* takes the latest measurement every *Update interval* (knob); *Buffered*
  measures every single period (best for statistics and jitter; Single measures the number of
  *Samples* and stops). Edge counts and encoders are always on demand.
- The big readout shows the value with its unit and the secondary quantity, then a trend chart
  (time window 10 s, 1 min, 10 min or All in its toolbar; drag or scroll to look back) and the
  statistics cards: Min, Max, Mean, σ and Count. **Reset
  Stats** (or **Reset Count**) starts over. The trend also starts empty with every new run. The
  Run button says **No input** while no complete measurement has arrived.

## Pulse Train (DIO 3)

Set *Freq & Duty* (two knobs) or *High & Low* times (two knobs), the *Idle State* (level before, between runs and
after), an *Initial Delay*, and *Continuous* or *Finite* (a number of pulses, after which the run
stops by itself). The counter makes frequencies of 100 MHz ÷ n: the actual frequency and duty
cycle are shown, and the diagram draws the first pulses.

## FREQOUT (DIO 4)

A square wave of 20 MHz, 10 MHz or 100 kHz divided by 1 to 16, so from 20 MHz down to 6.25 kHz
in fixed steps. Switch it on in the header of its card; the knob steps through the available
frequencies, and a typed frequency snaps to the nearest one. While it runs, its actual frequency
is shown at the top right of the display. FREQOUT runs together with a measurement or a pulse
train, or alone (*FREQOUT Only*). When a finite
pulse train has sent its last pulse, the run stays active while FREQOUT is on, until you press Stop.

## Export

**Export ▸ Export data** saves the trend (time and value) with the settings and statistics.
