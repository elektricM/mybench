# Writing a MYBENCH instrument

One directory per instrument: `dmm`, `scope`, `gen`, `bode`, `dsa`, `dio`, `ctr`. Each is a QML
module `MyBench.Instruments.<Name>` (Dmm, Scope, Gen, Bode, Dsa, Dio, Ctr) with a C++ controller,
a full panel, a dashboard tile, a registration function and a dev executable `mybench-<id>`. The
skeletons already run against the simulator; replace their placeholders following your spec
(`docs/instruments/<id>.md`, common conventions in `docs/instruments/app.md`, help page
`docs/help/<id>.md`).

**Stay inside your directory.** Its `CMakeLists.txt` globs every `*.cpp`/`*.h`/`*.qml` (and
`assets/*`) of the directory, so adding files needs no CMake edit anywhere; the app links and
registers every built instrument automatically. If you need something from a shared module
(framework, controls, plot, core), ask its owner instead of editing it.

```
instruments/scope/
  CMakeLists.txt        self-contained (globs); module + plugin + dev executable
  scopecontroller.h/.cpp ScopeController : InstrumentController, and registerScope()
  ScopePanel.qml        full panel:   required property ScopeController controller
  ScopeTile.qml         dashboard tile: required property ScopeController controller
  dev/main.cpp          mybench-scope (the panel alone in a minimal shell)
  *.qml, *.cpp, *.h     anything else you add (sub-components, helpers, singletons)
```

Keep QML files at the top of your directory (the panel and tile URLs are
`qrc:/qt/qml/MyBench/Instruments/<Name>/<Name>Panel.qml`). A QML file starting with
`pragma Singleton` becomes a singleton automatically.

## Build, run, screenshot

```sh
cmake -S . -B build-<you> -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt \
      -DMYBENCH_INSTRUMENTS=scope          # only your instrument (optional)
cmake --build build-<you> --target mybench-scope
build-<you>/instruments/scope/mybench-scope              # interactive
```

Every executable renders offscreen with `--screenshot` (no window, no Dock icon; implies
`--fresh`, i.e. temporary empty settings, so screenshots are deterministic):

```sh
S=build-<you>/instruments/scope/mybench-scope
$S --screenshot /tmp/scope.png --size 1280x800 --delay 2000 --run       # panel, running
$S --screenshot /tmp/scope-light.png --theme light --run                 # light theme
$S --screenshot /tmp/scope-tile.png --tile --run                         # dashboard tiles (both sizes)
$S --screenshot /tmp/scope-help.png --show-help                          # help drawer open
$S --screenshot /tmp/scope@2x.png --dpr 2 --run                          # retina
build-<you>/app/MYBENCH.app/Contents/MacOS/MYBENCH --panel scope --split gen --run all \
    --screenshot /tmp/app.png --size 1440x900 --delay 2500               # inside the app (Linux/Windows: app/mybench)
```

Then **look at the PNG** (e.g. with your image viewer tool) and iterate. Other options:
`--help` lists them all (`--device <id>`, `--simulated`, `--dashboard`, `--palette`, `--log`,
`--page home`, `--no-animations`, `--keep-settings`, `--delay`, `--size`, `--dpr`).

## The controller

Subclass `mybench::InstrumentController` (`<mybench/instrumentcontroller.h>`; umbrella
`<mybench/framework.h>`). It gives you the run state machine, the worker thread, errors, toasts,
resource arbitration, settings persistence and export; you provide the settings, the worker loop
and the results.

```cpp
class ScopeController : public mybench::InstrumentController
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the InstrumentRegistry")
    // Settings: writable + NOTIFY  =>  persisted automatically (JSON, enums by key name).
    Q_PROPERTY(int timeDivIndex READ timeDivIndex WRITE setTimeDivIndex NOTIFY timeDivIndexChanged)
    Q_PROPERTY(Coupling coupling READ coupling WRITE setCoupling NOTIFY couplingChanged)
    // Not a setting: mark it STORED false (view state, transient values).
    Q_PROPERTY(bool cursorsDragging READ ... WRITE ... NOTIFY ... STORED false)
    // Results: read-only properties (never persisted).
    Q_PROPERTY(double ch0Rms READ ch0Rms NOTIFY measurementsChanged)
    // Traces for MyBench.Plot (see below).
    Q_PROPERTY(mybench::plot::TraceBuffer* ch0 READ ch0 CONSTANT)
public:
    ScopeController() : InstrumentController(u"scope"_s)
    {
        setRequiredResources(mybench::ResourceArbiter::AnalogInput);
    }
    ~ScopeController() override { shutdown(); }   // required when the worker captures `this`
protected:
    WorkerFunction createWorker() override;        // the run (next section)
    mybench::DataTable exportTable() const override;   // Export data (optional)
};
```

