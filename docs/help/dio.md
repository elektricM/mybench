# Digital I/O

The Digital I/O panel reads and drives the eight digital lines of the myDAQ, DIO 0 to DIO 7. It
combines the classic **Digital Reader** (watch the level of a group of lines) and **Digital Writer**
(set lines high or low by hand, or play a ramp, alternating or walking-ones pattern). Both work at
the same time on different lines, so you can, for example, drive lines 0-3 and read them back on
lines 4-7. A timeline records the state of every line, like a simple logic analyzer.

## Connections

- The lines are on the myDAQ screw terminals `DIO 0` … `DIO 7`; use `DGND` as the ground reference.
- Outputs are 3.3 V logic (compatible with TTL inputs) and supply only a few milliamps: drive LEDs
  through a resistor (330 Ω – 1 kΩ) and never connect an output to a supply or to another output.
- Inputs accept 0 – 5 V logic. An unconnected input may read either level.
- DIO 0 – 4 are also the terminals of the Counter/Timer and of the FREQOUT square wave (DIO 4). The
  panel marks those lines with the instrument's name (see *Lines shared with other instruments*).

## Quick start

1. By default lines **4 - 7** are read and lines **0 - 3** are written with the Manual pattern.
2. Press **Run** (or `Space`).
3. Flip the switches on the output cards (or press `0` … `7`): the lines change at once.
4. Wire DIO 0-3 to DIO 4-7 and the input LEDs and the `Inputs` value follow the switches.

## The display

- **Inputs / Outputs** – the read lines and the written lines as one number each. The lowest line of
  a group is bit 0 ("right-justified": lines 4-7 all high give `x F`). Click the small prefix letter
  to choose Decimal (`d`), Hex (`x`), Octal (`o`), Binary (`b`) or SI notation (`p`).
- **Line cards** – `DIO 7` … `DIO 0`, most significant on the left. Each card shows an LED (lit =
  high; for outputs the level written), the direction (`IN`, `OUT`, `–` unused), the HI/LO switch of
  output lines, your label and, if another instrument uses the line, its name. A warning mark
  appears if an output reads back a different level (short circuit or overload).
- **Timeline** – every line over the last 5, 10, 30 or 60 s (inputs cyan, outputs amber). Buttons:
  window, cursors (two cursors with the time, Δt and the level of all lines at each, also as a number
  in Decimal, Hex, Octal, Binary or SI notation: click the radix letter in the cursor box), pause (freezes
  the view while recording continues) and clear. The last 10 minutes are kept.

## Line Directions

Set each line to **In**, **Out** or **–** (unused), and give it a label (e.g. `LED`, `CLK`). A line
is read or driven, never both. Unused lines are left as high-impedance inputs.

### Lines shared with other instruments

Lines declared by another instrument (Counter/Timer inputs, pulse output on DIO 3, FREQOUT on DIO 4)
carry that instrument's name. While that instrument runs, pressing Run here pauses it (and the other
way round); set the line to `–` to leave it to the other instrument.

## Digital Reader

- **Lines to Read** – `0 - 3`, `4 - 7`, `0 - 7` or `None` (shows `Custom` for any other set). If the
  group overlaps written lines, those lines move to reading and a message says so.
- **Sample interval** – how often the lines are read and added to the timeline (10 ms – 1 s,
  default 100 ms).

## Digital Writer

- **Lines to Write** – `0 - 3`, `4 - 7`, `0 - 7` or `None` (or `Custom`).
- **Pattern**:
  - **Manual** – set each line with its switch, or type a **Pattern Number** (in the chosen radix,
    `−`/`+` step by one).
  - **Ramp (0-15)** / **Ramp (0-255)** – counts 0, 1, 2 … up to the largest value, then again.
  - **Alternating 1/0's** – alternates `0101…` and `1010…` (x 5 / x A on four lines, x 55 / x AA on
    eight).
  - **Walking 1's** – one high line moving through the group, in the chosen **Direction**.
- **Action** (Manual): **Toggle** inverts every line; **Rotate** moves the pattern one line in the
  **Direction** (the bit leaving comes back on the other side); **Shift** moves it one line and
  fills with 0.
- **Step interval** – how long each value of a pattern is held (10 ms – 10 s, default 100 ms).
- **Release** – after Stop the outputs keep driving their last value; Release makes them inputs
  again. Closing the panel, starting a new session or a device reset also returns them to inputs.
- **Pattern Number** – type a value in the chosen radix; a value above the maximum is not accepted
  (the previous one stays). The − / + buttons and the arrow keys step by one.
- **Numeric Value** – `x 0` before the first reading; no leading zeros in any radix.

## Run bar

- **Run** – read continuously and drive the outputs until **Stop**. Settings, patterns and even the
  line directions can be changed while running (a pattern then restarts from its first value).
- **Single** – reads the lines, writes the Manual value once or plays a pattern one time (one step
  interval per value), reads again, then stops. Pressed while running it ends the run after the
  reading in progress.
- **Stop** – reading and patterns stop; outputs keep their last level until you change their
  direction, press Release or quit. If another instrument takes a line, the outputs are released.

## Export

**Export ▾ → Export data…** saves the timeline (time and the 0/1 level of every line, one row per
sample) with the directions and the sample interval in the header. Export image/PDF saves the
display.

## Tips

- Classic exercise: Writer on 0 - 3 with Ramp, wires from DIO 0-3 to DIO 4-7, Reader on 4 - 7:
  both values count together.
- With the Manual pattern, clicking an output LED on the dashboard tile flips it.

## Troubleshooting

- **Short pulses are missing**: the lines are read by software every sample interval (at best about
  10 ms), so faster pulses can be missed. This panel is not a hardware logic analyzer.
- **Inputs flicker when nothing is connected**: floating inputs are undefined; tie them to DGND or
  3.3 V through a resistor.
- **"Select lines to read or to write first."**: every line is unused; choose a preset.
- **The Counter/Timer paused**: it used one of the lines you run here; set that line to `–` and
  resume the counter.
