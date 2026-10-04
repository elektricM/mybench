pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Content of the Sweep tool panel: per Function channel the ELVISmx sweep settings (start, stop,
// step, step interval) and Start Sweep / Stop, with the step progress while it sweeps.
ColumnLayout {
    id: root

    required property GenController controller

    spacing: Theme.spacingM

    Text {
        visible: !root.controller.anyFunction
        Layout.fillWidth: true
        text: qsTr("Pick Sine, Square or Triangle on a channel to sweep its frequency.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: [root.controller.channel0, root.controller.channel1]
        delegate: ColumnLayout {
            id: block
            required property GenChannel modelData
            readonly property color channelColor: modelData.index === 0 ? Theme.signalAo0 : Theme.signalAo1
            visible: modelData.mode === GenChannel.Function
            Layout.fillWidth: true
            spacing: Theme.spacingXs

            RowLayout {
                Layout.fillWidth: true
                Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 5; color: block.channelColor }
                Text {
                    Layout.fillWidth: true
                    text: block.modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Font.DemiBold
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacingS
                rowSpacing: Theme.spacingXs
                enabled: !block.modelData.sweeping
                Text { text: qsTr("Start"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
                Text { text: qsTr("Stop"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
                SiField {
                    objectName: "sweepStart" + block.modelData.index
                    Layout.fillWidth: true
                    unit: "Hz"; from: 0.2; to: 20000; digits: 5
                    value: block.modelData.sweepStart
                    onValueModified: v => block.modelData.sweepStart = v
                }
                SiField {
                    objectName: "sweepStop" + block.modelData.index
                    Layout.fillWidth: true
                    unit: "Hz"; from: 0.2; to: 20000; digits: 5
                    value: block.modelData.sweepStop
                    onValueModified: v => block.modelData.sweepStop = v
                }
                Text { text: qsTr("Step"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
                Text { text: qsTr("Step Interval"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeS }
                SiField {
                    Layout.fillWidth: true
                    unit: "Hz"; from: 0; to: 20000; digits: 5
                    value: block.modelData.sweepStep
                    onValueModified: v => block.modelData.sweepStep = v
                }
                SiField {
                    Layout.fillWidth: true
                    unit: "ms"; engineering: false; decimals: 0; stepSize: 1; from: 1; to: 300000
                    value: block.modelData.sweepInterval
                    onValueModified: v => block.modelData.sweepInterval = v
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Button {
                    objectName: "startSweep" + block.modelData.index
                    text: block.modelData.sweeping ? qsTr("Stop Sweep") : qsTr("Start Sweep")
                    icon.source: Theme.icon(block.modelData.sweeping ? "stop" : "sweep")
                    highlighted: !block.modelData.sweeping
                    // A sweep starts from a stopped channel.
                    enabled: block.modelData.sweeping || !block.modelData.generating
                    ToolTip.visible: hovered && !enabled
                    ToolTip.text: qsTr("Switch %1 off to sweep it").arg(block.modelData.name)
                    onClicked: block.modelData.sweeping ? root.controller.setOutput(block.modelData.index, false)
                                                        : root.controller.startSweep(block.modelData.index)
                }
                ProgressBar {
                    visible: block.modelData.sweeping
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: block.modelData.sweepProgress
                }
                Text {
                    visible: block.modelData.sweeping
                    text: qsTr("%1 / %2").arg(block.modelData.sweepStepIndex).arg(block.modelData.sweepStepCount)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS
                }
            }
        }
    }
}
