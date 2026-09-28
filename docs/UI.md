# MYBENCH UI design

MYBENCH is **one modern application**, not a set of separate instrument programs. Every
ELVISmx feature is kept (see `docs/instruments/*.md` for the complete feature lists), but the
presentation is modernized and the instruments live together in a single workspace, sharing one
device connection, one resource arbiter, one session file and one design system.

## Technology (use what modern Qt 6 offers)

- **Qt Quick / QML** with **Qt Quick Controls 2**, built with `qt_standard_project_setup(REQUIRES 6.9)`
  and `qt_add_qml_module` (qmlcachegen/qmlsc compiled QML, `QML_ELEMENT`/`QML_SINGLETON`
  registration from C++, `pragma ComponentBehavior: Bound`, required properties, no context
  properties).
- Our own design system on top of the Controls **Basic**/Templates style (consistent on every OS),
  dark theme by default (instrument displays), light theme available; theme = QML singleton.
- **Qt Quick Shapes** (curve renderer) for knobs/gauges, **Qt Quick Effects** (`MultiEffect`) for
  subtle glow/shadow, **VectorImage**/SVG icons tinted by the theme, animations/transitions,
  **Qt Quick Dialogs** (file/color/message), `QtCore.Settings` for UI preferences, Markdown
  rendering (`Text.MarkdownText`) for help. Native window integration where available
  (e.g. expanded client area / unified title bar on macOS).
- **Plots are our own scene-graph item** (`MyBench.Plot`, C++ `QQuickItem` + `QSGGeometryNode`),
  GPU-rendered through RHI (Metal/Vulkan/D3D/OpenGL), fed directly from C++ buffers (never through
  QML arrays), with min/max decimation. It serves scope, spectrum, Bode, trends and previews.
- C++ controllers (`QObject`, `Q_PROPERTY`, `QML_ELEMENT`) hold all instrument state and logic;
  QML is view only. Device work runs in worker threads; results reach the UI by queued signals.

## Application shell

```
┌──────┬──────────────────────────────────────────────────────────┬───────────┐
│ nav  │ top bar: device ▾ (myDAQ 036CD1C6 ● / Simulated), resource │ dashboard │
│ rail │ usage chips (AI: Scope · AO: Generator), Stop all, session, │ (pinnable │
│      │ theme, help                                                 │  tiles:   │
│ Home │───────────────────────────────────────────────────────────  │  DMM      │
│ DMM  │ workspace: instrument panels as tabs; any panel can be      │  reading, │
│ Scope│ split beside another (SplitView) — e.g. Scope | Generator   │  DIO LEDs,│
│ Gen  │                                                             │  Gen      │
│ Bode │  panel = display area (plot/readout) + collapsible          │  status…) │
│ Spec │          inspector (settings sections) + run bar            │           │
│ DIO  │                                                             │           │
│      │ status bar: sample rate, throughput, errors (click → log)   │           │
└──────┴──────────────────────────────────────────────────────────┴───────────┘
```

- **Home**: dashboard of all instruments as live tiles (mini preview + key value + run state),
  device card (connection, serial, simulated, reset), recent sessions.
- **Nav rail**: instrument icons with a running indicator; click opens/focuses the panel.
- **Workspace**: tabs + split view (at least: single, side-by-side, and a pinned right dashboard
  column). Layout and all instrument settings persist, and can be saved/loaded as a session file
  (`.mybench`, JSON).
- **Help drawer**: the instrument's `docs/help/<id>.md` rendered as Markdown, beside the panel.
- **Notifications**: non-blocking toasts/banners for device errors, preemption, hot-plug.
- **Keyboard**: space = run/stop focused instrument, shortcuts for panels, command palette (Ctrl/⌘K).

## Instruments (all ELVISmx features, merged where the hardware shares a resource)

