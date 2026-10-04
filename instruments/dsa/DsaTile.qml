import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Spectrum Analyzer (content only; the shell draws title, state and
// Run/Stop): a bare mini spectrum (or the 1/3-octave bars) on the display surface, the fundamental
// and THD (or the overall level). Neutral text; the source color only draws the trace.
Item {
    id: root

    required property DsaController controller
    readonly property color sourceColor: Theme.signalColor(["ai0", "ai1", "audioInL", "audioInR"][controller.sourceChannel] ?? "ai0")
    readonly property bool stale: !controller.running

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
                visible: !root.controller.octave
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                bare: true
                placeholderText: ""
                xAxis { min: 0; max: root.controller.frequencySpan; labelsVisible: false; gridVisible: false; minorGridVisible: false }
                yAxis { min: root.controller.spectrumMinimum; max: root.controller.spectrumMaximum; labelsVisible: false; gridVisible: false; minorGridVisible: false }
                PlotTrace { buffer: root.controller.spectrum; color: root.sourceColor; width: 1 }
            }
            BenchPlot {
                visible: root.controller.octave
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                bare: true
                placeholderText: ""
                xAxis { min: root.controller.octaveFrequencyMinimum; max: root.controller.octaveFrequencyMaximum; scale: PlotAxis.Log; labelsVisible: false; gridVisible: false; minorGridVisible: false }
                yAxis { min: root.controller.octaveMinimum; max: root.controller.octaveMaximum; labelsVisible: false; gridVisible: false; minorGridVisible: false }
                PlotTrace { buffer: root.controller.octaveBars; color: root.sourceColor; width: 1; fill: true; fillOpacity: 0.5 }
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
                label: root.controller.octave ? qsTr("Overall") : qsTr("Fundamental")
                valueText: root.controller.octave
                           ? (root.controller.hasOctaveResult ? root.controller.octaveOverall.toFixed(1) + " dBV" : "—")
                           : (root.controller.hasResult ? Units.format(root.controller.fundamentalFrequency, "Hz", 4) : "—")
                stale: root.stale
            }
            Readout {
                visible: !root.controller.octave
                Layout.fillWidth: true
                size: "s"
                alignment: Qt.AlignLeft
                label: qsTr("THD")
                valueText: root.controller.hasResult ? root.controller.thd.toFixed(2) + " %" : "—"
                stale: root.stale
            }
            Item { Layout.fillHeight: true }
        }
    }
}
