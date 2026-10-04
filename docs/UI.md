# MYBENCH UI design (v2)

MYBENCH is **one desktop application** for the NI myDAQ. It keeps every ELVISmx capability (see
`docs/instruments/*.md`), but not ELVISmx's look, naming or ordering. It presents the instruments in
one calm, dense, professional window that behaves like a native Qt application.

> **Round 2 (2026-10-04, Martin):** the instrument panels follow a PicoScope 7–style layout: knobs are
> back for primary parameters (Knob + typeable value + − / +), primary controls always visible,
> segmented and picture buttons instead of dropdowns, channel cards and a tool palette with floating
> panels on the right of the display, measurement cards with statistics. Dark is the default theme.
> Where this conflicts with §1, §6 or the "no knobs" wording below, this note wins.

This document is the **contract** for every UI change. It supersedes UI v1 (2026-09) and
`docs/ui-proposal.md`. Migration progress: `docs/audit.md`, section "UI v2". If code and this
document disagree, this document wins; change it first if it is wrong.

## Technology

- **Qt Quick / QML with Qt Quick Controls**, Qt ≥ 6.9 (developed on 6.11):
  - `qt_add_qml_module` with qmlcachegen;
  - `QML_ELEMENT` / `QML_SINGLETON` from C++;
  - `pragma ComponentBehavior: Bound` and required properties;
  - no context properties.
- **Our own style `MyBench.Style`** (Templates-based, in `ui/controls/style`), driven by the system
  palette. Native Qt styles are not used: the macOS style draws no bezels under
  `QT_QPA_PLATFORM=offscreen`, which CI and our screenshot checks use.
- **Native integration:**
  - `MenuBar`: the global menu on macOS, in the window elsewhere;
  - `ContextMenu` (6.9) or `Menu { popupType: Popup.Native }` for context menus;
  - `QtQuick.Dialogs` (FileDialog, MessageDialog, ColorDialog);
  - `QtCore.Settings`, `Action` / `Shortcut` / `StandardKey`;
  - the unified title bar on macOS (`Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint`,
    `SafeArea`);
  - `Application.styleHints.colorScheme` set from the Appearance preference.
- **Plots are our own scene-graph item** (`MyBench.Plot`, C++ `QQuickItem`, RHI): fed from C++
  buffers, decimated, colored from Theme through `BenchPlot`.
- **Controllers own logic and state.** C++ controllers (`InstrumentController` subclasses) hold all
  instrument state and logic; QML is view only. Device work runs in worker threads, and results reach
  the UI through queued signals.

---

## 1. Principles

1. **Content first.** At 1440×900 the instrument display gets at least 60 % of the window. Chrome is
   quiet, flat and neutral.
2. **Native citizen.** System palette, accent, font and size; native menus, context menus and
   dialogs; platform shortcut conventions; native scrolling. If Qt provides something, use it before
   drawing our own.
3. **Everything once.** Each setting has exactly one editable control. The display may show a
   read-only summary of it ("5 ms/div" in the info line), never a second editor. Each value appears
   in one place.
4. **Show what matters now.** Hide controls that don't apply (Immediate trigger → no level or slope).
   Disable a control only while it is temporarily locked, such as while running, and give the reason
   in a tooltip.
5. **Calm color.**
   - The accent marks selection, focus and the one primary action.
   - Status colors appear only as small dots and short text, never as filled buttons or glows.
   - Signal colors identify channels and nothing else.
6. **One place per state.**
   - Device: the toolbar device button.
   - Run state: the sidebar row and the pane header.
   - Resource use and messages: the status bar.
   - Errors and conflicts: an inline banner in the affected pane, plus the event log.
7. **Desktop density.** Sized for mouse and keyboard; every control can be reached by keyboard.

Banned: cards inside panes, ALL-CAPS or letter-spaced labels, glows, gradients, specular
highlights, pulsing animations, badges for state ("SIMULATED", "SIM", "DEV", "RANGE"), green or red
filled buttons, a knob paired with a field for the same value, decorative icon boxes.

## 2. Color

`Theme` derives every chrome color from the **effective palette**:
- When the system palette's lightness matches the chosen mode, the system palette is used.
- Otherwise a built-in light or dark **reference palette** is used. That is the case when Qt reports
  no color scheme (offscreen, CI, some KDE schemes) and when the user forces the opposite of the
  desktop.
- The reference palette is what screenshots show, so it gets the same design care: neutral grays
  close to macOS and Breeze.
- A forced Light or Dark sets `Application.styleHints.colorScheme`, so native dialogs, menus and the
  title bar follow; "System" resets it.

