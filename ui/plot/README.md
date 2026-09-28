# MyBench.Plot — GPU scene-graph plotting

QML module `MyBench.Plot` (static library `mybench_plot`, plugin `mybench_plotplugin`). One
`Plot` item (C++ `QQuickItem` + `QSGGeometryNode`s, rendered through RHI: Metal / Vulkan / D3D /
OpenGL) serves the oscilloscope, spectrum, Bode, DMM trend, waterfall, recorder, DIO timeline
and dashboard previews. Sample data never goes through QML: C++ owns `TraceBuffer` /
`LogicBuffer` / `WaterfallBuffer` objects, worker threads write them, QML only binds them.

```cmake
target_link_libraries(my_instrument PUBLIC mybench_plot mybench_plotplugin)   # plugin self-registers
```

Gallery: `build/ui/plot/gallery/mybench-plot-gallery [--page scope|deep|bode|spectrum|waterfall|trend|xy|recorder|logic]`
(`--screenshot out.png --size 1280x800 [--theme light] [--dpr 2]`, see `ui/offscreen/README.md`;
`--bench` measures frame times). Tests: `ctest -R plot`.

## Feeding data from C++ (the pattern)

```cpp
#include <mybench/plot/tracebuffer.h>

class ScopeController : public InstrumentController {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(mybench::plot::TraceBuffer *ch0 READ ch0 CONSTANT)
    Q_PROPERTY(mybench::plot::TraceBuffer *ch1 READ ch1 CONSTANT)
public:
    mybench::plot::TraceBuffer *ch0() { return &m_ch0; }
    mybench::plot::TraceBuffer *ch1() { return &m_ch1; }
private:
    // In the worker thread, once per acquired record (no locking needed on your side):
    void onRecord(const std::vector<double> &a, const std::vector<double> &b, double t0, double rate)
    {
        m_ch0.setUniform(t0, 1.0 / rate, a);   // std::span<const double|float>
        m_ch1.setUniform(t0, 1.0 / rate, b);
    }
    mybench::plot::TraceBuffer m_ch0, m_ch1;   // live in the GUI thread (controller's thread)
};
```

```qml
Plot { PlotTrace { buffer: scope.ch0 } }
```

* Setters are thread-safe and cheap for readers: data is stored in 65 536-sample chunks with a
  min/max pyramid (levels of 16×) computed by the *writer*; readers take an immutable
  `snapshot()` (a chunk-list copy under a mutex). The plot re-decimates only when `version`
  changes (or the view changes).
* `changed()` is emitted on the buffer's thread, **coalesced** (many writes between two event-loop
  iterations → one signal), so writing at kHz rates is fine.
* Forms: `setUniform(x0, dx, y)` (scope, spectra), `setXY(x, y)` (Bode, XY). Explicit x should be
  non-decreasing; unsorted x (XY mode) is detected and drawn as a path.
* Recording (loggers, trends, hours of data, 10⁸+ samples): `startUniform(x0, dx)` then
  `appendSamples(y)` (uniform), or `append(x, y)` (timestamped). Amortized O(1), earlier samples are
  never copied; `maximumCount` > 0 makes it a rolling window. Rendering any zoom level costs
  O(pixels · log n) thanks to the pyramid. 8 bytes per sample (+8 with explicit x), ~0.5 byte of
  pyramid.
* QML readout helpers on TraceBuffer: `count`, `version`, `valueAt(x)`, `nearestSample(x)`,
  `xMinimum()`, `xMaximum()`.
* C++ readers: `TraceSnapshot s = buf->snapshot(); s->size(), s->xAt(i), s->yAt(i),
  s->lowerBound(x), s->rangeMinMax(i, j, &lo, &hi), s->interpolate(x), s->yRange(x0, x1, ...)`.

Digital lines (`LogicBuffer`, per-lane edge lists, O(edges) storage):

```cpp
m_dio.setLaneCount(8);
m_dio.append(t, mask);                  // software-timed polling: edges extracted where bits change
m_dio.setEnd(now);                      // extend the current state to "now"
m_dio.appendSamples(t0, 1 / rate, std::span<const quint8>(states));  // sampled states
m_dio.setMaximumSpan(60.0);             // rolling window (seconds of history)
```

