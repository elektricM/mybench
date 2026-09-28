import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 6
    horizontalPadding: 10
    spacing: 8
    hoverEnabled: true
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: Theme.textSecondary

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: control.display === IconLabel.IconOnly || control.display === IconLabel.TextUnderIcon ? Qt.AlignCenter : Qt.AlignLeft
        icon: control.icon
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: Theme.controlHeight + 2
        radius: Theme.radiusS
        color: control.down ? Theme.surfacePressed
             : control.highlighted || control.hovered ? Theme.surfaceHover
             : control.checked ? Theme.surfaceSelected : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; anchors.margins: 0; radius: Theme.radiusS }
    }
}
