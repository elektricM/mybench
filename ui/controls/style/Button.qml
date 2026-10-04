import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// Push button (docs/UI.md §4): flat fill from the palette's button color and a hairline border;
// `highlighted` = the one primary action (accent fill); `flat` = no fill until hovered.
// Height Theme.controlHeight (set implicitHeight to Theme.controlHeightL for the Run button and
// dialog buttons).
T.Button {
    id: control

    readonly property bool iconOnly: display === T.AbstractButton.IconOnly || (text.length === 0 && icon.source.toString().length > 0)
    // A disabled primary button looks like any disabled button.
    readonly property bool primary: highlighted && enabled
    readonly property color foreground: !enabled ? Theme.textDisabled
                                      : highlighted ? Theme.accentText
                                      : checked ? Theme.accent
                                      : Theme.text

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: iconOnly ? 0 : Theme.spacingM
    spacing: Theme.spacingXs + 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.pixelSize: Theme.fontSizeBody
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: foreground

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: control.foreground
    }

    background: Rectangle {
        implicitWidth: control.iconOnly ? Theme.controlHeight : 64
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl
        color: control.primary ? (control.down ? Theme.accentPressed : control.hovered ? Theme.accentHover : Theme.accent)
             : control.flat ? "transparent" : Theme.controlFill
        border.width: control.primary || control.flat ? 0 : Theme.borderWidth
        border.color: Theme.controlBorder
        opacity: control.enabled ? 1 : 0.55

        // Hover and press states: an overlay of the text color (none on the primary button).
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            visible: !control.primary
            color: control.down ? Theme.pressedFill
                 : control.checked ? Theme.selectionInactive
                 : control.hovered ? Theme.hoverFill : "transparent"
        }
        FocusRing { shown: control.visualFocus }
    }
}
