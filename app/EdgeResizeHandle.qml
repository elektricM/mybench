import QtQuick
import MyBench.Controls

// The draggable left edge of a right-hand column (the inspector dock): emits the width the column
// should get. Place it as a child of the column.
//     EdgeResizeHandle { currentWidth: dock.width; onResized: width => prefs.setInspectorWidth(width) }
MouseArea {
    id: handle

    property real currentWidth: 0
    property real startX: 0
    property real startWidth: 0

    signal resized(real width)

    x: -Theme.spacingXs
    width: Theme.spacingS
    height: parent ? parent.height : 0
    z: 10
    cursorShape: Qt.SplitHCursor
    onPressed: mouse => {
        startX = mapToItem(null, mouse.x, 0).x
        startWidth = currentWidth
    }
    onPositionChanged: mouse => {
        if (pressed)
            resized(startWidth - (mapToItem(null, mouse.x, 0).x - startX))
    }
}
