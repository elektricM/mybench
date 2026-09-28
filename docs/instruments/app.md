# MYBENCH application shell and common conventions (`app`)

This document specifies the MYBENCH application shell (the `mybench` executable, `app/`) and every
convention shared by the instrument panels. Instrument specs (`dmm.md`, `scope.md`, `gen.md`,
`bode.md`, `dsa.md`, `dio.md`) only describe what is specific to them and refer here for the rest.
It replaces the former `launcher.md` (the ELVISmx Instrument Launcher) and the common-conventions
part of `README.md`; `README.md` keeps the instrument index and the myDAQ hardware constraints.
The overall design and technology choices are in [`docs/UI.md`](../UI.md) (Qt Quick / QML, own
design system, `MyBench.Plot`, C++ controllers, worker threads); this spec makes them precise.

Keywords: **must** = required, **should** = strongly recommended, **may** = optional.
**[MYBENCH extension]** marks features that ELVISmx does not have.

## 1. Purpose

MYBENCH is one application window in which all myDAQ instruments live together: they share one
device connection (DeviceHub), one resource arbiter, one session file and one design system. Every
ELVISmx instrument feature is available; the presentation is modern (panels, inspector cards, run
bars, dashboard tiles) but keeps the ELVISmx names of instruments, groups, controls and values so
that ELVISmx users recognize everything.

## 2. Shell layout

```
┌──────┬────────────────────────────────────────────────────────────────┬────────────┐
│ nav  │ TOP BAR: [● myDAQ (036CD1C6) ▾]  [AI: Scope] [AO: Generator]    │ DASHBOARD  │
│ rail │          [DIO: 4 lines]  [DMM]   [■ Stop all]  session ▾  ◐  ?  │ COLUMN     │
│      ├────────────────────────────────────────────────────────────────┤ (pinnable) │
│ Home │ [Scope ×] [Waveform Generator ×] [Multimeter ×]        [⫽ split]│ ┌────────┐ │
│ DMM  │ ┌──────────────────────────────┬─────────────────────────────┐ │ │ DMM    │ │
│ Scope│ │ panel: display │ inspector    │ panel (split pane)          │ │ │ 1.235 V│ │
│ Gen  │ │                │ (cards)      │                             │ │ └────────┘ │
│ Bode │ │                │              │                             │ │ ┌────────┐ │
│ Spec │ │ run bar ▶ Run  Single  status│                             │ │ │ DIO    │ │
│ DIO  │ └──────────────────────────────┴─────────────────────────────┘ │ │ ●●○●○○●○│ │
│      │ STATUS BAR: device · 200 kS/s × 2 · 800 kB/s       ⚠ 1 · events │ └────────┘ │
│  ?   │                                                   [help drawer ▸]│            │
└──────┴────────────────────────────────────────────────────────────────┴────────────┘
```

### 2.1 Main window

- Title: `MYBENCH` or `MYBENCH — <session name>` with a modified marker (`•`) when the session has
  unsaved changes. Icon `app`.
- Minimum size 1024 x 640 px; default 1440 x 900 px (or 90 % of the screen if smaller); geometry
  (position, size, maximized, screen) remembered. macOS: unified/expanded title bar where Qt offers
  it (traffic lights over the top bar); other platforms: normal decorated window.
- Theme: dark by default, light available (section 14).

### 2.2 Nav rail (left, ~72 px)

- Items top to bottom: `Home`, `DMM`, `Scope`, `Gen`, `Bode`, `Spec`, `DIO`; at the bottom `Help`
  (toggles the help drawer) and `Settings` (preferences dialog).
- Each item: icon (`instrument-<id>`, theme-tinted SVG) above a short label (11 px). Tooltip: full
  panel title and what it replaces ("Waveform Generator - FGEN, ARB and Waveform Editor").
- Selected item (the focused panel) has an accent pill behind the icon.
- **Running indicator**: small dot at the icon's top-right: green = running, amber = paused
  (preempted) or waiting for trigger, red = stopped by an error (until acknowledged). No dot = idle.
- Click opens the panel (as a new tab in the focused pane) or focuses it if already open.
  `Ctrl/⌘+click` opens it in the other split pane (creating the split if needed).
- This rail plus the Home tiles replace the ELVISmx Instrument Launcher (section 18).

### 2.3 Top bar (~44 px)

Left to right:
1. **Device selector** (section 4): status dot + device name + `▾`. Menu: device list, `Rescan`,
   `Device info...`, `Reset device...`.
2. **Resource chips** (section 5): one chip per held resource, e.g. `AI: Scope`, `AO: Generator`,
   `DIO: 4 lines`, `DMM`; chip color = holder's state (green running, amber paused). Click focuses the
   holder; chip context menu: `Stop <panel>`. No chips when nothing runs (a muted "Idle" text).
3. **Stop all** button (red square icon + text): stops every running or paused panel (all go to
   Idle, outputs to their safe state). Disabled when nothing runs. Shortcut `Ctrl/⌘+.`.
