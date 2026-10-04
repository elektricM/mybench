import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The scrolling column of Sections of an instrument (docs/UI.md §13.4). It draws no background (the
// host draws `Theme.window`), lays out at any width from 260 to 420, and keeps its scroll position
// when it is reparented at runtime (InspectorDock moves it between hosts) or resized.
//     Inspector { Section { sectionId: "timebase"; title: qsTr("Timebase") } Section { ... } }
Item {
    id: root

    default property alias sections: column.data
    property alias contentY: flick.contentY
    readonly property alias flickable: flick
    // v1 properties (ignored: the sections carry their own padding and hairlines).
    property int padding: 0
    property int spacing: 0
    property color color: "transparent"
    property real radius: 0

    implicitWidth: Theme.inspectorWidth
    implicitHeight: column.implicitHeight

    // The scroll position survives reparenting and size changes: while a parent or size change
    // settles, a move of contentY onto a bound (0 or the end) is the view being clamped, not a
    // scroll, and the position is restored once the new geometry is known.
    property real savedContentY: 0
    property bool restoring: false
    property bool settling: false
    function settle() {
        settling = true
        Qt.callLater(root.endSettle)
    }
    function endSettle() {
        settling = false
        restoreContentY()
    }
    function restoreContentY() {
        if (flick.height <= 0 || !root.visible)
            return
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        const target = Math.min(root.savedContentY, maxY)
        if (Math.abs(flick.contentY - target) > 0.5) {
            root.restoring = true
            flick.contentY = target
            root.restoring = false
        }
    }
    onParentChanged: settle()
    onVisibleChanged: if (visible) settle()

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        onContentYChanged: {
            if (root.restoring || height <= 0 || !root.visible)
                return
            const maxY = Math.max(0, contentHeight - height)
            const clamped = root.settling && (Math.abs(contentY - maxY) < 0.5 || Math.abs(contentY) < 0.5)
            if (!clamped)
                root.savedContentY = contentY
        }
        onHeightChanged: root.settle()
        onWidthChanged: root.settle()
        ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
        WheelScroll { flickable: flick }

        ColumnLayout {
            id: column
            width: flick.width
            spacing: 0
        }
    }
}
