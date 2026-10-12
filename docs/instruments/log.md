# Data Logger (`log`)

Read [app.md](app.md) first. Behaviour matches the ELVISmx featured application. MYBENCH version of the ELVISmx **Data Logger**
featured app (slow multi-channel voltage logging with a strip chart and a text file).

## 1. Purpose

Record AI 0 and/or AI 1 (and optionally the DMM) for minutes to days at 1 – 1000 S/s: a live
chart of the whole recording that you can scroll back through while it grows, per-channel
statistics, and a delimited text file written while recording.

## 2. ELVISmx parity

| ELVISmx | Values / default | MYBENCH |
|---|---|---|
| Channel Select (ai0, ai1; Logging?) | ai0 on | **Channel Select** chips AI 0 (on), AI 1 (off) |
| Sampling Rate (S/s) | 1 … 1000, step 1, `%.2f`; **starts at 1000** (observed; the panel's saved value 10 is overridden by the `SamplingRate` key of `DataLogger.ini`, absent from the shipped ini, code default 1000) | **Sampling** card, same range/format; default 1000 (audit `log` #2) |
| Maximum (V) / Minimum (V) | −10 … +10, step 0.1; +10 / −10 | **Input Limits** card; limits within ±2 V use the ±2 V range, otherwise ±10 V; also the fixed chart scale |
| Auto-scale graph | on/off | toggle in Input Limits (default on) |
| Log toggle ("Log"/"Logging…") | | **Record** / **Stop Recording** (run bar) |
| File path for saved data | | **Log File** card: folder, name, format, options |
| AI task | continuous, buffer 10 × rate, 1-s blocks | continuous, buffer 10 × rate; reads what is available every ~50 ms (display ≥ 10 Hz) |
| File | per channel a pair `Time`, `<dev>/ai<n>`; Tab; absolute date-time strings; appended per 1-s block; see §8 (exact format) | see §4 (MYBENCH native layout) and §8 (**ELVISmx-compatible layout**, optional) |
| Invalid path | "no data was saved" | warning toast "Could not create <file> (<reason>): no data was saved." |

## 3. MYBENCH design

- **Run** acquires and charts (status *Monitoring*); **Record** opens a new file and streams to it
  (starting the logger if idle); **Pause** / **Resume** suspend writing (the chart continues; the
  gap shows in the time column); **Stop Recording** closes the file (toast with the row count);
  **Stop** ends everything and closes the file. **Single** does not exist.
- Options: *Record when Run starts*, *Stop after* N minutes (closes the file and stops the run).
- **Channels**: AI 0, AI 1 (differential, one task), **DMM** [MYBENCH extension] with function
  (DC V, AC V, DC A, AC A, Resistance) and range (Auto = software autoranging, or a fixed range,
  largest first). The DMM is read at its own pace (the meter delivers 2 readings/s for V, A and Ω; a range change costs ~2.6 s) in the same loop.
  Resources: `AnalogInput` if an AI channel is on, `Multimeter` if the DMM is on. Channel, rate
  and limit settings are locked while running.
- **Chart**: `TraceBuffer` recording (min/max pyramid) per channel, x = absolute time (sample
  clock: first sample time + k / rate). Keeps the last 20 M samples per channel (e.g. 5.5 h at
  1000 S/s, 23 days at 10 S/s; shown in the Sampling card); the file keeps everything. View:
  span presets 10 s / 1 min / 10 min / 1 h / All, **Follow latest** (panning or zooming turns it
  off), hover readout, legend, an overview strip with the visible window (drag it to scroll
  back), a DMM lane under the AI chart (same time axis), Clear chart (file unaffected).
- **Statistics** per channel (since start or Clear): latest value, min, max, mean, standard
  deviation, count, over-range badge (|v| ≥ 1.05 × range).
- Display header: channels, sampling rate; recording card: REC/PAUSED/MONITORING, recorded
  time, file name and size.

## 4. File format

```
# MYBENCH 0.1.0 - Data Logger
# Date: 2026-09-28T10:15:00+02:00
# Device: Simulated myDAQ (NI myDAQ, serial SIMULATED), simulated
# Sample rate: 10 S/s
# Input limits: -10 V to 10 V (range ±10 V)
# Channels: AI 0, AI 1, DMM DC Voltage (Auto)
# Time: seconds since the start of the recording
# Start: 2026-09-28T10:15:00.123
Time (s)	[Timestamp]	AI 0 (V)	AI 1 (V)	DMM time (s)	DMM DC Voltage (V)
0	0.01234	-0.4987	0.0123	1.2345
0.1	0.01240	-0.4991		
```

- Delimiter Tab (`.tsv`, ELVISmx default) or Comma (`.csv`); `.` decimal point, up to 10
  significant digits, empty cell = no value (DMM overload, or no new DMM reading on that row).
- Optional ISO 8601 *Timestamp* column. With AI channels, each row is one AI sample; a new DMM
  reading goes on the next row with its own time. DMM only: one row per reading.
- File name `<name>_<yyyyMMdd_HHmmss>.<ext>` (date/time optional) in the chosen folder
  (default `Documents/MYBENCH Logs`), `_2`, `_3`… when taken: never overwrites.
- **Crash safety**: only whole rows are written; the file is flushed after every block (~50 ms)
  and synced to disk every 5 s and on close, so a crash or power loss leaves a valid file up to
  the last block.

## 5. Export and tile

Export data: the chart contents (time from the first sample, AI columns, DMM time/value).
Tile: REC state and time, latest values, last minute of the chart.

## 6. Checks against the simulator (automated: `instruments/log/tests`)

Naming and never-overwrite, writer layout and crash-safe flush, DMM autoranging, recording AI 0 +
AI 1 at 200 S/s with a pause (gap in time), CSV with timestamp + DMM + stop-after limit, DMM-only
resistance logging, refusal without channels.

## 7. Open questions

- Everything about the ELVISmx file that is not settled yet is listed in §8.4 and is
  asked of a real ELVISmx session.

## 8. ELVISmx Data Logger file format

Source tags: **VI** = ELVISmx Data Logger behaviour (defaults, flags, call order) and the NI
"Export Waveforms To Spreadsheet File (1D)" library function it uses; **LOG** = real files written by
that same function in the Oscilloscope and DSA `Log` command (see scope.md 8; the Data Logger
uses the same VI with the header switched off). Confidence H / M / L.

### 8.1 Dialogs and file handling

| Item | Value | Source, conf. |
|---|---|---|
| Where the path is chosen | a path control `File path for saved data` with a folder button beside the `Log` button (no separate save dialog). The control also exists twice: a second hidden path control `Filename` with the same options | VI, H |
| Browse dialog defaults | starting folder `C:\Users\Public\Documents` (the shared Documents folder); file-name filter `*.lvm`. The filter is the only type offered by the control's options; nothing writes LVM content (see 8.2), the name is just what the user types | VI (path-control browse options), H for the values, M for how the dialog presents them |
| Extension added automatically | not known: no code adds an extension; the name typed is used as is | VI, M |
| Default path | empty (the control's default is "Invalid file path") | VI, H |
| Log with an invalid or empty path | logging is refused, an error dialog says (in our words) that nothing was saved because the destination path is invalid and the folder button should be used | VI, H |
| Existing file | **appended to**, never replaced: both the header write and every data write use "append = true". Starting a second logging session into the same file adds a new header row in the middle of the file | VI flags H; behaviour M |
| When data are written | `Log` (toggle, caption `Logging...` while on) is only usable while the app runs. Pressing it writes the header once, then one data block per second is appended while it stays on. Blocks acquired before `Log` was pressed are not written | VI, H (commands `write header` / `write data`), M (timing) |
| A file "saved to" indicator | a path indicator named `FilePathIndicator` exists in the VI; not visible in the screenshots | VI, L |
| Line ending, encoding | CR LF (Windows), single-byte text, no byte-order mark | LOG for the library VI (CRLF), M |

### 8.2 Layout

```
<header row>  <TAB>-separated, one row, written when Log is pressed
<data rows>   <TAB>-separated, appended once per 1-s block
```

- **Header row** (VI, H for the pieces, M for exact join): for every channel whose `Logging?` chip is on,
  in channel order, two cells: the fixed text `Time` and then `<device name>/ai<n>` where
  `<device name>` is the name of the selected device (for example `myDAQ1`) and `<n>` the AI channel
  number (0 or 1 on a myDAQ; the number is the row position in the channel list). Example with both
  channels on: `Time<TAB>myDAQ1/ai0<TAB>Time<TAB>myDAQ1/ai1`. No comment lines, no date line, no
  units, no `waveform`/`t0`/`delta t` rows (the library VI is told not to write its own header).
  Whether the row ends with a trailing TAB is unknown (L).
- **Data rows** (VI H for flags; LOG for cell formats): the library VI is called with "multiple
  time columns" on, so **every channel has its own time column** followed by its value column:
  `t_ai0<TAB>v_ai0<TAB>t_ai1<TAB>v_ai1`. One row per sample; a block holds `round(rate)` rows
  (1 second). The two time cells of a row are expected to be equal for a shared sample clock (M: the Scope log shows the two channels' `t0` 2 microseconds apart, so they may differ slightly).
  No trailing TAB on data rows (LOG).
- **Time cell**: the absolute wall-clock time of that sample (block start plus k / rate), written as
  the computer's *short date*, **two spaces**, then the 24-hour time with **six fractional digits**:
  `9/28/2026  06:28:38.381043` (LOG, H for the shape; the date part follows the Windows region
  setting: `M/D/YYYY` on the test PC, so the observed sample is US style).
- **Value cell**: volts in exponent notation with **six decimals** and an unpadded exponent, for
  example `5.174413E+0`, `-1.220740E-2` (negative and E-n forms confirmed in real Scope and DSA log
  files; LOG, H for that VI; the logger passes raw volts, so no
  units in the file).
- Decimal separator: `.` on the test PC; the library formatting is not locale-aware for numbers
  (LOG, M). Delimiter: TAB; the delimiter input is not wired (VI, H).
- No blank line between blocks, no row numbering, no flush marker (M).

### 8.3 Synthetic example (ours; not an NI file)

Two channels, 1000 S/s, logging started at 10:15:00 on 28 Sep 2026, US regional settings (`<TAB>`
stands for one tab character, every line ends with CR LF):

```
Time<TAB>myDAQ1/ai0<TAB>Time<TAB>myDAQ1/ai1
9/28/2026  10:15:00.000000<TAB>1.234560E-1<TAB>9/28/2026  10:15:00.000000<TAB>-4.500000E-1
9/28/2026  10:15:00.001000<TAB>1.240012E-1<TAB>9/28/2026  10:15:00.001000<TAB>-4.499871E-1
9/28/2026  10:15:00.002000<TAB>1.229874E-1<TAB>9/28/2026  10:15:00.002000<TAB>-4.501003E-1
...  (997 more rows for the first second, then the next second appended)
```

With only `ai1` on the header would be `Time<TAB>myDAQ1/ai1` and rows `t<TAB>v`.

### 8.4 What only a real ELVISmx file can settle

1. The exact header row: device-name text, `Time` caption, trailing tab or not, line ending.
2. Whether `Time` cells really are absolute date-time strings in the Data Logger (LOG shows it
   for the library VI; the logger's own waveforms could carry a different `t0`), and how the
   date part looks with a non-US region (asked only to document; MYBENCH writes ISO by default).
3. Whether a value column ever holds another format (small and negative values are already
   confirmed for the library VI).
4. The browse dialog: window title, file types offered besides `*.lvm`, default file name, whether
   a name typed without extension gets one, behaviour when picking an existing file (overwrite
   prompt or silent append).
5. Second `Log` press in one run, or a new `Start` with `Log` already on: header repeated? file
   reopened?
6. What happens to `Log` when the file cannot be written mid-run (disk removed).
7. Timestamp drift: whether the time of block N+1 continues exactly from block N.

### 8.5 MYBENCH decision

Keep the native layout of §4 as default. Add a third **Layout** choice, `ELVISmx compatible` (file extension `.txt`, not `.lvm`: the content is not LabVIEW Measurement format), in the
Log File card that writes §8.2 exactly (per-channel Time/value pairs, absolute date-time in the ELVISmx short-date form,  `E` values, no `#` header, append to an existing file) so that scripts written
for the ELVISmx logger keep working; the DMM extension columns are not written in that layout.
Audit rows `log` #5-#7.
