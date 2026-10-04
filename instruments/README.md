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
        .group = u"measure"_s,   // sidebar group, see "Shell hooks of the controller and descriptor"
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

The design contract is docs/UI.md (v2): §7 (pane, display, measurement strip, inspector dock),
§11 (reliability rules) and §13.5 (`InstrumentPanel`). Name things for what they do, in Title Case
for section and command titles; no ELVISmx group names or ordering are required.

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

    InstrumentPanel {                          // pane header + banners + display + strip + inspector
        anchors.fill: parent
        controller: root.controller
        headerControls: [                      // ≤ 3 extras, left of Single / Run
            Button { text: qsTr("Autoscale"); onClicked: root.controller.autoscale() }
        ]
        menuItems: [ MenuItem { text: qsTr("Save Reference…"); onTriggered: refDialog.open() } ]   // extra ⋯ entries
        runBar.showTimeout: true               // PaneHeader options, see below

        BenchPlot { anchors.fill: parent }     // default children = the display, edge to edge

        measurements: [ MeasurementTable { columns: [...]; rows: [...] } ]   // optional strip
        banners: [ Banner { visible: root.controller.clipping; tone: "warning"; text: qsTr("Input clipped") } ]

        inspector: [
            Section {
                sectionId: "channel0"; title: qsTr("Channel 0"); swatchColor: Theme.signalCh0
                summary: "AI 0 · 1 V/div"                       // shown while collapsed
                trailing: Switch { checked: root.controller.ch0Enabled; onToggled: root.controller.ch0Enabled = checked }
                FormRow { label: qsTr("Source"); ComboBox { Layout.fillWidth: true; model: ["AI 0", "AI 1"] } }
                FormRow { label: qsTr("Scale"); StepSelector { model: [0.01, 0.1, 1, 10]; unit: "V/div" } }
            },
            Section { sectionId: "timebase"; title: qsTr("Timebase") }
        ]
    }
}
```

What the frame gives you, with no code:

- **Pane header** (`PaneHeader`, `runBar` is its alias): icon, name and a state text derived from
  the controller (`state`, `statusText`, `pauseReason`, `summary`; "Starting…" only for a start
  slower than 300 ms), your extras, **Single**, the **Run/Stop** toggle (accent "Run" when
  stopped, neutral "Stop" while running), **Release Outputs** while `holdsOutputs` and stopped,
  and **⋯**: Export Data…, Export Image…, Print…, your `controller.commands` (those without an
  argument), Trace Style… (when `traceStyles` is set), Show Measurements, your `menuItems`, Reset
  Settings… (confirmed), Help. Options: `showSingle`, `singleText`, `singleIcon`, `singleToolTip`,
  `singleEnabled`, `runText`, `stopText`, `resumeText`, `releaseText`, `releaseToolTip`,
  `showTimeout` + `timeoutText` (a caution word while `controller.timedOut`), `showRate`, and
  `compact` (read it to shorten your extras in narrow panes). `stateText` overrides the state
  text; set the controller's `summary` for the detail after it ("Running · 200 kS/s").
- **Banners** under the header: Paused [Resume], Reconnected [Resume], input warning, error
  [Details], and the pre-run conflict hint ("Uses AI, now held by Oscilloscope — Run pauses it").
  Your `banners` come after them; never show device errors in a modal dialog.
- **Display**: `Theme.display`, no frame, no badge, no export or help buttons (Export is in ⋯, in
  File and in the plots' context menu). `displayTitle` is the read-only one-line summary at its
  top left.
- **Measurement strip**: `measurements` (MeasurementTable / StatisticsTable) stacked under the
  display; `measurementsVisible` is view state saved per pane; below `Theme.shortHeight` the strip
  shows the first table's `summaryText`.
- **Inspector**: your `inspector` sections live in one `Inspector` item. The app's
  `InspectorDock` shows the focused pane's inspector (Settings | Help); without a dock (tests) the
  panel docks it on its right. Don't bind anything to the inspector's parent or size.
- **Section state**: give every `Section` a stable, untranslated `sectionId`; the panel keys its
  collapsed state `section/<paneId>/<sectionId>` (the title is the fallback).
- **Shell commands**: the panel registers in `PanelRegistry` under its `paneId` once complete;
  shell commands reach it through `PanelRegistry.whenReady(paneId, panel => …)` (exportData(),
  exportImage(pdf), printPanel(), toggleInspector(), showHelp()). The shell provides Space
  (Run/Stop), Shift+Space (Single), the menus and the palette.

A secondary view of an instrument (its own pane, e.g. the Waveform Editor) is an
`InstrumentPanel` with `viewId: "gen/editor"` in a QML file named by the descriptor's `views`.

v1 names still accepted until the cleanup (don't use them in new code): `runBarControls` (=
`headerControls`), `runBarActions` and `displayTools` (header items left of Single),
`displayInfo` (state detail when the controller has no `summary`), `narrowWidth`,
`inspectorWidth`, `showErrorBanner`, `showExport`, `inspectorItem`, `displayArea`; `RunBar`,
`DisplayArea`, `ExportButton`, `HelpButton`, `ShellActions` (now forwarding to PanelRegistry).

### Shell hooks of the controller and descriptor

- **`keyValue`**: call `setKeyValue(text)` in your publish path with the one short value that
  identifies the instrument's state, unit-bearing and formatted with `Units` ("1.2346 V DC",
  "1.00 kHz · 2.00 Vpp", "In 0x5A · Out 0x0F"); `""` when there is nothing meaningful (stopped
  generator, no result yet). Sidebar rows, tooltips and Overview tiles show it. Call it at any
  rate: the base class delivers the first change at once, then at most 4 per second, and always
  the last value.
- **`group`** in the descriptor: `"measure"`, `"generate"`, `"digital"` or `"apps"` (sidebar
  group, Overview order); **`views`**: secondary panes,
  `{ "gen/editor", tr("Waveform Editor"), "wave-arbitrary", "GenEditorPanel" }` →
  `<module>/GenEditorPanel.qml`, sharing the instrument's controller.

```cpp
registry.registerInstrument({
    .id = u"gen"_s, .title = QObject::tr("Waveform Generator"), .module = u"MyBench.Instruments.Gen"_s,
    .icon = u"instrument-gen"_s, .order = 30, .group = u"generate"_s,
    .views = {{u"gen/editor"_s, QObject::tr("Waveform Editor"), u"wave-arbitrary"_s, u"GenEditorPanel"_s}},
    .createController = [] { return new GenController; },
});
```

**Controls** (`MyBench.Controls`, see `mybench-controls-gallery` and docs/UI.md §13): numeric
entry is `SiField` (`unit`, `from`/`to`, `digits`; `steppers: true` for ‹ › steps and drag-scrub)
or `StepSelector` for 1-2-5 style lists (`model`, `unit`, `activated(index)`) — no knobs.
`FormRow` (label + control) inside `Section`; `SegmentedControl`, `Readout` (`size` s/m/l/xl, OL,
hold, stale, %FS), `StatusIndicator`, `ChannelBadge`, `MeasurementTable`, `StatisticsTable`,
`TrendView`, `Banner`, `Icon`, and the standard `QtQuick.Controls` (Button, ToolButton,
ComboBox, CheckBox, Switch, Slider, SpinBox, TextField, Menu, ToolTip…) styled by
MyBench.Style. Colors, sizes and fonts only from `Theme` (`Theme.signalCh0`, `Theme.spacingM`,
`Theme.fontSizeBody`, `Theme.tabularFigures`…). Notifications from QML:
`Notifications.notify(Notifications.Warning, title, message)`. Engineering formatting:
`Units.format(v, unit, digits)`, `Units.parse(text, unit)`.

## The tile (`<Name>Tile.qml`)

Content only (the shell draws the card, title, state dot, Run/Stop and Open around it). About
280–320 × 100–150 px: one or two key values (`Readout` with `alignment: Qt.AlignLeft`) and a
mini plot (`BenchPlot` with `interactive: false; frameVisible: false`, margins 0, labels off).
Updates at most ~10 Hz. Check both sizes with `mybench-<id> --tile --screenshot t.png`.

## Framework hooks

Shared hooks of the shell and framework (added for the acceptance audit, docs/audit.md). All are
opt-in and backwards compatible: an instrument that uses none of them behaves as before.

### Safe state: panel close, session reset, Stop of an idle output holder

The shell calls `closePanel()` when the last tab/pane showing the instrument closes, and
SessionManager calls `resetForSession()` before a new or loaded session is applied. Both emit a
signal (`panelClosed()`, `sessionReset()`) and then `returnToSafeState()`: stop, and **once the
worker has finished** call your `applySafeState()` (UI thread, idle). An idle instrument that
still drives an output declares it with `setHoldsOutputs(true)`: the pane header then shows a
"Release Outputs" button, and Stop / Shift+F5 / Stop all call `applySafeState()` for it.

```cpp
class DioController : public InstrumentController {
    ...
protected:
    void applySafeState() override { releaseOutputs(); }   // lines back to inputs, AO to 0 V...
};
// constructor: keep holdsOutputs in sync with what you drive
connect(this, &DioController::drivenMaskChanged, this, [this] { setHoldsOutputs(m_drivenMask != 0); });
```
QML: `Connections { target: controller; function onPanelClosed() { ... } }` for view state.
`runBar.releaseText: qsTr("Output to 0 V")` (InstrumentPanel) renames the button.

### Stale worker results are dropped (run tagging)

`ctx.post()` / `ctx.publish()` callbacks are tagged with their run. `restart()` discards the
old run's undelivered callbacks automatically, so a reading taken with the old settings is never
applied after a setting change. When a setting change makes in-flight results wrong *without*
restarting, call `discardPendingResults()`. `runGeneration()` (incremented at every start) is
available for your own tags. Stop does not discard: the last acquisition is still shown.

### Clearing results on restart

`restart()` calls the virtual `onRestart()` (and emits `restarting()`) after the old run ended
and before `createWorker()` builds the new one: clear plot buffers, statistics and trends there.
```cpp
void onRestart() override { m_trend.clear(); m_stats.reset(); }
```

### Single enable

`setSingleEnabled(bool)` greys out Single (run bar, Shift+Space, palette) and makes `runOnce()`
a no-op, e.g. Single not available for a mode. `singleEnabled` is a QML property; `RunBar
{ singleEnabled: ... }` can add view-side conditions.

### User-input validation (warning, not a device error)

Throw `mybench::InvalidInput` from `createWorker()` (or from the worker loop) for a problem with
the user's settings. The run is refused with an amber warning (toast, run-bar status,
`inputWarning` + a closable warning banner), `hasError` stays false and the message is never
wrapped in "The device rejected the settings". Outside a start, `reportInvalidInput(message)`.
```cpp
if (m_stop <= m_start)
    throw mybench::InvalidInput(tr("The stop frequency must be above the start frequency."));
