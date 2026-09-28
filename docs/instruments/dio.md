# Digital I/O (`dio`)

Read [app.md](app.md) first (application shell, run bar, inspector cards, ResourceArbiter,
notifications, export, SiField, plots) and the hardware constraints in [README.md](README.md).
[`docs/UI.md`](../UI.md) is the overall UI design.

## 1. Purpose

One panel for the eight myDAQ digital lines DIO 0..7. It merges the two ELVISmx instruments
**Digital Reader** (show the levels of a group of lines, continuously or once) and **Digital Writer**
(drive a group of lines with a manual pattern or a predefined sequence: Ramp, Toggle, Walking 1's).
Because both functions live in one panel, reading and writing can use disjoint lines at the same
time (the classic lab exercise: write on 0 - 3, loop the wires back to 4 - 7, read on 4 - 7).
MYBENCH generalizes the ELVISmx group presets to a per-line direction and adds a logic-analyzer-like
timeline of the line states.

Output levels are 3.3 V LVTTL (TTL compatible); inputs accept 0-5 V logic. All I/O is static and
software-timed (see section 5).

Reference screenshots (informative): `ni-local/screens/DigReader.png`, `DigWriter.png`.

## 2. MYBENCH design notes

### 2.1 Panel anatomy

```
┌───────────────────────────────────────────────────────────────┬───────────────────────────┐
│ DISPLAY (dark)                                                │ INSPECTOR (cards)         │
│  Inputs  x 5 [radix]            Outputs  x A [radix]          │ ▾ Line Directions          │
│ ┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐┌──────┐│   DIO 7 [In|Out|–] ...    │
│ │DIO 7 ││DIO 6 ││DIO 5 ││DIO 4 ││DIO 3 ││DIO 2 ││DIO 1 ││DIO 0 ││ ▾ Digital Reader           │
│ │ (●)  ││ (●)  ││ (○)  ││ (●)  ││ (●)  ││ (○)  ││ (●)  ││ (○)  ││   Lines to Read [4 - 7 v] │
│ │  IN  ││  IN  ││  IN  ││  IN  ││ OUT  ││ OUT  ││ OUT  ││ OUT  ││ ▾ Digital Writer           │
│ │      ││      ││      ││      ││ [HI] ││ [LO] ││ [HI] ││ [LO] ││   Lines to Write [0 - 3 v]│
│ └──────┘└──────┘└──────┘└──────┘└──────┘└──────┘└──────┘└──────┘│   Pattern [Manual v]      │
│ TIMELINE  (strip chart of line states, last 10 s)             │   Pattern Number x [A]    │
│  DIO 7 ▁▁▁▁▔▔▔▔▁▁▁▁▔▔▔▔                                        │   [Toggle][Rotate][Shift] │
│  ...                                                          │   Direction [Left v]      │
│  DIO 0 ▔▔▁▁▔▔▁▁▔▔▁▁▔▔▁▁                                        │   Interval (ms) [200]     │
│                                         [⏸] [window 10 s v]    │ ▾ Timeline                 │
├───────────────────────────────────────────────────────────────┤   Sample interval [100 ms]│
│ RUN BAR  [▶ Run] [Single]   ● Running · reading 4 - 7 · writing 0 - 3   │                   │
└───────────────────────────────────────────────────────────────┴───────────────────────────┘
```

- **Display area** (dark surface):
  - Header row: `Inputs` numeric value and `Outputs` numeric value, each with its radix selector
    (the ELVISmx "Numeric Value" indicator, one per function). Hidden when the function has no lines.
  - **Line cards**, eight in a row, MSB left (DIO 7 ... DIO 0, like ELVISmx). Each card: line name
    (`DIO n`, plus an optional user label **[MYBENCH extension]**), a large round `Led`, a direction
    badge (`IN`, `OUT` or `–` for unused), and for OUT lines a vertical `ToggleSwitch` (HI up / LO
    down) that edits the Manual Pattern. The card border uses the function accent: IN = cyan
    (reader), OUT = amber (writer), unused = neutral and dimmed.
  - LED colors: off = dark navy blue, on = bright blue-cyan with a soft glow (`MultiEffect`),
    matching the ELVISmx look. Unused lines: LED dimmed, never lit.
  - **Timeline** (section 4): a strip chart (`MyBench.Plot`) under the cards, one row per line.
- **Inspector** cards (collapsible, ELVISmx group names kept inside each function):
  `Line Directions` [MYBENCH], `Digital Reader` (its `Configuration Settings`: Lines to Read, input
  radix), `Digital Writer` (its `Configuration Settings`: Lines to Write, Pattern, Manual Pattern
  number, Action, Direction, Interval), `Timeline`.
- **Run bar**: Run/Stop toggle, `Single` (= Run Once), status text (`Idle`, `Running`,
  `Paused - DIO in use by <panel>`, error), and a summary of what is read and written.
- **Dashboard tile** (`DioTile.qml`): title `Digital I/O`, eight mini LEDs with a tiny IN/OUT mark,
  the input and output values in hex, and the run-state dot. Click focuses the panel.
  **[MYBENCH extension]** While running with Pattern = Manual, clicking an OUT mini LED toggles that
  line's manual bit.

### 2.2 Per-line direction (generalization of the ELVISmx groups)

- Every line has a direction: `In` (read by the reader function), `Out` (driven by the writer
  function) or `Unused` (left as a high-impedance input, not shown, not claimed).
- The ELVISmx presets remain the primary way to configure the panel:
  - `Lines to Read`: `0 - 3`, `4 - 7`, `0 - 7`, plus `None` and `Custom` **[MYBENCH]**. Choosing a
    preset sets those lines to `In`.
  - `Lines to Write`: `0 - 3`, `4 - 7`, `0 - 7`, plus `None` and `Custom` **[MYBENCH]**. Choosing a
    preset sets those lines to `Out`.
  - Defaults: Lines to Read `4 - 7`, Lines to Write `0 - 3` (the ELVISmx defaults of the two
    instruments, which happen not to overlap). All other lines `Unused` (none with these defaults).
- **A line is either read or driven, never both.** Choosing a preset that overlaps the other
  function's lines moves the overlapping lines to the new function and shows a toast
  ("DIO 0-3 changed from Out to In"). Lines of the old preset that are no longer covered become
  `Unused`. The other preset combo then shows `Custom` (or `None` if it has no lines left).
- The `Line Directions` card shows one `SegmentedControl` [In | Out | –] per line for fine control;
  any set that is not a preset shows `Custom` in the preset combo.
- **Bit mapping** (used by numeric values and patterns): the value of a function's line set is built
  from its lines in ascending line order, lowest line = bit 0 ("right-justified"). For the presets
  this gives exactly the ELVISmx behaviour (0..15 for `0 - 3` / `4 - 7`, 0..255 for `0 - 7`); for a
  custom set such as {1, 4, 6} the value has 3 bits (DIO 1 = bit 0, DIO 4 = bit 1, DIO 6 = bit 2).

### 2.3 Enhancements summary [MYBENCH extension]

Per-line directions and custom sets, simultaneous reading and writing, the timeline with cursors and
export, line labels, step interval for patterns, readback check of outputs (2.4), live direction
changes while running, keyboard toggling of manual bits (`0`..`7` keys with the panel focused),
dashboard tile. None of these change the ELVISmx default behaviour described in section 3.

### 2.4 Output readback check [MYBENCH extension]

While running, `dioRead()` also returns the level of OUT lines. If an OUT line reads back a level
different from the one written for two consecutive samples, its card shows a small warning mark
("Output DIO n reads LO while driven HI - short circuit or overload?"). The LED itself keeps showing
the written value (ELVISmx semantics).

## 3. Functional specification (ELVISmx parity)

These two sections describe the complete behaviour of each ELVISmx instrument as it must be
available in the panel. Where the merged panel changes the presentation, 2.x says how.

### 3.1 Digital Reader

**ELVISmx reference layout (informative, not binding)** - window ~337 x 355 px:

```
+------------------------------------------------------+
| [logo]                                        [tv]   |  black display ~120 px
|                                  Numeric Value  x 0  |
|  Line    (o)  (o)  (o)  (o)  (o)  (o)  (o)  (o)      |
|  States   7    6    5    4    3    2    1    0       |
+------------------------------------------------------+
| Configuration Settings:  Lines to Read [ 4 - 7 v]    |
+------------------------------------------------------+
| Instrument Control: Device [..v]  Acquisition Mode [Run Continuously v]
|                     Run [->]  Stop [■]  |  Help [?]  |
+------------------------------------------------------+
```

Controls and indicators:

| Control | Type | Values | Default | Enabled when | Live while running |
|---|---|---|---|---|---|
| Lines to Read | combo | `0 - 3`, `4 - 7`, `0 - 7` (+ `None`, `Custom` in MYBENCH) | `4 - 7` | ELVISmx: idle only. MYBENCH: always (2.2) | MYBENCH: yes |
| Input radix | radix selector (click on the prefix letter -> menu) | `Decimal` (`d`), `Hex` (`x`), `Octal` (`o`), `Binary` (`b`), `SI Notation` (`p`) | Hex | always | yes (display only) |
| Acquisition Mode | run bar | Run Continuously = `Run`, Run Once = `Single` | - | - | - |

| Indicator | Content |
|---|---|
| Line States | one LED per line of the set: level from the last `dioRead()` (on = high) |
| Numeric Value | right-justified value of the set (2.2) in the chosen radix: `d 13`, `x D` (uppercase, no leading zeros), `o 15`, `b 1101`, `p 13`; zero is `x 0` |

Behaviour:
1. Run: claim the IN lines in the ResourceArbiter; `dioSetDirection(inMask, false)`.
2. Run Continuously: every sample interval (default **100 ms**) `dioRead()`, mask, update LEDs,
   numeric value and timeline. Repaint only on change (the timeline still advances).
3. Run Once (`Single`): one `dioRead()`, update, done.
4. Stop: stop polling; IN lines stay inputs (harmless); the display keeps the last values.
5. Pulses shorter than the sample interval can be missed (static, software-timed reads).

### 3.2 Digital Writer

**ELVISmx reference layout (informative, not binding)** - window ~332 x 525 px:

```
+----------------------------------------------------+
| [logo]                       Numeric Value  x 0    |  black display ~105 px
|  Line  (o)  (o)  (o)  (o)  (o)  (o)  (o)  (o)      |
|  States 7    6    5    4    3    2    1    0       |
+----------------------------------------------------+
| Configuration Settings                             |
|  Lines to Write [ 0 - 3 v]                         |
|  Pattern        [ Manual v]               x [0  ]  |  Pattern Number
|  Manual Pattern                                    |
|  | ||  ||  ||  ||  ||  ||  ||  ||   HI |           |  8 vertical slide switches
|  |Lines: 7   6   5   4   3   2   1   0  LO |       |  (outside the group: grayed)
|  Action [Toggle]      [Rotate] [Shift]  Direction [Left v]
+----------------------------------------------------+
| Instrument Control: Device [..v]  Generation Mode [Run Continuously v]
|                     Run [->]  Stop [■]  |  Help [?]|
+----------------------------------------------------+
```

Controls and indicators (`width` = number of OUT lines, `max = 2^width - 1`):

| Control | Type | Values | Default | Enabled when | Live while running |
|---|---|---|---|---|---|
| Lines to Write | combo | `0 - 3`, `4 - 7`, `0 - 7` (+ `None`, `Custom`) | `0 - 3` | ELVISmx: idle only. MYBENCH: always | MYBENCH: yes (sequence restarts) |
| Pattern | combo | `Manual`, `Ramp (0-255)`, `Toggle`, `Walking 1's` | `Manual` | always | yes (new pattern starts at its first value at the next step; Manual writes immediately) |
| Manual Pattern switches | one `ToggleSwitch` per OUT line (HI/LO) | | all LO | Pattern = Manual | yes (written immediately) |
| Pattern Number | `SiField` integer with radix prefix | 0 ... max, arrows ±1, coerced | 0 | Pattern = Manual | yes; typing a value moves the switches |
| Pattern Number radix | radix selector | as 3.1 | Hex | always | display only |
| Toggle | button | | | Pattern = Manual | yes |
| Rotate | button | | | Pattern = Manual | yes |
| Shift | button | | | Pattern = Manual | yes |
| Direction | combo / `SegmentedControl` | `Left`, `Right` | `Left` | Pattern = Manual | yes |
| Interval (ms) **[MYBENCH extension]** | `SiField` | 10 ... 10000 ms, step 10 | 200 | Pattern != Manual | yes |
| Generation Mode | run bar | Run Continuously = `Run`, Run Once = `Single` | | | |

| Indicator | Content |
|---|---|
| Line States | last value **written** to each OUT line (on = HI); MYBENCH adds the readback mark (2.4) |
| Numeric Value (`Outputs`) | last written value, right-justified, in the output radix (default Hex) |

Actions on the Manual Pattern (value `v`, applied within `width` bits):
- **Toggle**: `v = ~v & max`.
- **Rotate** (1 bit, circular): Left `v = ((v << 1) | (v >> (width - 1))) & max`; Right
  `v = (v >> 1) | ((v & 1) << (width - 1))`.
- **Shift** (1 bit, logical, zero fill): Left `v = (v << 1) & max`; Right `v = v >> 1`.
- Actions change the switches and the Pattern Number; if running in Manual, the result is written
  at once.

Predefined sequences (one cycle):

| Pattern | Sequence |
|---|---|
| Ramp (0-255) | 0, 1, ..., max (0..15 for 4 lines, 0..255 for 8) |
| Toggle | `0x55 & max`, `0xAA & max` (4 lines: 0x5, 0xA) |
| Walking 1's | 1, 2, 4, ..., 2^(width - 1) |

The item label `Ramp (0-255)` becomes `Ramp (0-<max>)` when `width` < 8 **[MYBENCH]**.

Behaviour:
1. Run: claim the OUT lines in the ResourceArbiter; `dioSetDirection(outMask, true)`.
2. Writing a value `v`: the bits of `v` are scattered to the OUT lines (bit k -> k-th lowest OUT
   line), then `dioWrite(bits, outMask)`; only OUT lines change.
3. Run Continuously: Manual -> write now and on every change of the pattern (event-driven).
   Sequences -> write the first value now, then the next value every Interval (200 ms), wrapping
   forever.
4. Run Once (`Single`): Manual -> write once. Sequences -> one full cycle (first to last value, one
   interval each). Then done.
5. Stop: stop stepping. OUT lines **keep driving the last value**. They return to inputs
   (`dioSetDirection(mask, false)`) when their direction is changed to In/Unused, when the panel is
   closed, when the session is closed or the app quits, and on Device reset.
6. Changing the OUT set while running (MYBENCH): newly added lines become outputs and get their bit
   of the current value; removed lines become inputs; a running sequence restarts from its first
   value with the new width.

### 3.3 Combined run semantics in the panel

One run bar drives both functions:

| Action | Reader part (IN lines) | Writer part (OUT lines) |
|---|---|---|
| `Run` | continuous polling (3.1) | Run Continuously behaviour (3.2) |
| `Single` | one read at start and one at the end of the writer's cycle | Run Once behaviour; the panel returns to idle when the writer is done (immediately after the read if there are no OUT lines) |
| `Stop` | polling stops | stepping stops, outputs hold |

With no IN and no OUT lines, Run shows the toast "Select lines to read or to write first." and stays
idle. The status text summarizes, e.g. `Running · reading 4 - 7 · writing 0 - 3 (Ramp)`.

## 4. Timeline [MYBENCH extension]

- A strip chart of the eight line states over time, software-sampled at every sample interval of the
  poll loop (default 100 ms; `Timeline > Sample interval` 10 ms ... 1000 ms, SiField; the reader uses
  the same interval). One `dioRead()` per sample gives all eight levels (IN and OUT).
- One row per line, DIO 7 at the top down to DIO 0 (same order as the cards), row label `DIO n`
  (+ user label). High = raised trace, drawn as a digital (step) trace in the function accent (IN
  cyan, OUT amber, unused gray). Time axis: last `Window` seconds (`5 s`, `10 s` (default), `30 s`,
  `60 s`), newest at the right, scrolling.
- Pause button (freezes the view while sampling continues into the buffer), Clear button.
  Cursors (two, `MyBench.Plot` cursors) show the time of each and Δt, and the value of the lines at
  each cursor.
- The buffer keeps the last 10 minutes (or 100 000 samples, whichever is smaller) while the panel is
  open; it is cleared on Clear and when the session is closed.
- Timing is only as good as the software polling (USB and OS jitter of several ms); the chart states
  this in a tooltip. It is not a hardware-timed logic analyzer.

## 5. Hardware and timing notes

- DIO 0..7 on the screw terminals, referenced to DGND. Outputs 3.3 V LVTTL, a few mA; inputs are
  5 V tolerant. Direction per line.
- All reads and writes are static calls from the worker thread; a write or read takes on the order
  of a millisecond over USB. Minimum sample/step interval 10 ms.

## 6. Errors and notifications

| Situation | Presentation |
|---|---|
| Run with no lines selected | toast "Select lines to read or to write first." |
| Preset moves lines between functions | toast "DIO <lines> changed from <Out|In> to <In|Out>." |
| DIO line claimed by another panel (future instruments) | ResourceArbiter preemption (app.md): the holder is paused with a banner |
| Output readback mismatch | card warning mark (2.4), no toast |
| Device errors during I/O | error banner/toast per app.md; the panel stops; outputs keep what the hardware last received |
| Device disconnected | panel stops; banner "Device disconnected"; lines are reconfigured on the next Run |

## 7. Interactions and resources

- ResourceArbiter resource: **per-line DIO** (`DIO n`), claimed on Run for every IN and OUT line,
  released on Stop. Holding OUT lines after Stop (they keep driving) does not keep the claim; the
  panel re-asserts directions on the next Run.
- Within the panel, conflicts cannot happen (a line has one direction). Other panels do not use DIO
  today; the arbiter rule is ready for future ones (e.g. counters on DIO lines).
- Independent of AI, AO and DMM: runs alongside every other instrument.

## 8. Export

- `Export data` (DataExport, CSV/TSV; `.sr` planned - logic channels map naturally to sigrok) of the
  timeline buffer: comment header as defined in app.md, plus
  `# Directions: DIO 0 Out, DIO 1 Out, ..., DIO 7 In` and `# Sample interval: 100 ms`;
  columns `Time (s)`, `DIO 0` ... `DIO 7` (0/1), one row per sample. Also `Export image/PDF` of the
  panel (app.md).
- ELVISmx's Reader and Writer have no Log; this is an extension.

## 9. Device API mapping

| Feature | Call |
|---|---|
| Directions | `Device::dioSetDirection(mask, true)` for OUT lines, `(mask, false)` for IN/unused lines |
| Current directions (diagnostics) | `Device::dioDirection()` |
| Read (all lines) | `Device::dioRead()` |
| Write | `Device::dioWrite(value, outMask)` |
| Line count | `mydaq::kDioLines` (8) |

No tasks. The poll/step loop runs in the controller's worker thread; the UI receives snapshots
(`levels`, `written`, timeline chunk) through queued signals.

## 10. Differences from ELVISmx

- Two instruments merged into one panel; per-line directions and custom sets; simultaneous read and
  write; Lines to Read/Write changeable while running; timeline, export, readback check, labels,
  step interval (all MYBENCH).
- ELVISmx's note "with 0 - 7 the Reader and Writer cannot run simultaneously" becomes the per-line
  rule of 2.2 (choosing `0 - 7` for one function takes the lines from the other).
- The ELVIS II digital bus and additional lines do not exist on the myDAQ.
- ELVISmx shows a Toggle View button on the Reader; the MYBENCH equivalent is the shell's focus mode.

## 11. Checks against the simulator

- Defaults + Run: reading 4 - 7 (cards 7..4 IN), writing 0 - 3 (cards 3..0 OUT, Manual, all LO),
  `Outputs x 0`, timeline scrolling.
- Flip DIO 2's switch: LED 2 on, `Outputs x 4`, `dioRead() & 0x0F == 0x04`, timeline row DIO 2 high.
- Toggle -> `x B`; Rotate Left -> `x 7`; Shift Right -> `x 3`.
- Pattern Ramp: outputs count 0..F every 200 ms and wrap; `Single` with Toggle writes 5 then A and
  returns to idle.
- Lines to Read = `0 - 7`: toast about DIO 0-3 moving to In; Lines to Write shows `None`; Run reads
  all eight lines.
- Custom: DIO 1, 4, 6 Out, Pattern Number 5 -> DIO 1 and DIO 6 high.
- Close the panel: `dioDirection() == 0`.
- `SimDevice::setConnected(false)` while running: panel stops with the disconnect banner.
- `sim_device.h` documents no wiring between DIO lines, so IN lines show the simulator's idle input
  level unless the sim models loopback.

## 12. Open questions / assumptions

1. **Numeric value justification**: right-justified group values (0..15 for 4-line groups) are
   assumed for both Reader and Writer; ELVISmx might show raw port positions (`x F0`).
2. **LED on color** (bright blue-cyan) is assumed; only the off color is visible in the screenshots.
3. **Toggle pattern** values 0x55/0xAA are assumed (could be all-zeros/all-ones).
4. **Step interval** of the sequences (200 ms) and **poll interval** (100 ms) are MYBENCH choices.
5. **Run Once for sequences** = one full cycle (our reading of "single signal").
6. **Outputs after Stop** keep driving; released on direction change / close / reset (ELVISmx
   behaviour unknown).
7. **Single for the combined panel** (3.3) is our composition rule.
8. **Timeline row order** DIO 7 at the top (matches the cards); logic analyzers often put line 0 at the
   top - make it a view option if users ask.
9. **Line labels** are stored in the session file (app.md), not on the device.
