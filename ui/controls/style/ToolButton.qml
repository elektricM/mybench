import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    spacing: 6
    hoverEnabled: true
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: !enabled ? Theme.textDisabled : checked ? Theme.accent : hovered ? Theme.text : Theme.textSecondary

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled : control.checked ? Theme.accent : Theme.text
    }

    background: Rectangle {
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusS
        color: control.down ? Theme.surfacePressed : control.checked ? Theme.accentMuted
             : control.hovered ? Theme.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusS + 3 }
    }
}