| Token | Derived from | Used for |
|---|---|---|
| `window` | `palette.window` | toolbar, pane headers, inspector, status bar |
| `sidebar` | `window` moved 4 % toward `windowText` (light) or toward black (dark) | sidebar |
| `base` | `palette.base` | fields, tables, Overview background, popups |
| `display` | light: `base`; dark: `base` mixed 35 % toward black. `alwaysDarkDisplays` forces the dark value | plot and readout surfaces |
| `separator` | `windowText` at 12 % (light) / 16 % (dark) | every 1 px hairline |
| `controlFill` / `controlBorder` | `palette.button` / `windowText` at 22 % | buttons, fields, combos |
| `hoverFill` / `pressedFill` | `windowText` overlay at 5 % / 9 % | rows, tool buttons |
| `selection` / `selectionText` / `selectionInactive` | `palette.highlight` / `palette.highlightedText` / `windowText` at 10 % | sidebar and list selection; inactive when the window is unfocused |
| `accent` / `accentText` | `palette.accent` adjusted to ≥ 3:1 on `window` / readable text on it | focus ring, primary button, toggles that are on, links |
| `text`, `textSecondary`, `textTertiary`, `textDisabled` | `windowText`; mixed 30 % and 50 % toward `window`; the disabled group | values, labels, hints |
| `positive` / `caution` / `negative` | fixed per theme: dark #3FB950 / #D29922 / #F85149, light #1A7F37 / #9A6700 / #CF222E | state dots, banner icons, the border of an invalid field |

**Display tokens**
- `display`, `displayText`, `displayTextSecondary` (axis labels);
- `plotGridMajor` = text at 14 %, `plotGridMinor` = text at 6 %, `plotFrame`;
- `displayDark` (bool) = dark, or `alwaysDarkDisplays`.
- The plot font is the caption font. The C++ plot no longer has its own palette: `BenchPlot` binds
  every color role and the font from Theme.

**Signal palette** (identity only). `tst_theme_contrast` checks ≥ 3:1 contrast against the display
in both schemes, and that the colors are distinct from each other.

| Token | Signal | Dark display | Light display |
|---|---|---|---|
| `signalCh0` | CH0 / AI0 / Audio L in | #4CC38A | #1E8E5A |
| `signalCh1` | CH1 / AI1 / Audio R in | #3FB8E0 | #0B7FAB |
| `signalAo0` | AO0 / Audio L out | #F2994A | #C25E0A |
| `signalAo1` | AO1 / Audio R out | #E26FC0 | #B4318F |
| `signalMath` | Math | #E8C547 | #9A7A00 |
| `signalRef` | Reference | #A78BFA | #6D4FD8 |
| `signalCursor` | Cursors A/B (dashed, labelled) | `displayText` | `displayText` |

`Theme.signalColor(name)` maps the names "ai0", "ai1", "audioInL", "audioInR", "ao0", "ao1",
"audioOutL", "audioOutR", "math", "ref" and "cursor" to these tokens. The DMM reading, DIO lines and
EQ bands do not use signal colors: they use `text` and `accent`.

## 3. Typography

- **Font size:** the system UI font at the platform's default size (macOS SF 13, Windows Segoe UI
  9 pt, KDE/GNOME the user's font). Only offscreen screenshots force 13 px, so they are reproducible.
- **Scale:** every size derives from `fontSizeBase`.

  | Token | Size and weight | Used for |
  |---|---|---|
  | `fontSizeCaption` | base − 2 (≥ 10 px) | |
  | `fontSizeSmall` | base − 1 | |
  | `fontSizeBody` | base | |
  | `fontSizeHeading` | base + 1, DemiBold | section headers, pane title |
  | `fontSizeTitle` | base + 5, DemiBold | Overview, dialogs |
  | `fontSizeReadoutM` | base + 7 | |
  | `fontSizeReadoutL` | 2 × base | |
  | `fontSizeReadoutXL` | 4.5 × base, Light | DMM main reading |

- **Numbers:** always `font.features: Theme.tabularFigures` and a fixed number of significant digits,
  so values don't change width. `Fonts.mono` only for hex, serial numbers and code.
- **Weights:** Regular and Medium; DemiBold only for headings.
- **Case:** Title Case for menu items, buttons and section headers; sentence case for field labels,
  hints, tooltips and messages. Section titles have no "Settings" suffix: "Channel 0", "Timebase",
  "Trigger", "Log File".

## 4. Metrics, shape, elevation, motion

- **`lineHeight`** = `FontMetrics.height` of the body font. Control heights derive from it, so larger
  system fonts scale the UI:

  | Token | Height | Used for |
  |---|---|---|
  | `controlHeightS` | lineHeight + 6 (≈ 22) | plot toolbars, dense tables |
  | `controlHeight` | lineHeight + 10 (≈ 26) | |
  | `controlHeightL` | lineHeight + 16 (≈ 32) | the Run button and dialog default buttons only |

- **Spacing:** `spacingXs` 4, `spacingS` 8, `spacingM` 12, `spacingL` 16, `spacingXl` 24. Nothing
  else. Inspector padding is 12 horizontally and form rows are 6 apart; sections are separated by a
  hairline only.
