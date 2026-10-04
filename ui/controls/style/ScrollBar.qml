import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Overlay scroll bar: a thin rounded thumb that appears while scrolling or hovered, then fades.
T.ScrollBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2
    visible: control.policy !== T.ScrollBar.AlwaysOff
    minimumSize: orientation === Qt.Horizontal ? height / width : width / height

    contentItem: Rectangle {
        implicitWidth: control.interactive ? (control.hovered || control.pressed ? 8 : 6) : 3
        implicitHeight: control.interactive ? (control.hovered || control.pressed ? 8 : 6) : 3
        radius: width / 2
        color: Theme.alpha(Theme.text, control.pressed ? 0.5 : control.hovered ? 0.4 : 0.3)
        opacity: 0.0

        states: State {
            name: "active"
            when: control.policy === T.ScrollBar.AlwaysOn || (control.active && control.size < 1.0)
            PropertyChanges { control.contentItem.opacity: 1 }
        }
        transitions: Transition {
            from: "active"
            SequentialAnimation {
                PauseAnimation { duration: 600 }
                NumberAnimation { target: control.contentItem; duration: Theme.durationNormal; property: "opacity"; to: 0.0 }
            }
        }
    }
}
