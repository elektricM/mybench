import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.ScrollIndicator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 2

    contentItem: Rectangle {
        implicitWidth: 3
        implicitHeight: 3
        radius: 1.5
        color: Theme.alpha(Theme.textMuted, 0.6)
        visible: control.size < 1.0
        opacity: 0.0

        states: State {
            name: "active"
            when: control.active
            PropertyChanges { control.contentItem.opacity: 1 }
        }
        transitions: Transition {
            from: "active"
            SequentialAnimation {
                PauseAnimation { duration: 450 }
                NumberAnimation { target: control.contentItem; duration: 200; property: "opacity"; to: 0.0 }
            }
        }
    }
}
