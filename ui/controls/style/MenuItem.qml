import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// In-window menu entry (native menus draw their own): check mark column, icon, Title Case text,
// the shortcut right-aligned in secondary text, a chevron for submenus; the highlighted entry
// takes the selection colors.
T.MenuItem {
    id: control

    readonly property string shortcutText: {
        const sequence = action && action.shortcut !== undefined ? String(action.shortcut) : ""
        return sequence.length > 0 ? Theme.keys(sequence) : ""
    }
    readonly property color foreground: !enabled ? Theme.textDisabled : highlighted ? Theme.selectionText : Theme.text

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    leftPadding: Theme.spacingS + 16
    rightPadding: Theme.spacingS + (subMenu ? 16 : 0)
    spacing: Theme.spacingS
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: foreground

    contentItem: Item {
        implicitWidth: label.implicitWidth + (shortcut.visible ? shortcut.implicitWidth + 2 * Theme.spacingL : 0)
        implicitHeight: label.implicitHeight
        IconLabel {
            id: label
            width: parent.width - (shortcut.visible ? shortcut.width + Theme.spacingL : 0)
            height: parent.height
            spacing: control.spacing
            mirrored: control.mirrored
            display: control.display
            alignment: Qt.AlignLeft | Qt.AlignVCenter
            icon: control.icon
            text: control.text
            font: control.font
            color: control.foreground
        }
        Text {
            id: shortcut
            visible: control.shortcutText.length > 0
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.shortcutText
            font.pixelSize: control.font.pixelSize
            color: control.highlighted ? Theme.selectionText : Theme.textSecondary
        }
    }

    indicator: Icon {
        x: control.mirrored ? control.width - width - Theme.spacingS / 2 - 2 : (control.leftPadding - width) / 2 + 2
        y: (control.height - height) / 2
        visible: control.checked
        name: "check"
        size: 12
        color: control.foreground
    }

    arrow: Icon {
        x: control.mirrored ? Theme.spacingS / 2 : control.width - width - Theme.spacingS / 2
        y: (control.height - height) / 2
        visible: control.subMenu
        name: "chevron-right"
        size: 12
        color: control.highlighted ? Theme.selectionText : Theme.textSecondary
    }

    background: Rectangle {
        implicitWidth: 180
        implicitHeight: Theme.controlHeight - 2
        radius: Theme.radiusControl - 1
        color: control.highlighted ? Theme.selection : "transparent"
    }
}
