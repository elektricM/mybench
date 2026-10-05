pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// A value over time (docs/UI.md §13.8): a BenchPlot with a time-window selector in its toolbar and
// "Time Window ▸" / "Clear" in its context menu. Declare traces and other plot elements inside;
// `plot` gives the BenchPlot itself.
//     TrendView {
//         anchors.fill: parent
//         windows: [{ text: "10 s", seconds: 10 }, { text: "1 min", seconds: 60 }, { text: "All", seconds: 0 }]
//         windowIndex: ctl.trendWindow
//         onWindowActivated: index => ctl.trendWindow = index
//         onClearRequested: ctl.clearTrend()
//         PlotTrace { buffer: ctl.trend; color: Theme.text }
//     }
// `buffers` (optional): the TraceBuffers whose time stamps drive the time axis. Then the axis starts
// at the first sample and grows with the data (at least `minimumSpan` s) up to the chosen window,
// then slides with the latest sample — no trace squeezed at the right end of a long, empty axis
// right after Run. A zoom or pan by the user sets `following` false (the axis stays where the user
// put it); choosing a time window sets it true again; `fit()` applies it at once. Without `buffers` the owner sets the axis itself, as before.
Item {
    id: root

    property var windows: []             // [{ text, seconds }]
    property int windowIndex: 0
    property alias plot: benchPlot
    default property alias elements: benchPlot.data
    readonly property real windowSeconds: windowIndex >= 0 && windowIndex < windows.length ? windows[windowIndex].seconds : 0
    property var buffers: []
    property bool following: true
    property real minimumSpan: 10

    // The time range for samples from `first` to `last` (s) in a window of `window` s (0 = all).
    function rangeFor(first: real, last: real, window: real): var {
        let span = Math.max(minimumSpan, last - first)
        if (window > 0)
            span = Math.min(span, Math.max(window, minimumSpan))
        const hi = Math.max(last, first + span)
        return { lo: hi - span, hi: hi }
    }
    function fit() {
        if (buffers.length === 0)
            return
        let first = NaN
        let last = NaN
        for (const b of buffers) {
            if (b && b.count > 0) {
                const lo = b.xMinimum()
                const hi = b.xMaximum()
                first = isNaN(first) ? lo : Math.min(first, lo)
                last = isNaN(last) ? hi : Math.max(last, hi)
            }
        }
        if (isNaN(first)) {
            first = Date.now() / 1000
            last = first
        }
        const r = rangeFor(first, last, windowSeconds)
        benchPlot.xAxis.follow = false     // the range is set here with every sample
        benchPlot.xAxis.setRange(r.lo, r.hi)
    }
    onWindowSecondsChanged: if (following) fit()
    onFollowingChanged: if (following) fit()
    onBuffersChanged: if (following) fit()
    Component.onCompleted: if (following) fit()

    Connections {
        target: benchPlot.xAxis
        enabled: root.buffers.length > 0
        function onViewChangedByUser() { root.following = false }
    }
    Instantiator {
        model: root.buffers
        delegate: Connections {
            required property var modelData
            target: modelData
            function onChanged() {
                if (root.following && !benchPlot.xAxis.zoomed)
                    root.fit()
            }
        }
    }

    signal windowActivated(int index)
    signal clearRequested()

    implicitWidth: 320
    implicitHeight: 200

    BenchPlot {
        id: benchPlot
        anchors.fill: parent
        timeWindows: root.windows
        timeWindowIndex: root.windowIndex
        clearAvailable: true
        onTimeWindowActivated: index => {
            root.following = true
            root.windowActivated(index)
        }
        onClearRequested: root.clearRequested()
        toolbarItems: [
            ComboBox {
                id: windowSelector
                visible: root.windows.length > 0
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                implicitHeight: Theme.controlHeightS
                flat: true
                font.pixelSize: Theme.fontSizeCaption
                model: root.windows.map(w => w.text)
                currentIndex: root.windowIndex
                Accessible.name: qsTr("Time window")
                ToolTip.visible: hovered && !popup.visible
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: qsTr("Time window")
                onActivated: index => {
                    root.following = true
                    root.windowActivated(index)
                }
            },
            ToolButton {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                implicitWidth: Theme.controlHeightS
                implicitHeight: Theme.controlHeightS
                icon.source: Theme.icon("trash")
                icon.width: Theme.iconSizeS
                icon.height: Theme.iconSizeS
                ToolTip.visible: hovered
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: qsTr("Clear")
                Accessible.name: qsTr("Clear")
                onClicked: root.clearRequested()
            }
        ]
    }
}
