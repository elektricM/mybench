import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.Slider {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    padding: 4
    hoverEnabled: true

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 16
        implicitHeight: 16
        radius: width / 2
        color: "#FFFFFF"
        border.width: 1
        border.color: control.pressed ? Theme.accent : Theme.dark ? "transparent" : Theme.borderStrong
        scale: control.pressed ? 1.12 : control.hovered ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: width / 2 + 3 }
    }

    background: Rectangle {
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth: control.horizontal ? 160 : 4
        implicitHeight: control.horizontal ? 4 : 160
        width: control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight
        radius: 2
        color: Theme.dark ? "#2A2E37" : "#DADDE3"
        scale: control.horizontal && control.mirrored ? -1 : 1
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            y: control.horizontal ? 0 : control.visualPosition * parent.height
            width: control.horizontal ? control.position * parent.width : 4
            height: control.horizontal ? 4 : control.position * parent.height
            radius: 2
            color: Theme.accent
        }
    }
}
