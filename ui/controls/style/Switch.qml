import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.Switch {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 2
    spacing: 8
    hoverEnabled: true

    indicator: Rectangle {
        implicitWidth: 30
        implicitHeight: 18
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? Theme.accent : (control.hovered ? Theme.surfacePressed : Theme.surfaceHover)
        border.width: control.checked ? 0 : 1
        border.color: Theme.borderStrong
        opacity: control.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }

        Rectangle {
            x: Math.max(2, Math.min(parent.width - width - 2, control.visualPosition * parent.width - width / 2))
            y: 2
            width: 14
            height: 14
            radius: 7
            color: control.checked ? "#FFFFFF" : (Theme.dark ? "#C9CDD6" : "#FFFFFF")
            border.width: control.checked || Theme.dark ? 0 : 1
            border.color: Theme.borderStrong
            Behavior on x {
                enabled: !control.down
                NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic }
            }
        }
        FocusRing { shown: control.visualFocus; radius: height / 2 + 3 }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
