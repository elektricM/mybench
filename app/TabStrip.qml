pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// Workspace tabs: Home, one tab per open instrument (or two side by side), "+" to open one, and
// the split actions of the current tab. Drag a tab along the strip to reorder it, or onto the
// left/right half of the workspace to show it in that pane (WorkspaceView's DropArea, drag keys
// "mybench-tab" + "instrument:<id>"; app.md 2.4).
Rectangle {
    id: strip

    required property WorkspaceState workspace

    implicitHeight: 40
    color: Theme.chrome

    // Tab drag state: the dragged tab, its instrument (single tabs only: split tabs are only
    // reordered), the pointer in strip coordinates and the insertion index (0..tabs.length).
    property int dragIndex: -1
    property string dragInstrument: ""
    property point dragPoint: Qt.point(0, 0)
    readonly property bool pointerInStrip: dragPoint.y >= -8 && dragPoint.y <= height + 8
    readonly property int insertIndex: {
        if (dragIndex < 0)
            return -1
        for (let i = 0; i < tabRepeater.count; ++i) {
            const item = tabRepeater.itemAt(i)
            if (!item)
                continue
            const p = item.mapToItem(strip, item.width / 2, 0)
            if (dragPoint.x < p.x)
                return i
        }
        return tabRepeater.count
    }
    function beginDrag(index: int, instrumentId: string) {
        dragInstrument = instrumentId
        dragIndex = index
    }
    function endDrag() {
        const from = dragIndex
        const insert = insertIndex
        const overPane = dragGhost.Drag.target !== null
        if (overPane)
            dragGhost.Drag.drop()
        dragIndex = -1
        dragInstrument = ""
        if (!overPane && pointerInStrip && from >= 0 && insert >= 0)
            workspace.moveTab(from, insert > from ? insert - 1 : insert)
    }

    component StripTab: T.AbstractButton {
        id: tab

        property bool selected: false
        property var icons: []
        property bool running: false
        property bool closable: true
        signal closeRequested()

        implicitHeight: strip.height - 6
        implicitWidth: row.implicitWidth + (closable ? 40 : 24)
        hoverEnabled: true
        focusPolicy: Qt.TabFocus

        // Middle click closes the tab, like a browser tab (Home is not closable).
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            enabled: tab.closable
            onTapped: tab.closeRequested()
        }

        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.bottomMargin: -Theme.radiusL
                radius: Theme.radiusL
                color: tab.selected ? Theme.surface : tab.hovered ? Theme.surfaceHover : "transparent"
                border.width: tab.selected ? 1 : 0
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
            // hide the bottom border of the selected tab so it joins the panel
            Rectangle {
                visible: tab.selected
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -1
                x: 1
                width: parent.width - 2
                height: 2
                color: Theme.surface
            }
            FocusRing { shown: tab.visualFocus; radius: Theme.radiusL + 3 }
        }
        contentItem: Item {
            Row {
                id: row
                anchors.verticalCenter: parent.verticalCenter
                x: 12
                spacing: 7
                Repeater {
                    model: tab.icons
                    delegate: Icon {
                        required property string modelData
                        y: (row.height - height) / 2
                        name: modelData
                        size: Theme.iconSizeS
                        color: tab.selected ? Theme.text : Theme.textSecondary
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tab.text.length > 0
                    text: tab.text
                    color: tab.selected ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: tab.selected ? Font.DemiBold : Font.Medium
                }
                StatusDot {
                    visible: tab.running
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.success
                    pulse: true
                    size: 6
                }
            }
            IconButton {
                visible: tab.closable && (tab.hovered || tab.selected)
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                size: 20
                iconSize: 12
                iconName: "close"
                toolTip: qsTr("Close tab (stops the instrument)")
                shortcutText: Theme.keys("Ctrl+W")
                onClicked: tab.closeRequested()
            }
        }
    }

    Divider {
        anchors.bottom: parent.bottom
        width: parent.width
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingS
        anchors.rightMargin: Theme.spacingS
        anchors.topMargin: 6
        spacing: 2

        StripTab {
            Layout.alignment: Qt.AlignBottom
            icons: ["home"]
            text: qsTr("Home")
            closable: false
            selected: strip.workspace.home
            onClicked: strip.workspace.showHome()
        }

        Repeater {
            id: tabRepeater
            model: strip.workspace.tabs
            delegate: StripTab {
                id: instrumentTab
                objectName: "instrumentTab-" + modelData.primary
                required property var modelData
                required property int index
                readonly property var primary: InstrumentRegistry.info(modelData.primary)
                readonly property var secondary: modelData.secondary.length > 0 ? InstrumentRegistry.info(modelData.secondary) : null
                readonly property InstrumentController primaryController: InstrumentRegistry.controller(modelData.primary)
                readonly property InstrumentController secondaryController: modelData.secondary.length > 0 ? InstrumentRegistry.controller(modelData.secondary) : null
                Layout.alignment: Qt.AlignBottom
                icons: secondary ? [primary.icon, secondary.icon] : [primary.icon]
                text: secondary ? primary.shortTitle + "  ·  " + secondary.shortTitle : primary.title
                selected: strip.workspace.currentIndex === index
                running: (primaryController !== null && primaryController.running)
                         || (secondaryController !== null && secondaryController.running)
                onClicked: strip.workspace.select(index)
                onCloseRequested: strip.workspace.closeTab(index)

                DragHandler {
                    target: null
                    dragThreshold: 8
                    onActiveChanged: {
                        if (active)
                            strip.beginDrag(instrumentTab.index, instrumentTab.modelData.secondary.length > 0 ? "" : instrumentTab.modelData.primary)
                        else
                            strip.endDrag()
                    }
                    onCentroidChanged: {
                        if (active)
                            strip.dragPoint = strip.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                    }
                }
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 4
            iconName: "plus"
            size: 26
            iconSize: Theme.iconSizeS
            toolTip: qsTr("Open an instrument")
            onClicked: openMenu.popup()
            Menu {
                id: openMenu
                Repeater {
                    model: InstrumentRegistry
                    delegate: MenuItem {
                        required property string instrumentId
                        required property string title
                        required property var model   // roles; `icon` is a (FINAL) MenuItem property
                        text: title
                        icon.source: Theme.icon(model.icon)
                        onTriggered: strip.workspace.open(instrumentId)
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Split actions of the current tab.
        ActionButton {
            visible: !strip.workspace.home
            tone: "ghost"
            compact: true
            iconName: "split"
            text: strip.workspace.split ? qsTr("Unsplit") : qsTr("Split")
            toolTip: strip.workspace.split ? qsTr("Move the right panel to its own tab") : qsTr("Open another instrument beside this one")
            onClicked: {
                if (strip.workspace.split)
                    strip.workspace.unsplit(strip.workspace.currentIndex)
                else
                    besideMenu.popup()
            }
            Menu {
                id: besideMenu
                title: qsTr("Open beside")
                Repeater {
                    model: InstrumentRegistry
                    delegate: MenuItem {
                        required property string instrumentId
                        required property string title
                        required property var model   // roles; `icon` is a (FINAL) MenuItem property
                        text: title
                        icon.source: Theme.icon(model.icon)
                        enabled: strip.workspace.currentTab !== null && strip.workspace.currentTab.primary !== instrumentId
                        onTriggered: strip.workspace.openBeside(instrumentId)
                    }
                }
            }
        }
        IconButton {
            visible: strip.workspace.split
            iconName: "refresh"
            size: 26
            iconSize: Theme.iconSizeS
            toolTip: qsTr("Swap sides")
            onClicked: strip.workspace.swap(strip.workspace.currentIndex)
        }
    }

    // Insertion marker while a tab is dragged along the strip.
    Rectangle {
        visible: strip.dragIndex >= 0 && strip.pointerInStrip && dragGhost.Drag.target === null
        readonly property Item before: strip.insertIndex >= 0 && strip.insertIndex < tabRepeater.count ? tabRepeater.itemAt(strip.insertIndex) : null
        readonly property Item last: tabRepeater.count > 0 ? tabRepeater.itemAt(tabRepeater.count - 1) : null
        x: before ? before.mapToItem(strip, 0, 0).x - 2
                  : last ? last.mapToItem(strip, last.width, 0).x : 0
        y: 8
        width: 3
        height: strip.height - 12
        radius: 1.5
        color: Theme.accent
    }

    // What follows the pointer while a tab is dragged; dropping it on the workspace shows the
    // instrument in the pane under the pointer.
    Rectangle {
        id: dragGhost
        parent: strip.Window.contentItem ?? strip
        visible: strip.dragIndex >= 0
        z: 10000
        readonly property point scenePoint: strip.mapToItem(null, strip.dragPoint.x, strip.dragPoint.y)
        readonly property point parentPoint: parent ? parent.mapFromItem(null, scenePoint.x, scenePoint.y) : Qt.point(0, 0)
        x: parentPoint.x - width / 2
        y: parentPoint.y - height / 2
        width: ghostText.implicitWidth + 24
        height: 28
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.accent
        opacity: 0.92
        Drag.active: strip.dragIndex >= 0 && strip.dragInstrument.length > 0
        Drag.keys: ["mybench-tab", "instrument:" + strip.dragInstrument]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        Text {
            id: ghostText
            anchors.centerIn: parent
            text: strip.dragIndex >= 0 && strip.dragIndex < strip.workspace.tabs.length
                  ? (InstrumentRegistry.info(strip.workspace.tabs[strip.dragIndex].primary).title ?? "") : ""
            color: Theme.text
            font.pixelSize: Theme.fontSizeM
            font.weight: Font.DemiBold
        }
    }
}
