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
Item {
    id: root

    property var windows: []             // [{ text, seconds }]
    property int windowIndex: 0
    property alias plot: benchPlot
    default property alias elements: benchPlot.data
    readonly property real windowSeconds: windowIndex >= 0 && windowIndex < windows.length ? windows[windowIndex].seconds : 0

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
        onTimeWindowActivated: index => root.windowActivated(index)
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
                onActivated: index => root.windowActivated(index)
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
