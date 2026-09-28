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

- **Navigation rail** (left): Home and one button per instrument. A dot shows that an instrument is
  running (green), paused (amber) or stopped by an error (red).
- **Top bar**: the **device** in use, chips showing which instrument uses which part of the myDAQ
  (`AI: Scope`, `AO: Generator` ...), **Stop all**, the **session** menu, the theme and help buttons.
- **Workspace**: instruments open as tabs. Use the split button (or `Ctrl/⌘+\`) to show two side by
  side, for example the Generator next to the Scope.
- **Dashboard column** (right, optional): small live tiles, e.g. the DMM reading and the DIO LEDs,
  always visible.
- **Help drawer**: `F1` shows the help of the instrument you are using.
- **Status bar**: device state, sample rate and data rate, and a counter of warnings (click it to
  see the event log).

Each instrument panel has a **display** (graphs and readouts), an **inspector** with its settings
grouped in cards (the familiar ELVISmx group names), and a **run bar**:
- **Run** - measure or generate continuously (ELVISmx "Run Continuously"); press again to **Stop**.
- **Single** - one acquisition or one pass (ELVISmx "Run Once").
- Status: running, waiting for trigger, paused, or the error that stopped it.

## Device

- The device menu in the top bar lists your myDAQ(s) and the simulated myDAQ. Green dot = real
  device connected, yellow = simulated, red = disconnected.
- While the simulator is used, every display shows a **SIMULATED** badge.
- Unplugging the myDAQ stops the running instruments with a message; plug it back in and press
  **Resume**. MYBENCH never switches to the simulator on its own while you work.
- **Home > Device card** shows the serial number and offers **Rescan**, **Reset device** (all outputs
  to 0 V, digital lines to inputs) and **Device info**.

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
  **Open** brings it back. Instruments are not started automatically when a session opens.
- **Session > New** starts fresh with the default settings of every instrument.
- **Revert to defaults** resets one panel or all of them.

## Saving results

- **Export data** (`Ctrl/⌘+L`) saves the last measurement as CSV or TSV, with a header describing
  the settings and the device.
- **Export image / PDF** (`Ctrl/⌘+P`) saves a picture of the instrument or a one-page PDF report you
  can print.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| `Space` / `Shift+Space` | Run/Stop / Single for the current instrument |
| `Ctrl/⌘+.` | Stop all |
| `Ctrl/⌘+K` | command palette (type to find any command) |
| `F1` | help |
| `Ctrl/⌘+0` ... `6` | Home, DMM, Scope, Gen, Bode, Spec, DIO |
| `Ctrl/⌘+\` | split view |
| `Ctrl/⌘+Shift+F`, `Esc` | focus mode (display only) on/off |
| `Ctrl/⌘+L`, `Ctrl/⌘+P` | export data, export image/PDF |
| `Ctrl/⌘+S`, `Ctrl/⌘+O`, `Ctrl/⌘+N` | save, open, new session |

## Tips

- Numbers accept units and prefixes: type `1k`, `2.5m`, `100 mV`. Scroll over a digit to change just
  that digit.
- Knobs work with drag, mouse wheel and arrow keys; double-click resets them.
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