| id | Panel | Replaces (ELVISmx) | Notes |
|---|---|---|---|
| `dmm` | Multimeter | DMM | + trend chart, statistics (min/max/avg/σ), hold, relative (null), logging |
| `scope` | Oscilloscope | Scope | + draggable trigger level / position markers, more measurements, XY, math, persistence |
| `gen` | Waveform Generator | FGEN + ARB + Waveform Editor | AO0 and AO1 each: Function (sine/square/triangle, sweep) or Arbitrary (editor); both channels can run together (one AO task, streaming/DDS) |
| `bode` | Bode Analyzer | Bode | + overlay/compare sweeps, −3 dB/phase-margin markers, export |
| `dsa` | Spectrum Analyzer | DSA | + harmonic markers, spectrogram/waterfall |
| `dio` | Digital I/O | Digital Reader + Digital Writer | per-line direction, LEDs, manual switches, patterns, timeline of line states |
| `ctr` | Counter/Timer | — (no ELVISmx instrument; myDAQ ctr0 + FREQOUT) | frequency / period / pulse width / semi-period / two-edge separation / edge count (up/down/external direction) / linear+angular encoder, with statistics and trend; pulse-train (PWM) generator with live frequency/duty; FREQOUT square wave on DIO4; shows which DIO lines it occupies (shared with `dio`, see docs/device.md) |
| `log` | Data Logger | ELVISmx featured app "Data Logger" | long recordings of AI0/AI1 (and DMM) at 1–1000 S/s to a tab-separated file, live chart with scroll-back |
| `aeq` | Audio Equalizer | ELVISmx featured app "Audio Equalizer" | AudioIn → 3-band EQ → AudioOut in real time, level meters, before/after spectrum |

Each instrument provides: a C++ controller (`InstrumentController` subclass), a full panel
(`<Name>Panel.qml`), a compact dashboard tile (`<Name>Tile.qml`), and its help page.

**Resource arbiter** (framework): the hardware allows one AI task (AI or audio-in), one timed AO
task, independent DMM, per-line DIO. When an instrument starts and a resource is held by another,
the holder is paused with a banner ("Scope paused — AI in use by Bode", Resume), never an opaque
error. The top bar shows who holds what.

## Visual language

- Dark neutral surfaces (display areas near-black, panels dark gray), light theme mirrors it.
- Channel colors are consistent everywhere: CH0 green, CH1 cyan (as in ELVISmx), math/ref
  amber/violet; each channel's inspector section and chip carries its color.
- Inspector sections are cards with titles (keeps ELVISmx grouping names: "Channel 0 Settings",
  "Timebase", "Trigger", "Instrument Control"…), collapsible, compact controls.
- Numeric entry everywhere is an `SiField`: engineering units, typed SI prefixes ("1.5k"),
  per-digit scroll/arrow tuning like bench instruments. Knobs remain for the classic feel where
  ELVISmx had them (Volts/Div, Time/Div, frequency…), paired with an SiField/combo.
- Run bar per panel: Run/Stop toggle, Single (Run Once), and status (running, waiting for
  trigger, timeout LED).

## Code layout

| Path | QML module | What |
|---|---|---|
| `ui/framework/` | `MyBench.Framework` | C++: DeviceHub (singleton), InstrumentController base + worker, ResourceArbiter, InstrumentRegistry, SessionManager, DataExport (CSV, future `.sr`), Units (SI format/parse), screenshot/offscreen rendering support |
| `ui/controls/` | `MyBench.Controls` | QML design system: Theme, SiField, Knob, SegmentedControl, Led, ToggleSwitch, Card/Section, RunBar, ChannelChip, Readout, IconButton, styled Combo/Switch/Slider, Toast |
| `ui/plot/` | `MyBench.Plot` | C++ scene-graph plotting: Plot, traces, axes (lin/log), graticule, cursors, markers, zoom/pan |
| `instruments/<id>/` | `MyBench.Instruments.<Id>` | controller + panel + tile per instrument, and a dev executable `mybench-<id>` showing only that panel |
| `app/` | — | `mybench` executable: shell (Main.qml), workspace, dashboard |

Every executable supports `--screenshot <file.png> [--delay ms] [--size WxH]`, rendering
offscreen (works with `QT_QPA_PLATFORM=offscreen`, no window on screen) so agents and CI can
check the UI visually.
