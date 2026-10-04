import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Bode Analyzer (content only; the shell draws title, state and Run/Stop):
// a bare mini gain curve on the display surface and the −3 dB frequency (or the gain at the last
// point). Neutral text; the trace color only draws the curve.
Item {
    id: root

    required property BodeController controller

    readonly property bool hasCutoff: controller.cutoffFrequencies.length > 0

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
                xAxis { min: root.controller.startFrequency; max: Math.max(root.controller.stopFrequency, root.controller.startFrequency * 1.001); scale: PlotAxis.Log; labelsVisible: false; gridVisible: false; minorGridVisible: false }
                yAxis { autoScale: root.controller.pointCount > 0; min: -40; max: 10; labelsVisible: false; gridVisible: false; minorGridVisible: false }
                PlotTrace { id: tileGain; buffer: root.controller.gain; color: Theme.signalColor("ai0"); width: 1.5 }
                PlotMarker {
                    visible: root.hasCutoff
                    x: root.hasCutoff ? root.controller.cutoffFrequencies[0] : 0
                    trace: tileGain
                    shape: PlotMarker.Ring
                    size: 7
                    color: Theme.text
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            spacing: Theme.spacingXs

            Readout {
                Layout.fillWidth: true
                size: "m"
                alignment: Qt.AlignLeft
                label: root.hasCutoff ? qsTr("f−3dB") : qsTr("Gain")
                valueText: root.hasCutoff ? Units.format(root.controller.cutoffFrequencies[0], "Hz", 4)
                         : root.controller.pointCount > 0 ? Units.decibels(root.controller.readoutGainDb) : "—"
                stale: !root.controller.running && root.controller.pointCount === 0
            }
            Item { Layout.fillHeight: true }
        }
    }
}
