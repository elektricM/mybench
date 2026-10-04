import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Counter/Timer (content only; the shell draws title, state and Run/Stop):
// a bare mini trend of the measured value on the display surface and the value (or the pulse
// train / FREQOUT frequency). Neutral text; the channel color only draws the trace.
Item {
    id: root

    required property CtrController controller

    readonly property bool measuring: controller.counterFunction === CtrController.Measure
    readonly property bool pulsing: controller.counterFunction === CtrController.PulseTrain

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM

        Rectangle {
            visible: root.measuring
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
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 30, Date.now() / 1000)
                PlotTrace { buffer: root.controller.trend; color: Theme.signalCh0; width: 1.2 }
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
                label: root.measuring ? root.controller.functionTitle : root.pulsing ? qsTr("Pulse train") : qsTr("FREQOUT")
                valueText: root.measuring ? root.controller.valueText : ""
                value: root.pulsing ? (root.controller.outputFrequency > 0 ? root.controller.outputFrequency : root.controller.coercedPulseFrequency)
                                    : root.controller.freqOutEnabled ? root.controller.coercedFreqOut : NaN
                unit: root.measuring ? root.controller.unit : "Hz"
                digits: 5
                stale: !root.controller.running
            }
            Item { Layout.fillHeight: true }
        }
    }
}
