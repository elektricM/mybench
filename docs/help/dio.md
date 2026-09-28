# Digital I/O

The Digital I/O panel reads and drives the eight digital lines of the myDAQ, DIO 0 to DIO 7. It
combines the classic **Digital Reader** (watch the level of some lines) and **Digital Writer** (set
lines high or low by hand, or play a ramp, toggle or walking-ones sequence). Both work at the same
time on different lines, so you can, for example, drive lines 0-3 and read them back on lines 4-7.

A timeline under the line cards records the state of every line over the last seconds, like a
simple logic analyzer.

## Connections

- The lines are on the myDAQ screw terminals `DIO 0` ... `DIO 7`; use `DGND` as the ground reference.
- Outputs are 3.3 V logic (compatible with TTL inputs) and can only supply a few milliamps: drive
  LEDs through a resistor (for example 330 Ω - 1 kΩ) and never connect an output directly to a
  supply or to another output.
- Inputs accept 0 V to 5 V logic signals. An unconnected input may read either level.
- A line is either an input or an output, never both.

## Quick start

1. Open **DIO** from the navigation rail.
2. By default lines **4 - 7** are read and lines **0 - 3** are written (Manual pattern).
3. Press **Run** (or `Space`).
4. Flip the switches on the output cards: the lines change at once and the LEDs follow.
5. To see your outputs on the inputs, wire DIO 0-3 to DIO 4-7: the input LEDs and the `Inputs`
   value follow the switches.

## Controls

### Line cards and values

Each card shows one line: its name (`DIO 7` ... `DIO 0`, most significant on the left), an LED
(lit = high), and its direction (`IN`, `OUT` or `–` when unused). Output cards have a **HI/LO switch**.
**Inputs** and **Outputs** show the lines of each function as one number; click the small prefix
letter to choose **Decimal**, **Hex**, **Octal**, **Binary** or **SI Notation**. The lowest line of a
set is bit 0 (lines 4-7 all high give `x F`).

### Line Directions

Set each line to **In**, **Out** or **–** (unused). The presets below do the same for whole groups.

### Digital Reader

**Lines to Read**: `0 - 3`, `4 - 7`, `0 - 7` (or `None` / `Custom`). Choosing a group that overlaps
the written lines moves those lines to reading (a message tells you).

### Digital Writer

- **Lines to Write**: `0 - 3`, `4 - 7`, `0 - 7` (or `None` / `Custom`).
- **Pattern**:
  - **Manual** - you set each line with its switch or type a value in **Pattern Number**.
  - **Ramp** - counts 0, 1, 2 ... up to the largest value, then starts again.
  - **Toggle** - alternates between 0101... and 1010...
  - **Walking 1's** - a single high line moves from the lowest to the highest line.
- **Action** (Manual only): **Toggle** inverts every line, **Rotate** moves the pattern one line in
  the chosen **Direction** (the bit that falls off comes back on the other side), **Shift** moves it
  one line and fills with 0.
- **Interval (ms)**: time each value of a Ramp/Toggle/Walking sequence is held (default 200 ms).

### Timeline

Shows the last 10 s of all lines (choose 5-60 s). **Sample interval** sets how often the lines are
read (default 100 ms). Pause freezes the view, Clear empties it; two cursors show times and Δt.

### Run bar

- **Run** - read continuously and generate the pattern until you press **Stop**.
- **Single** - read once; a Manual pattern is written once, a sequence is played one time.
- **Stop** - reading and sequences stop; outputs keep their last level until you change their
  direction, close the panel or quit.

## Tips

- The classic exercise: Writer on 0 - 3 with Ramp, wires from DIO 0-3 to DIO 4-7, Reader on 4 - 7:
  both values count together.
- Keys `0` to `7` toggle the manual output lines while the panel has focus.
- Pin the DIO tile to the dashboard to keep an eye on the lines while you use other instruments.
- **Export data** saves the timeline as CSV (time and the level of every line).

## Troubleshooting

- **An output does not change the LED of a connected input**: check the wiring and DGND; the card
  shows a warning mark if an output reads back a different level (short circuit or overload).
- **Short pulses are missing**: the lines are read by software every sample interval (at best about
  10 ms), so faster pulses can be missed. This panel is not a hardware logic analyzer.
- **Inputs flicker when nothing is connected**: floating inputs are undefined; tie them to DGND or
  3.3 V through a resistor.
- **"Select lines to read or to write first."**: all lines are unused; choose a preset.
