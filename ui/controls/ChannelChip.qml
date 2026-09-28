import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// Pill carrying a channel's color ("CH 0", "AI 1", "Math"). Checkable by default: a toggled-off
// chip is outlined (channel hidden/disabled).
//     ChannelChip { text: "CH 0"; color: Theme.ch0; checked: ctl.ch0Enabled; onToggled: ctl.ch0Enabled = checked }
T.AbstractButton {
    id: control

    property color color: Theme.ch0
    property string detail            // secondary text after the name, e.g. "1 V/div"
    property string toolTip
    property bool compact: false

    checkable: true
    checked: true
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    implicitHeight: compact ? 20 : 24
    implicitWidth: row.implicitWidth + (compact ? 14 : 18)
    font.pixelSize: compact ? Theme.fontSizeS : Theme.fontSizeM
    font.weight: Font.DemiBold

    background: Rectangle {
        radius: height / 2
        color: control.checked ? Theme.alpha(control.color, control.hovered ? 0.24 : (Theme.dark ? 0.15 : 0.12))
                               : (control.hovered ? Theme.surfaceHover : "transparent")
        border.width: 1
        border.color: control.checked ? Theme.alpha(control.color, 0.55) : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: height / 2 + 3 }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: control.compact ? 6 : 8
                height: width
                radius: width / 2
                color: control.checked ? control.color : "transparent"
                border.width: control.checked ? 0 : 1.5
                border.color: control.color
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.checked ? Theme.text : Theme.textMuted
            }
            Text {
                visible: control.detail.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: control.detail
                font.pixelSize: control.font.pixelSize
                font.features: Theme.tabularFigures
                color: Theme.textSecondary
            }
        }
    }

    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.delay: 500
    ToolTip.text: toolTip
}
