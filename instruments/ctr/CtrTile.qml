import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Counter/Timer: the measured value (with a mini trend), or the pulse
// train / FREQOUT frequency, and the terminals in use.
Item {
    id: root

    required property CtrController controller

    readonly property bool measuring: controller.counterFunction === CtrController.Measure
    readonly property bool pulsing: controller.counterFunction === CtrController.PulseTrain

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        Readout {
            Layout.fillWidth: true
            alignment: Qt.AlignLeft
            label: root.controller.functionTitle
            valueText: root.measuring ? root.controller.valueText : ""
            value: root.pulsing ? (root.controller.outputFrequency > 0 ? root.controller.outputFrequency : root.controller.coercedPulseFrequency)
                                : root.controller.freqOutEnabled ? root.controller.coercedFreqOut : NaN
            unit: root.measuring ? root.controller.unit : "Hz"
            digits: 5
            fontSize: 28
            color: root.measuring ? Theme.ch0 : root.pulsing ? Theme.math : Theme.ch1
            stale: !root.controller.running
        }
        BenchPlot {
            visible: root.measuring
            Layout.fillWidth: true
            Layout.fillHeight: true
            interactive: false
            frameVisible: false
            leftMargin: 0
            rightMargin: 0
            topMargin: 0
            bottomMargin: 0
            xLabelsVisible: false
            yLabelsVisible: false
            xAxis { labelFormat: PlotAxis.Time; follow: true; gridVisible: false }
            yAxis { autoScale: true; gridVisible: false }
            Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 30, Date.now() / 1000)
            PlotTrace { buffer: root.controller.trend; color: Theme.ch0; width: 1.2 }
        }
        Item { visible: !root.measuring; Layout.fillHeight: true }
        Text {
            Layout.fillWidth: true
            text: {
                const parts = [root.controller.plannedLines !== 0 ? root.controller.terminals : qsTr("No terminals")]
                if (root.pulsing)
                    parts.push(qsTr("duty %1 %").arg((root.controller.coercedDutyCycle * 100).toFixed(1)))
                if (root.controller.freqOutEnabled && root.controller.counterFunction !== CtrController.CounterOff)
                    parts.push(qsTr("FREQOUT %1").arg(Units.format(root.controller.coercedFreqOut, "Hz", 4)))
                return parts.join(" · ")
            }
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
            elide: Text.ElideRight
        }
    }
}
