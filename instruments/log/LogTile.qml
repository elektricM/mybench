import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Data Logger (content only; the shell draws title, state and Run/Stop): the
// last minute of the chart on the display surface, the latest value of each logged channel (its
// swatch is the legend) and the recording time while a file is written. Neutral text.
Item {
    id: root

    required property LogController controller

    function entry(key: string): var {
        const list = root.controller.channelStats
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === key)
                return list[i]
        }
        return null
    }
    function valueOf(key: string, unit: string): string {
        const e = root.entry(key)
        return e && e.count > 0 ? Units.format(e.last, unit, 4) : "—"
    }
    function duration(seconds: real): string {
        const s = Math.floor(seconds)
        const hh = Math.floor(s / 3600)
        return (hh > 0 ? hh + ":" : "") + String(Math.floor(s / 60) % 60).padStart(hh > 0 ? 2 : 1, "0") + ":" + String(s % 60).padStart(2, "0")
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            color: Theme.display
            radius: Theme.radiusControl
            border.width: Theme.borderWidth
            border.color: Theme.separator
            clip: true

            BenchPlot {
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                bare: true
                placeholderText: ""
                xAxis { labelFormat: PlotAxis.Time; follow: true; labelsVisible: false; gridVisible: false }
                yAxis { autoScale: true; labelsVisible: false; gridVisible: false }
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                PlotTrace { visible: root.controller.ai0Enabled; buffer: root.controller.ai0; color: Theme.signalCh0; width: 1.2 }
                PlotTrace { visible: root.controller.ai1Enabled; buffer: root.controller.ai1; color: Theme.signalCh1; width: 1.2 }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            spacing: Theme.spacingXs

            Readout {
                visible: root.controller.ai0Enabled
                Layout.fillWidth: true
                size: "s"
                alignment: Qt.AlignLeft
                swatchColor: Theme.signalCh0
                label: "AI 0"
                valueText: root.valueOf("ai0", "V")
                stale: !root.controller.running
            }
            Readout {
                visible: root.controller.ai1Enabled
                Layout.fillWidth: true
                size: "s"
                alignment: Qt.AlignLeft
                swatchColor: Theme.signalCh1
                label: "AI 1"
                valueText: root.valueOf("ai1", "V")
                stale: !root.controller.running
            }
            Readout {
                visible: root.controller.dmmEnabled
                Layout.fillWidth: true
                size: "s"
                alignment: Qt.AlignLeft
                label: qsTr("DMM")
                valueText: root.valueOf("dmm", root.controller.dmmUnit)
                stale: !root.controller.running
            }
            Item { Layout.fillHeight: true }
            // The one fact that matters: a file is being written.
            Text {
                visible: root.controller.recording
                text: (root.controller.recordPaused ? qsTr("Paused") : qsTr("Rec")) + "  " + root.duration(root.controller.recordSeconds)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.features: Theme.tabularFigures
            }
        }
    }
}