Spectrogram rows (`WaterfallBuffer`, a ring of rows; only new rows are colorized and uploaded):

```cpp
m_fall.setCapacity(240);                // rows of history
m_fall.setColumns(0.0, df, bins);       // column k at x0 + k·dx
m_fall.appendRow(std::span<const double>(dbv));
```

## QML API

`import MyBench.Plot`. All coordinates are *display* (axis) units unless noted.

### Plot (Item)

| Property | Default | |
|---|---|---|
| `xAxis`, `yAxis` : PlotAxis | own axes | grouped (`xAxis.min: 0`) or assign a shared `PlotAxis` |
| `colorScheme` : `Plot.Dark`/`Plot.Light` | Dark | palette defaults below follow it |
| `backgroundColor`, `plotAreaColor`, `gridColor`, `minorGridColor`, `frameColor`, `textColor`, `titleColor`, `accentColor` | scheme | each has a reset (assign `undefined`) |
| `graticule` : `Plot.Grid` / `Plot.Scope` / `Plot.NoGraticule` | Grid | Scope: fixed divisions (axis `divisions`, default 10 × 8), dotted lines, center axes with minor ticks |
| `gridStyle` : `PlotElement.Solid/Dash/Dot` | Solid | major grid lines (Grid) |
| `frameVisible`, `font`, `placeholderText` | true, 11 px app font, "" | placeholder shown while no trace has data |
| `xLabelsVisible`, `yLabelsVisible` | true | per-plot override (hide x labels on the upper plot of a Bode pair) |
| `leftMargin`, `rightMargin`, `topMargin`, `bottomMargin` | -1 (auto) | |
| `plotArea` : rect (read-only), `implicitLeftMargin` (read-only) | | align stacked plots: `leftMargin: Math.max(a.implicitLeftMargin, b.implicitLeftMargin)` |
| `interactive`, `dragMode` : `Plot.Pan`/`Plot.Zoom`/`Plot.NoDrag` | true, Pan | |
| `hoverReadout` | false | crosshair + tooltip with all traces' values |
| `legendVisible`, `legendPosition` : `Plot.TopRight`/`TopLeft`/`BottomLeft`/`BottomRight` | false, TopRight | traces with a `name` |
| `containsMouse`, `hoverValue` : point (read-only) | | |

Methods: `mapToValue(point)`, `mapFromValue(point)`, `xToPixel(x)`, `yToPixel(y)`, `pixelToX(px)`,
`pixelToY(py)` (item coordinates, log aware — position QML overlays with them), `autoscale()`,
`autoscaleX()`, `autoscaleY()`, `resetView()`, `zoomAt(point, fx, fy)`, `format(value, unit, digits = 4)`
("1.250 ms"), `traceColor(trace)`, `paletteColor(i)`, `saveImage(url)` (→ `imageSaved(path, ok)`),
`saveCsv(url, visibleRangeOnly = true)`, `toCsv(visibleRangeOnly)`, `addElement(e)`/`removeElement(e)`.
Signals: `clicked(x, y)`, `doubleClicked(x, y)`, `viewReset()`, `imageSaved(path, ok)`, `plotAreaChanged()`.

Interaction: wheel = zoom x at the pointer · Shift+wheel = zoom y · Ctrl/⌘+wheel = zoom both ·
Alt+wheel / horizontal trackpad scroll = pan x · pinch = zoom x (Shift: y) · drag = pan
(`dragMode: Plot.Zoom`, Shift+drag or right-drag = rubber-band zoom) · double-click = home ranges ·
drag cursors, lines, handles, regions · click a cursor/line then ←/→ (↑/↓) nudge 1 px (Shift 10 px) ·
+/− zoom · Home/0 reset · Esc deselect. Per-axis `interactive: false` locks an axis (scope y).

Palette (`paletteColor(i)`, default trace colors): CH0 green, CH1 cyan, amber (math), violet
(ref), pink, blue, orange, lime — darker variants in Light.

### PlotAxis

