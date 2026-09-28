pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// The display readout of one channel (the ELVISmx "frequency display" / "Update Rate :" readout):
// OFF, the generated frequency (4 decimals), the actual update rate, the DC level or Done, with the
// channel's mode, the Output switch and the sweep progress.
Rectangle {
    id: root

    required property GenController controller
    required property GenChannel channel
    property bool compact: false
    readonly property color channelColor: channel.index === 0 ? Theme.ch0 : Theme.ch1
    readonly property bool live: channel.state === GenChannel.Running || channel.state === GenChannel.Sweeping
    readonly property var modeNames: [qsTr("Off"), qsTr("Function"), qsTr("Arbitrary"), qsTr("DC Level")]
    readonly property var modeIcons: ["power", "wave-sine-line", "wave-arbitrary-line", "wave-dc-line"]

    implicitHeight: column.implicitHeight + 2 * Theme.spacingM
    radius: Theme.radiusL
    color: Theme.alpha(channelColor, live ? (Theme.dark ? 0.09 : 0.07) : (Theme.dark ? 0.03 : 0.025))
    border.width: 1
    border.color: Theme.alpha(channelColor, live ? 0.45 : 0.18)
    Behavior on color { ColorAnimation { duration: Theme.durationNormal } }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.spacingM
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            ChannelChip {
                text: root.channel.name
                color: root.channelColor
                checkable: false
                checked: root.channel.outputOn
                compact: true
            }
            Icon {
                name: root.modeIcons[root.channel.mode]
                color: Theme.textSecondary
                size: Theme.iconSizeS
            }
            Text {
                Layout.fillWidth: true
                text: root.modeNames[root.channel.mode]
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                elide: Text.ElideRight
            }
            Led {
                on: root.live || root.channel.state === GenChannel.Done || root.channel.state === GenChannel.Paused
                color: root.live ? Theme.success : Theme.warning
                size: 8
            }
            ToggleSwitch {
                accentColor: root.channelColor
                enabled: root.channel.mode !== GenChannel.Off
                checked: root.channel.outputOn
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Output of %1").arg(root.channel.name)
                onToggled: root.controller.setOutput(root.channel.index, checked)
            }
        }

        Text {
            Layout.fillWidth: true
            text: root.channel.readoutText
            color: root.live ? root.channelColor
                 : root.channel.state === GenChannel.Done ? Theme.warning : Theme.textMuted
            font.pixelSize: root.compact ? 28 : 38
            font.weight: Font.DemiBold
            font.features: Theme.tabularFigures
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 18
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: root.channel.mode === GenChannel.Off ? qsTr("0 V") : root.channel.summary + "  ·  " + root.channel.readoutCaption
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
            font.features: Theme.tabularFigures
            elide: Text.ElideRight
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: root.channel.sweeping
            from: 0
            to: 1
            value: root.channel.sweepProgress
        }
    }
}
