import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// List row: hover overlay; `highlighted` = the keyboard/pointer row of a popup list (selection
// colors); `checked` = the selected row of a list (inactive selection).
T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    horizontalPadding: Theme.spacingS
    spacing: Theme.spacingS
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: highlighted ? Theme.selectionText : enabled ? Theme.textSecondary : Theme.textDisabled

    contentItem: IconLabel {
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: control.display === IconLabel.IconOnly || control.display === IconLabel.TextUnderIcon ? Qt.AlignCenter : Qt.AlignLeft
        icon: control.icon
        text: control.text
        font: control.font
        color: !control.enabled ? Theme.textDisabled : control.highlighted ? Theme.selectionText : Theme.text
    }

    background: Rectangle {
        implicitWidth: 100
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl - 1
        color: control.highlighted ? Theme.selection
             : control.down ? Theme.pressedFill
             : control.checked ? Theme.selectionInactive
             : control.hovered ? Theme.hoverFill : "transparent"
        FocusRing { shown: control.visualFocus; anchors.margins: 0; radius: parent.radius }
    }
}