**Rules**
- QML is view only: all state and logic live in the controller. Bind panel controls to its
  properties and write them back (`onActivated: i => controller.timeDivIndex = i`).
- Setters check for change, emit NOTIFY, and apply live changes (see "Live changes").
- Declare properties in dependency order (settings are restored in declaration order, e.g.
  `function` before `rangeIndex`).
- Settings the JSON conversion cannot express (tables, arbitrary waveforms): override
  `saveExtraSettings(QJsonObject&)` / `restoreExtraSettings(const QJsonObject&)` and call
  `markSettingsChanged()` after changing them.
- Persistence is automatic: saved to QSettings (debounced) and in session files, restored at
  start, `resetSettings()` (palette "Reset … to defaults") returns to the values your constructor
  set. Your constructor's member initializers **are** the documented defaults.
- `setSupportsSingle(false)` in the constructor if Single makes no sense (hides the button).
- `setSummary("AI 200 kS/s × 2")` for the status bar (optional), `setStatusText()` for the run bar.
- QML pitfalls: don't name a property `function` or an enum `Function` (JS clashes: qmllint and
  the compiler get confused; the DMM uses `selectedFunction` / `MeterFunction`); in delegates of
  buttons/menu items read an `icon` model role as `model.icon` (`icon` is a FINAL control property).

### Registration

```cpp
namespace mybench::instruments {
void registerScope(mybench::InstrumentRegistry& registry)
{
    registry.registerInstrument({
        .id = u"scope"_s, .title = QObject::tr("Oscilloscope"), .shortTitle = QObject::tr("Scope"),
        .description = QObject::tr("Two channels, edge trigger, measurements, cursors"),
        .module = u"MyBench.Instruments.Scope"_s, .icon = u"instrument-scope"_s, .order = 20,
        .createController = [] { return new ScopeController; },
    });
}
}
```

Keep the name `register<Name>` and the namespace: `app/` generates the call list from the built
instrument ids. The registry owns one controller per instrument, shared by the panel and every
tile.

## The worker (acquisition / generation loop)

`createWorker()` runs on the UI thread when Run or Single is pressed. Validate, take a **snapshot**
of the settings by value, and return the loop; the framework runs it in a worker thread with a
`WorkerContext` (`<mybench/worker.h>`):

```cpp
InstrumentController::WorkerFunction ScopeController::createWorker()
{
    if (!m_ch0Enabled && !m_ch1Enabled)
        throw mydaq::DeviceError(mydaq::DeviceError::InvalidConfig, tr("enable at least one channel"));
    mydaq::AiConfig config = makeConfig();          // snapshot
    return [this, config, live = m_live](mybench::WorkerContext& ctx) {
        auto task = ctx.device().createAiTask(config);   // resources: see below
        ctx.setSampleRate(task->sampleRate());
        while (!ctx.stopRequested()) {
            if (auto changed = live->take()) { /* apply a live setting */ }
            task->start();
            auto data = task->read(config.samplesPerChannel, 200);   // short timeouts
            ctx.addSamples(config.samplesPerChannel * 2);           // status-bar throughput
            m_ch0.setUniform(0.0, 1.0 / task->sampleRate(), data[0]);   // TraceBuffer: thread-safe
            ctx.publish([this, m = measure(data)] {                   // UI thread, coalesced
                m_measurements = m;
                emit measurementsChanged();
            });
            ctx.acquisitionDone();                                     // hasData, acquisitionCount
            if (ctx.once())                                            // Single (also when pressed while running)
                break;
        }
    };   // leaving the lambda destroys the task: only then are the resources released
}
```

- **Never touch controller state from the loop** except through `ctx.post(fn)` (in order) or
  `ctx.publish(fn, slot)` (latest only: use it for display updates) — both run `fn` on the UI
  thread — or thread-safe objects (TraceBuffer, LogicBuffer, WaterfallBuffer, Mailbox, atomics).
- **Errors**: throw (or let the device throw) `mydaq::DeviceError`. The framework stops the run,
  releases the resources, shows the spec's message (banner + toast + event log) and never crashes.
  Errors caused by Stop are ignored. Non-fatal problems: `ctx.warn(message)`.
