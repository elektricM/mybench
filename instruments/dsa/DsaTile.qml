import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Spectrum Analyzer: detected fundamental. SKELETON.
Item {
    id: root

    required property DsaController controller

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS
        Readout {
            Layout.fillWidth: true
            alignment: Qt.AlignLeft
            label: qsTr("Fundamental")
            value: root.controller.fundamentalFrequency
            unit: "Hz"
            fontSize: 24
            color: Theme.ch0
            stale: !root.controller.running
        }
        BenchPlot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            bare: true
            xAxis { min: 0; max: root.controller.frequencySpan; labelsVisible: false; gridVisible: false }
            yAxis { min: -120; max: 10; labelsVisible: false; gridVisible: false }
            PlotTrace { buffer: root.controller.spectrum; color: Theme.ch0; width: 1; fill: true; fillOpacity: 0.2 }
        }
        Text {
            text: qsTr("Span %1 · %2 lines").arg(Units.format(root.controller.frequencySpan, "Hz"))
                                              .arg(root.controller.resolutionLabels[root.controller.resolutionIndex])
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
        }
    }
}
