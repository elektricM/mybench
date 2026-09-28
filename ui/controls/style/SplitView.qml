pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.SplitView {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    // A 1 px divider with a wider invisible grab area; highlighted while hovered/dragged.
    handle: Item {
        id: handleItem
        implicitWidth: control.orientation === Qt.Horizontal ? 5 : control.width
        implicitHeight: control.orientation === Qt.Horizontal ? control.height : 5
        Rectangle {
            anchors.centerIn: parent
            width: control.orientation === Qt.Horizontal ? (handleItem.T.SplitHandle.hovered || handleItem.T.SplitHandle.pressed ? 3 : 1) : parent.width
            height: control.orientation === Qt.Horizontal ? parent.height : (handleItem.T.SplitHandle.hovered || handleItem.T.SplitHandle.pressed ? 3 : 1)
            color: handleItem.T.SplitHandle.pressed ? Theme.accent
                 : handleItem.T.SplitHandle.hovered ? Theme.alpha(Theme.accent, 0.6) : Theme.divider
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
    }
}
