pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Framework
import MyBench.Controls

// The source list on the left (docs/UI.md §7): Overview, then the instruments by group (Measure,
// Generate, Digital, Apps) with their views as child rows. A click shows the entry in the focused
// pane, ⌘/Ctrl-click shows it beside. Keys: ↑/↓ move, Return shows, Space runs or stops.
// `compact`: the 56 px form (icons and dots, details in tooltips).
Rectangle {
    id: sidebar

    required property WorkspaceState workspace
    required property ShellDialogs dialogs
    property bool compact: false

    readonly property var groups: [
        { id: "measure", title: qsTr("Measure") },
        { id: "generate", title: qsTr("Generate") },
        { id: "digital", title: qsTr("Digital") },
        { id: "apps", title: qsTr("Apps") }
    ]
    // [{ kind: "overview" | "group" | "instrument" | "view", paneId, instrumentId, title, icon }]
    readonly property var entries: {
        const list = [{ kind: "overview", paneId: "", instrumentId: "", title: qsTr("Overview"), icon: "grid" }]
        const known = groups.map(g => g.id)
        for (const group of groups) {
            const ids = InstrumentRegistry.ids.filter(id => {
                const g = InstrumentRegistry.info(id).group ?? "apps"
                return g === group.id || (group.id === "apps" && known.indexOf(g) < 0)
            })
            if (ids.length === 0)
                continue
            list.push({ kind: "group", paneId: "", instrumentId: "", title: group.title, icon: "" })
            for (const id of ids) {
                const info = InstrumentRegistry.info(id)
                list.push({ kind: "instrument", paneId: id, instrumentId: id, title: info.title, icon: info.icon })
                for (const view of (info.views ?? []))
                    list.push({ kind: "view", paneId: view.id, instrumentId: id, title: view.title, icon: view.icon })
            }
        }
        return list
    }
    // The entry shown in the focused pane ("" = Overview).
    readonly property string selectedPane: workspace.overview ? "" : workspace.focused
    readonly property string besidePane: !workspace.split ? ""
        : workspace.focused === workspace.beside ? workspace.selected : workspace.beside

    function indexOfPane(paneId: string): int {
        return entries.findIndex(e => e.kind !== "group" && e.paneId === paneId)
    }
    // An entry was shown (the drawer closes).
    signal entryActivated()

    function activate(index: int, beside: bool) {
        const entry = entries[index]
        if (!entry || entry.kind === "group")
            return
        if (entry.kind === "overview")
            workspace.showOverview()
        else if (beside)
            workspace.showBeside(entry.paneId)
        else
            workspace.show(entry.paneId)
        entryActivated()
    }
    function toggleRun(index: int) {
        const entry = entries[index]
        const controller = entry && entry.kind === "instrument" ? InstrumentRegistry.controller(entry.instrumentId) : null
        if (controller && (controller.running || controller.canRun))
            controller.toggle()
    }
    // Next selectable entry from `index` in direction `step` (groups are skipped).
    function step(index: int, direction: int): int {
        for (let i = index + direction; i >= 0 && i < entries.length; i += direction)
            if (entries[i].kind !== "group")
                return i
        return index
    }
    function row(paneId: string): SidebarRow {
        const index = indexOfPane(paneId)
        const item = index >= 0 ? list.itemAtIndex(index) : null
        return item ? (item as Loader).item as SidebarRow : null
    }

    implicitWidth: compact ? Theme.sidebarCompactWidth : Theme.sidebarWidth
    color: Theme.sidebar

    // The keyboard cursor follows the selection.
    onSelectedPaneChanged: list.currentIndex = indexOfPane(selectedPane)

    // A pane header dropped here closes that pane (the instrument keeps running).
    DropArea {
        id: closeDrop
        objectName: "sidebarDropArea"
        anchors.fill: parent
        keys: ["mybench/instrument"]
        onEntered: drag => drag.accepted = (drag.source as DragChip)?.origin === "pane"
        onDropped: drop => {
            const chip = drop.source as DragChip
            if (chip && chip.origin === "pane") {
                drop.accept()
                sidebar.workspace.dropOnSidebar(chip.paneId)
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.spacingXs
        z: 2
        visible: closeDrop.containsDrag
        radius: Theme.radiusControl
        color: Qt.alpha(Theme.accent, 0.12)
        border.width: Theme.borderWidth
        border.color: Theme.accent
        Icon {
            anchors.centerIn: parent
            name: "close"
            size: Theme.iconSizeL
            color: Theme.accent
        }
    }

    ListView {
        id: list
        objectName: "sidebarList"
        anchors.fill: parent
        anchors.topMargin: Theme.spacingS
        anchors.bottomMargin: Theme.spacingS
        model: sidebar.entries
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        keyNavigationEnabled: false
        highlightFollowsCurrentItem: false
        currentIndex: sidebar.indexOfPane(sidebar.selectedPane)
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Instruments")
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        // Space belongs to the sidebar while it has the focus (not to the window's Run/Stop).
        Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space && event.modifiers === Qt.NoModifier
        Keys.onUpPressed: currentIndex = sidebar.step(currentIndex, -1)
        Keys.onDownPressed: currentIndex = sidebar.step(currentIndex, 1)
        Keys.onReturnPressed: sidebar.activate(currentIndex, false)
        Keys.onEnterPressed: sidebar.activate(currentIndex, false)
        Keys.onSpacePressed: sidebar.toggleRun(currentIndex)

        delegate: Loader {
            id: entry
            required property var modelData
            required property int index
            width: ListView.view.width
            sourceComponent: modelData.kind === "group" ? groupLabel : instrumentRow

            Component {
                id: groupLabel
                Item {
                    implicitHeight: sidebar.compact ? Theme.spacingM + 1 : Theme.spacingM + caption.implicitHeight + Theme.spacingXs
                    // Compact: a hairline between groups instead of the label.
                    Rectangle {
                        visible: sidebar.compact
                        anchors.centerIn: parent
                        width: Theme.sidebarCompactWidth - 2 * Theme.spacingM
                        height: Theme.borderWidth
                        color: Theme.separator
                    }
                    Text {
                        id: caption
                        visible: !sidebar.compact
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.spacingL
                        anchors.bottomMargin: Theme.spacingXs
                        text: entry.modelData.title
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                        font.weight: Font.Medium
                    }
                }
            }
            Component {
                id: instrumentRow
                SidebarRow {
                    objectName: "sidebarRow-" + (entry.modelData.paneId.length > 0 ? entry.modelData.paneId : "overview")
                    paneId: entry.modelData.paneId
                    title: entry.modelData.title
                    iconName: entry.modelData.icon ?? ""
                    child: entry.modelData.kind === "view"
                    controller: entry.modelData.instrumentId.length > 0 ? InstrumentRegistry.controller(entry.modelData.instrumentId) : null
                    compact: sidebar.compact
                    selected: entry.modelData.paneId === sidebar.selectedPane
                    shownBeside: entry.modelData.paneId.length > 0 && entry.modelData.paneId === sidebar.besidePane
                    current: list.currentIndex === entry.index
                    listFocused: list.activeFocus
                    contextMenu: entry.modelData.kind === "overview" ? null : rowMenu
                    onClicked: modifiers => {
                        list.currentIndex = entry.index
                        sidebar.activate(entry.index, (modifiers & Qt.ControlModifier) !== 0)
                    }

                    RowMenu {
                        id: rowMenu
                        paneId: entry.modelData.paneId
                        instrument: entry.modelData.kind === "instrument"
                    }
                }
            }
        }
    }

    // Context menu of a row: Show, Show Beside, Run/Stop, Single, Reset Settings…, Help. A view
    // row has Show, Show Beside and Help (entries are removed: native menus ignore `visible`).
    component RowMenu: Menu {
        id: menu
        property string paneId
        property bool instrument: true
        readonly property InstrumentController controller: InstrumentRegistry.controller(paneId.split("/")[0])
        readonly property bool running: controller !== null && controller.running
        MenuItem {
            text: qsTr("Show")
            onTriggered: sidebar.workspace.show(menu.paneId)
        }
        MenuItem {
            text: qsTr("Show Beside")
            enabled: !sidebar.workspace.isShown(menu.paneId)
            onTriggered: sidebar.workspace.showBeside(menu.paneId)
        }
        MenuSeparator { id: runSeparator }
        MenuItem {
            id: runItem
            text: menu.running ? qsTr("Stop") : qsTr("Run")
            enabled: menu.controller !== null && (menu.running || menu.controller.canRun)
            onTriggered: menu.controller.toggle()
        }
        MenuItem {
            id: singleItem
            text: qsTr("Single")
            enabled: menu.controller !== null && menu.controller.singleEnabled && (menu.running || menu.controller.canRun)
            onTriggered: menu.controller.runOnce()
        }
        MenuSeparator {}
        MenuItem {
            id: resetItem
            text: qsTr("Reset Settings…")
            onTriggered: sidebar.dialogs.resetSettings(menu.paneId)
        }
        MenuItem {
            text: qsTr("Help")
            onTriggered: sidebar.workspace.showHelp(sidebar.workspace.helpTopicOf(menu.paneId))
        }
        Component.onCompleted: {
            if (!instrument) {
                for (const item of [runSeparator, runItem, singleItem, resetItem])
                    removeItem(item)
            } else if (!controller || !controller.supportsSingle) {
                removeItem(singleItem)
            }
        }
    }

    // Hairline towards the content.
    Rectangle {
        anchors.right: parent.right
        width: Theme.borderWidth
        height: parent.height
        color: Theme.separator
    }
}
