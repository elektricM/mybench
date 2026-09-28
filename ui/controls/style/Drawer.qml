import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.Drawer {
    id: control

    parent: T.Overlay.overlay

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    topPadding: control.edge === Qt.BottomEdge ? 1 : 0
    leftPadding: control.edge === Qt.RightEdge ? 1 : 0
    rightPadding: control.edge === Qt.LeftEdge ? 1 : 0
    bottomPadding: control.edge === Qt.TopEdge ? 1 : 0

    enter: Transition { SmoothedAnimation { velocity: 5 } }
    exit: Transition { SmoothedAnimation { velocity: 5 } }

    background: Rectangle {
        color: Theme.surface
        Rectangle {
            readonly property bool horizontal: control.edge === Qt.LeftEdge || control.edge === Qt.RightEdge
            width: horizontal ? 1 : parent.width
            height: horizontal ? parent.height : 1
            color: Theme.divider
            x: control.edge === Qt.LeftEdge ? parent.width - 1 : 0
            y: control.edge === Qt.TopEdge ? parent.height - 1 : 0
        }
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
