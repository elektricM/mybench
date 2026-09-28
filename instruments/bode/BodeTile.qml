import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Bode Analyzer: miniature gain curve, the -3 dB frequency and the sweep
// progress.
Item {
    id: root

    required property BodeController controller

    readonly property bool hasCutoff: controller.cutoffFrequencies.length > 0

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            Readout {
                Layout.fillWidth: true
                alignment: Qt.AlignLeft
                label: root.hasCutoff ? qsTr("−3 dB") : root.controller.pointCount > 0 ? qsTr("Gain @ %1").arg(Units.format(root.controller.readoutFrequency, "Hz", 3)) : qsTr("Bode")
                valueText: root.hasCutoff ? Units.format(root.controller.cutoffFrequencies[0], "Hz", 4)
                         : root.controller.pointCount > 0 ? Units.decibels(root.controller.readoutGainDb) : "—"
                fontSize: 22
                color: root.hasCutoff ? Theme.math : Theme.text
                stale: !root.controller.running && root.controller.pointCount === 0
            }
            Text {
                Layout.alignment: Qt.AlignTop | Qt.AlignRight
                text: root.controller.running && root.controller.totalPoints > 0
                      ? qsTr("%1/%2").arg(root.controller.pointCount).arg(root.controller.totalPoints)
                      : root.controller.pointCount > 0 ? qsTr("%1 pts").arg(root.controller.pointCount) : ""
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
                font.features: Theme.tabularFigures
            }
        }
        BenchPlot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            bare: true
            xAxis { min: root.controller.startFrequency; max: Math.max(root.controller.stopFrequency, root.controller.startFrequency * 1.001); scale: PlotAxis.Log; labelsVisible: false; gridVisible: false; minorGridVisible: false }
            yAxis { autoScale: root.controller.pointCount > 0; min: -40; max: 10; labelsVisible: false; gridVisible: false; minorGridVisible: false }
            PlotTrace { id: tileGain; buffer: root.controller.gain; color: Theme.ch0; width: 1.5 }
            PlotMarker {
                visible: root.hasCutoff
                x: root.hasCutoff ? root.controller.cutoffFrequencies[0] : 0
                trace: tileGain
                shape: PlotMarker.Ring
                size: 7
                color: Theme.math
            }
        }
        ProgressBar {
            visible: root.controller.running && root.controller.totalPoints > 0
            Layout.fillWidth: true
            value: root.controller.progress
        }
        Text {
            visible: !root.controller.running || root.controller.totalPoints === 0
            text: Units.format(root.controller.startFrequency, "Hz", 3) + " – " + Units.format(root.controller.stopFrequency, "Hz", 3)
                  + qsTr(" · %1/decade").arg(root.controller.stepsPerDecade)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }
}
