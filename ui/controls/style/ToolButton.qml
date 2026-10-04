import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// Tool button: no fill or border until hovered; `checked` shows a quiet selection fill and an
// accent icon. Icon-only buttons are square (Theme.controlHeight; Theme.controlHeightS in plot
// toolbars).
T.ToolButton {
    id: control

    readonly property bool iconOnly: display === T.AbstractButton.IconOnly || (text.length === 0 && icon.source.toString().length > 0)

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: iconOnly ? 0 : Theme.spacingS
    spacing: Theme.spacingXs + 2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.pixelSize: Theme.fontSizeBody
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: !enabled ? Theme.textDisabled
              : checked ? Theme.accent
              : hovered || down ? Theme.text : Theme.textSecondary

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
        radius: Theme.radiusControl
        color: control.down ? Theme.pressedFill
             : control.checked ? Theme.selectionInactive
             : control.hovered ? Theme.hoverFill : "transparent"
        FocusRing { shown: control.visualFocus }
    }
}
