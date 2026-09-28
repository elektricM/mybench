import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// Square, flat button with a tinted icon and a tooltip (toolbar actions).
//     IconButton { iconName: "help-circle"; toolTip: qsTr("Help"); shortcutText: "F1"; onClicked: ... }
T.AbstractButton {
    id: control

    property string iconName
    property string toolTip
    property string shortcutText         // shown in the tooltip: Theme.keys("Ctrl+K") ("⌘K" on macOS)
    property color iconColor: !enabled ? Theme.textDisabled
                              : checked ? accentColor
                              : (hovered || down) ? Theme.text : Theme.textSecondary
    property color accentColor: Theme.accent
    property real iconSize: Theme.iconSize
    property int size: Theme.controlHeight
    property bool bordered: false        // outlined variant (stand-alone buttons)
    property int badgeCount: 0           // small counter bubble (e.g. unread errors)
    property color badgeColor: Theme.danger

    implicitWidth: size
    implicitHeight: size
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: toolTip.length > 0 ? toolTip : iconName

    background: Rectangle {
        radius: Theme.radiusS
        color: control.down ? Theme.surfacePressed
             : control.checked ? Theme.accentMuted
             : control.hovered ? Theme.surfaceHover
             : control.bordered ? Theme.surfaceRaised : "transparent"
        border.width: control.bordered ? 1 : 0
        border.color: Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusS + 3 }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.iconColor
        }
    }

    Rectangle {
        visible: control.badgeCount > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -2
        anchors.topMargin: -2
        height: 15
        width: Math.max(height, badgeText.implicitWidth + 8)
        radius: height / 2
        color: control.badgeColor
        border.width: 2
        border.color: Theme.chrome
        Text {
            id: badgeText
            anchors.centerIn: parent
            text: control.badgeCount > 99 ? "99+" : control.badgeCount
            color: "#FFFFFF"
            font.pixelSize: 9
            font.weight: Font.Bold
        }
    }

    ToolTip.visible: hovered && toolTip.length > 0 && !down
    ToolTip.delay: 500
    ToolTip.text: shortcutText.length > 0 ? toolTip + "   " + shortcutText : toolTip
}
