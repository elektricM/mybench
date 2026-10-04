import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Slider: a 4 px track filled with the accent up to the value, a 14 px white handle.
T.Slider {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitHandleHeight + topPadding + bottomPadding)

    padding: 2
    hoverEnabled: true

    handle: Rectangle {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * (control.availableWidth - width) : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : control.visualPosition * (control.availableHeight - height))
        implicitWidth: 14
        implicitHeight: 14
        radius: width / 2
        color: control.pressed ? Theme.mix("#FFFFFF", Theme.text, 0.08) : "#FFFFFF"
        border.width: Theme.borderWidth
        border.color: Theme.dark ? Theme.alpha("#000000", 0.35) : Theme.controlBorder
        opacity: control.enabled ? 1 : 0.6
        FocusRing { shown: control.visualFocus; radius: width / 2 + 3 }
    }

    background: Item {
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - width) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - height) / 2 : 0)
        implicitWidth: control.horizontal ? 160 : 4
        implicitHeight: control.horizontal ? 4 : 160
        width: control.horizontal ? control.availableWidth : implicitWidth
        height: control.horizontal ? implicitHeight : control.availableHeight
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: Theme.alpha(Theme.text, 0.15)
        }
        Rectangle {
            x: control.horizontal ? (control.mirrored ? parent.width - width : 0) : 0
            y: control.horizontal ? 0 : control.visualPosition * parent.height
            width: control.horizontal ? control.position * parent.width : parent.width
            height: control.horizontal ? parent.height : control.position * parent.height
            radius: 2
            color: Theme.accent
        }
    }
}
