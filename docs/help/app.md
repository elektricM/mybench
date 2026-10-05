# Getting started with MYBENCH

MYBENCH turns an NI myDAQ into a small electronics bench: multimeter, oscilloscope, waveform
generator, Bode analyzer, spectrum analyzer and digital I/O, all in one window. It talks to the myDAQ
directly over USB, so no other software is needed. Without a myDAQ it runs on a built-in
**simulated myDAQ**, which is handy for learning and preparing labs.

MYBENCH is an independent open-source project, not affiliated with or endorsed by NI. If you have
used the NI ELVISmx instruments, you will find the same instruments, settings and names.

## The instruments

| Short name | Instrument | What it does | myDAQ terminals |
|---|---|---|---|
| DMM | Multimeter | DC/AC voltage and current, resistance, diode, continuity | banana jacks `HI`, `COM`, `HI (A)` |
| Scope | Oscilloscope | one or two waveforms, trigger, measurements, cursors | `AI 0±`, `AI 1±` or AUDIO IN |
| Gen | Waveform Generator | sine/square/triangle, sweeps, arbitrary waveforms | `AO 0`, `AO 1` or AUDIO OUT, `AGND` |
| Bode | Bode Analyzer | gain and phase of a circuit versus frequency | `AO 0` -> circuit, `AI 0` = input, `AI 1` = output |
| Spec | Spectrum Analyzer | spectrum of one signal, THD, SINAD, averaging | `AI 0±`, `AI 1±` or AUDIO IN |
| DIO | Digital I/O | read and drive DIO 0-7, patterns, timeline | `DIO 0` ... `DIO 7`, `DGND` |

## The window

- **Sidebar** (left): **Overview** and every instrument, grouped as Measure, Generate, Digital and
  Apps. Click an instrument to show it; showing another one never stops the first. A green dot
  marks a running instrument, amber a paused one, red one stopped by an error, and a running
  instrument shows its key value under its name (`100 Hz · 1.00 Vpp`). The sidebar button of the
  toolbar (`Ctrl+Alt+S`, on macOS `Ctrl+Cmd+S`) hides it, `Ctrl+0` shows Overview. In a narrow
  window the sidebar becomes a row of icons, in a very narrow one a drawer.
- **Toolbar** (top): the session name, how many instruments run and **Stop All**, the **device**
  (click for the Device menu) and **Search** (`Ctrl+K`): every command of the menus by name.
