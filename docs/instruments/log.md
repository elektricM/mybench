# Data Logger (`log`)

Read [app.md](app.md) first. Behaviour recovered from the ELVISmx featured application:
[algorithms/featured.md §3](algorithms/featured.md). MYBENCH version of the ELVISmx **Data Logger**
featured app (slow multi-channel voltage logging with a strip chart and a text file).

## 1. Purpose

Record AI 0 and/or AI 1 (and optionally the DMM) for minutes to days at 1 – 1000 S/s: a live
chart of the whole recording that you can scroll back through while it grows, per-channel
statistics, and a delimited text file written while recording.

## 2. ELVISmx parity

| ELVISmx | Values / default | MYBENCH |
|---|---|---|
| Channel Select (ai0, ai1; Logging?) | ai0 on | **Channel Select** chips AI 0 (on), AI 1 (off) |
| Sampling Rate (S/s) | 1 … 1000, step 1, `%.2f`; default 10 | **Sampling** card, same range/default/format |
| Maximum (V) / Minimum (V) | −10 … +10, step 0.1; +10 / −10 | **Input Limits** card; limits within ±2 V use the ±2 V range, otherwise ±10 V; also the fixed chart scale |
| Auto-scale graph | on/off | toggle in Input Limits (default on) |
| Log toggle ("Log"/"Logging…") | | **Record** / **Stop Recording** (run bar) |
| File path for saved data | | **Log File** card: folder, name, format, options |
| AI task | continuous, buffer 10 × rate, 1-s blocks | continuous, buffer 10 × rate; reads what is available every ~50 ms (display ≥ 10 Hz) |
| File | per channel `Time`, `<dev>/ai<n>` columns, Tab, appended per block | see §4 (one shared time column for the AI channels, which share one sample clock) |
| Invalid path | "no data was saved" | warning toast "Could not create <file> (<reason>): no data was saved." |

## 3. MYBENCH design

- **Run** acquires and charts (status *Monitoring*); **Record** opens a new file and streams to it
  (starting the logger if idle); **Pause** / **Resume** suspend writing (the chart continues; the
  gap shows in the time column); **Stop Recording** closes the file (toast with the row count);
  **Stop** ends everything and closes the file. **Single** does not exist.
- Options: *Record when Run starts*, *Stop after* N minutes (closes the file and stops the run).
- **Channels**: AI 0, AI 1 (differential, one task), **DMM** [MYBENCH extension] with function
  (DC V, AC V, DC A, AC A, Resistance) and range (Auto = software autoranging, or a fixed range,
  largest first). The DMM is read at its own pace (~10/s DC, ~3/s AC) in the same loop.
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

- ELVISmx's exact time-column format (absolute vs relative) is unknown; MYBENCH writes relative
  seconds plus the start time (and an optional timestamp column).
