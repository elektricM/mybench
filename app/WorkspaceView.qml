pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The workspace content: Home, or the current tab's panel(s) side by side. Each instrument's
// panel is created the first time it is shown and then kept (moved between the split slots and
// a hidden stash), so switching tabs never loses view state.
// The pane that gets the pointer or the keyboard focus becomes `workspace.focused`. A tab dragged
// from the tab strip onto the left/right half shows its instrument in that pane.
Item {
    id: view

    required property WorkspaceState workspace

    signal newSessionRequested()
    signal openSessionRequested()
    signal openRecentRequested(url file)
    signal deviceInfoRequested()
    readonly property string focusId: ShellActions.focusInstrument
    readonly property bool showHome: workspace.home && focusId.length === 0
    readonly property bool splitShown: workspace.split && focusId.length === 0

    HomePage {
        anchors.fill: parent
        visible: view.showHome
        workspace: view.workspace
        onNewSessionRequested: view.newSessionRequested()
        onOpenSessionRequested: view.openSessionRequested()
        onOpenRecentRequested: file => view.openRecentRequested(file)
        onDeviceInfoRequested: view.deviceInfoRequested()
    }

    SplitView {
        id: split
        anchors.fill: parent
        visible: !view.showHome
        orientation: Qt.Horizontal

        onResizingChanged: {
            if (!resizing && view.splitShown && width > 0)
                view.workspace.setRatio(view.workspace.currentIndex, leftSlot.width / width)
        }

        PaneSlot {
            id: leftSlot
            instrumentId: view.focusId.length > 0 ? view.focusId
                          : view.workspace.currentTab ? view.workspace.currentTab.primary : ""
            showHeader: view.splitShown
            SplitView.fillWidth: !view.splitShown
            SplitView.preferredWidth: view.splitShown ? split.width * (view.workspace.currentTab.ratio ?? 0.5) : split.width
            SplitView.minimumWidth: 480
        }
        PaneSlot {
            id: rightSlot
            visible: view.splitShown
            instrumentId: view.splitShown ? view.workspace.currentTab.secondary : ""
            showHeader: true
            SplitView.fillWidth: view.splitShown
            SplitView.minimumWidth: 480
        }
    }

    Item {
        id: stash
        visible: false
    }

    // Keyboard focus moving into a pane (Tab, a click on a control) focuses its instrument.
    function paneOf(item: Item): string {
        for (let i = item; i !== null; i = i.parent) {
            if (i === leftSlot)
                return leftSlot.instrumentId
            if (i === rightSlot)
                return view.splitShown ? rightSlot.instrumentId : ""
        }
        return ""
    }
    Connections {
        target: view.Window.window
        function onActiveFocusItemChanged() {
            const item = view.Window.activeFocusItem
            const id = item ? view.paneOf(item) : ""
            if (id.length > 0 && id !== view.workspace.focused && view.focusId.length === 0)
                view.workspace.focused = id
        }
    }

    // Drop target for tabs dragged from the tab strip (TabStrip.qml).
    DropArea {
        id: paneDrop
        anchors.fill: split
        enabled: !view.showHome
        keys: ["mybench-tab"]
        property string instrumentId: ""
        readonly property string side: containsDrag ? (drag.x < width / 2 ? "left" : "right") : ""
        function instrumentOf(keys: list<string>): string {
            for (const k of keys)
                if (k.startsWith("instrument:"))
                    return k.slice(11)
            return ""
        }
        onEntered: drag => {
            instrumentId = instrumentOf(drag.keys)
            const tab = view.workspace.currentTab
            // Nothing to do for the current tab's own single panel.
            if (instrumentId.length === 0 || (tab !== null && tab.secondary.length === 0 && tab.primary === instrumentId))
                drag.accepted = false
        }
        onExited: instrumentId = ""
        onDropped: drop => {
            const id = instrumentOf(drop.keys)
            if (id.length > 0)
                view.workspace.openInPane(id, drop.x < width / 2 ? "left" : "right")
            instrumentId = ""
        }

        Rectangle {
            visible: paneDrop.containsDrag
            x: paneDrop.side === "left" ? 0 : parent.width / 2
            width: parent.width / 2
            height: parent.height
            color: Theme.alpha(Theme.accent, 0.12)
            border.width: 2
            border.color: Theme.alpha(Theme.accent, 0.6)
            radius: Theme.radiusM
            Text {
                anchors.centerIn: parent
                text: qsTr("Show here")
                color: Theme.accent
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
            }
        }
    }

    component PaneSlot: Item {
        id: pane
        property string instrumentId
        property bool showHeader: false
        readonly property alias content: paneContent

        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            PaneHeader {
                Layout.fillWidth: true
                visible: pane.showHeader && pane.instrumentId.length > 0
                instrumentId: pane.instrumentId
                focused: view.workspace.focused === pane.instrumentId
                onCloseRequested: view.workspace.closeInstrument(pane.instrumentId)
                onPopOutRequested: view.workspace.unsplit(view.workspace.currentIndex)
            }
            Item {
                id: paneContent
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    // One lazily created panel per instrument (Instantiator: the hosts are moved between slots).
    Instantiator {
        model: InstrumentRegistry
        delegate: Item {
            id: host
            required property string instrumentId
            required property url panelUrl
            readonly property bool opened: view.workspace.opened[instrumentId] === true || view.focusId === instrumentId
            readonly property bool isLeft: leftSlot.instrumentId === instrumentId && !view.showHome
            readonly property bool isRight: rightSlot.instrumentId === instrumentId && view.splitShown

            parent: isLeft ? leftSlot.content : isRight ? rightSlot.content : stash
            anchors.fill: parent

            function load() {
                if (opened && loader.status === Loader.Null)
                    loader.setSource(panelUrl, { controller: InstrumentRegistry.controller(instrumentId) })
            }
            onOpenedChanged: load()
            Component.onCompleted: load()

            Loader {
                id: loader
                anchors.fill: parent
                // Compiled and created in slices between frames: opening never blocks the UI.
                asynchronous: true
            }
            // Light placeholder while the panel is created in the background.
            Rectangle {
                anchors.fill: parent
                visible: loader.status === Loader.Loading
                color: Theme.surface
                BusyIndicator {
                    anchors.centerIn: parent
                    running: parent.visible
                }
            }
            EmptyState {
                anchors.centerIn: parent
                visible: loader.status === Loader.Error
                iconName: "error"
                title: qsTr("The panel could not be loaded")
                text: host.panelUrl.toString()
            }
            // Which pane was used last (Space, help, status bar follow it).
            PointHandler {
                onActiveChanged: if (active) view.workspace.focused = host.instrumentId
            }
        }
    }
}