`min`, `max` (visible range; assigning them from QML/C++ also sets the *home* range; interaction
changes only the view, `zoomed` tells), `scale: PlotAxis.Linear/Log`, `divisions` (0 = nice 1-2-5
ticks; N = N equal divisions), `minorTicks` (-1 auto), `tickSpacing` (px, default 90 x / 56 y),
`unit`, `title` (shown "Title (unit)"), `unitInLabels` ("5 kHz" instead of "5 k"),
`labelsVisible`, `gridVisible`, `minorGridVisible`, `inverted`,
`labelFormat: PlotAxis.Engineering/Fixed/Time` (Time = seconds since the epoch → "12:34:56"),
`decimals` (Fixed), `interactive`, `lowerLimit`/`upperLimit` (pan/zoom bounds), `minimumSpan`
(smallest view for zoom *and* for `autoscale()`/`autoScale`, which widen symmetrically around the
data centre — e.g. `minimumSpan: 1e-3` keeps a steady DMM reading from filling the plot with noise),
`autoScale` (+ `autoScaleMargin`: follow the data every update, with hysteresis),
`follow` (x of recordings: keep the latest sample at the right edge; user panning turns it off),
`span`, `zoomed` (read-only).
Methods: `setRange(min, max)` (home), `setViewRange(min, max)`, `reset()`, `zoom(factor, anchor)`,
`pan(fraction)`, `formatValue(v, digits)`, `normalized(v)`, `fromNormalized(t)`.
Signals: `rangeChanged()`, `viewChangedByUser()`, `followChanged()`.

Tick labels are engineering notation with a common prefix on linear axes ("0", "5 k", "10 k";
"−0.5", "0", "0.5"; U+2212 minus) and per-tick prefixes on log axes ("10", "100", "1 k", "10 k";
1-2-5 labels when there is room, every n-th decade when crowded).

### Elements (declare inside the Plot, or anywhere with `plot: thePlot`)

All have `visible`, `z` (stacking within their kind), `plot`. Enums on `PlotElement`:
`Solid, Dash, Dot, DashDot`; `Vertical, Horizontal`.

**PlotTrace** — `buffer` (TraceBuffer), `name`, `unit` (readouts; default y unit), `color`
(unset = palette), `width` (1.5 px), `style`, `lineVisible`, `pointsVisible` + `pointSize`
(drawn while samples are sparser than ~1 per 3 px), `step`, `fill` + `fillBaseline` (NaN = plot
bottom) + `fillOpacity` (gradient fill, spectra), `xScale`/`xOffset`/`yScale`/`yOffset`
(display = value·scale + offset: scope `yScale: 1 / voltsPerDiv; yOffset: positionDiv`),
`persistence` (ms; afterglow of previous records, additive), `legendVisible`.
Methods: `valueAt(x)`, `nearestSample(x)`, `toDisplayY(v)`, `fromDisplayY(d)`, `toDisplayX`,
`fromDisplayX`. Signal `dataChanged()`.

