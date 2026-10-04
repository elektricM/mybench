import QtQuick
import MyBench.Controls

// The drag image of an instrument (docs/UI.md §8): icon + name next to the pointer, in the window
// overlay. `handler` is the DragHandler (target: null) of the row, tile or pane header that is
// dragged; the drop targets (Workspace, Sidebar) read `paneId` and `origin` from `drag.source`.
// The drag ends with a drop (not a cancel) when the handler is released.
Rectangle {
    id: chip

    property DragHandler handler: null
    // The item the drag starts from (default: the handler's item); its window hosts the chip.
    property Item source: handler ? handler.parent : null
    property string paneId
    property string title
    property string iconName
    property string origin: "sidebar"     // "sidebar" | "tile" | "pane"

    readonly property real offset: Theme.spacingM

    // Above everything in the source's window, the header bar included (an ApplicationWindow's
    // content item has the window's item as parent). Overlay.overlay is not known yet when the
    // chip is created, and a binding to it does not update.
    parent: {
        const content = source && source.Window.window ? source.Window.window.contentItem : null
        return content ? content.parent ?? content : null
    }
    visible: Drag.active
    z: 1000
    width: chipRow.implicitWidth + 2 * Theme.spacingS
    height: Theme.controlHeight
    radius: Theme.radiusControl
    color: Theme.base
    border.width: Theme.borderWidth
    border.color: Theme.accent
    opacity: 0.95

    Drag.keys: ["mybench/instrument"]
    Drag.dragType: Drag.Internal
    // The pointer, not the chip's corner, is the drop position (follow() keeps it so).
    Drag.hotSpot.x: -offset
    Drag.hotSpot.y: -offset

    // Puts the chip next to the scene point `p` (the parent may not start at the scene origin).
    function follow(p: point) {
        if (!parent)
            return
        const local = parent.mapFromItem(null, p.x, p.y)
        // Stays in the window: left of / above the pointer near the right or bottom edge.
        x = local.x + offset + width <= parent.width ? local.x + offset : local.x - offset - width
        y = local.y + offset + height <= parent.height ? local.y + offset : local.y - offset - height
        Drag.hotSpot = Qt.point(local.x - x, local.y - y)   // the pointer is the drop position
    }
    Connections {
        target: chip.handler
        function onCentroidChanged() {
            if (chip.handler.active)
                chip.follow(chip.handler.centroid.scenePosition)
        }
        function onActiveChanged() {
            if (chip.handler.active) {
                chip.follow(chip.handler.centroid.scenePosition)
                chip.Drag.active = true
            } else if (chip.Drag.active) {
                chip.Drag.drop()
            }
        }
    }

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: Theme.spacingXs
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.iconName.length > 0
            name: chip.iconName
            size: Theme.iconSize
            color: Theme.textSecondary
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.title
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
        }
    }
}
