import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// A 4 px bar: accent fill on a faint track; indeterminate = a sliding segment.
T.ProgressBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    contentItem: Item {
        implicitWidth: 160
        implicitHeight: 4
        clip: true
        Rectangle {
            width: control.indeterminate ? parent.width * 0.3 : control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: control.enabled ? Theme.accent : Theme.textDisabled
            SequentialAnimation on x {
                running: control.indeterminate && control.visible && Theme.animations
                loops: Animation.Infinite
                NumberAnimation { from: -control.width * 0.3; to: control.width; duration: 1200; easing.type: Easing.InOutQuad }
            }
        }
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 4
        radius: 2
        color: Theme.alpha(Theme.text, 0.12)
    }
}
