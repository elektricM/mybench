import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// Standard push button: outlined; `highlighted` = primary (accent, filled); `flat` = ghost.
T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 6
    horizontalPadding: 12
    spacing: 6
    hoverEnabled: true
    font.weight: highlighted ? Font.DemiBold : Font.Medium
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: !enabled ? Theme.textDisabled : highlighted ? Theme.textOnAccent : Theme.text

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        icon: control.icon
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled : control.highlighted ? Theme.textOnAccent : Theme.text
    }

    background: Rectangle {
        implicitWidth: 72
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusM
        visible: !control.flat || control.down || control.hovered || control.checked
        color: control.highlighted ? (control.down ? Theme.accentPressed : control.hovered ? Theme.accentHover : Theme.accent)
             : control.down ? Theme.surfacePressed
             : control.checked ? Theme.accentMuted
             : control.hovered ? Theme.surfaceHover
             : control.flat ? "transparent" : Theme.surfaceRaised
        border.width: control.highlighted || control.flat ? 0 : 1
        border.color: control.checked ? Theme.accent : Theme.borderStrong
        opacity: control.enabled ? 1 : 0.6
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusM + 3 }
    }
}
