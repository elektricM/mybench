import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

T.MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    horizontalPadding: 8
    spacing: 8
    hoverEnabled: true
    icon.width: Theme.iconSize
    icon.height: Theme.iconSize
    icon.color: enabled ? Theme.textSecondary : Theme.textDisabled

    contentItem: IconLabel {
        readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real indicatorPadding: control.indicator ? control.indicator.width + control.spacing : 0
        leftPadding: !control.mirrored ? indicatorPadding : arrowPadding
        rightPadding: control.mirrored ? indicatorPadding : arrowPadding
        spacing: control.spacing
        mirrored: control.mirrored
        display: control.display
        alignment: Qt.AlignLeft
        icon: control.icon
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
    }

    indicator: Item {
        implicitWidth: control.checkable ? 14 : 0
        implicitHeight: 14
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        Icon {
            anchors.centerIn: parent
            visible: control.checked
            name: "check"
            size: 14
            color: Theme.accent
        }
    }

    arrow: Icon {
        x: control.mirrored ? control.leftPadding : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.subMenu
        name: "chevron-right"
        size: 12
        color: Theme.textMuted
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 30
        radius: Theme.radiusS
        color: control.down ? Theme.surfacePressed : control.highlighted ? Theme.surfaceHover : "transparent"
    }
}
