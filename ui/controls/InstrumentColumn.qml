import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The column of cards right of an instrument's display (round 5, docs/UI.md §13.8b): one fixed
// width for every instrument (Theme.instrumentColumnWidth, `compact`: the narrow width), so a
// changed setting never resizes the display. The cards (the items declared inside) scroll; the
// tool palette (`tools`) stays pinned at the bottom, always reachable.
//     InstrumentColumn {
//         anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
//         compact: root.narrow
//         ChannelCard { Layout.fillWidth: true; ... }
//         tools: ToolPalette { ... }
//     }
Rectangle {
    id: column

    property bool compact: false
    default property alias cards: cardsLayout.data
    // The pinned tool palette (one item; it is given the column's inner width).
    property Item tools: null
    property alias spacing: cardsLayout.spacing
    readonly property alias flickable: flick
    readonly property bool scrolls: flick.contentHeight > flick.height + 0.5

    width: compact ? Theme.instrumentColumnCompactWidth : Theme.instrumentColumnWidth
    implicitWidth: width
    color: Theme.window

    onToolsChanged: {
        if (tools) {
            tools.parent = toolsSlot
            tools.x = Theme.spacingS
            tools.width = Qt.binding(() => toolsSlot.width - 2 * Theme.spacingS)
        }
    }

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.separator
    }

    Flickable {
        id: flick
        objectName: "instrumentColumnCards"
        anchors { left: parent.left; leftMargin: 1; right: parent.right; top: parent.top; bottom: toolsSlot.top }
        contentWidth: width
        contentHeight: cardsLayout.implicitHeight + 2 * Theme.spacingS
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { id: bar; policy: column.scrolls ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        ColumnLayout {
            id: cardsLayout
            x: Theme.spacingS
            y: Theme.spacingS
            // Room for the scroll bar when it shows (macOS draws it over the content).
            width: flick.width - 2 * Theme.spacingS - (column.scrolls ? bar.implicitWidth : 0)
            spacing: Theme.spacingS
        }
    }

    // The pinned palette, with a hairline above it while the cards scroll under it.
    Item {
        id: toolsSlot
        objectName: "instrumentColumnTools"
        anchors { left: parent.left; leftMargin: 1; right: parent.right; bottom: parent.bottom }
        // No `visible: height > 0` here: the palette inherits the slot's visibility, so that loop
        // kept a palette assigned before the column was shown hidden for good (R5 dsa).
        height: column.tools && column.tools.visible ? column.tools.implicitHeight + 2 * Theme.spacingS : 0
        Rectangle {
            visible: column.scrolls
            width: parent.width
            height: 1
            color: Theme.separator
        }
        // The palette is reparented here (onToolsChanged) and centred vertically.
        Binding {
            target: column.tools
            property: "y"
            value: Theme.spacingS
            when: column.tools !== null
        }
    }
}
