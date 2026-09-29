# Getting started with MYBENCH

MYBENCH turns an NI myDAQ into a small electronics bench: multimeter, oscilloscope, waveform
generator, Bode analyzer, spectrum analyzer and digital I/O, all in one window. It talks to the myDAQ
directly over USB, so no other software is needed. Without a myDAQ it runs on a built-in
**simulated myDAQ**, which is handy for learning and preparing labs.

MYBENCH is an independent open-source project, not affiliated with or endorsed by NI. If you have
used the NI ELVISmx instruments, you will find the same instruments, settings and names.

## The instruments

| Rail | Instrument | What it does | myDAQ terminals |
|---|---|---|---|
| DMM | Multimeter | DC/AC voltage and current, resistance, diode, continuity | banana jacks `HI`, `COM`, `HI (A)` |
| Scope | Oscilloscope | one or two waveforms, trigger, measurements, cursors | `AI 0±`, `AI 1±` or AUDIO IN |
| Gen | Waveform Generator | sine/square/triangle, sweeps, arbitrary waveforms | `AO 0`, `AO 1` or AUDIO OUT, `AGND` |
| Bode | Bode Analyzer | gain and phase of a circuit versus frequency | `AO 0` -> circuit, `AI 0` = input, `AI 1` = output |
| Spec | Spectrum Analyzer | spectrum of one signal, THD, SINAD, averaging | `AI 0±`, `AI 1±` or AUDIO IN |
| DIO | Digital I/O | read and drive DIO 0-7, patterns, timeline | `DIO 0` ... `DIO 7`, `DGND` |

## The window

- **Navigation rail** (left): Home and one button per instrument. A click opens the instrument or
  brings its tab to the front (it never closes anything; close the tab for that). A dot shows that an instrument is
  running (green), paused (amber) or stopped by an error (red).
- **Top bar**: the **device** in use, chips showing which instrument uses which part of the myDAQ
  (`AI: Scope`, `AO: Generator` ...), **Stop all**, the **session** menu and the help button.
- **Workspace**: instruments open as tabs. Use the split button (or `Ctrl+\`) to show two side by
  side, for example the Generator next to the Scope. `Ctrl`+click an instrument in the rail to
  open it in the other pane. Drag a tab to reorder it, or drop it on the left or right half of the
  workspace to show it there. Middle-click a tab, or use its **x** or `Ctrl+W`, to close it. Closing a tab stops that instrument and puts its outputs in a safe
  state (digital lines back to inputs, analog outputs to 0 V).
- **Resource chips**: right-click one to stop or show that instrument.
- **Dashboard column** (right, optional, `Ctrl+Shift+D`): small live tiles, always visible next
  to the panel you work in. It starts with the DMM and DIO tiles; **+** adds another instrument, the
  **×** of a tile removes it, and the small grip at the top of a tile drags it to another place. The
  choice is kept with the layout and in sessions.
- **Help drawer**: `F1` shows the help of the instrument you are using; while it is open it follows
  the panel you work in. Drag its left edge to make it wider or narrower.
- **Status bar**: device state, sample rate and data rate, and a counter of unread warnings and
  errors (click it to see the event log). Error banners and messages have **Details**, which opens
  the event log with the error code and the device's own message.

Each instrument panel has a **display** (graphs and readouts), an **inspector** with its settings
grouped in cards (the familiar ELVISmx group names), and a **run bar**:
- **Run** - measure or generate continuously (ELVISmx "Run Continuously"); press again to **Stop**.
- **Single** - one acquisition or one pass (ELVISmx "Run Once").
- Status: running, waiting for trigger, paused, or the error that stopped it. A setting that cannot
  work (for example a stop frequency below the start frequency) is reported in amber and nothing
  starts.
- **Release outputs** appears when an instrument is stopped but still drives an output (for
  example digital lines left high after Single): it returns them to their safe state. **Stop** and
  **Stop all** do the same.
- Inspector cards remember whether you collapsed them.

## Home

Home is the starting page (the house button of the rail). It replaces the ELVISmx Instrument
Launcher:
- **Favorites**: hover an instrument tile and click its **star** to keep that instrument in the
  Favorites strip at the top of Home; click a favorite to open it, or its play button to run it
  without leaving Home. Click the star again to remove it.
- **Instruments**: every instrument as a live tile; click to open, or use the run button on hover.
- **Tools**: your own shortcuts to other programs, documents, folders or web pages (a circuit
  simulator, a lab sheet, a course page). **Add tool** asks for a name and the program, file or web
  address (**Browse** picks a file); programs can get arguments. The **...** menu of a tool edits,
  moves or removes it. **Find more online** opens the MYBENCH web site.
- **Files**: files you **pin** (for example the lab instructions or a datasheet) and the last ten
  files you exported (data, images, PDF reports). Click a file to open it with its usual program;
  the folder button shows it in its folder.
- **Resources**: this page (**Getting started**), the help of each instrument, links to the NI
  myDAQ user guide and specifications on ni.com, and the **About** box (version, Qt version,
  whether USB support is built in, license and credits).
- **Recent sessions**: the last ten `.mybench` files; click one to open it.

Favorites, tools and files are kept on this computer (they are not part of a session).

## Device

- The device menu in the top bar lists your myDAQ(s) and the simulated myDAQ. Green dot = real
  device connected, yellow = simulated, red = disconnected.
- While the simulator is used, every display shows a **SIMULATED** badge.
- Unplugging the myDAQ stops the running instruments with a message; plug it back in and press
  **Resume**. MYBENCH never switches to the simulator on its own while you work.
- **Home > Device card** shows the serial number and offers **Rescan**, **Reset device** (all outputs
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

**Preferences** (`Ctrl+,`) has the appearance: **System** (the default: follows the light or dark
setting and the accent color of your desktop), **Light** or **Dark**. Instrument displays keep
their own display colors in every mode.

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
