import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// One title of the in-window MenuBar: plain text, a hover overlay, pressed while its menu is open.
T.MenuBarItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: Theme.spacingS + 2
    spacing: Theme.spacingXs
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: enabled ? Theme.text : Theme.textDisabled

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignCenter
        icon: control.icon
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
    }

    background: Rectangle {
        implicitWidth: 32
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl
        color: control.down || (control.menu && control.menu.visible) ? Theme.pressedFill
             : control.highlighted || control.hovered ? Theme.hoverFill : "transparent"
        FocusRing { shown: control.visualFocus }
    }
}
