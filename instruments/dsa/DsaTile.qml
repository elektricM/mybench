import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Spectrum Analyzer: fundamental and THD over a mini spectrum (or the
// overall level over the band bars in 1/3-octave mode).
Item {
    id: root

    required property DsaController controller

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            Readout {
                Layout.fillWidth: true
                alignment: Qt.AlignLeft
                label: root.controller.octave ? qsTr("Overall") : qsTr("Fundamental")
                valueText: root.controller.octave
                           ? (root.controller.hasOctaveResult ? root.controller.octaveOverall.toFixed(1) + " dBV" : "—")
                           : (root.controller.hasResult ? Units.format(root.controller.fundamentalFrequency, "Hz", 4, false) : "—")
                fontSize: 22
                color: Theme.ch0
                stale: !root.controller.running
            }
            Readout {
                visible: !root.controller.octave
                implicitWidth: 80
                alignment: Qt.AlignLeft
                label: qsTr("THD")
                valueText: root.controller.hasResult ? root.controller.thd.toFixed(2) + " %" : "—"
                fontSize: 16
                color: Theme.math
                stale: !root.controller.running
            }
        }
        BenchPlot {
            visible: !root.controller.octave
            Layout.fillWidth: true
            Layout.fillHeight: true
            bare: true
            xAxis { min: 0; max: root.controller.frequencySpan; labelsVisible: false; gridVisible: false; minorGridVisible: false }
            yAxis { min: root.controller.spectrumMinimum; max: root.controller.spectrumMaximum; labelsVisible: false; gridVisible: false; minorGridVisible: false }
            PlotTrace { buffer: root.controller.spectrum; color: Theme.ch0; width: 1; fill: true; fillOpacity: 0.2 }
        }
        BenchPlot {
            visible: root.controller.octave
            Layout.fillWidth: true
            Layout.fillHeight: true
            bare: true
            xAxis { min: root.controller.octaveFrequencyMinimum; max: root.controller.octaveFrequencyMaximum; scale: PlotAxis.Log; labelsVisible: false; gridVisible: false; minorGridVisible: false }
            yAxis { min: root.controller.octaveMinimum; max: root.controller.octaveMaximum; labelsVisible: false; gridVisible: false; minorGridVisible: false }
            PlotTrace { buffer: root.controller.octaveBars; color: Theme.ch0; width: 1; fill: true; fillOpacity: 0.5 }
        }
        Text {
            Layout.fillWidth: true
            text: root.controller.octave
                  ? qsTr("1/3 octave · %1").arg(root.controller.sourceName)
                  : qsTr("%1 · %2 span · %3 lines").arg(root.controller.sourceName)
                        .arg(Units.format(root.controller.frequencySpan, "Hz", 3)).arg(root.controller.lines)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
            elide: Text.ElideRight
        }
    }
}
