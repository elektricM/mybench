# Multimeter

The Multimeter uses the myDAQ's built-in, electrically isolated meter (the three banana jacks) to
measure DC and AC voltage, DC and AC current, resistance, diode forward voltage and continuity. It
works independently of the analog inputs and outputs, so you can use it while the Oscilloscope,
the Generator or any other instrument is running.

It opens in DC Voltage on the 60V range and waits for you to press **Run** (an option in the
**Options** tool starts it automatically when the panel is shown).

The panel has the readout and the trend on the left, the controls in a column on the right
(function, range, tools) and the statistics cards at the bottom. Run, Single, Hold and Relative sit
in the bar at the top. In a narrow pane (split screen) the column gets narrower and the tool
buttons show icons only.

## Connections

The myDAQ has three banana jacks for the meter:

| Jack | Used for |
|---|---|
| **HI (V Ω)** – red, left | voltage, resistance, diode, continuity |
| **COM** – black, middle | common (always used) |
| **HI (A)** – red, right | current |

- **Voltage, resistance, diode, continuity**: red lead in HI (V Ω), black lead in COM.
- **Current**: red lead in HI (A), black lead in COM, and put the meter *in series* with the load.
- The **Connections** tool (right column) shows the jacks and highlights the two to use for the
  selected function.

Safety: the meter is meant for low-voltage laboratory circuits only (60 V DC / 20 V AC RMS at
most). The current input is fused (1 A). Never connect the current jack across a voltage source.
Measure resistance, diodes and continuity only on circuits that are switched off.

## Quick start

1. Plug the leads into the jacks shown by the **Connections** tool.
2. Choose a function with the picture buttons (keys `1` … `7` select them in order).
3. Choose a range: **Auto** lets the meter pick it (it goes to a smaller range only when the value
   is clearly below it, and every range change makes the real myDAQ pause for about 2.5 s while it
   settles); any other button fixes that range.
4. Press **Run** (or `Space`) to measure continuously, or **Single** for one reading.

## Function and range (right column)

- **Function** – DC V, AC V, DC A, AC A, Ω (resistance), Diode and Continuity. (The myDAQ
  cannot measure capacitance or inductance, so there are no buttons for them.)
- **Range** – **Auto**, then the ranges of the function from the smallest to the largest. Auto is
  kept when you change the function. Continuity always ranges automatically (only Auto is shown):
  - DC voltage: 200mV, 2V, 20V, 60V · AC voltage (RMS): 200mV, 2V, 20V
  - DC and AC current: 20mA, 200mA, 1A
  - Resistance: 200Ω, 2kΩ, 20kΩ, 200kΩ, 2MΩ, 20MΩ
  - Diode: 2V · Continuity: no range to choose

  Changing the function selects its largest range again. In Auto the line under the readout
  shows the range in use.
- **Relative** (top bar) – null offset. When you switch it on, the next valid reading becomes the
  null value (shown under the readout) and later readings are shown minus that value.
  Use it to cancel the resistance of the test leads or an offset. Switch it off and on again to take
  a new null value; changing the function switches it off.

## The display

- **Readout** – the value with the resolution of the range (3½ digits: e.g. `1.235 V` on 2V,
  `12.3 mA` on 200mA, `4.70 kΩ` on 20kΩ, `12.34 V` on 60V). `---` means no reading yet.
- **% FS** – how much of the range the raw reading uses (0 – 100 %). A small percentage means a
  smaller range would give more resolution.
- **+Over** – the input is beyond the range: choose a larger range or Auto.
- **Under the readout** – only what the controls do not show: the range Auto picked, the null
  value while Relative is on, and the diode state.
- **Trend** – the readings over the last 1 min, 10 min, 1 h or everything (time window and Clear
  in the chart's toolbar). Overloads leave gaps. Drag to pan, scroll to zoom, double-click to
  return. The trend restarts when you change the function.

### Diode

The meter drives 1 mA through the diode and shows its forward voltage (about 0.6 – 0.7 V for a
silicon diode). An open or reversed diode shows `OPEN` in the readout.

### Continuity

Below 15 Ω the circuit is considered closed: a continuous 1 kHz tone plays on the computer's sound
output and the status line says `Continuity` (the dashboard tile glows too). At 15 Ω or more
`Open` shows next to the readout (beyond the range: `+Over` as well). The tone can be turned off in
the **Options** tool.

## Top bar

- **Run / Stop** – measure continuously (at most five readings per second; the real myDAQ delivers two per second for V, A and Ω, about ten for diode and continuity), or stop. The last
  reading stays displayed.
- **Single** – take one reading and stop.
- **Hold** – freezes the readout; measuring, trend, statistics and logging continue.
- **Relative** – null offset (see above).

## Statistics cards

Minimum, maximum, mean, standard deviation σ and count of the displayed values since the last
**Reset** or function change (relative values while Relative is on). Overloads are counted
separately (shown with the count) and left out of the numbers.

## Logging (tool)

- **Record** – every reading is added to a log (time, elapsed seconds, function, range, value,
  unit, overload, relative, null value). The row count is shown; the log keeps rows across function
  changes.
- **Also write to file** – choose a file and every recorded reading is appended to it at once, so
  long recordings are safe. Files are tab-separated text (or CSV if the name ends in `.csv`).
- **Save log…** – writes the whole log to a file; **Clear** empties it.
- **Export ▾ → Export data…** in the display header saves the readings of the current function
  (elapsed time, value, overload) as CSV; Export image/PDF saves the display.

## Tips

- AC functions display the RMS value and take longer per reading than DC functions.
- In Auto the meter starts on the largest range and re-reads at once on the range that fits, so
  the first value shown is already on the right range.
- For small resistances, short the leads, switch on Relative, then measure: the lead resistance
  is removed.

## Troubleshooting

- **The display shows `---`**: no reading yet (just started or the function changed), or the meter
  stopped because of an error — check the message and press Run.
- **Always +Over in resistance or diode mode**: the leads are open, or the part is not connected
  between HI (V Ω) and COM.
- **Current reads zero**: the red lead must be in the HI (A) jack; the fuse may be blown.
- **No continuity tone**: check the tone option and the computer's sound output; the status line
  still says `Continuity`.
