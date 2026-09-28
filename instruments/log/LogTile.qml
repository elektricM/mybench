import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Data Logger: recording state and time, the latest values and the last
// minute of the chart.
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
        return e && e.count > 0 ? Units.format(e.last, unit, 4) : "— " + unit
    }
    function duration(seconds: real): string {
        const s = Math.floor(seconds)
        const hh = Math.floor(s / 3600)
        return (hh > 0 ? hh + ":" : "") + String(Math.floor(s / 60) % 60).padStart(hh > 0 ? 2 : 1, "0") + ":" + String(s % 60).padStart(2, "0")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Rectangle {
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: root.controller.recording ? (root.controller.recordPaused ? Theme.warning : Theme.danger) : Theme.textMuted
            }
            Text {
                text: root.controller.recording ? (root.controller.recordPaused ? qsTr("PAUSED") : qsTr("REC"))
                                                  + "  " + root.duration(root.controller.recordSeconds)
                                                : root.controller.running ? qsTr("Monitoring") : qsTr("Not recording")
                color: root.controller.recording ? Theme.text : Theme.textMuted
                font.pixelSize: Theme.fontSizeM
                font.weight: Font.DemiBold
                font.features: Theme.tabularFigures
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            Text {
                visible: root.controller.ai0Enabled
                text: root.valueOf("ai0", "V")
                color: Theme.ch0
                font.pixelSize: Theme.fontSizeXl
                font.features: Theme.tabularFigures
            }
            Text {
                visible: root.controller.ai1Enabled
                text: root.valueOf("ai1", "V")
                color: Theme.ch1
                font.pixelSize: Theme.fontSizeXl
                font.features: Theme.tabularFigures
            }
            Text {
                visible: root.controller.dmmEnabled
                text: root.valueOf("dmm", root.controller.dmmUnit)
                color: Theme.math
                font.pixelSize: Theme.fontSizeXl
                font.features: Theme.tabularFigures
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusM
            color: Theme.display
            border.width: 1
            border.color: Theme.displayBorder
            clip: true
            BenchPlot {
                anchors.fill: parent
                anchors.margins: 1
                bare: true
                xAxis { labelFormat: PlotAxis.Time; follow: true; gridVisible: false }
                yAxis { autoScale: true; gridVisible: false }
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                PlotTrace { visible: root.controller.ai0Enabled; buffer: root.controller.ai0; color: Theme.ch0; width: 1.2 }
                PlotTrace { visible: root.controller.ai1Enabled; buffer: root.controller.ai1; color: Theme.ch1; width: 1.2 }
            }
        }
    }
}