- **Workspace**: one instrument, or two side by side (**Show Beside** in the context menu of a
  sidebar row, the View menu, or `Ctrl+\`), for example the Generator next to the Scope. The pane you
  work in has a blue line under its header; the **×** of a pane closes the split, never the
  instrument. In a window too narrow for two panes the split waits until there is room again.
- **Drag and drop**: drag a sidebar row or an Overview tile onto the workspace. The left or right
  third splits with the instrument on that side, the middle replaces the pane you work in; a blue
  area shows where it goes. Drag a pane by its name onto the other half to swap the two panes, or
  onto the sidebar to close that pane. Dragging never starts or stops an instrument.
- **Inspector** (right): the settings of the instrument you work in, or its help page (**Help**,
  `F1`). Instruments that keep their controls next to their display need no inspector: the column
  then disappears and comes back only when you open the help. Drag its left edge to resize it.
- **Status bar**: which instrument uses which part of the myDAQ (`AI: Oscilloscope`), the sample
  rate and a counter of unread warnings and errors (click it for the event log). Error banners
  have **Details**, which opens the event log with the error code and the device's own message.

Each instrument pane has a **header** and a **display**:
- **Run** - measure or generate continuously (ELVISmx "Run Continuously"); press again to **Stop**
  (`Space`).
- **Single** - one acquisition or one pass (ELVISmx "Run Once", `Shift+Space`).
- The state next to the name: running, waiting for trigger, paused, or the error that stopped it.
  A setting that cannot work (for example a stop frequency below the start frequency) is reported
  in amber and nothing starts.
- **Release Outputs** appears when an instrument is stopped but still drives an output (for
  example digital lines left high after Single): it returns them to their safe state. **Stop** and
  **Stop All** do the same.
- **↺** (before **⋯**): **Reset to Defaults**, after a confirmation (**Reset**). In a pane
  narrower than about 700 px (a split) the ↺ button folds into **⋯ > Reset Settings…** and the
  instrument name shortens first, so the instrument's own header controls keep their room.
- **⋯**: Export Data, Export Image, Print, Trace Style, Show Measurements, Reset Settings and Help.

## Overview

Overview (top of the sidebar) is the starting page and replaces the ELVISmx Instrument Launcher:
- **Device**: the myDAQ in use and its state, or how to connect one (or **Use Simulated Device**).
  With the simulated myDAQ it also shows its wiring: AO 0 → AI 0 direct, AO 0 → RC low-pass → AI 1,
  AO 1 not connected. **Reset All…** (also Instrument menu and Search: *Reset All Instruments to
  Defaults…*) stops every instrument, returns the outputs to their safe state and restores every
  setting; the layout is kept.
- **Instruments**: every instrument as a live tile; click to show it, or **Run**/**Stop** from the
  tile. The context menu of a tile has **Show Beside** and **Pin to Top**.
- **Recent Sessions**: the last saved `.mybench` files; click one to open it.

## Device

- The device menu in the top bar lists your myDAQ(s) and the simulated myDAQ. Green dot = real
  device connected, yellow = simulated, red = disconnected.
- While the simulator is used, every display shows a **SIMULATED** badge.
- Unplugging the myDAQ stops the running instruments with a message; plug it back in and press
  **Resume**. MYBENCH never switches to the simulator on its own while you work.
- **Overview > Device** shows the serial number and offers **Rescan**, **Reset device** (all outputs
  to 0 V, digital lines to inputs) and **Device info**. With the simulator, **Simulator settings…**
  sets its test signals, noise, RC corner and DMM values.
- Options that the connected device cannot do yet are greyed out, with a tooltip saying so.

## Sharing the myDAQ between instruments

The myDAQ has one analog-input converter pair, one analog-output engine and one DMM:
- **AI**: only one of Scope, Spectrum Analyzer or Bode can acquire at a time (the audio input uses
  the same converters).
- **AO**: the Waveform Generator and the Bode Analyzer both need the analog outputs.
- **DMM** and **DIO** are independent and can always run.

If you start an instrument whose part is busy, the other one is **paused** with a banner such as
"Oscilloscope paused — AI in use by Bode Analyzer". Click **Resume** to take it back. Generators
return their outputs to 0 V when they stop or pause.

## Sessions

- MYBENCH remembers your layout and every instrument setting automatically and restores them next
  time.
- **Session > Save As** stores everything in a `.mybench` file (for a lab, an assignment, a demo);
  **Open** brings it back. Instruments are not started automatically when a session opens. If the
  current session has unsaved changes, Open, Open recent and Quit ask whether to save it first. The
  window title shows the session name, with `•` when it has unsaved changes.
- **Session > New** starts fresh with the default settings of every instrument.
- **Revert to defaults** resets one panel or all of them.

## Graphs

Every graph has a small toolbar that appears at its top right when the pointer is over it:
**pointer** (drag only cursors and markers), **zoom** (a menu: zoom box, zoom X only, zoom Y only,
zoom in, zoom out, undo zoom, reset) and **pan**, then **X** and **Y autoscale**. Click an autoscale
button to fit that axis to the data once; click its lock to keep fitting it as new data arrive (any
zoom or pan of that axis turns the lock off). Without the toolbar: wheel zooms time/frequency,
`Shift`+wheel zooms vertically, `Ctrl`+drag draws a zoom box, dragging pans, double-click resets.
**Undo zoom** steps back through your zooms.

Right-click a graph for Autoscale X/Y, the autoscale locks, Undo and Reset zoom, Cursors on/off
(where the instrument has cursors), **Trace style…**, Copy image, Export image… and Export data….
Trace style (also in the panel's `⋯` menu) sets each trace's color, line width and style, point
style, interpolation (points only, linear or one of the step modes) and fill (to zero, to the bottom
or to the top of the graph); changes show immediately, Cancel undoes them.

## Saving results

- **Export data** (`Ctrl+L`) saves the last measurement as CSV or TSV, with a header describing
  the settings and the device.
- **Export image… / Export PDF… / Print…** (`Ctrl+P` prints) first show the print options
  (like the ELVISmx Print dialog): what to include (the display or the whole panel), **Ink saver**
  (white background, dark grid and text, traces in colour), your **user name**, the **computer
  name**, a **time stamp** and the **settings table**, then the page orientation and, for printing,
  the printer and number of copies. The choices are remembered. Export dialogs start in the folder
  you last used for that kind of file.

## Appearance

**Preferences** (`Ctrl+,`) has the appearance: **Dark** (the default), **Light** or **System**
(follows the light or dark setting and the accent color of your desktop). **Always use dark
displays** keeps graphs dark in the light appearance.

**Controls**: **Native** (the default) draws buttons, fields, check boxes and menus in your
desktop's own style (macOS, Windows 11, KDE or Fusion); **MYBENCH** uses MYBENCH's own flat style
on every system. The change applies at the next start of MYBENCH.

**General > Reset Window Layout** puts the window back to its default size (centred, not
maximized), the split back to half and half, and the sidebar and inspector back to their default
visibility. The instruments on screen and their settings stay as they are.

## Keyboard shortcuts

Shortcuts use `Ctrl`; on macOS the same keys use `⌘` (Command) instead. The hints in tooltips and the command palette show the keys of your system.

| Keys | Action |
|---|---|
| `Space` / `Shift+Space` | Run/Stop / Single for the current instrument |
| `Ctrl+.` | Stop all |
| `Ctrl+K` | command palette (type to find any command) |
| `F1` | help |
| `Ctrl+0` ... `6` | Home, DMM, Scope, Gen, Bode, Spec, DIO |
| `Ctrl+\` | split view |
| `Ctrl+Shift+F`, `Esc` | focus mode (display only) on/off |
| `Ctrl+L`, `Ctrl+P` | export data, print (options for image / PDF / printer) |
| `Ctrl+S`, `Ctrl+O`, `Ctrl+N` | save, open, new session |

## Tips

- Numbers accept units and prefixes: type `1k`, `2.5m`, `100 mV`. Scroll over a digit to change just
  that digit (no need to click first); the tooltip shows the allowed range.
- Knobs work with drag, mouse wheel and arrow keys; double-click returns a knob to its default
  (where the instrument defines one).
- The command palette (`Ctrl+K`) also lists each instrument's own commands; some ask for a value
  (type it and press Enter).
- `Space` runs/stops the instrument, except while a button, switch, list or text field has the
  keyboard focus (then Space belongs to that control).
- In graphs: wheel = zoom time/frequency, `Shift`+wheel = zoom vertically, double-click = fit,
  right-click for autoscale, cursors, trace style and export.
- Try everything on the simulated myDAQ first: its AO 0 is wired to AI 0 directly and to AI 1
  through a 1 kHz low-pass filter, so the Bode Analyzer shows a real response.

## Troubleshooting

- **No myDAQ in the device list**: check the USB cable and that the myDAQ's LED is on, then
  **Rescan**. On Linux you may need permission to access the USB device (see the project README).
- **"... is busy (used by another program?)"**: close other programs (or another MYBENCH window) that
  use the myDAQ.
- **"Data were lost ..."**: the computer could not keep up; choose a slower timebase or lower sample
  rate, or close other heavy programs.
- **An instrument shows "paused"**: another instrument took the shared part of the myDAQ; stop it or
  click **Resume**.
