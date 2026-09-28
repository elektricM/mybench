import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Oscilloscope: a live mini trace of both channels, CH 0 frequency and Vp-p.
Item {
    id: root

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeChannel shown: ch0.enabled ? ch0 : ch1
    readonly property color shownColor: shown === ch0 ? Theme.ch0 : Theme.ch1

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM
        Rectangle {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredWidth: 3
            radius: Theme.radiusM
            color: Theme.display
            border.width: 1
            border.color: Theme.displayBorder
            clip: true
            BenchPlot {
                id: miniPlot
                anchors.fill: parent
                anchors.margins: 1
                graticule: Plot.Scope
                bare: true
                placeholderText: ""
                // A few periods of the shown channel (the full screen when no frequency is known).
                readonly property real periods: root.shown.frequency > 0 ? 4 / root.shown.frequency : Infinity
                xAxis { min: 0; max: Math.min(root.controller.screenTime, miniPlot.periods); divisions: 10; labelsVisible: false }
                yAxis { min: -5; max: 5; divisions: 8; labelsVisible: false }
                PlotTrace {
                    buffer: root.controller.ch1
                    visible: root.ch1.enabled && root.controller.displayMode === ScopeController.YT
                    color: Theme.ch1
                    width: 1.2
                    yScale: 1 / root.ch1.voltsPerDiv
                    yOffset: root.ch1.position
                }
                PlotTrace {
                    buffer: root.controller.ch0
                    visible: root.ch0.enabled && root.controller.displayMode === ScopeController.YT
                    color: Theme.ch0
                    width: 1.2
                    yScale: 1 / root.ch0.voltsPerDiv
                    yOffset: root.ch0.position
                }
            }
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 5
                text: miniPlot.xAxis.max < root.controller.screenTime ? Units.format(miniPlot.xAxis.max / 10, "s", 2) + "/div" : root.controller.timeDivLabel + "/div"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                font.features: Theme.tabularFigures
            }
            Text {
                anchors.centerIn: parent
                visible: root.controller.displayMode === ScopeController.XY
                text: "XY"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
            }
        }
        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: true
            Layout.preferredWidth: 2
            spacing: 2
            Text {
                text: root.shown.name + "  " + root.shown.sourceName
                color: root.shownColor
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Readout {
                Layout.fillWidth: true
                alignment: Qt.AlignLeft
                valueText: isNaN(root.shown.frequency) ? "?" : Units.format(root.shown.frequency, "Hz", 4, false)
                unit: "Hz"
                fontSize: 20
                color: root.shownColor
                stale: !root.controller.running
            }
            Readout {
                Layout.fillWidth: true
                alignment: Qt.AlignLeft
                label: qsTr("Vp-p")
                valueText: root.controller.formatMeasurement(root.shown.hasResults ? root.shown.peakToPeak : 0, "V")
                unit: "V"
                fontSize: 14
                stale: !root.controller.running
            }
            Item { Layout.fillHeight: true }
        }
    }
}
