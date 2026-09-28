import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Vertical slide switch of the Digital Writer's manual pattern: up = HI, down = LO (like the
// ELVISmx switches). Controlled: bind `checked`, handle toggled().
T.AbstractButton {
    id: root

    property color accentColor: Theme.math

    checkable: true
    implicitWidth: 18
    implicitHeight: 36
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked

    Keys.onUpPressed: if (!checked) { toggle(); toggled() }
    Keys.onDownPressed: if (checked) { toggle(); toggled() }

    indicator: Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.checked && root.enabled ? Theme.alpha(root.accentColor, Theme.dark ? 0.35 : 0.28) : Theme.surfaceSunken
        border.width: 1
        border.color: root.checked && root.enabled ? root.accentColor : root.hovered && root.enabled ? Theme.borderStrong : Theme.border
        opacity: root.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            width: parent.width - 4
            height: width
            radius: width / 2
            x: 2
            y: root.checked ? 2 : parent.height - height - 2
            color: root.checked && root.enabled ? root.accentColor : Theme.textSecondary
            Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        }
        FocusRing { shown: root.visualFocus; radius: parent.radius + 3 }
    }
}
