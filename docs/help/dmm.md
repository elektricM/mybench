# Multimeter

The Multimeter uses the myDAQ's built-in, electrically isolated meter (the three banana jacks) to
measure DC and AC voltage, DC and AC current, resistance, diode forward voltage and continuity. It
works independently of the analog inputs and outputs, so you can use it while the Oscilloscope,
Generator or any other instrument is running.

The Multimeter starts measuring as soon as you open it (DC Voltage).

## Connections

The myDAQ has three banana jacks for the meter:

| Jack | Used for |
|---|---|
| **HI (V Ω ⊣▶)** – red, left | voltage, resistance, diode, continuity |
| **COM** – black, middle | common (always used) |
| **HI (A)** – red, right | current |

- **Voltage, resistance, diode, continuity**: red lead in HI (V), black lead in COM.
- **Current**: red lead in HI (A), black lead in COM, and put the meter *in series* with the load.
- The picture in the Measurement Settings section always shows the two jacks to use for the selected
  function.

Safety: the meter is rated for low-voltage laboratory circuits only (up to 60 V DC / 20 V AC RMS).
The current input is protected by a fuse (1 A max). Never connect the current jack across a voltage
source. Measure resistance, diodes and continuity only on circuits that are switched off.

## Quick start

1. Plug the leads into the jacks shown in the connection picture.
2. Choose a function (DC Voltage is selected at start).
3. Leave **Mode** on *Specify Range* with the largest range, or choose *Auto* to let the meter pick
   the best range.
4. Read the value on the big display. The bar under it shows how much of the range is used.

## Controls

### Measurement Settings

- **Function** – DC Voltage, AC Voltage, DC Current, AC Current, Resistance, Diode, Continuity.
  Capacitance and Inductance are shown for completeness but are not available on the myDAQ.
- **Mode** – *Auto* chooses the range automatically (the display shows `AUTO` and the range in use);
  *Specify Range* uses the range you choose.
- **Range** – the full-scale value. Ranges available:
  - DC voltage: 60 V, 20 V, 2 V, 200 mV
  - AC voltage (RMS): 20 V, 2 V, 200 mV
  - DC and AC current: 1 A, 200 mA, 20 mA
  - Resistance: 20 MΩ, 2 MΩ, 200 kΩ, 20 kΩ, 2 kΩ, 200 Ω
  - Diode (2 V) and Continuity have a fixed range.
- **Null Offset** – relative mode. When you switch it on, the next reading is stored as the null
  value and later readings are shown relative to it (useful to cancel the resistance of the test
  leads or an offset). The null value is shown in the display. Each function keeps its own null
  value. Switch it off and on again to take a new null value.

### Display

- The main readout shows the value with a resolution fixed by the range (3½ digits).
- **% FS** – percentage of the range in use. A low percentage means a smaller range would give more
  resolution.
- **+OVER / -OVER** – the input is above (or below) the selected range: choose a larger range or
  Auto.
- **Diode GOOD / OPEN** – in Diode mode: GOOD when the forward voltage is below 2 V, OPEN when the
  diode is open or connected backwards.
- **Continuity** – the computer beeps (and the indicator lights) while the resistance is below 15 Ω.

### Run bar

- **Run / Stop** – measure continuously, or stop.
- **Single** – take one reading and stop.

### Trend, statistics and logging

- **Trend** – a chart of the recent readings.
- **Statistics** – minimum, maximum, average and standard deviation since the last **Reset**.
- **Hold** – freezes the displayed value while measuring continues.
- **Record / Export** – records every reading with its time stamp and saves the table as CSV.

## Tips

- AC functions display the RMS value and take longer per reading than DC functions.
- In Auto mode the first reading of a function starts on the largest range, so it may take a moment
  to settle on small signals.
- For small resistances, short the leads, switch Null Offset on, then measure: the lead resistance is
  removed.
- Readings are shown at most five times per second.

## Troubleshooting

- **The display shows `---`**: no reading yet (just started or the function changed), or the meter
  stopped because of an error — check the notification and press Run.
- **Always +OVER in resistance or diode mode**: the leads are open, or the component is not
  connected between HI (V) and COM.
- **Current reads zero**: the red lead must be in the HI (A) jack, and the fuse may be blown.
- **Nothing happens at start**: no device is connected; select one in the top bar.
