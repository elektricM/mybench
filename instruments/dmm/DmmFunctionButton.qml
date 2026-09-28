import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import MyBench.Controls

// One of the nine function buttons of the Multimeter (icon + short caption), exclusive like the
// ELVISmx function selector. Disabled buttons (Capacitance, Inductance) keep their tooltip.
T.AbstractButton {
    id: root

    property string iconName
    property string caption
    property string toolTip
    property string shortcutText

    implicitWidth: 84
    implicitHeight: 52
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: toolTip
    Accessible.checked: checked

    background: Rectangle {
        radius: Theme.radiusM
        color: root.checked ? Theme.accentMuted
             : root.down ? Theme.surfacePressed
             : root.hovered && root.enabled ? Theme.surfaceHover
             : Theme.surfaceSunken
        border.width: 1
        border.color: root.checked ? Theme.alpha(Theme.accent, 0.75) : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: root.visualFocus }
    }

    contentItem: Item {
        Column {
            anchors.centerIn: parent
            spacing: 3
            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: root.iconName
                size: 20
                color: !root.enabled ? Theme.textDisabled : root.checked ? Theme.accent : Theme.textSecondary
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.caption
                color: !root.enabled ? Theme.textDisabled : root.checked ? Theme.text : Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.weight: root.checked ? Font.DemiBold : Font.Normal
            }
        }
    }

    // Disabled buttons do not get hover events: a HoverHandler keeps their tooltip.
    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && root.toolTip.length > 0
    ToolTip.delay: 500
    ToolTip.text: root.shortcutText.length > 0 ? root.toolTip + "  (" + root.shortcutText + ")" : root.toolTip
}
