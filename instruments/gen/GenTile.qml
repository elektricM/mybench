pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Waveform Generator: per channel its mode icon, the readout (OFF / frequency
// / update rate / DC level), the output LED, and a mini preview of both outputs.
Item {
    id: root

    required property GenController controller

    component ChannelRow: RowLayout {
        id: row
        required property GenChannel channel
        readonly property color channelColor: channel.index === 0 ? Theme.ch0 : Theme.ch1
        readonly property bool live: channel.state === GenChannel.Running || channel.state === GenChannel.Sweeping
        readonly property var icons: ["power", "wave-sine-line", "wave-arbitrary-line", "wave-dc-line"]
        spacing: Theme.spacingS
        Rectangle {
            Layout.preferredWidth: 3
            Layout.fillHeight: true
            radius: 1.5
            color: row.channelColor
            opacity: row.live ? 1 : 0.35
        }
        Text {
            Layout.preferredWidth: 34
            text: row.channel.audio ? (row.channel.index === 0 ? "L" : "R") : row.channel.name.replace(" ", "")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
            font.weight: Font.DemiBold
        }
        Icon {
            name: row.icons[row.channel.mode]
            size: Theme.iconSizeS
            color: row.live ? row.channelColor : Theme.textMuted
        }
        Text {
            Layout.fillWidth: true
            text: row.channel.readoutText
            color: row.live ? Theme.text : row.channel.state === GenChannel.Done ? Theme.warning : Theme.textMuted
            font.pixelSize: 16
            font.weight: Font.DemiBold
            font.features: Theme.tabularFigures
            elide: Text.ElideRight
        }
        Led {
            on: row.live || row.channel.state === GenChannel.Done || row.channel.state === GenChannel.Paused
            color: row.live ? Theme.success : Theme.warning
            size: 7
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4
        ChannelRow { Layout.fillWidth: true; Layout.preferredHeight: 22; channel: root.controller.channel0 }
        ChannelRow { Layout.fillWidth: true; Layout.preferredHeight: 22; channel: root.controller.channel1 }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 24
            spacing: Theme.spacingS
            Repeater {
                model: [root.controller.channel0, root.controller.channel1]
                delegate: BenchPlot {
                    id: mini
                    required property GenChannel modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: modelData.mode !== GenChannel.Off
                    bare: true
                    xAxis { min: 0; max: mini.modelData.previewSpan }
                    yAxis { min: mini.modelData.previewMin; max: mini.modelData.previewMax }
                    PlotTrace {
                        buffer: mini.modelData.preview
                        color: mini.modelData.index === 0 ? Theme.ch0 : Theme.ch1
                        width: 1.3
                        step: mini.modelData.previewStep
                    }
                }
            }
        }
    }
}
