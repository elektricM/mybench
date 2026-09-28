import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Waveform Generator: output state per channel. SKELETON.
Item {
    id: root

    required property GenController controller

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS
        RowLayout {
            spacing: Theme.spacingS
            Text {
                text: root.controller.running ? qsTr("ON") : qsTr("OFF")
                color: root.controller.running ? Theme.success : Theme.textMuted
                font.pixelSize: 22
                font.weight: Font.Light
                font.letterSpacing: 2
            }
        ChannelChip {
            text: "AO 0"
            color: Theme.ch0
            checkable: false
            checked: root.controller.ch0Enabled
            compact: true
            detail: root.controller.ch0Enabled ? Units.format(root.controller.ch0Frequency, "Hz") + " · " + Units.format(root.controller.ch0Amplitude, "Vpp", 3) : qsTr("off")
        }
        ChannelChip {
            text: "AO 1"
            color: Theme.ch1
            checkable: false
            checked: root.controller.ch1Enabled
            compact: true
            detail: root.controller.ch1Enabled ? Units.format(root.controller.ch1Frequency, "Hz") + " · " + Units.format(root.controller.ch1Amplitude, "Vpp", 3) : qsTr("off")
        }
        }
        BenchPlot {
            id: tilePlot
            Layout.fillWidth: true
            Layout.fillHeight: true
            bare: true
            xAxis { labelsVisible: false; gridVisible: false }
            yAxis { min: -10.5; max: 10.5; labelsVisible: false; gridVisible: false }
            Component.onCompleted: xAxis.setRange(root.controller.preview0.xMinimum(), root.controller.preview0.xMaximum())
            PlotTrace { buffer: root.controller.preview0; color: Theme.ch0; width: 1.2 }
            PlotTrace { buffer: root.controller.preview1; color: Theme.ch1; width: 1.2 }
        }
    }
}
