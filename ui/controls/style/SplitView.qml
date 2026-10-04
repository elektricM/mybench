pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// A 1 px hairline between the panes with a wider invisible grab area.
T.SplitView {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    handle: Item {
        id: handleItem
        readonly property bool active: handleItem.T.SplitHandle.hovered || handleItem.T.SplitHandle.pressed
        implicitWidth: control.orientation === Qt.Horizontal ? 1 : control.width
        implicitHeight: control.orientation === Qt.Horizontal ? control.height : 1
        Rectangle {
            anchors.fill: parent
            color: handleItem.active ? Theme.controlBorder : Theme.separator
        }
        // Grab area wider than the line.
        containmentMask: QtObject {
            function contains(point: point): bool {
                return control.orientation === Qt.Horizontal ? point.x >= -3 && point.x <= 4 && point.y >= 0 && point.y <= handleItem.height
                                                             : point.y >= -3 && point.y <= 4 && point.x >= 0 && point.x <= handleItem.width
            }
        }
    }
}