**PlotCursor** — `orientation` (Vertical: x = position), `position`, `color`, `label` ("C1"),
`draggable`, `trace` (readout dot + value chip at the intersection; `value` = trace value in data
units), `snap` (to the trace's samples), `readoutVisible`, `style`, `selected`, `dragging`.
Signal `moved(position)`. Two vertical cursors show a Δ chip (and 1/Δ when the x unit is "s").

**PlotLine** — reference line with an edge handle: `orientation` (Horizontal: y = value, e.g.
trigger level; Vertical: trigger position), `value` (axis units, or the data units of `trace`
mapped through its transform), `color`, `width`, `style`, `draggable`,
`handle: PlotLine.Start/End/NoHandle` (left/right edge, or top/bottom for vertical lines; the
handle shows `label` and an arrow when the value is off-screen),
`lineMode: PlotLine.Always/WhileDragging/Never`, `dragging`. Signal `moved(value)`.

**PlotMarker** — `x`, `y` (NaN + `trace` = on the trace at x), `trace`, `label`, `color`,
`shape: Circle/Ring/Diamond/TriangleDown/TriangleUp/Square`, `size`,
`labelPosition: Above/Below/Left/Right`, `guides` (dashed lines to the axes, e.g. −3 dB).
Many markers from a model: `Instantiator { model: ...; delegate: PlotMarker { plot: p; ... } }`.

**PlotRegion** — highlighted span: `orientation` (Vertical: x from..to), `from`, `to`, `color`
(with alpha), `label`, `edgesVisible`, `draggable` (+ `resizable` edges) → `moved(from, to)`.
Recorder overview pattern:
`PlotRegion { from: main.xAxis.min; to: main.xAxis.max; draggable: true; onMoved: (f, t) => main.xAxis.setViewRange(f, t) }`.

**PlotLogicTrace** — stacked digital lanes from a `LogicBuffer`: `buffer`, `lanes` (bit numbers
per row, top first; default all), `laneNames`, `laneColors`, `color`, `firstRow` (row r occupies
y ∈ [firstRow + r, firstRow + r + 1]; use `yAxis { min: 0; max: 8; inverted: true }`),
`laneHeight` (0.62 of a row), `width`, `fillOpacity` (under high levels), `labelsVisible` (lane
names in the left margin). Edges are exact at any zoom; where edges are denser than pixels, a
column becomes an "activity" block. Cost O(pixels · log edges).

**PlotWaterfall** — spectrogram from a `WaterfallBuffer`: `buffer`,
`colormap: PlotWaterfall.Inferno/Viridis/Magma/Grayscale/Phosphor/Ice`, `minimum`/`maximum`
(value range of the colormap), `yOrigin` (y of the newest row, default 0) and `rowHeight` (rows go
down from there: set the y axis to `[-capacity·rowHeight, 0]`), `smooth`; `colorAt(t)` for a
color bar. Log x axes are supported (quad tessellated at bin edges).

## Recipes

Scope (divisions, V/div, trigger):

```qml
Plot {
    graticule: Plot.Scope
    xAxis { min: -5 * tdiv; max: 5 * tdiv; unit: "s"; unitInLabels: true }
    yAxis { min: -4; max: 4; divisions: 8; labelsVisible: false; interactive: false }
    PlotTrace { id: ch0; buffer: scope.ch0; unit: "V"; yScale: 1 / scope.vdiv0; yOffset: scope.pos0; persistence: 300 }
    PlotLine { value: scope.pos0; handle: PlotLine.Start; label: "0"; lineMode: PlotLine.WhileDragging; color: ch0.color
               onMoved: v => scope.pos0 = v }                                // channel marker (divisions)
    PlotLine { trace: ch0; value: scope.triggerLevel; handle: PlotLine.End; label: "T"; onMoved: v => scope.triggerLevel = v }
    PlotLine { orientation: PlotElement.Vertical; value: scope.triggerTime; handle: PlotLine.Start; label: "T"
               lineMode: PlotLine.WhileDragging; onMoved: v => scope.triggerTime = v }
    PlotCursor { label: "C1"; trace: ch0; snap: true }
}
```

Bode pair (shared log axis, aligned margins): see `gallery/BodePage.qml`. DMM trend:
`xAxis { labelFormat: PlotAxis.Time; follow: true }`, `yAxis { autoScale: true }`, buffer with
`maximumCount` and `append(t, v)`. Recorder + overview: `gallery/RecorderPage.qml`. DIO timeline:
`gallery/LogicPage.qml`. Waterfall + color bar: `gallery/WaterfallPage.qml`.

## Implementation notes

* Lines: one quad per segment, expanded on the CPU; the fragment shader computes the exact
  distance to the segment → round joins/caps, 1-device-pixel anti-aliasing without MSAA, dashes
  from the arc length, hairlines below 1 px fade instead of thinning. Grid, frame, cursor and
  reference lines are snapped to device pixels. Handles, chips, markers and panels are SDF shapes
  (rounded boxes, triangles, diamonds) in one batched node. Text uses `QSGTextNode` with cached
  `QTextLayout`s.
* Decimation (`mybench/plot/decimation.h`): pixel-column buckets computed through the axis
  mapping (log aware), min + max per bucket from the pyramid (O(log n) per bucket), raw samples
  when sparse; unsorted data is de-duplicated per half pixel. The core's
  `mydaq::dsp::minMaxDecimate` splits by sample count, so it is not used here.
* Work split: geometry is built in `updatePolish` (GUI thread) only when the data version, the
  view, the size or the style changed; `updatePaintNode` copies vertex arrays. Waterfall rows are
  uploaded as sub-rectangles of a ring texture.
* Persistence keeps up to 48 previous records per trace as separate nodes (uploaded once) whose
  opacity fades; drawn additively for a phosphor look.