```

### Device capabilities (grey out unsupported options)

`mydaq::Device::unsupportedFeatures()` lists optional features a backend does not implement
(names in `mydaq::feature`, core/include/mydaq/device.h: `counter.pulseWidth`,
`counter.semiPeriod`, `counter.twoEdgeSeparation`, `counter.linearEncoder`,
`counter.angularEncoder`, `counter.fallingEdge`, `counter.countDown`,
`counter.externalDirection`, `counter.initialCount`, `counter.resetOnDio`, `counter.buffered`,
`pulse.idleHigh`, `pulse.initialDelay`, `freqout`; add names there as needed). Default: all
supported. `DeviceHub.unsupportedFeatures` (QML, kept while unplugged) and
`DeviceHub.supports(f)` / `DeviceHub.unsupportedReason(f)`; C++: `DeviceHub::instance()->supports(f)`.
The simulator can pretend (`DeviceHub.simulatorUnsupportedFeatures = [...]`) to preview/test.
```qml
readonly property bool pwOk: !DeviceHub.unsupportedFeatures.includes("counter.pulseWidth")
ScopeCombo { ...; enabled: pwOk; ToolTip.text: DeviceHub.unsupportedReason("counter.pulseWidth") }
```
Controllers should also refuse such a setting in `createWorker()` with `InvalidInput`.

### Opening another panel preconfigured

`ShellActions.openInstrument(id, preset, beside)` (QML) or
`InstrumentRegistry::instance()->openInstrument(id, preset, beside)` (C++): applies `preset` to
the target controller (`applyPreset(QVariantMap)`: by default sets the named settings
properties, enums by key name; override for other keys) and opens/focuses its panel, in a split
pane beside the current one when `beside` is true.
```qml
ActionButton { text: qsTr("Open in Spectrum Analyzer")
               onClicked: ShellActions.openInstrument("dsa", { frequencySpan: 20000, window: "Hann" }, true) }
