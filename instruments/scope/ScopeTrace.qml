import QtQuick
import MyBench.Plot

// A PlotTrace drawn with a ScopeTraceStyle (Graph Properties > Plots): color, line width and
// style, points, interpolation (points only / linear / step) and fill (to zero, -∞, +∞).
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
    width: traceStyle ? traceStyle.lineWidth : 1.5
    // ScopeTraceStyle.LineStyle has the order of PlotElement.LineStyle.
    style: interpolation === ScopeTraceStyle.PointsOnly ? PlotElement.Dot
         : traceStyle ? traceStyle.lineStyle : PlotElement.Solid
    // Points are only drawn while samples are sparse (≥ 3 px apart): with dense records "points
    // only" falls back to a dotted line so the trace never disappears.
    readonly property bool dense: buffer !== null && plot !== null && buffer.count > plot.plotArea.width / 3
    lineVisible: interpolation !== ScopeTraceStyle.PointsOnly || dense
    pointsVisible: interpolation === ScopeTraceStyle.PointsOnly || (traceStyle !== null && traceStyle.points)
    pointSize: traceStyle ? Math.max(3.5, traceStyle.lineWidth * 2.5) : 4.5
    step: interpolation === ScopeTraceStyle.Step
    fill: fillTo !== ScopeTraceStyle.NoFill
    fillOpacity: 0.5
    // Display units: zero volts sits at yOffset; +∞ is clamped to the top of the plot.
    fillBaseline: fillTo === ScopeTraceStyle.Zero ? yOffset : fillTo === ScopeTraceStyle.PlusInfinity ? 1e6 : NaN
}