4. **Session** menu button (session name): New, Open..., Save, Save As..., Recent ▸, Revert panel to
   defaults, Revert all to defaults (section 11).
5. **Theme** toggle (dark/light).
6. **Help** toggle (help drawer) and **command palette** button (`Ctrl/⌘+K`).

### 2.4 Workspace

- Panels are shown as **tabs**; a tab shows the panel title, the channel/run-state dot and a close
  button. Tabs can be reordered by dragging and dragged into the other pane.
- **Split view** (`SplitView`): single pane (default) or two panes side by side (e.g. Scope | Waveform
  Generator); the split handle is draggable; `Ctrl/⌘+\` toggles the split. **[MYBENCH, may]**
  vertical split as an option.
- **Dashboard column** (right, ~260 px, pinnable from the top bar or `Ctrl/⌘+Shift+D`): a vertical
  stack of instrument tiles (section 3.3) chosen by the user (`+` menu at the top of the column,
  drag to reorder, `×` to unpin). Default pinned tiles: DMM and DIO.
- One panel per instrument id: opening an already open panel focuses its tab.
- Closing a panel's tab stops the instrument (safe state, section 6.6) and closes the panel; its
  settings stay in the session (reopening restores them).

### 2.5 Help drawer

A right-side drawer (default width 420 px, resizable 320-720 px) over or beside the workspace
showing `docs/help/<id>.md` of the focused panel (Home: `docs/help/app.md`), rendered with
`Text.MarkdownText` in a scrollable view, theme colors, headings and tables supported. Relative links
to other help pages open in the drawer; external links open the system browser. A small index
(`Contents ▾`) lists all help pages. `F1` toggles it; it follows the focused panel while open.
Help pages are compiled into the app as resources.

### 2.6 Status bar (~24 px)

- Left: device status (dot, name, `connected` / `simulated` / `disconnected`).
- Middle: focused panel's acquisition summary, provided by its controller (e.g.
  `AI 200 kS/s × 2 · 800 kB/s`, `AO 200 kS/s · regenerating`, `DMM 5 readings/s`).
- Right: warning/error counter (`⚠ 1`) opening the **event log** drawer (section 9.3).

### 2.7 Home

- **Device card** (top left): device name and status dot, product type, serial number, `SIMULATED`
  badge for the simulator, connection state, buttons `Rescan`, `Reset device...`, `Device info...`,
  and for the simulator **[MYBENCH extension]** `Simulator settings...` (test signals, noise, RC
  corner, DMM values, connected toggle: the `SimDevice` controls), useful for teaching and testing.
- **Instrument tiles**: a responsive grid (2-4 columns) with every instrument's live tile.
- **Recent sessions**: the last 10 session files (name, path, date), click opens; `Open...`, `New`.
- **Getting started**: link to the `app` help page.

## 3. Instruments and panels

### 3.1 Instrument list

| id | Nav label | Panel title | Replaces (ELVISmx) | Resources (section 5) | Spec |
|---|---|---|---|---|---|
| `dmm` | DMM | Multimeter | Digital Multimeter | DMM | `dmm.md` |
| `scope` | Scope | Oscilloscope | Oscilloscope | AI | `scope.md` |
| `gen` | Gen | Waveform Generator | Function Generator + Arbitrary Waveform Generator + Waveform Editor | AO | `gen.md` |
| `bode` | Bode | Bode Analyzer | Bode Analyzer | AO + AI | `bode.md` |
| `dsa` | Spec | Spectrum Analyzer | Dynamic Signal Analyzer | AI | `dsa.md` |
| `dio` | DIO | Digital I/O | Digital Reader + Digital Writer | DIO per line | `dio.md` |

ELVIS-only instruments (Variable Power Supplies, Impedance Analyzer, Two-/Three-Wire Current-Voltage
Analyzers) do not exist in MYBENCH.

### 3.2 Panel anatomy

Every instrument panel (`<Name>Panel.qml`, view only; logic in its `InstrumentController`
subclass) has:

1. **Panel header** (thin): title, instrument icon, and header actions: `Focus` (display-only mode,
   6.5), `Export ▾` (Export data..., Export image..., Export PDF...), `Help` (opens the drawer on this
   instrument), overflow menu (Revert to defaults, Trace style..., instrument-specific items).
2. **Display area**: the dark "instrument display" surface (theme token `displayBackground`) holding
   plots and readouts. This is what ELVISmx draws on black.
3. **Inspector** (right side of the panel, 300-380 px, collapsible to a 36 px strip with
   `Ctrl/⌘+I`): a scrollable column of **cards** (`Card/Section`), one per ELVISmx group, with the
   ELVISmx titles ("Channel 0 Settings", "Timebase", "Trigger", "Measurement Settings",
   "Configuration Settings", ...). Cards are collapsible (state stored in the session). Channel cards
   carry the channel color as an accent bar and chip.
4. **Run bar** (bottom of the panel, section 6).
5. **Dashboard tile** (`<Name>Tile.qml`): separate compact view (3.3).

When the panel is narrower than ~800 px the inspector becomes an overlay drawer over the display
(toggle button in the header).

### 3.3 Dashboard tiles

- Size ~240 x 140 px (Home grid and dashboard column). Content: instrument title, run-state dot,
  1-2 key values in large display font (e.g. DMM reading, Scope CH 0 frequency and Vp-p, Generator
  `AO 0 1 kHz sine`, Spectrum fundamental, DIO LEDs), and a mini preview (sparkline/mini plot) where
  meaningful. Specs define each tile's content.
- Hover shows `Run/Stop` and `Open` buttons. Click opens/focuses the panel.
- Tiles update at most 10 times per second.

### 3.4 Instrument Control equivalents

ELVISmx's per-window "Instrument Control" group maps as follows:

| ELVISmx | MYBENCH |
|---|---|
| Device combo | global device selector in the top bar (one device per session, section 4) |
| Acquisition Mode / Generation Mode: Run Continuously | run bar `Run` |
| Acquisition Mode / Generation Mode: Run Once | run bar `Single` |
| Run / Stop buttons | run bar Run/Stop toggle |
| Print | `Export image...` / `Export PDF...` (section 10.2) |
| Log | `Export data...` (section 10.1) |
| Help | help drawer (`F1`, panel header `Help`) |
| Toggle View (display-only view) | panel `Focus` mode (6.5) |
| Other instrument-specific buttons (Scope Autoscale, FGEN Sweep) | run bar actions |

The instrument specs list their controls in ELVISmx terms (including "Acquisition Mode"); this table
is how they appear.

## 4. Device handling (DeviceHub)

- `DeviceHub` (C++ singleton, `MyBench.Framework`) wraps `mydaq::DeviceManager`: device list,
  current device, connection state, hot-plug signals, rescan, reset.
- **One current device for the whole session**; every panel uses it. Default at startup: the device
  stored in the restored session if present, else `DeviceManager::defaultDevice()` (first real
  device, else the simulator).
- **Selector** entries: `DeviceInfo::name` (`myDAQ (036CD1C6)`, `Simulated myDAQ`) with a status dot:
  green = real device connected, yellow = simulated, red = selected device disconnected. The
  simulator is always listed, last.
- **Simulated device tag**: while the simulator is current, every display area shows a small
  `SIMULATED` badge (theme warning color) so simulated data are never mistaken for measurements.
- **Switching device** while any panel runs asks "Switching the device stops all running
  instruments." (`Switch` / `Cancel`), then Stop all, then switches. Settings are kept.
- **Hot-plug**:
  - The selected device disconnects: running panels stop with a banner "Device disconnected"
    (section 9); the selector shows the device as `<name> (disconnected)` in red; MYBENCH never
    switches silently to another device. When the same id comes back, the banner changes to
    "Device reconnected" with a `Resume` action (no automatic restart).
  - A real device appears while the simulator is current: if nothing runs, MYBENCH switches to it and
    shows a toast "myDAQ (036CD1C6) connected - now using it" (`Undo` action); if something runs,
    it only shows a toast with a `Switch` action.
- **Rescan**: `DeviceManager::rescan()` (also periodic, done by the manager).
- **Reset device...**: enabled only when no panel runs and the device is connected; confirmation
  "Reset <name>? All outputs go to 0 V and all digital lines become inputs."; runs
  `Device::reset()` in a worker thread with a busy indicator.
- **Device info...**: dialog with name, id, product type, serial number, simulated, connected, max AI
  and AO rates (`maxAiRate()`, `maxAoRate()`), backend (USB / simulator).
- Instruments may be used without hardware (simulator); ELVISmx's rule "connect the device before
  opening an instrument" does not apply.

## 5. ResourceArbiter

### 5.1 Resources

| Resource | Meaning | Possible holders |
|---|---|---|
| `AI` | the single hardware-timed AI task (AI 0/AI 1 **or** AudioInput L/R: the ADCs are shared) | Scope, Spectrum Analyzer, Bode |
| `AO` | the single hardware-timed AO task (AO 0, AO 1, AudioOutput L/R) | Waveform Generator (both channels in one task), Bode (AO 0) |
| `DMM` | the isolated DMM | Multimeter only (never contended) |
| `DIO n` (n = 0..7) | one digital line (read or driven) | Digital I/O (future instruments may use lines too) |

The device itself enforces `AI` and `AO` exclusivity (`DeviceError::ResourceBusy`); the arbiter
makes it visible and resolvable. DIO lines are not enforced by the device, so the arbiter is the only
guard.

### 5.2 Rules

1. A panel **claims** its resources when it starts (Run, Single, or a one-shot action that needs the
   hardware, e.g. Scope Autoscale while idle) and **releases** them when it becomes Idle, Paused or
   stops on an error. Idle panels hold nothing.
2. **Preemption**: when panel B starts (user action) and a resource it needs is held by panel A,
   the arbiter asks A to **pause**: A's controller stops its device tasks cleanly (outputs to their
   safe state), releases the resource and enters the `Paused` state; then B starts. A shows a
   banner in its display area "<A title> paused — <resource> in use by <B title>" with a `Resume`
   button (and a toast if A is not visible), e.g. "Oscilloscope paused — AI in use by Bode Analyzer".
3. **Resume** on A: if the resource is free, A restarts with its current settings. If it is held
   again (by B or another panel), Resume preempts that holder (rule 2): the latest explicit user
   action always wins. No automatic resume when B stops (the banner stays; see open questions).
4. **Who may preempt**: only explicit user actions (Run, Single, Resume, one-shot hardware actions,
   Sweep). Automatic starts never preempt: restoring a session with "resume running instruments"
   enabled starts a panel only if its resources are free, otherwise it opens Paused with the banner.
   The Multimeter's auto-start needs only `DMM`.
5. Partial claims: a panel needing several resources (Bode: AO + AI) claims all or nothing; it may
   preempt several holders at once (e.g. Scope and Generator), each gets its banner.
6. `Stop all` stops running and paused panels (banners removed).
7. Instrument-specific pause semantics are in the specs (e.g. a paused Bode sweep resumes with the
   next frequency point; a Single acquisition that was preempted is redone on Resume).
8. The arbiter is per process. If another program (or a second MYBENCH / a `mybench-<id>` dev
   executable) holds the hardware, the device throws `ResourceBusy` and the panel shows the error
   "The analog input of <device> is busy (used by another program?)." (section 9.4).

### 5.3 Presentation

Top bar chips (2.3), nav-rail dots (2.2), panel banners, event log entries ("Bode Analyzer took AI
from Oscilloscope").

## 6. Run bar and run semantics

### 6.1 Run bar layout (bottom of each panel, ~44 px)

Left: **Run/Stop toggle** (primary button: `▶ Run` when idle, `■ Stop` when running or paused),
**Single** button (`▶|`, text `Single`), then instrument actions (e.g. Scope `Autoscale`, Generator
`Sweep`). Right: status LED(s) and status text:

| Status | LED | Text |
|---|---|---|
| Idle | gray | `Idle` (or `Stopped` after a run) |
| Running | green (slow pulse) | `Running` + short summary from the controller |
| Waiting for trigger | amber | `Waiting for trigger` (Scope) |
| Timeout | instrument's Timeout LED lit | e.g. `Trigger timeout` (Scope) |
| Paused | amber | `Paused — AI in use by Bode Analyzer` + `Resume` |
| Error | red | `Stopped: <short error>` (click shows details) |

### 6.2 Mapping of ELVISmx modes

- `Run` = Run Continuously: repeat acquisitions/generation until Stop.
- `Single` = Run Once: one acquisition (or one pass of the output), then Idle by itself.
- Pressing `Single` while running continuously finishes the current record/pass, then stops (the
  ELVISmx equivalent of switching Acquisition Mode to Run Once while running).
- Instruments whose ELVISmx version has no mode (FGEN, Bode) define `Single` in their spec (or
  hide it).
- `Space` toggles Run/Stop of the focused panel; `Shift+Space` = Single (not while typing in a text
  field).

### 6.3 State machine

`Idle` → (`Run`/`Single`) → `Starting` → `Running` → (`Stop` / Single done) → `Stopping` → `Idle`.
From `Running`: preemption → `Paused` (resources released); `Resume` → `Starting`. Device error →
`Stopping` → `Error` (visible) → `Idle` when acknowledged or on the next Run. Validation errors at
start (invalid settings) show a toast/banner and stay `Idle`.

### 6.4 Workers, snapshots and live changes

- All device I/O runs in the controller's worker thread; the UI thread never blocks on the device.
  Results reach the UI through queued signals; plots are fed from C++ buffers, never QML arrays.
- A run uses an immutable **snapshot** of the settings; every change while running sends a new
  snapshot, applied by the worker at the next safe point (next acquisition, or a restart of the
  generation task). Each spec says which changes are immediate (display-only), which apply at the
  next cycle, and which are locked while running.
- UI updates are throttled (plots ≤ 60 fps, readouts ≤ 30 Hz, tiles ≤ 10 Hz).
- Stop must take effect within ~100 ms (workers use short read/wait timeouts in loops).

### 6.5 Focus mode (Toggle View)

`Focus` in the panel header (icon `toggle-view`, `Ctrl/⌘+Shift+F`) hides the nav rail, top bar,
inspector, other pane and dashboard column so the panel's display fills the window; a small floating
bar keeps Run/Stop and an exit button; `Esc` exits. The instrument keeps running. `F11` toggles
full screen independently.

### 6.6 Safe states

| Panel | On Stop / pause / close |
|---|---|
| Multimeter | nothing to reset |
| Oscilloscope, Spectrum Analyzer | nothing to reset (AI only) |
| Waveform Generator | AO task destroyed; AO 0/AO 1 set to 0 V with `aoWriteStatic` (audio outputs need nothing) |
| Bode Analyzer | AO 0 set to 0 V after the task is destroyed |
| Digital I/O | IN lines stay inputs; OUT lines keep their last value on Stop and return to inputs on close (see `dio.md`) |

Quitting the app stops everything and applies the safe states.

## 7. Controls (design system, `MyBench.Controls`)

### 7.1 SiField (all numeric entry)

- Text field with unit suffix and small up/down steppers. Engineering input via the Units parser:
  `1k`, `2.5m`, `10u`/`10µ`, `1e3`, `4,7k`, optional unit (`100 mV`, `1 kHz`); invalid text shows a
  red outline and reverts on focus loss.
- Displays with the per-control format of the spec (e.g. `10.00k`, `1.0k`, `100.00`, `0.00`,
  `1.00 Vpp`); SI prefix letter appended for large/small values as in ELVISmx.
- Out-of-range values are **coerced** to the nearest limit (never rejected); integer fields round.
- Steppers, `Up`/`Down` keys and the mouse wheel change the value by the control's increment
  (from the spec) and coerce.
- **Per-digit tuning** [MYBENCH extension]: hovering a digit and scrolling, or placing the caret on
  a digit and pressing `Up`/`Down`, changes that digit's decade (carry propagates), like bench
  instruments; `Left`/`Right` move between digits.
- Commit on Enter, focus loss or stepper click; `Esc` reverts; `Tab` moves on.
- Disabled look: muted text and background. Tooltips show the allowed range and unit.

### 7.2 Knob

Kept where ELVISmx has knobs (Volts/Div, Vertical Position, Time/Div, FGEN frequency, amplitude,
offset), always paired with its SiField or combo. Drawn with Qt Quick Shapes: 270-degree arc,
minimum at 7:30, maximum at 4:30, center up; pointer line; optional min/max labels at the arc ends
(`200m`/`20k`). Interaction: vertical or circular drag, wheel, arrow keys (one step), `PageUp/Down`
(10 steps), double-click resets to the default. Ring knobs step through discrete lists; continuous
knobs are linear or logarithmic (spec says). Changing the paired control moves the knob and vice
versa.

### 7.3 Choices

- Combos (styled) for ELVISmx rings, with the ELVISmx value lists. Items unsupported on the myDAQ are
  listed **disabled** with the tooltip "Not available on NI myDAQ" where ELVISmx shows them (e.g.
  Scope trigger `Digital`), or omitted where the spec says.
- `SegmentedControl` for small enums where it improves speed (DMM functions, Slope, Direction,
  In/Out); labels identical to ELVISmx items.
- `ToggleSwitch`, checkboxes, `Led` (round, glossy, on/off colors from the theme or the spec),
  radix selector (small prefix letter `d/x/o/b/p` opening a menu: Decimal, Hex, Octal, Binary,
  SI Notation), `ChannelChip` (channel color + name), `Readout` (large display-font value + unit),
  `IconButton`.

### 7.4 Accessibility

Every control is reachable by keyboard, has an accessible name equal to its ELVISmx label and a
tooltip; focus is visible; colors are never the only carrier of state (LEDs have text states in
tooltips, channel chips have names).

## 8. Plots (`MyBench.Plot`)

### 8.1 Rendering

C++ scene-graph item (RHI), fed from C++ buffers, min/max decimation (`dsp::minMaxDecimate`) when
there are more samples than pixels so every peak stays visible; linear/log axes; graticule; traces;
cursors; markers; zoom/pan. It serves the Scope, spectra, Bode, trends, the DIO timeline and previews.

### 8.2 Colors and families (theme tokens; values of the classic dark look)

| Token | Value | Use |
|---|---|---|
| `displayBackground` | #000000 (dark theme may use #050607) | plot and readout surfaces |
| `scopeGrid` | #009900 | Scope graticule |
| `scopeAxis` | #64FF00 | Scope center axes and minor ticks |
| `analyzerAxis` | #00FFFC | axis numbers, titles, major grid (Bode, Spectrum, Generator preview) |
| `analyzerGrid` | #006189 | minor grid |
| `ch0` | #64FF00 (green) | CH 0 / first trace, everywhere (chips, cards, traces) |
| `ch1` | #11CBFF (cyan) | CH 1 / second trace |
| `math` | amber (#FFB000) | math traces |
| `ref` | violet (#B388FF) | reference / overlay traces |
| `cursor1` | yellow (#FFFF00) | cursor C1 |
| `cursor2` | magenta (#FF5AFF) | cursor C2 |
| `displayText` | #00FFFC | cyan readouts |
| `displayYellow` | #FFFF33 | secondary readouts |

The Scope family draws no axis numbers by default (ELVISmx); analyzers draw numbers and titles.

### 8.3 Interaction

- Wheel: zoom X about the pointer; `Shift+wheel`: zoom Y; `Ctrl/⌘+drag`: rubber-band zoom;
  drag on empty area while zoomed: pan; double-click: fit (autoscale both axes once).
- An overlay toolbar (shown on hover, top-right of the plot) replaces the LabVIEW graph palette:
  pointer tool, zoom tool menu (rectangle, X only, Y only, zoom in/out about point, undo zoom), pan
  tool, `X autoscale` and `Y autoscale` toggles (click = once, lock icon = continuous).
- Context menu: Autoscale X, Autoscale Y, Reset zoom, Cursors on/off, Trace style..., Copy image,
  Export image..., Export data...
- Draggable markers where specs define them (trigger level/position, Bode -3 dB marker...).

### 8.4 Cursors

- `Cursors On` shows the cursors; each is a vertical line with a marker where it meets its linked
  trace; drag horizontally (hit area ±6 px); it snaps to the nearest data point of its linked trace.
  `Position Left/Right` buttons (where the spec has them) and the arrow keys move the selected
  cursor by one point. Colors `cursor1`/`cursor2`; single cursors use `cursor1`.
- Readouts are drawn in the display area in the cursor colors. On new data, cursors keep their x
  position (time or frequency), clamped to the data range.

### 8.5 Trace style (Graph Properties equivalent)

`Trace style...` (context menu / overflow menu / Scope's Graph Properties card) edits per trace:
color, line style (Solid, Dash, Dot, Dash Dot, Dash Dot Dot), line width (1-5 px), point style (None,
Circle, Filled Circle, Square, Filled Square, Diamond, Cross, X), interpolation (None/points only,
Linear, Step), fill to (None, Zero, -Infinity, +Infinity; 50 % alpha), plot background; per cursor:
style (Vertical Line, Full Crosshair, Small Crosshair), color, point style, linked trace. Changes
apply immediately and are stored in the session.

## 9. Notifications and errors

### 9.1 Toasts

Bottom-right of the window, auto-hide after 5 s (errors 10 s), max 3 stacked, severity color
(info, success, warning, error), optional action button (`Undo`, `Switch`, `Resume`, `Details`).

### 9.2 Banners

Inside a panel, at the top of its display area, persistent until resolved or dismissed: preemption
(`Resume`), device disconnected/reconnected (`Resume`), errors that stopped the panel (`Details`,
`Dismiss`). One banner per panel at a time (newest wins; older ones go to the event log).

### 9.3 Event log

Drawer listing every notification with time, panel, severity, message and technical details (error
code name + backend text); `Copy`, `Clear`. Also written to the application log (`qWarning`).

### 9.4 Error messages

`mydaq::DeviceError` during a run: the worker stops, releases its tasks, the panel shows an error
banner (and a toast if not visible) and goes to `Error`/`Idle`. The app never crashes on device
errors.

| Condition | Message |
|---|---|
| `NotConnected` | "The device is not connected. Connect the myDAQ (or select another device) and press Run again." |
| `ResourceBusy` inside MYBENCH | handled by preemption (section 5), not an error |
| `ResourceBusy` from outside | "The <analog input / analog output> of <device> is busy (used by another program?)." |
| `InvalidConfig` | "The device rejected the settings: <message>." |
| `Timeout` (unexpected) | "The device did not answer in time. <message>" |
| `Overflow` | "Data were lost because the computer could not read fast enough. Try a slower timebase / lower sample rate." |
| `Underflow` | "The output ran out of data because the computer could not keep up. The generation was stopped." |
| `UsbError` | "USB communication error: <message>. Check the cable and reconnect the device." |
| `NotSupported` | "This operation is not supported by the selected device: <message>." |
| `Generic` / other | "<message>" |

Validation messages defined by the specs (e.g. "Enable at least one channel.") are shown as toasts
next to the run bar (or banners when they block a start).

## 10. Export

### 10.1 Export data (ELVISmx Log)

- `Export data...` (panel header / context menu / `Ctrl/⌘+L`) writes the data of the last completed
  record through **DataExport**: CSV (default, `.csv`), TSV (`.tsv`/`.txt`); sigrok session `.sr` is
  planned (PulseView). Available whenever a completed record exists, also while running (it exports
  a consistent copy of the last completed record) **[MYBENCH: ELVISmx allows Log only when idle]**.
- Every file starts with comment lines:

  ```
  # MYBENCH <version> - <Panel title / instrument name>
  # Date: <ISO 8601 local date-time with offset>
  # Device: <DeviceInfo::name> (<productType>, serial <serialNumber>)[, simulated]
  # <instrument-specific settings, one "Key: value" per line>
  ```

  then one header row and the data rows (`.` decimal point, up to 10 significant digits, empty cell
  for NaN). Columns may have different lengths. Specs define the settings lines and columns.
- Suggested file name `<id>_<yyyyMMdd_HHmmss>.csv` in the last used directory.

### 10.2 Export image / PDF (ELVISmx Print)

- `Export image...` (`Ctrl/⌘+P` opens the export dialog): PNG of the panel's display area (or the
  whole panel, option), rendered at 2x device pixel ratio via `grabToImage` / offscreen rendering.
- `Export PDF...`: one A4/Letter landscape page with the panel image, the instrument's settings (the
  same key/value lines as the data export header) and date/device, written with `QPdfWriter`.
- Direct printing is not provided (print the PDF). ELVISmx enables Print only after an acquisition;
  MYBENCH allows export any time.

## 11. Sessions

### 11.1 Content (`.mybench`, JSON, UTF-8)

```json
{
  "format": "mybench-session",
  "version": 1,
  "app": { "name": "MYBENCH", "version": "0.1.0" },
  "savedAt": "2026-09-28T12:34:56+02:00",
  "device": { "id": "usb:036CD1C6", "name": "myDAQ (036CD1C6)" },
  "layout": {
    "theme": "dark",
    "panes": [ { "tabs": ["scope", "gen"], "current": "scope", "size": 0.6 },
               { "tabs": ["dmm"], "current": "dmm", "size": 0.4 } ],
    "dashboard": { "pinned": true, "tiles": ["dmm", "dio"] },
    "helpDrawer": { "open": false, "width": 420 },
    "collapsedCards": { "scope": ["Advanced"] }
  },
  "instruments": {
    "scope": { "settingsVersion": 1, "running": false, "settings": { "...": "..." } },
    "dmm":   { "settingsVersion": 1, "running": true,  "settings": { "...": "..." } }
  }
}
```

- Each controller serializes **all** its settings (every control of its spec, trace styles, cursor
  positions, labels, embedded documents such as the Generator's arbitrary waveforms) to a JSON
  object with stable camelCase keys, and restores from it: unknown keys ignored, missing keys take
  the documented default, out-of-range values coerced. `settingsVersion` allows migrations.
- Acquired data are not stored (export them instead).
- Window geometry is a UI preference, not part of the session.

### 11.2 Behaviour

- **Fresh session** (`Session > New`, first start, `--fresh`): every instrument at its documented
  defaults (the ELVISmx launch state), layout = Home.
- **Autosave**: the current session is saved to the app data directory (`autosave.mybench`) 2 s
  after each change (debounced) and on quit; on startup it is restored (preference, default on).
  Named sessions are saved only by Save/Save As; the title shows `•` while they have unsaved changes.
- **Open**: replaces the current session (asks to save a modified named session first), stops all
  instruments, applies layout and settings; selects the stored device if present, else keeps the
  current one with a toast "Session device <name> not found; using <current>".
- **Running instruments** are not restarted on open unless the preference "Resume running
  instruments when opening a session" is on (default **off**: outputs must never start
  unexpectedly); the Multimeter auto-starts as its spec says. Restarts obey rule 5.2-4.
- `Revert panel to defaults` / `Revert all to defaults` reset settings (confirmation; running panels
  apply the defaults live or restart).
- Recent sessions: last 10, shown on Home and in the Session menu.

## 12. Preferences (`QtCore.Settings`)

Theme; restore last session on start (on); resume running instruments when opening a session (off);
confirm quit while instruments run (on); last directories per file type; recent sessions; window
geometry; help drawer width. `Settings` in the nav rail opens a small preferences dialog.

## 13. Keyboard and command palette

| Shortcut | Action |
|---|---|
| `Space` / `Shift+Space` | Run/Stop / Single of the focused panel (not in text fields) |
| `Ctrl/⌘+.` | Stop all |
| `Ctrl/⌘+K` | command palette |
| `F1` | help drawer |
| `Ctrl/⌘+0` ... `Ctrl/⌘+6` | Home, DMM, Scope, Gen, Bode, Spec, DIO |
| `Ctrl/⌘+W` | close panel tab |
| `Ctrl/⌘+\` | toggle split view |
| `Ctrl/⌘+Shift+D` | toggle dashboard column |
| `Ctrl/⌘+I` | toggle inspector |
| `Ctrl/⌘+Shift+F`, `Esc` | focus mode on / off |
| `F11` | full screen |
| `Ctrl/⌘+L` | export data |
| `Ctrl/⌘+P` | export image/PDF |
| `Ctrl/⌘+N`, `Ctrl/⌘+O`, `Ctrl/⌘+S`, `Ctrl/⌘+Shift+S` | session new/open/save/save as |
| `Ctrl/⌘+Q` | quit |

Instrument specs may add panel shortcuts (e.g. Scope Autoscale). The **command palette** is a
filterable list (fuzzy match) of commands: open/focus panel, Run/Stop/Single <panel>, Stop all,
instrument actions (Autoscale, Sweep, Restart averaging...), export, session commands, device
commands (rescan, reset, switch), theme, help pages. It shows each command's shortcut.

## 14. Themes

- **Dark** (default): near-black display surfaces, dark-gray panels/cards (~#1E2124 / #25292D),
  light text, accent blue.
- **Light**: light-gray panels/cards, dark text; display areas stay dark by default (instrument
  look, as in ELVISmx); **[MYBENCH, may]** a "light displays" option switches plots to a light
  background with darkened trace colors.
- Theme is a QML singleton (`Theme`); all colors come from tokens (section 8.2 and control tokens);
  instruments never hard-code colors.

## 15. Executables and command line

- `mybench [options] [session.mybench]` - the application.
- `mybench-<id>` (`mybench-dmm`, `mybench-scope`, `mybench-gen`, `mybench-bode`, `mybench-dsa`,
  `mybench-dio`) - dev executables showing only that panel (display, inspector, run bar) with a
  minimal top bar (device selector, help). Same device/screenshot options; their own arbiter.
- Options:

| Option | Effect |
|---|---|
| `--device <id>` | use this device (`sim:0`, `usb:036CD1C6`) |
| `--simulated` | same as `--device sim:0` |
| `--panel <id>` | open and focus this panel (repeatable) |
| `--fresh` | ignore the autosave; documented defaults |
| `--screenshot <file.png>` | render offscreen (works with `QT_QPA_PLATFORM=offscreen`), save the image, quit |
| `--delay <ms>` | wait before the screenshot (default 500 ms; lets data arrive) |
| `--size <W>x<H>` | window size for the screenshot (default 1440x900) |
| `--run <id>` **[MYBENCH, may]** | start that instrument after launch |
| `--version`, `--help` | |

## 16. Number formatting

Readouts use the Units formatter (`dsp::formatEng`: SI prefixes f p n µ m k M G T, µ = U+00B5,
Ω = U+03A9) unless the spec gives a fixed format. Undefined values: `?` in Scope measurements
(ELVISmx style), `---` elsewhere; `Inf`/`-Inf` where ELVISmx shows it (Bode Gain (dB) at gain 0).

## 17. About and help pages

- **About** (from the Help menu / palette): `MYBENCH`, version, one-line description ("Open-source
  instruments for the NI myDAQ"), license with a `License...` button, a notice in our own words that
  MYBENCH is independent and not affiliated with or endorsed by NI (NI, myDAQ, ELVIS and LabVIEW are
  trademarks of their owner), credits, Qt version, whether the USB backend is built in
  (`MYBENCH_WITH_USB`).
- Help pages: `docs/help/app.md` (getting started, shell) and one per instrument id.

## 18. Former ELVISmx Instrument Launcher

| ELVISmx launcher | MYBENCH |
|---|---|
| Instruments & Apps grid (one round button per instrument) | nav rail + Home tiles |
| Launching an instrument opens its own window | opens/focuses its panel (one panel per instrument) |
| Each instrument also startable from the Start menu | `--panel <id>`, dev executables `mybench-<id>` |
| Help button in every instrument | help drawer |

Not replicated: the `Add Item` tile (shortcuts to other programs, downloadable featured apps), the
Favorites tab (star on tiles), the Labs tab, the My Files tab, the Resources tab (documents,
tutorials, forum links, `Add Link`), and the ELVIS-only instruments. Recent sessions on Home cover
part of "My Files".

## 19. Checks against the simulator

- Start with no hardware: yellow dot, `SIMULATED` badges, every panel runs.
- Run Scope, then Run Bode: Scope shows "Oscilloscope paused — AI in use by Bode Analyzer"; chips
  `AI: Bode Analyzer`, `AO: Bode Analyzer`; Resume on Scope pauses Bode.
- Generator running on AO 0 + Bode Run: Generator paused; after Stop, AO 0 reads 0 V.
- Stop all: every panel idle, no chips.
- `SimDevice::setConnected(false)` during a run: disconnect banners, red dot; `true`: "Device
  reconnected" + Resume.
- Save a session with modified settings, `Revert all to defaults`, Open the session: settings back.
- `mybench --fresh --screenshot /tmp/x.png` with `QT_QPA_PLATFORM=offscreen` writes a 1440x900 image.
- Space toggles the focused panel; `Ctrl/⌘+K` finds "Scope: Autoscale".

## 20. Open questions / assumptions

1. **Auto-resume after preemption**: we require an explicit Resume (predictable, no surprise
   outputs). An opt-in "resume when free" could be added to the banner.
2. **Session autosave/restore** replaces ELVISmx's "defaults at every launch"; a fresh session still
   gives the ELVISmx defaults.
3. **Export while running** is allowed (ELVISmx: Log only when idle).
4. **Direct printing** is dropped in favour of PDF export.
5. **Global device**: ELVISmx lets each instrument pick its device; MYBENCH uses one device per
   session (multi-device sessions could come later with a per-panel override).
6. **Dark theme panel colors** and exact sizes are guidance for the design-system work in
   `ui/controls/`.
