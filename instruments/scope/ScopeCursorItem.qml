import QtQuick
import MyBench.Controls
import MyBench.Plot

// A time cursor (C1 / C2) of the YT graph, drawn with its ScopeCursor settings: the cursor style
// (line, full or small crosshair, hidden) and its trace style (color, line, width, point marker).
// It snaps to the samples of its linked trace; the values are rows of the measurement strip.
PlotCursor {
    id: item

    required property ScopeCursor cursor
    property bool shown: false
    readonly property TraceStyle look: cursor.style
    // The horizontal line through the cursor's value (Full crosshair).
    readonly property bool crosshair: visible && cursor.cursorStyle === ScopeCursor.FullCrosshair && !isNaN(value)
    // The marker at the cursor's point (Small crosshair, or a point style).
    readonly property bool marked: visible && !isNaN(value)
                                   && (look.pointStyle !== TraceStyle.NoPoints || cursor.cursorStyle === ScopeCursor.SmallCrosshair)

    visible: shown && cursor.cursorStyle !== ScopeCursor.NoCursor
    label: cursor.label
    position: cursor.position
    color: look.lineColor.valid ? look.lineColor : Theme.signalCursor
    style: look.lineStyle
    lineWidth: look.lineWidth > 0 ? look.lineWidth : 1
    readoutVisible: false
    snap: true
    onMoved: p => item.cursor.position = p
}
