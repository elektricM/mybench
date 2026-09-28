import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

T.TabButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    horizontalPadding: 12
    spacing: 6
    hoverEnabled: true
    font.weight: checked ? Font.DemiBold : Font.Medium
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: checked ? Theme.text : Theme.textSecondary

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled : control.checked || control.hovered ? Theme.text : Theme.textSecondary
    }

    background: Rectangle {
        implicitHeight: 34
        radius: Theme.radiusS
        color: control.down ? Theme.surfacePressed : control.hovered && !control.checked ? Theme.surfaceHover : "transparent"
        anchors.margins: 3
        FocusRing { shown: control.visualFocus; radius: Theme.radiusS + 3 }
    }
}