- **Stop quickly**: check `ctx.stopRequested()` often; use read/wait timeouts of ≤ 200 ms and
  `ctx.sleepMs(ms)` (returns false when stopping) instead of `sleep`. Stop must act within ~100 ms.
- **Safe state**: put outputs back at the end of the loop (after destroying tasks): e.g.
  `device.aoWriteStatic(AO0, 0.0)`, `dioSetDirection(0xFF, false)`. The loop also ends this way
  when the instrument is paused by another one.
- `ctx.setStatus(text)`, `ctx.setTimedOut(bool)` (run-bar Timeout LED), `ctx.setSampleRate()`,
  `ctx.continuous()`/`ctx.once()` (live: Single pressed while running ends after this record).
- `onStarted()` / `onFinished()` (UI thread) are called when the worker started / ended.

### Live changes

Settings changed while running follow your spec: display-only ones need nothing; the others either
go through a `mybench::Mailbox<Snapshot>` (`m_live->post(snapshot())` in the setter,
`live->take()` in the loop) or call `restart()` in the setter (stops, then runs again with a new
`createWorker()`; no-op when idle) — e.g. a new sample rate needs a new task.

### Resources and preemption

Declare what you use: `setRequiredResources(AnalogInput | AnalogOutput)` (Bode), `Multimeter`,
`Counter`, `FrequencyOutput`, `DigitalLines` with a line mask (`setRequiredResources(DigitalLines,
0x0F)`); call it again when a setting changes the lines (e.g. DIO line groups, counter
terminals). On Run, the `ResourceArbiter` pauses whoever holds a conflicting resource ("Scope
paused — AI in use by Bode", banner + Resume) and your worker only starts once their tasks are
gone. Never hold a device task while idle. `ResourceBusy` from the device then means another
program.

### Export

Override `exportTable()` to return the last completed record as a `mybench::DataTable`
(`addColumn(name, unit, values)`, `addMetadata(key, value)` for the spec's settings lines). That
is all: the panel header's **Export ▾** menu (InstrumentPanel) offers Export data… (CSV/TSV with
the standard header: app, date, device, your metadata), Export image… (PNG of the display at 2x)
and Export PDF… (image + settings), with file dialogs and toasts; `Ctrl+L` / `Ctrl+P` and the
palette reach it too. `ExportButton { controller: ... }` exists for extra placements.

## Plots: MyBench.Plot

Read `ui/plot/README.md`. Controllers own `mybench::plot::TraceBuffer` (analog),
`LogicBuffer` (DIO lanes) and `WaterfallBuffer` (spectrogram) members exposed as `CONSTANT`
pointer properties; the worker writes them directly (thread-safe, coalesced `changed()`), QML
binds them. Never pass sample arrays through QML.

```cpp
#include <mybench/plot/tracebuffer.h>
Q_PROPERTY(mybench::plot::TraceBuffer* ch0 READ ch0 CONSTANT)
mybench::plot::TraceBuffer* ch0() { return &m_ch0; }
mybench::plot::TraceBuffer m_ch0;
// worker: m_ch0.setUniform(x0, dx, samples);  m_trend.append(t, v);  m_logic.append(t, mask);
```

```qml
import MyBench.Plot
BenchPlot {                       // MyBench.Controls: Plot with the theme (dark/light, background)
    graticule: Plot.Scope
    xAxis { min: 0; max: 10 * root.controller.timePerDiv; divisions: 10; unit: "s"; unitInLabels: true }
    yAxis { min: -5; max: 5; divisions: 10; labelsVisible: false; interactive: false }
    PlotTrace { buffer: root.controller.ch0; color: Theme.ch0; yScale: 1 / root.controller.voltsPerDiv0 }
    PlotLine { trace: ...; value: root.controller.triggerLevel; handle: PlotLine.End; label: "T"
               onMoved: v => root.controller.triggerLevel = v }
}
```

Use `BenchPlot` (not `Plot`) so the theme follows light/dark. Channel colors always come from
the Theme (`Theme.ch0`, `ch1`, `math`, `ref`, `cursor1`, `cursor2`).

## The panel (`<Name>Panel.qml`)

```qml
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

Item {
    id: root
    required property ScopeController controller

    InstrumentPanel {                          // display + banners + inspector + run bar
        anchors.fill: parent
        controller: root.controller
        displayTitle: "CH 0 · 5 ms/div"         // header line of the display area
        displayInfo: qsTr("Sample Rate %1").arg(Units.format(root.controller.sampleRate, "S/s"))
        runBar.showTimeout: true               // RunBar options: runText, singleText, showSingle...

        BenchPlot { anchors.fill: parent }     // default children = display content

        inspector: [                           // ELVISmx group names, in ELVISmx order
            Section {
                title: qsTr("Channel 0 Settings"); accentColor: Theme.ch0
                summary: "AI 0 · 1 V/div"                       // shown when collapsed
                headerTools: ToggleSwitch { checked: root.controller.ch0Enabled; onToggled: root.controller.ch0Enabled = checked }
                FieldRow {
                    Field { label: qsTr("Source"); ComboBox { Layout.fillWidth: true; model: ["AI 0"] } }
                    Field { label: qsTr("Coupling"); ComboBox { Layout.fillWidth: true; model: ["DC"]; enabled: false } }
                }
            },
            Section { title: qsTr("Timebase") }
        ]
        runBarControls: [ ActionButton { text: qsTr("Autoscale"); iconName: "autoscale" } ]   // after Single
        // runBarActions: [ ... ]            // right side of the run bar (rarely needed)
    }
}
```

`InstrumentPanel` already provides: the display header (SIMULATED tag, your title/info, and the
actions Export ▾, Help, ⋯ Reset to defaults, Focus mode, settings toggle), the pause banner
(Resume), the "device reconnected" banner, the error banner, the inspector (docked and
resizable, or an overlay drawer when the panel is narrower than 820 px, e.g. in a split pane) and
the RunBar bound to the controller (Run/Stop/Resume, Single, status, rate, timeout LED; compact in
narrow panes). Don't add your own Help/Export buttons. The shell provides Space (Run/Stop),
Shift+Space (Single), F5/Shift+F5, F1 (help), Ctrl+I (settings), Ctrl+L/Ctrl+P (export),
Ctrl+Shift+F (focus mode) and the palette entries (Open/Run/Stop/Help/Reset <instrument>).

**Controls** (`MyBench.Controls`, look at `mybench-controls-gallery`):
`SiField` (every numeric entry: `unit`, `from`/`to`, `digits` or `engineering: false; decimals`,
`stepSize`; bind `value`, handle `valueModified(v)`), `Knob` (ring: `values`/`labels`/
`currentIndex` + `indexMoved(i)`; continuous: `from`/`to`/`logarithmic`/`value` + `moved(v)`;
pair it with an SiField/ComboBox), `SegmentedControl` (`model` of strings or
`{text, icon, toolTip, enabled}`, `currentIndex`, `activated(i)`), `ComboBox`/`CheckBox`/
`RadioButton`/`Switch`/`Slider`/`SpinBox`/`TabBar`/`Menu`/`ToolTip`/`TextField` (standard
QtQuick.Controls, styled by MyBench.Style), `ToggleSwitch`, `Led`, `StatusDot`, `Badge`,
`ChannelChip`, `Readout` (big value + unit, OL, hold, stale, % FS bar), `Section`/`Card`,
`Field`/`FieldRow`, `IconButton`/`ActionButton`, `Banner`, `DisplayArea`, `EmptyState`,
`KeyHint`, `Icon` (names = `assets/icons/*.svg`, tinted). Tokens: `Theme.*` (surfaces, text,
accent, status, channel colors, spacing, radii, font sizes, `Theme.tabularFigures`). Never
hard-code colors or sizes. Notifications from QML: `Notifications.notify(Notifications.Warning,
title, message)`. Engineering formatting: `Units.format(v, unit, digits)`, `Units.parse(text, unit)`.

## The tile (`<Name>Tile.qml`)

Content only (the shell draws the card, title, state dot, Run/Stop and Open around it). About
280–320 × 100–150 px: one or two key values (`Readout` with `alignment: Qt.AlignLeft`) and a
mini plot (`BenchPlot` with `interactive: false; frameVisible: false`, margins 0, labels off).
Updates at most ~10 Hz. Check both sizes with `mybench-<id> --tile --screenshot t.png`.

## Checklist

- [ ] Every control, value list, default and behaviour of your spec; ELVISmx names and groups.
- [ ] Works fully against the simulator (`SimDevice`); errors never crash; Stop within 100 ms.
- [ ] Resources declared; preemption and Resume behave (run another instrument that needs them).
- [ ] Settings persist and restore; defaults = spec defaults (`--fresh`).
- [ ] Export data (and image/PDF where the spec asks); help button → `docs/help/<id>.md`.
- [ ] Dark and light screenshots of panel and tiles look right; no QML warnings on the console;
      no compiler warnings (`-Wall -Wextra`).