- **Layout constants:**

  | Token | Value |
  |---|---|
  | `toolbarHeight` | 52 on macOS with the unified title bar, 44 elsewhere |
  | `paneHeaderHeight` | 40 |
  | `statusBarHeight` | 24 |
  | `sidebarWidth` / `sidebarCompactWidth` | 220 / 56 |
  | `inspectorWidth` | 300 (`inspectorMinWidth` 260, `inspectorMaxWidth` 420; the user's width is saved) |
  | `minWindowWidth` × `minWindowHeight` | 640 × 480 |

- **Radii:** `radiusControl` 5; `radiusPopup` 8 (popups, menus, dialogs, Overview tiles). Panes,
  display, sidebar and inspector have radius 0.
- **Elevation:** only popups, menus, dialogs and the overlay inspector have a shadow: one style
  (`shadowBlur` 16, `shadowOffsetY` 4, `shadowColor` black at 22 % dark / 12 % light). Running is a
  steady filled dot.
- **Motion:** `durationFast` 120 ms (state), `durationNormal` 200 ms (layout), `Easing.OutCubic`
  only. Both are 0 when `Theme.animations` is false (screenshots, or the OS asks for reduced motion).
- **Focus:** a 2 px accent ring outside the control, only for keyboard focus (`visualFocus`).

## 5. Icons

- One set: `assets/icons/*.svg`, on a 24 grid, strokes normalized to 1.75, round caps and joins, black.
- Shown at `iconSize` 16 in controls and `iconSizeL` 20 in the sidebar and toolbar.
- Tinted through the controls' public `icon.color`, or the `Icon` wrapper.
- No pre-tinted, `-disabled` or filled/`-line` duplicates.

## 6. Interaction conventions

- **Numbers:** `SiField { steppers: true }`.
  - Typed SI entry ("1.5k"); ↑/↓ step the digit under the caret.
  - The wheel follows the WheelGuard rule (focused, or hovered after the scroll gesture has ended).
  - Dragging the label scrubs the value; Esc reverts the edit.
  - Invalid input gets a `negative` border and a tooltip. A clamped value is always shown, never
    applied silently.
- **1-2-5 sequences** (V/div, s/div): `StepSelector` — ‹ value ▾ ›, wheel, click to open a list.
  There are no knobs.
- **Choices:**
  - 2–4 short options: `SegmentedControl` (segments sized to their content);
  - more options: `ComboBox` (press-drag-release; wheel only per WheelGuard);
  - on/off: `Switch` in section headers and forms, `CheckBox` only in lists and dialogs.
- **Plots:**
  - hover readout; wheel zoom (Shift = X only, Alt = Y only); drag pans; double-click autoscales;
  - native context menu: Autoscale, Reset View, Cursors, Time Window ▸ (trends), Copy Image, Export
    Data…;
  - a plot toolbar (`controlHeightS` buttons) appears on hover, top-right.
- **Readouts:** context menu "Copy Value"; the tooltip shows full precision.
- **Confirmations:** destructive or output-affecting actions (Reset Device, Reset Settings, Release
  Outputs while running) ask through a native `MessageDialog`.

---

## 7. Application shell

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ ● ● ●  ⧉   MYBENCH — Lab 3          [2 running · Stop All]   [● myDAQ 036CD1C6 ▾]  ⌕   ⧉   │ toolbar
├───────────────────┬───────────────────────────────────────────────┬──────────────────────┤
│ Overview          │ Oscilloscope · Waiting for trigger            │ ▾ ■ Channel 0    ◉━  │
│                   │        [Autoscale] [Single] [▶ Run] [⋯]       │   Source      AI 0 ▾ │
│ Measure           ├───────────────────────────────────────────────┤   Scale     ‹ 1 V ›  │
│  ⎍ Multimeter   ● │ CH0 1 V/div · 5 ms/div · Edge ↑ CH0 0.00 V    │   Position   0.00 V  │
│    1.2346 V DC    │                                               │ ▸ ■ Channel 1    ○   │
│  ⎍ Oscilloscope ● │                                               │ ▾ Timebase           │
│    1.00 kHz       │         display: edge to edge,                │   Time/div  ‹ 5 ms › │
│  ⎍ Spectrum       │         theme-aware plot                      │ ▾ Trigger            │
│  ⎍ Bode           │                                               │   Type  Immed.│Edge  │
│ Generate          │                                               │ ▸ Math               │
│  ⎍ Generator    ● │                                               │ ▸ FFT                │
│    1 kHz sine     ├───────────────────────────────────────────────┤ ▸ Cursors            │
│    Waveform Ed.   │ Measurement   ■ CH0        ■ CH1          +   │ ▸ References         │
│ Digital           │ RMS           0.707 V      —                  │ ▸ Display            │
│  ⎍ Digital I/O    │ Frequency     1.000 kHz    —                  │                      │
│  ⎍ Counter        │                                               │                      │
│ Apps              │                                               │                      │
├───────────────────┴───────────────────────────────────────────────┴──────────────────────┤
│ AI: Oscilloscope · AO: Generator · 200 kS/s                               ⚠ 1 message    │ status
└──────────────────────────────────────────────────────────────────────────────────────────┘
```

- **Toolbar (global items only).**
  - On macOS the unified title bar is on by default (`--no-unified-titlebar` turns it off), with
    margins from `SafeArea`. On Windows and Linux: the system title bar, the menu bar, then this
    toolbar.
  - Contents:
    - sidebar toggle;
    - window title (session name; "— Edited" when there are unsaved changes);
    - "N running · Stop All", only while something runs;
    - the device button: state dot plus name, or "Simulated myDAQ". Its native menu holds the
      devices, Simulated, Rescan, Reset Device…, Device Info…, Simulator Settings…;
    - the command palette field (⌘K);
    - inspector toggle.
  - Not in the toolbar: a theme button, a session button, resource chips, a logo tile.
- **Sidebar (source list).**
  - "Overview", then four groups (descriptor `group`):
    - Measure: Multimeter, Oscilloscope, Spectrum Analyzer, Bode Analyzer;
    - Generate: Waveform Generator, with the Waveform Editor view as a child row;
    - Digital: Digital I/O, Counter/Timer;
    - Apps: Data Logger, Audio Equalizer.
  - Group labels: caption size, sentence case, `textSecondary`.
  - Each row:
    - a 20 px icon, the name and a trailing state dot: running = `positive`, waiting or paused =
      `caution`, error = `negative`, stopped = no dot;
    - a second line with the controller's `keyValue`, while running or holding a value;
    - on hover, a small Run/Stop button replaces the dot.
  - Clicking shows the instrument in the focused pane; ⌘/Ctrl-click or "Show Beside" opens a split.
  - Native context menu: Show, Show Beside, Run/Stop, Single, Reset Settings…, Help.
  - Keys: ↑/↓ select, Return shows, Space runs or stops the selected row.
  - Compact mode (56 px): icon and dot only; the tooltip gives name, state and key value.
- **Panes.**
  - One pane, or two side by side (`SplitView`, ratio saved).
  - Each pane is a pane header, a display and an optional measurement strip.
  - The focused pane has a 2 px accent line under its header; focus follows clicks and the keyboard.
  - Showing another instrument or closing a split pane **never** stops an instrument. Stopping is
    always explicit.
- **Pane header** (`PaneHeader`, 40 px).
  - Icon, name, state text ("Running · 200 kS/s", "Waiting for trigger", "Paused — AI used by Bode",
    "Stopped").
  - Trailing:
    - up to 3 instrument extras;
    - Single and Run/Stop. Run/Stop is one toggle: accent "▶ Run" when stopped, neutral "■ Stop"
      when running;
    - "Release Outputs", only while outputs are held and the instrument is stopped;
    - ⋯, a native menu: Export Data…, Export Image…, Print…, the instrument's commands, Reset
      Settings…, Help.
- **Display.**
  - Edge to edge, `display` color, radius 0, no frame and no badges.
  - A one-line read-only summary at top-left (caption size, `displayTextSecondary`).
  - Plot toolbar on hover.
  - Empty state: "Press Run or Space to start".
- **Measurement strip.**
  - `MeasurementTable` or `StatisticsTable` under the display: one row per measurement, one column
    per channel.
  - "+" picks measurements; the row context menu offers Copy and Remove.
  - Up to 5 rows, then it scrolls. Shown or hidden per instrument (view state).
  - Below 600 px window height it collapses to a single summary line.
- **Inspector dock (right).**
  - Shows the focused panel's `Inspector`.
  - A Settings | Help switch in its header; Help is `docs/help/<id>.md` rendered as Markdown.
- **Status bar** (24 px): resource use ("AI: Oscilloscope · AO: Generator"), throughput while
  streaming, and a message counter that opens the event-log popover. No device name, no version, no
  running count.
- **Overview** (first sidebar entry). It fits one screen at 1280×800.
  - **Device block:** state, serial, firmware, Simulated, Reset Device…, Device Info….
  - **Instruments grid:** uniform `OverviewTile`s with icon, name, state, key value and a mini preview
    where useful. Click shows the instrument; Run/Stop appears on hover; "Pin to Top" in the context
    menu.
  - **Recent Sessions.**
  - A first-run hint when no device is connected.

## 8. Adaptive layout

The breakpoints live only in Theme (`Theme.layoutClass(width)`), keyed on the window width W:

| Class | W | Sidebar | Inspector | Split |
|---|---|---|---|---|
| `wide` | ≥ 1280 | expanded 220 | docked 300 | yes, if the panes area is ≥ 900 |
| `medium` | 1024–1279 | compact 56 | docked 280 | yes, if ≥ 900 |
| `narrow` | 760–1023 | compact 56 | overlay sheet from the right (toggle; stays open until toggled) | suspended |
| `compact` | < 760 | hidden; the toolbar button opens it as a drawer | overlay | suspended |

- **Split width:** when a split is shown, the sidebar compacts automatically if that is what it takes
  to reach 900 px of pane area, and it expands again when the split closes.
- **Suspended split:** if 900 px still can't be reached, the split state is kept, only the focused pane
  shows, and its header says "Split hidden — window too narrow". Minimum pane width 450.
- **Height < 600:** the measurement strip collapses to its summary line.
- **User choices:** the sidebar and inspector toggles are saved per layout class.
- **Moving items:** items move between docked and overlay hosts by reparenting or `LayoutItemProxy`;
  they are never duplicated.

## 9. Menus, actions, shortcuts

- **One source for every command.** `app/AppActions.qml` holds QML `Action` objects. They feed:
  - the menu bar;
  - context menus;
  - toolbar and pane buttons;
  - the command palette and shortcuts.
- **Per-instrument commands:** `controller.commands` (C++) feeds the Instrument menu, the pane ⋯ menu
  and the palette.
- **Menu bar:** the Controls `MenuBar` — native on macOS; `style/MenuBar.qml` in the window elsewhere,
  hideable with Ctrl+M.

| Menu | Items |
|---|---|
| **MYBENCH** (macOS) | About · Preferences… ⌘, · Quit ⌘Q |
| **File** | New Session ⌘N · Open… ⌘O · Open Recent ▸ (sessions, exports, pinned files) · Save ⌘S · Save As… ⇧⌘S · Revert to Saved · Export Data… ⌘E · Export Image… ⇧⌘E · Print… ⌘P · Close Window (Windows/Linux: Preferences, Quit) |
| **Edit** | Copy (value or plot image), text editing; Waveform Editor commands while it is focused |
| **View** | Show Sidebar (⌃⌘S / Ctrl+Alt+S) · Show Inspector (⌥⌘I / Ctrl+I) · Show Measurements · Show Status Bar · Show Beside / Close Split ⌘\ · Appearance ▸ System/Light/Dark · Always Use Dark Displays · Enter Full Screen (`StandardKey.FullScreen`) |
| **Instrument** | Overview ⌘0 · instruments ⌘1–⌘9 · Next/Previous Instrument (`StandardKey.NextChild`/`PreviousChild`) · Run/Stop ⌘R · Single ⇧⌘R · Stop All ⌘. · the focused instrument's commands · Reset Settings… · Release Outputs |
| **Device** | devices (radio) · Simulated Device · Rescan · Reset Device… · Device Info… · Simulator Settings… |
| **Tools** | user tools (edited in Preferences ▸ Tools) |
| **Window** (macOS) | Minimize · Zoom · Bring All to Front |
| **Help** | MYBENCH Help (F1 / ⌘?) · Instrument Help · Keyboard Shortcuts · Event Log · About (Windows/Linux) |

- **Space and Shift+Space** run/stop and single-shot through window `Shortcut`s gated by
  `ShellKeys.spaceAllowed`, never as menu keys.
- **Command palette** (⌘K): a centered popup with a `SearchField`. Entries come from AppActions plus
  per-instrument Show / Show Beside / Run and `commands`. Commands for a panel that is still loading go
  through `PanelRegistry.whenReady`.

## 10. Dialogs, preferences, notifications

- **Preferences:** a separate `Window` whose content is a reusable `PreferencesPane`.
  - **General:** restore session, resume running, confirm quit.
  - **Appearance:** System/Light/Dark, Always Use Dark Displays.
  - **Instruments:** Gen output engine.
  - **Device:** auto-connect, simulator.
  - **Tools.**
- **Native dialogs:** `MessageDialog` for confirmations (`ConfirmDialog` wraps it), `FileDialog`,
  `ColorDialog`.
- **Own small windows:** About, Device Info and Simulator Settings are small `Window`s in our style,
  each with a reusable pane.
- **`Banner`** (in the pane, under its header, closable, with actions). Its kinds:
  - Paused [Resume];
  - Reconnected [Resume];
  - input over range;
  - Error [Details → event log];
  - a pre-run conflict hint ("Uses AI, now held by Oscilloscope — Run pauses it"), computed from
    `ResourceArbiter.holders` and `controller.resources`.
- **`Toast`** (bottom-right, one at a time, 4 s): global transient events only. The event log keeps
  everything.

## 11. UX reliability rules

- **Loading:** panels load asynchronously behind a placeholder shaped like the pane, so nothing jumps.
  The first open of an instrument takes < 150 ms.
- **Feedback:** every click responds within 100 ms. A device start slower than 300 ms shows
  "Starting…" in the pane header.
- **Errors:** no modal dialogs for device or instrument errors. An inline banner gives the cause and
  the next action, outputs go to their safe state, and the event log gets an entry.
- **Resource conflicts:** one framework path (the arbiter pauses the holder with a banner and Resume),
  announced by the pre-run hint. Settings that can't work together are impossible to choose in the
  control. The error never appears after Run.
- **Hot-plug:** the device button shows "Disconnected — waiting for device", affected panes show a
  banner, and on reconnect a banner offers Resume.
- **Values:** units always shown; fixed significant digits; tabular figures; "OL" for over-range;
  stale values dimmed after Stop.
- **Sessions:** autosave; layout and settings come back after a crash; "— Edited" in the title.

## 12. Instruments

| id | Panel | Group | Replaces (ELVISmx) |
|---|---|---|---|
| `dmm` | Multimeter | Measure | DMM |
| `scope` | Oscilloscope | Measure | Scope (+ FFT, math, XY, persistence, cursors, measurements) |
| `dsa` | Spectrum Analyzer | Measure | DSA (+ octave, spectrogram) |
| `bode` | Bode Analyzer | Measure | Bode |
| `gen` | Waveform Generator, + view `gen/editor` (Waveform Editor) | Generate | FGEN + ARB + Waveform Editor |
| `dio` | Digital I/O | Digital | Digital Reader + Writer |
| `ctr` | Counter/Timer | Digital | — (myDAQ ctr0 + FREQOUT) |
| `log` | Data Logger | Apps | featured app "Data Logger" |
| `aeq` | Audio Equalizer | Apps | featured app "Audio Equalizer" |

Each instrument provides:
- a C++ controller;
- `<Name>Panel.qml` (an `InstrumentPanel`);
- `<Name>Tile.qml` (the Overview tile content);
- a `keyValue`;
- `docs/help/<id>.md`.

Its layout (pane-header extras, display, strip, inspector sections) is specified in the redesign plan
and tracked in `docs/audit.md` "UI v2".

---

## 13. Contract v2 (interfaces shared between work packages)

Agents code against these names. A name marked *(alias)* exists only so code that has not been
migrated keeps building; it goes in the cleanup phase. Changing anything in this section needs the
orchestrator.

### 13.1 Theme (singleton `MyBench.Controls/Theme`)

| Kind | Tokens |
|---|---|
| Mode | `mode` ("system" \| "light" \| "dark"), `dark`, `light`, `alwaysDarkDisplays`, `displayDark`, `animations`, `platformProvided`, `followSystem`, `system*` override properties (kept for `--system-palette` and tests) |
| Colors | §2 names: `window`, `sidebar`, `base`, `display`, `separator`, `controlFill`, `controlBorder`, `hoverFill`, `pressedFill`, `selection`, `selectionText`, `selectionInactive`, `accent`, `accentText`, `focusRing`, `text`, `textSecondary`, `textTertiary`, `textDisabled`, `link`, `positive`, `caution`, `negative`, `overlay`, `shadowColor`, `displayText`, `displayTextSecondary`, `plotGridMajor`, `plotGridMinor`, `plotFrame`, `signalCh0` `signalCh1` `signalAo0` `signalAo1` `signalMath` `signalRef` `signalCursor`, `seriesColors` (list for plots), `signalColor(name)` |
| Type | `fontFamily`, `monoFamily`, `fontSizeBase`, `fontSizeCaption`, `fontSizeSmall`, `fontSizeBody`, `fontSizeHeading`, `fontSizeTitle`, `fontSizeReadoutM`/`L`/`XL`, `tabularFigures`, `lineHeight` |
| Metrics | `spacingXs`…`spacingXl`; `controlHeightS`, `controlHeight`, `controlHeightL`; `iconSize`, `iconSizeL`; `radiusControl`, `radiusPopup`; `borderWidth`, `focusWidth`; `toolbarHeight`, `paneHeaderHeight`, `statusBarHeight`, `sidebarWidth`, `sidebarCompactWidth`, `inspectorWidth`, `inspectorMinWidth`, `inspectorMaxWidth`, `minWindowWidth`, `minWindowHeight`, `splitMinWidth` (900), `paneMinWidth` (450), `shortHeight` (600) |
| Layout | `layoutClass(width)` → "wide" \| "medium" \| "narrow" \| "compact" |
| Elevation and motion | `shadowBlur`, `shadowOffsetY`, `durationFast`, `durationNormal`, `easing` |
| Helpers (unchanged) | `keys()`, `icon()`, `luminance()`, `contrast()`, `readable()`, `alpha()`, `mix()`, `onColor()` |

Aliases (old → new):

| Old | New |
|---|---|
| `chrome`, `surface` | `window` |
| `surfaceRaised`, `surfaceSunken` | `base` |
| `surfaceHover` | `hoverFill` |
| `surfacePressed` | `pressedFill` |
| `surfaceSelected` | `selectionInactive` |
| `border`, `divider`, `displayBorder` | `separator` / `plotFrame` |
| `borderStrong` | `controlBorder` |
| `displayGrid` | `plotGridMajor` |
| `textMuted` | `textTertiary` |
| `textOnAccent` | `accentText` |
| `accentHover`, `accentPressed`, `accentMuted` | kept, computed |
| `success`, `warning`, `danger` | `positive`, `caution`, `negative` |
| `info` | `accent` |
| `simulated` | `textSecondary` |
| `run`, `runHover` | `accent` |
| `stop`, `stopHover` | `textSecondary` |
| `ch0`, `ch1`, `math`, `ref` | `signal*` |
| `cursor1`, `cursor2` | `signalCursor` |
| `channelColors`, `channelColor(i)` | kept |
| `spacingXxs` → 2, `spacingXxl` → 24 | |
| `radiusXs` → 3; `radiusS`, `radiusM` | `radiusControl` |
| `radiusL`, `radiusXl` | `radiusPopup` |
| `iconSizeS` | 14 |
| `navRailWidth` | `sidebarCompactWidth` |
| `topBarHeight` | `toolbarHeight` |
| `fontSizeXs`, `fontSizeS` | caption |
| `fontSizeM` | small |
| `fontSizeL`, `fontSizeXl` | heading |
| `fontSizeHeadline` | readoutM |
| `fontSizeDisplay` | readoutXL |
| `durationSlow` | `durationNormal` |
| `tint()` | kept |

### 13.2 Section (`ui/controls/Section.qml`; owner A1)

A flat disclosure group inside an `Inspector`.

| Property | Meaning |
|---|---|
| `sectionId` | string, untranslated and stable ("channel0", "trigger"). Required for new code; without it the panel keys the section by its title |
| `title` | Title Case, no "Settings" suffix |
| `summary` | right-aligned in `textSecondary`, shown while collapsed |
| `swatchColor` | channel swatch before the title; transparent = none. `accentColor` is an *(alias)* |
| `trailing` | items at the right of the header, e.g. the channel's enable `Switch`. `headerTools` is an *(alias)* |
| `collapsible`, `collapsed` | |
| `stateKey` | set by `InstrumentPanel.keySections()` to `section/<paneId>/<sectionId>`; the Section only reads and writes its collapsed state under it |
| `default content` | a ColumnLayout of `FormRow`s, 6 px apart, 12 px horizontal padding |

`iconName`, `contentSpacing` and `padding` *(alias, ignored)*. Sections are separated by a hairline
and have no background or border of their own.

### 13.3 FormRow / Field (`ui/controls/FormRow.qml`, `Field.qml`; owner A1)

`FormRow` is a label column (40 % of the width, min 88 px, max 120 px, `textSecondary`) plus the
control, which fills the rest. `Field` is the same type under its old name.

| Property | Meaning |
|---|---|
| `label` | |
| `hint` | label tooltip |
| `trailing` | short unit or range text in `textTertiary` after the label |
| `disabled` | dims the label |
| `labelPosition` | "left" (default) \| "top", for wide controls such as lists and the jack diagram |
| `default content` | the control or controls |

`FieldRow` *(alias)* stacks its Fields vertically.

### 13.4 Inspector and InspectorDock

**`Inspector`** (`ui/controls/Inspector.qml`; owner A1) is a Flickable view of the sections.
- Properties: `default sections`, `contentY`.
- It must:
  - lay out correctly at any width from 260 to 420;
  - draw no background of its own (the host draws `window`);
  - keep `contentY` across parent and width changes;
  - work when reparented at runtime (no bindings to `parent` other than anchors set by the host).

**`InspectorDock`** (`ui/controls/InspectorDock.qml`; owner B1) is the right column of the shell.
- Properties: `panel` (the focused `InstrumentPanel` or null), `page` ("inspector" | "help"),
  `helpTopic`.
- It claims `panel.inspectorContent`: it reparents it into itself and fills.
- When `panel` changes, it hands the item back through `panel.releaseInspector()`.
- Header: instrument name and a Settings | Help switch.

### 13.5 InstrumentPanel v2 and PaneHeader (`ui/controls`; owner B1)

```qml
InstrumentPanel {
    controller: root.controller            // InstrumentController
    headerControls: [ ... ]                // ≤ 3 pane-header extras (ToolButton/Button)
    menuItems: [ ... ]                     // extra ⋯ entries (MenuItem/Menu/Action)
    stateText: ""                          // override; default from controller.statusText/state
    showSingle; showTimeout; timeoutText; singleToolTip; runText; releaseText
    default property: display              // display content, edge to edge
    measurements: [ Item ]                 // optional strip (MeasurementTable/StatisticsTable)
    measurementsVisible: true              // view state, saved per instrument
    banners: [ Banner ]                    // below the framework banners
    inspector: [ Section ]                 // goes into the Inspector item
    traceStyles: []
    readonly property Item inspectorContent
    readonly property bool inspectorClaimed
    function releaseInspector()
    readonly property string paneId        // controller.instrumentId, or "gen/editor"
    property bool focused                  // set by the shell
    signal ready()                         // once loaded → PanelRegistry
}
```

- If no dock claims `inspectorContent` within one frame, the panel docks it locally on its right, as
  in v1. This covers the old shell, `DevShell` and tests.
- Aliases:
  - `runBar` → the `PaneHeader` (same option names);
  - `runBarControls` → `headerControls`;
  - `runBarActions` and `displayTools` → header items before ⋯;
  - `displayTitle` → subtitle;
  - `displayInfo` → state text when the controller's `summary` is empty;
  - `narrowWidth`, `inspectorWidth`, `showErrorBanner`, `inspectorItem`, `displayArea` are accepted.
- The panel draws the framework banners (§10) itself.

`PaneHeader` properties: `controller`, `title`, `iconName`, `stateText`, `extras`, `menuItems`,
`showSingle`, `showTimeout`, `timeoutText`, `singleToolTip`, `runText`, `releaseText`, `compact`,
`focused`.

### 13.6 PanelRegistry (QML singleton `ui/controls/PanelRegistry.qml`; owner B1)

- `register(paneId, panel)`, `unregister(paneId)`, `panel(paneId)` → `Item | null`.
- `signal panelReady(string paneId)`.
- `whenReady(paneId, fn)`: runs `fn(panel)` now if the panel is loaded, otherwise once on
  `panelReady`.
- `ShellActions` *(alias)* forwards to it.

### 13.7 Controller and registry hooks (`ui/framework`; owner B1)

- `InstrumentController`:
  - `Q_PROPERTY(QString keyValue READ keyValue NOTIFY keyValueChanged)` with a protected
    `setKeyValue(const QString&)`;
  - notifications coalesced to ≤ 4 Hz: the first change is immediate, then at most every 250 ms, and
    the last value is always delivered;
  - "" when there is nothing to show.
- `InstrumentDescriptor`:
  - `QString group` ("measure" | "generate" | "digital" | "apps");
  - `QList<ViewDescriptor> views`, each `{id, title, icon, panelType}`, e.g.
    `{"gen/editor", "Waveform Editor", "wave-arbitrary", "GenEditorPanel"}`;
  - both are exposed as registry roles and in `info()`.

### 13.8 New and changed components (owner A1 unless noted)

| Component | API |
|---|---|
| `SiField` | v1 API, plus `steppers` (bool), `scrubbable` (bool, default = steppers), `invalid` (read-only) |
| `StepSelector` | `model` (numbers or `{value, text}`), `currentIndex`, `unit`, `formatter`, `signal activated(int index)` |
| `Readout` | v1 API (value, unit, label, overload, hold, stale, %FS), plus `size` ("s" \| "m" \| "l" \| "xl") and `swatchColor` |
| `StatusIndicator` | `status` ("running" \| "waiting" \| "paused" \| "error" \| "stopped" \| "positive" \| "caution" \| "negative" \| "neutral"), `text`, `dotOnly` |
| `ChannelBadge` | `color`, `text`, `checkable`, `checked`, `signal toggled(bool checked)` |
| `StatisticsTable` | `columns: [{title, color, unit, count, minimum, maximum, peakToPeak, mean, stdDev, overloads}]`, `showReset`, `signal resetRequested()` |
| `MeasurementTable` | `columns: [{title, color}]`, `rows: [{id, title, values: [string]}]`, `available: [{id, title}]`, `signal addRequested(string id)`, `signal removeRequested(string id)`, `summaryText` |
| `TrendView` | a BenchPlot with `windows: [{text, seconds}]`, `windowIndex`, `signal windowActivated(int index)`, `signal clearRequested()`, `plot` (alias). The time-window selector sits in the plot toolbar |
| `OverviewTile` (A2) | `title`, `iconName`, `status`, `keyValue`, `preview` (Component), `pinned`, `signal clicked()`, `signal runToggled()` |

### 13.8b Instrument controls, round 2 (`ui/controls`; PicoScope 7–style panels)

| Component | API |
|---|---|
| `Knob` | flat face, tick ring, value arc, pointer. Stepped `values`, `labels`, `currentIndex`, `defaultIndex` → `indexMoved(int)`; continuous `from`, `to`, `value`, `stepSize`, `logarithmic`, `defaultValue` → `moved(real)`; `size` (48 primary, 34 secondary), `accentColor` (channel color), `clockwiseIncreases` (true), `bipolarArc`, `resetToDefault()` (double-click). Drag (Shift = fine), rotary drag on the ring, wheel (WheelGuard rule), arrow/Page/Home/End keys |
| `KnobField` | knob + label + typeable value between − / + + optional caption: `label`, `unit`, `caption`, `accentColor`, `knobSize`, `compact` (no knob, for narrow panes), `formatter`, stepped `values`/`currentIndex` → `indexActivated(int)`, continuous `from`/`to`/`value`/`stepSize`/`logarithmic` → `valueModified(real)`. SI typing ("2m", "500u", "1.5k"); stepped snaps to the nearest value |
| `ChannelCard` | `title`, `subtitle`, `color`, `checked` → `toggled(bool)`, `checkable`, `facts: [string]`, `compact`, `headerExtras`, body = default content; unchecked collapses to the header |
| `ToolPalette` | `model: [{id, text, icon}]`, `currentId`, `activated(string id)`, `columns` (4) |
| `ToolPanel` | floating panel: `title`, content, `closeRequested()` (Esc, ×), width 280 |
| `PictureButton` | checkable pictogram + label: `text`, `glyph` (SVG path on 32×18) or `iconName` |
| `MeasurementCard` | `title`, `color`, `value`, `stats {min, max, mean, stdDev}`, `removable` → `removeRequested()` |
| `RunButton` | big Run/Stop with state line: `running`, `stateText`, `busy`, emits `toggled()` |

Panel pattern (round 2): `inspector: []` (the shell hides the empty dock); top bar = `RunButton`,
Single and the primary `KnobField`s (via `InstrumentPanel.headerContent` + `showRunControls: false`);
display left and largest; right column of `ChannelCard`s with knobs, then a `ToolPalette` whose tools
open a `ToolPanel` over the display; `MeasurementCard`s at the bottom; narrow panes switch the right
column to `compact`. Short choices are segmented or picture buttons; dropdowns only for long lists.

### 13.9 Offscreen rule

- In screenshot and test mode (`AppInfo.screenshot`, or the test harness), popups stay `Popup.Item`:
  ComboBox lists, menus and context menus. The menu bar on macOS is not drawn in the window.
- Every separate `Window` has its content in a reusable pane Item, which a flag shows in the window:
  `--preferences`, `--about`, `--device-info`, `--simulator-settings`.

### 13.10 Ownership during the migration

- `ui/controls` is split file by file between the design-system and framework work packages (see the
  table in `docs/audit.md` "UI v2").
- Instrument work packages never edit `ui/controls`, `ui/framework` or `ui/plot`; they ask the
  orchestrator.
- The shell work package owns `app/`.

---

## 14. Code layout

| Path | QML module | What |
|---|---|---|
| `ui/framework/` | `MyBench.Framework` | C++: DeviceHub, InstrumentController + worker, ResourceArbiter, InstrumentRegistry, SessionManager, DataExport, Units, shell startup (`shell.cpp`), screenshot/offscreen support |
| `ui/controls/` | `MyBench.Controls` | design system: Theme, Section, FormRow, Inspector, SiField, StepSelector, Readout, tables, TrendView, InstrumentPanel, PaneHeader, InspectorDock, PanelRegistry, Banner, Toast… |
| `ui/controls/style/` | `MyBench.Style` | the Quick Controls style (Button, ComboBox, Menu, MenuBar, Switch, …) |
| `ui/plot/` | `MyBench.Plot` | C++ scene-graph plotting |
| `instruments/<id>/` | `MyBench.Instruments.<Id>` | controller, panel, tile, dev executable `mybench-<id>` |
| `app/` | `MyBench.App` | the `mybench` shell: Main, AppActions, AppMenuBar, AppToolBar, Sidebar, Workspace, OverviewPage, Preferences |

Every executable supports `--screenshot <file.png> [--delay ms] [--size WxH] [--theme light|dark]`.
It renders offscreen with `QT_QPA_PLATFORM=offscreen`, so agents and CI can check the UI visually.
