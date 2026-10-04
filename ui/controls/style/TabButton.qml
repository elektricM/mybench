import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// A tab: secondary text, primary text when current or hovered (the TabBar draws the underline).
T.TabButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: Theme.spacingM
    spacing: Theme.spacingXs + 2
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody
    font.weight: checked ? Font.Medium : Font.Normal
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: !enabled ? Theme.textDisabled : checked || hovered ? Theme.text : Theme.textSecondary

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled : control.checked || control.hovered ? Theme.text : Theme.textSecondary
    }

    background: Item {
        implicitHeight: Theme.controlHeight + 6
        FocusRing { shown: control.visualFocus; anchors.margins: 2; radius: Theme.radiusControl }
    }
}
