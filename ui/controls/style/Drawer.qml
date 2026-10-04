import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Side sheet (compact sidebar, overlay inspector): the window color, a hairline on the inner
// edge, the popup shadow.
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

    enter: Transition { NumberAnimation { property: "position"; duration: Theme.durationNormal; easing.type: Theme.easing } }
    exit: Transition { NumberAnimation { property: "position"; duration: Theme.durationNormal; easing.type: Theme.easing } }

    background: Rectangle {
        color: Theme.window
        layer.enabled: control.position > 0
        layer.effect: PopupShadow {}
        Rectangle {
            readonly property bool horizontal: control.edge === Qt.LeftEdge || control.edge === Qt.RightEdge
            width: horizontal ? 1 : parent.width
            height: horizontal ? parent.height : 1
            color: Theme.separator
            x: control.edge === Qt.LeftEdge ? parent.width - 1 : 0
            y: control.edge === Qt.TopEdge ? parent.height - 1 : 0
        }
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
