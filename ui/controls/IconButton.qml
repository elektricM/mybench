import QtQuick
import QtQuick.Controls

// v1 compatibility (use ToolButton): a square tool button with a tinted icon, a tooltip and an
// optional counter.
//     IconButton { iconName: "help-circle"; toolTip: qsTr("Help"); shortcutText: "F1"; onClicked: ... }
ToolButton {
    id: control

    property string iconName
    property string toolTip
    property string shortcutText
    property color iconColor: !enabled ? Theme.textDisabled
                              : checked ? accentColor
                              : (hovered || down) ? Theme.text : Theme.textSecondary
    property color accentColor: Theme.accent
    property real iconSize: Theme.iconSize
    property int size: Theme.controlHeight
    property bool bordered: false        // v1, ignored
    property int badgeCount: 0           // small counter (e.g. unread errors)
    property color badgeColor: Theme.negative

    implicitWidth: size
    implicitHeight: size
    display: AbstractButton.IconOnly
    icon.source: Theme.icon(iconName)
    icon.width: iconSize
    icon.height: iconSize
    icon.color: iconColor
    Accessible.name: toolTip.length > 0 ? toolTip : iconName

    Rectangle {
        visible: control.badgeCount > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -2
        anchors.topMargin: -2
        height: 14
        width: Math.max(height, badgeText.implicitWidth + 8)
        radius: height / 2
        color: control.badgeColor
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: control.badgeCount > 99 ? "99+" : control.badgeCount
            color: Theme.onColor(control.badgeColor)
            font.pixelSize: Theme.fontSizeCaption - 1
            font.weight: Font.Medium
        }
    }

    ToolTip.visible: hovered && toolTip.length > 0 && !down
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: shortcutText.length > 0 ? toolTip + "  " + shortcutText : toolTip
}
