import QtQuick
import MyBench.Plot

// A PlotTrace drawn with a ScopeTraceStyle (Graph Properties > Plots): color, line width and
// style, point style, interpolation (points only / linear / the step modes) and fill (to zero,
// -∞, +∞) with its own color.
PlotTrace {
    id: trace

    property ScopeTraceStyle traceStyle: null
    property color defaultColor: "white"
    // Drawn faded (e.g. the kept trace while waiting for a trigger).
    property bool dimmed: false

    readonly property int interpolation: traceStyle ? traceStyle.interpolation : ScopeTraceStyle.Linear
    readonly property int fillTo: traceStyle ? traceStyle.fillTo : ScopeTraceStyle.NoFill

    readonly property color baseColor: traceStyle && traceStyle.lineColor.length > 0 ? traceStyle.lineColor : defaultColor
    color: dimmed ? Qt.rgba(baseColor.r, baseColor.g, baseColor.b, 0.4) : baseColor
    width: traceStyle ? Math.max(0.5, traceStyle.lineWidth) : 1
    // ScopeTraceStyle.LineStyle has the order of PlotElement.LineStyle.
    style: interpolation === ScopeTraceStyle.PointsOnly ? PlotElement.Dot
         : traceStyle ? traceStyle.lineStyle : PlotElement.Solid
    // Points are only drawn while samples are sparse (≥ 3 px apart): with dense records "points
    // only" falls back to a dotted line so the trace never disappears.
    readonly property bool dense: buffer !== null && plot !== null && buffer.count > plot.plotArea.width / 3
    readonly property bool hasLine: traceStyle === null || traceStyle.lineWidth > 0
    lineVisible: hasLine && (interpolation !== ScopeTraceStyle.PointsOnly || dense)
    pointsVisible: interpolation === ScopeTraceStyle.PointsOnly || (traceStyle !== null && traceStyle.points)
    // ScopeTraceStyle.PointStyle follows PlotTrace.PointShape after NoPoints.
    pointShape: traceStyle && traceStyle.pointStyle > 0 ? traceStyle.pointStyle - 1 : PlotTrace.FilledCircle
    pointSize: traceStyle ? Math.max(4, traceStyle.lineWidth * 2.5) : 4.5
    // ScopeTraceStyle.Interpolation follows PlotTrace.Interpolation after PointsOnly.
    interpolationMode: Math.max(0, interpolation - 1)
    fillColor: traceStyle && traceStyle.fillColor.length > 0 ? traceStyle.fillColor : baseColor
    fill: fillTo !== ScopeTraceStyle.NoFill
    fillOpacity: 0.5
    // Display units: zero volts sits at yOffset; +∞ is clamped to the top of the plot.
    fillBaseline: fillTo === ScopeTraceStyle.Zero ? yOffset : fillTo === ScopeTraceStyle.PlusInfinity ? 1e6 : NaN
}