```

### Command palette commands

Register commands in the constructor; the palette (Ctrl+K) lists them as "<Title>: <command>"
and focuses your panel before running them. `argumentLabel` makes the palette ask for a value
first (`argumentUnit`: parsed with SI prefixes; invalid input gives an input warning). Without
`run`, `commandTriggered(id, argument)` is emitted for the panel's QML.
```cpp
addCommand({.id = u"autoscale"_s, .title = tr("Autoscale"), .keywords = u"fit"_s, .icon = u"maximize"_s,
            .run = [this](const QVariant&) { autoscale(); }});
addCommand({.id = u"ao0"_s, .title = tr("Set AO 0 to … V"), .argumentLabel = tr("AO 0 level (V)"),
            .argumentUnit = u"V"_s, .run = [this](const QVariant& v) { setDcLevel(0, v.toDouble()); },
            .enabled = [this] { return !isRunning(); }});
```
```qml
Connections { target: root.controller; function onCommandTriggered(id, arg) { if (id === "zoomFit") plot.autoscale() } }
```

### Instrument-specific export formats

`DataExport::instance()->registerWriter(std::make_unique<MyTxtWriter>(), u"dsa"_s)` (e.g. in
the registration function): the writer is offered first in that instrument's Export data dialog
(`DataExport.nameFiltersFor(id)`) and wins for its extensions there (the generic tab-separated
writer keeps `*.tsv`); other instruments are unaffected. Put writer-specific data in
`DataTable::extra` (QVariantMap) from `exportTable()`.
```cpp
class DsaTxtWriter : public mybench::TableWriter {
    QString name() const override { return tr("ELVISmx text log"); }
    QStringList extensions() const override { return {u"txt"_s}; }
    bool write(const DataTable& t, const QStringList& header, QIODevice& out, QString* error) const override;
};
```

### Print / export image

Export image…, Export PDF… and Print… (Ctrl+P) of every panel open the shared `PrintDialog`
(ink saver, user / computer name, time stamp, settings table, content = display or whole panel,
orientation, printer, copies). The settings table is `exportMetadata()`, i.e. the
`DataTable::metadata` of your `exportTable()`: put every item your spec's Print lists there (e.g.
Bode "Graph Mapping", Scope "C1/C2 linked plot"), also when there is no data yet if possible.
C++/QML API: `DataExport.writeImage / writePdf / print(imagePath, title, metadata, options)`,
`DataExport::inkSaver(QImage)`.

### Unsaved documents

`setUnsavedDocument(tr("Waveform %1").arg(name))` while a document edited in the panel has
unsaved changes (`""` once saved or discarded): quitting asks first ("Quit without saving?").

### Collapsed inspector sections are remembered

`Section`s of an `InstrumentPanel` inspector keep the collapsed state the user chooses per pane
(`stateKey` = "section/<paneId>/<sectionId>", the title when `sectionId` is empty, stored in the
session's view state; the v1 keys "collapsed/<id>/<title>" are pruned once). Set `stateKey`
yourself for sections elsewhere. A section whose `collapsed` is bound (e.g. to `!ch1.enabled`)
follows the user's last manual choice once the user toggled it, as before.

### Plot tools (toolbar, context menu, trace styles)

Every interactive non-bare `BenchPlot` gets, with no code: a hover toolbar at the top right of the
plot area (pointer / zoom menu / pan tools, X and Y autoscale once + lock), a right-click menu
(Autoscale X/Y, Lock X/Y autoscale, Undo zoom, Reset zoom, Copy image, Export image…, Export data…),
`Ctrl/⌘+drag` rubber-band zoom and a zoom history. Export entries go to
`ShellActions.export{Image,Data}Requested(<id>)` of the enclosing `InstrumentPanel` (so the panel's
own export runs); set `instrumentId` to override, or listen to the `exportImageRequested()` /
`exportDataRequested()` signals. Opt out with `tools: false` / `contextMenu: false`.

Plot API (C++ `mybench::plot::Plot`, ui/plot/README.md): `undoZoom()`, `canUndoZoom`,
`clearZoomHistory()`, `xAutoscaleLock` / `yAutoscaleLock` (re-fit on every data update until the user
zooms that axis), `zoomAxes` (`Plot.ZoomXY/ZoomX/ZoomY`), `copyImage()` (→ `imageCopied(ok)`),
`contextMenuRequested(point)`. `PlotTrace` gained `interpolationMode` (Linear, Step, StepVertical,
CenterStep, CenterStepVertical), `pointShape`, `fillColor`, `fillGradient`, `fillBaseline: ±Infinity`,
`style: PlotElement.DashDotDot` and `userStyle`.

Trace styles: own one `TraceStyle` per trace (QML or C++, `mybench/plot/tracestyle.h`), attach it to
the trace, list them in the panel's `traceStyles`: the panel's `⋯` menu and the plots' right-click
menu get "Trace style…" (a `TraceStyleDialog`, live preview, Cancel reverts). Persist with
`toJson()` / `fromJson()` in the controller's saved settings. Cursors on/off: set
`cursorsAvailable: true` and bind `cursorsEnabled` / handle `cursorsToggled(on)`.

```qml
InstrumentPanel {
    id: panel
    traceStyles: [{ name: "CH 0", style: controller.ch0Style, color: Theme.ch0 }]
    BenchPlot {
        anchors.fill: parent
        cursorsAvailable: true
        cursorsEnabled: controller.cursorsOn
        onCursorsToggled: on => controller.cursorsOn = on
        PlotTrace { buffer: controller.ch0; color: Theme.ch0; userStyle: controller.ch0Style }
    }
}
```
```cpp
// controller: Q_PROPERTY(mybench::plot::TraceStyle* ch0Style READ ch0Style CONSTANT)
// saveExtraSettings: s["ch0Style"] = m_ch0Style.toJson();   restore: m_ch0Style.fromJson(s["ch0Style"].toMap());
```

`PlotAxis.logLabels: PlotAxis.Decades` labels a log axis only at decades (minor lines at 2..9);
`PlotCursor.lineVisible: false` hides a cursor's line (chip, readout and dragging stay) and
`PlotCursor.lineWidth` sets its width in px.

`exportDataHandled: true` leaves Export data… of a BenchPlot to the host (signal
`exportDataRequested()` only); `copyDataAvailable: true` adds Copy data →
`copyDataRequested()` (the host supplies the data; there is no built-in TSV).

## Checklist

- [ ] Every control, value list, default and behaviour of your spec; ELVISmx names and groups.
- [ ] Works fully against the simulator (`SimDevice`); errors never crash; Stop within 100 ms.
- [ ] Resources declared; preemption and Resume behave (run another instrument that needs them).
- [ ] Settings persist and restore; defaults = spec defaults (`--fresh`).
- [ ] Export data (and image/PDF where the spec asks); help button → `docs/help/<id>.md`.
- [ ] Dark and light screenshots of panel and tiles look right; no QML warnings on the console;
      no compiler warnings (`-Wall -Wextra`).

## Panel patterns (reference: Oscilloscope)

`instruments/scope` is the reference v2 panel. The other instruments copy these patterns; a
deviation gets a reason in the work package's checkpoint.

**Files.** `<Name>Panel.qml` holds the `InstrumentPanel` and its inspector sections inline. A section
used more than once is its own file (`ScopeChannelSection.qml`, per channel), the display is its
own file (`ScopeDisplay.qml`), and so is the strip's model (`ScopeMeasurements.qml`, a `QtObject`
with `columns`, `rows`, `available`, `summaryText`, `add(id)`, `remove(id)`). The panel root
exposes `panel`, `display` and the models as read-only aliases for tests.

**Pane header or ⋯.**
- `headerControls`: one or two frequent instrument actions, never settings (Scope: Autoscale):
  `Button { text: panel.paneHeader.compact ? "" : qsTr("Autoscale"); icon.source: Theme.icon(...);
  display: compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon }`. Single, Run/Stop,
  Export, Print, Trace Style…, Show Measurements, Reset Settings… and Help come from the frame.
- Other actions go into ⋯ through `menuItems` (Scope: Center Cursors) or `controller.commands`.
  No Log, Export or Help buttons of your own.
- State text: leave `stateText` empty (setting it hides the rate detail). The worker reports the
  instrument's state words with `ctx.setStatus()` only when they change ("Triggered", "Waiting for
  trigger", "Timeout — free running"; "" = "Running"); the frame clears them at start and stop.
  Say it once: no `showTimeout` word, no plot placeholder or badge repeating it.

**Display.**
- Plots edge to edge in a `ColumnLayout`; a second view (the spectrum) sits under a 1 px
  `Theme.separator`. Stacked plots align their plot areas (`leftMargin: other.plotArea.x`, same for
  `rightMargin`); XY plots are square.
- `displayTitle` is the read-only summary of the global settings ("5 ms/div · Edge ↑ CH0 0.00 V ·
  Average of 4"). Per-trace scales are a legend line under each plot, aligned to
  `plot.x + plot.plotArea.x`: an 8 px swatch (radius 2), the name (`displayText`, Medium) and the
  scale (`displayTextSecondary`), caption size. A value is in one of them, never both.
- The placeholder is only the empty state ("Press Run or Space to start") or an impossible
  configuration ("XY needs Channel 0 and Channel 1"), never a run state.
- Settings with a place on the graph get draggable markers (`PlotLine`: channel position, trigger
  level and position) in the trace's color, `lineMode: PlotLine.WhileDragging` unless the line
  itself is information. Status colors never draw traces or markers.
- Zoom, pan, reset, Copy Image, Export Data and the cursors toggle come from `BenchPlot` (tools,
  context menu, `cursorsAvailable`/`cursorsEnabled`): no zoom or reset buttons of your own.
- Trace colors: `style.lineColor.valid ? style.lineColor : Theme.signalCh0` (the user's trace style
  wins over the signal palette).

**Measurement strip.** `measurements: [MeasurementTable { columns; rows; available; summaryText;
onAddRequested: id => model.add(id); onRemoveRequested: id => model.remove(id) }]`.
- Columns are the shown traces only ("CH0", "CH1", "Math", with their colors); rows are the chosen
  measurements, plus the cursor rows while cursors are on.
- `available` lists every measurement (the "+" menu checks the shown ones): it is the only place to
  choose measurements — no checkboxes in the inspector, no per-channel selection.
- Always set `summaryText`: the strip collapses to it in short windows.
- "?" = undefined (the spec's value formatting); "" = not applicable (drawn "—").

**Inspector.**
- Order: inputs (one section per channel), horizontal (Timebase), trigger or acquisition, derived
  traces (Math, FFT), tools (Cursors, References), presentation (Display), options (Analysis).
  Title Case titles without "Settings"; a stable `sectionId` ("channel0", "timebase", "trigger").
- Header: `swatchColor` = the trace's color for trace sections; the section's on/off `Switch` in
  `trailing` — the only enable control of that thing (no chips or checkboxes elsewhere); `summary`
  = a few words for the collapsed header ("AI 0 · 1 V/div", "Off"). Sections of things that are off
  start collapsed (`collapsed: !channel.enabled`), rarely used ones `collapsed: true`.
- One `FormRow` per setting; sentence-case label without units (the control shows them), `hint` for
  a longer explanation.
- Controls: 1-2-5 sequences and short value lists → `StepSelector` (ascending numbers with `unit`
  "V/div", or `{ value, text }` for lists such as timeouts with "Never"); free numbers → `SiField`
  with `unit`, `from`/`to`, `digits` and a tooltip with the range (`engineering: false` + `decimals`
  for divisions and percent); 2–3 short options that fit the control column at 300 px →
  `SegmentedControl`; longer choices, or choices that would push the label above the control →
  `ComboBox`; on/off → `Switch`. No knobs.
- Facts that follow from other settings (coupling, range in use, sample rate and record length) are
  a caption line under the rows (`textTertiary`, caption size), not a disabled control.
- Hide, don't disable, what does not apply (Immediate trigger → no edge fields; audio input → no
  input range; Roll only from 50 ms/div; cursor rows only while cursors are on). Disable only what
  is temporarily impossible, with the reason in the tooltip; inside a `SegmentedControl` use the
  entry's `enabled` (a trigger source whose channel is off).
- Every setting control has an `objectName`: the QML test walks the panel and compares them with
  the expected list (`tst_scope_qml everySettingOnce`), so a second control for a setting fails.

**Trace styles.** Each trace and cursor owns a C++ `mybench::plot::TraceStyle` (set the defaults,
then `setAsDefault()`), saved with `toJson()`/`fromJson()` in `saveExtraSettings`. List them in
`traceStyles`; traces use `PlotTrace { userStyle: ...; color: Theme.signal... }`. The Display section
has a "Trace Styles…" button (`panel.openTraceStyleDialog()`).

**Tile.** A `RowLayout`: a `Theme.display` rectangle (`radiusControl`) holding a bare `BenchPlot`
(no grid, no labels), then a column with a `Readout { size: "m" }` for the key value ("CH0
frequency") and a `Readout { size: "s" }` for the second one; neutral text, `stale:
!controller.running`, "—" before the first result.

**Restoring older sessions.** The automatic property restore ignores unknown keys, so a removed
setting needs no code. A moved or renamed one is migrated in `restoreExtraSettings` (it runs after
the automatic properties), with a test that restores a JSON literal of the old format.
