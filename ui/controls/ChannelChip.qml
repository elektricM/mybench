import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// v1 compatibility (use ChannelBadge): a channel swatch and name, checkable (an unchecked chip has an
// empty swatch: channel hidden).
//     ChannelChip { text: "CH 0"; color: Theme.signalCh0; checked: ctl.ch0Enabled; onToggled: ctl.ch0Enabled = checked }
T.AbstractButton {
    id: control

    property color color: Theme.signalCh0
    property string detail
    property string toolTip
    property bool compact: false

    checkable: true
    checked: true
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    implicitHeight: compact ? Theme.controlHeightS - 2 : Theme.controlHeightS
    implicitWidth: row.implicitWidth + 2 * Theme.spacingS
    font.pixelSize: compact ? Theme.fontSizeCaption : Theme.fontSizeSmall

    background: Rectangle {
        radius: Theme.radiusControl
        color: control.down ? Theme.pressedFill : control.hovered ? Theme.hoverFill : "transparent"
        FocusRing { shown: control.visualFocus }
    }

    contentItem: Item {
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.spacingXs + 2
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 2
                color: control.checked ? control.color : "transparent"
                border.width: control.checked ? 0 : 1.5
                border.color: control.color
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.checked ? Theme.text : Theme.textTertiary
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
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: toolTip
}
