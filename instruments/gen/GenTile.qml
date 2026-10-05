pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Waveform Generator (content only; the shell draws title, state and
// Run/Stop): bare mini previews of the outputs on the display surface and one readout per channel
// (frequency / update rate / DC level; its swatch is the legend). Neutral text; the channel colors
// only draw the traces and swatches. A channel that does not output is dimmed.
Item {
    id: root

    required property GenController controller

    function channelColor(channel: GenChannel): color {
        return channel.index === 0 ? Theme.signalCh0 : Theme.signalCh1
    }
    function live(channel: GenChannel): bool {
        return channel.state === GenChannel.Running || channel.state === GenChannel.Sweeping
    }

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

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                spacing: Theme.spacingXs
                Repeater {
                    model: [root.controller.channel0, root.controller.channel1]
                    delegate: BenchPlot {
                        id: mini
                        required property GenChannel modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: modelData.mode !== GenChannel.Off
                        bare: true
                        placeholderText: ""
                        xAxis { min: 0; max: mini.modelData.previewSpan; labelsVisible: false; gridVisible: false }
                        yAxis { min: mini.modelData.previewMin; max: mini.modelData.previewMax; labelsVisible: false; gridVisible: false }
                        PlotTrace {
                            buffer: mini.modelData.preview
                            color: root.channelColor(mini.modelData)
                            width: 1.3
                            step: mini.modelData.previewStep
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            spacing: Theme.spacingXs

            Repeater {
                model: [root.controller.channel0, root.controller.channel1]
                delegate: Readout {
                    required property GenChannel modelData
                    Layout.fillWidth: true
                    size: "s"
                    alignment: Qt.AlignLeft
                    swatchColor: root.channelColor(modelData)
                    label: modelData.audio ? (modelData.index === 0 ? qsTr("Left") : qsTr("Right")) : modelData.name
                    valueText: modelData.readoutText
                    stale: !root.live(modelData)
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
