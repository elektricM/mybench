# Data Logger

The Data Logger records slow signals for a long time: AI 0 and AI 1 at 1 to 1000 samples per
second, and optionally the multimeter, into a live chart and a text file you can open in a
spreadsheet. It is MYBENCH's version of the NI "Data Logger" featured application.

## Quick start

The panel has the chart on the left, a column of channel cards and tools on the right, and a card
per channel with its latest value and statistics at the bottom. Run, Record and the sampling-rate
knob sit in the bar at the top.

1. Connect the signal to **AI 0+ / AI 0−** (and AI 1 if needed).
2. Switch on the channels to log on their cards (AI 0, AI 1, DMM). Set the **Sampling Rate** with
   the knob in the top bar (1000 S/s by default; type a value or use − / +) and, if needed, the
   **Limits** tool.
3. Press **Run** to watch the signals (nothing is saved yet), then **Record** to start a file.
   **Record** also starts the logger when it is idle.
4. **Pause** stops writing for a while (the chart keeps running), **Stop Recording** closes the
   file, **Stop** ends everything.

## Channels and ranges

- **AI 0 / AI 1**: differential inputs, sampled together at the same rate.
- **DMM** logs the multimeter too: pick the function (DC/AC voltage, DC/AC current, resistance)
  and a range, or *Auto*, with the buttons on its card. The meter delivers 2 readings per second, independently of the
  sampling rate; a range change (also an automatic one) makes it pause for about 2.6 s. Use the meter's banana jacks.
- **Limits** tool, **Maximum / Minimum (V)**: limits within ±2 V use the more precise ±2 V range,
  otherwise ±10 V (the AI cards show the range in use). The chart scales itself; lock its vertical
  scale with the chart toolbar.
- Channels, rate and limits are fixed while the logger runs (hovering them says so); stop it to
  change them.

## The chart

- The chart holds the whole recording (up to 20 million samples per channel, shown in the
  Display tool; the file has no limit). Choose the visible span (10 s … 1 h, All) in the chart's
  toolbar or zoom with the wheel.
- **Follow the latest data** (Display tool) keeps the newest data at the right edge. Dragging or
  zooming to look at older data turns it off; choosing a span turns it on again.
- The strip below the chart shows everything recorded, with the visible window: drag the
  window to jump through the recording (it can be hidden in the Display tool).
- The cards at the bottom show each channel's latest value and its minimum, maximum, mean and
  standard deviation. **Clear** in the chart toolbar clears the chart and statistics (not the file).

## The file

- **Log File** tool: folder, file name, *Date and time in the name*, **Format** (**Tab** .tsv,
  **CSV** or **ELVISmx** text, below), an optional *Timestamp column*, *Record when Run starts*,
  and *Stop after* a number of minutes. **Show Folder** opens the folder.
- The file starts with lines beginning with `#` (app, date, device, sampling rate, limits,
  channels, start time), then a header row and one row per sample: time in seconds since the
  start of the recording, then one column per channel. DMM readings appear on the row where they
  arrived, with their own time.
- Existing files are never overwritten (`_2`, `_3`… are added).
- When AI and the DMM are logged together and the AI rate is lower than the DMM rate (the DMM
  delivers 2 readings per second), every DMM reading still gets a row: readings without an
  AI sample beside them are written on rows of their own with empty AI cells.
- **Format ELVISmx**: the text layout of the NI Data Logger, for scripts written for it: a header row `Time`, `myDAQ1/ai0` (a `Time`/channel pair per AI channel), then one row
  per sample with the absolute time (`9/28/2026  10:15:00.001000`) and the value in exponent
  notation (`5.174413E+0`) for each channel; tab-separated, CR LF line ends, no `#` lines, extension
  `.txt`. Only AI channels are written (not the DMM). *Append to an existing file* adds to the
  named file, repeating the header row, as the NI app does (turn off *Date and time in the name*
  to reuse one file).
- The file is written while recording and saved to disk continuously: if the computer crashes,
  everything up to the last fraction of a second is kept.

**Export ▸ Export data** additionally saves what the chart holds (CSV or TSV).
