pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The content area (docs/UI.md §7, §8): Overview, or one pane, or two panes side by side
// (SplitView, ratio saved). Each pane's panel is created the first time it is shown (an
// asynchronous Loader behind a pane-shaped placeholder) and then kept: switching moves it
// between the two slots and a hidden stash, so nothing is lost and nothing stops.
// When the window is too narrow for two panes (`splitSuspended`), only the focused one shows.
Item {
    id: view

    required property WorkspaceState workspace
    required property AppActions actions
    property bool splitSuspended: false

    readonly property bool splitShown: workspace.split && !splitSuspended
    readonly property string leftPane: workspace.overview ? ""
                                     : workspace.split && !splitShown ? workspace.focused : workspace.selected
    readonly property string rightPane: splitShown ? workspace.beside : ""
    // Pane ids on screen.
    readonly property var visiblePanes: [leftPane, rightPane].filter(id => id.length > 0)
    // Every pane that can be shown: { paneId, instrumentId, panelUrl }.
    readonly property var panes: workspace.paneOrder().map(id => ({
        paneId: id,
        instrumentId: workspace.instrumentOf(id),
        panelUrl: id.indexOf("/") >= 0 ? InstrumentRegistry.viewInfo(id).panelUrl : InstrumentRegistry.info(id).panelUrl
    }))

    function slotOf(paneId: string): Item {
        return paneId === leftSlot.paneId ? leftSlot : paneId === rightSlot.paneId ? rightSlot : null
    }
    // The pane id an item belongs to ("" outside the panes).
    function paneOf(item: Item): string {
        for (let i = item; i !== null; i = i.parent) {
            if (i === leftSlot)
                return leftSlot.paneId
            if (i === rightSlot)
                return rightSlot.paneId
        }
        return ""
    }

    // Overview is created the first time it is shown, then kept (its tiles stay live).
    Loader {
        id: overview
        objectName: "overviewLoader"
        anchors.fill: parent
        readonly property bool wanted: view.workspace.overview
        visible: wanted
        active: false
        onWantedChanged: {
            if (wanted)
                active = true
        }
        // After the shell applied the startup options (--panel shows no Overview at all).
        Component.onCompleted: Qt.callLater(() => active = active || wanted)
        sourceComponent: OverviewPage {
            workspace: view.workspace
            actions: view.actions
        }
    }

    SplitView {
        id: split
        anchors.fill: parent
        visible: !view.workspace.overview
        orientation: Qt.Horizontal

        onResizingChanged: {
            if (!resizing && view.splitShown && width > 0)
                view.workspace.setRatio(leftSlot.width / width)
        }

        PaneSlot {
            id: leftSlot
            paneId: view.leftPane
            SplitView.fillWidth: !view.splitShown
            SplitView.preferredWidth: view.splitShown ? Math.round(split.width * view.workspace.ratio) : split.width
            SplitView.minimumWidth: view.splitShown ? Theme.paneMinWidth : 0
        }
        PaneSlot {
            id: rightSlot
            visible: view.splitShown
            paneId: view.rightPane
            SplitView.fillWidth: view.splitShown
            SplitView.minimumWidth: Theme.paneMinWidth
        }
    }

    Item {
        id: stash
        visible: false
    }

    // Keyboard focus moving into a pane focuses it.
    Connections {
        target: view.Window.window
        function onActiveFocusItemChanged() {
            const item = view.Window.activeFocusItem
            const id = item ? view.paneOf(item) : ""
            if (id.length > 0)
                view.workspace.focus(id)
        }
    }

    // A pane slot: where the panel of `paneId` is shown.
    component PaneSlot: Item {
        property string paneId
        readonly property alias content: slotContent
        Item {
            id: slotContent
            anchors.fill: parent
        }
    }

    // One host per pane: creates the panel on first show, then moves it between the slots.
    Instantiator {
        model: view.panes
        delegate: Item {
            id: host
            required property var modelData
            readonly property string paneId: modelData.paneId
            readonly property InstrumentController controller: InstrumentRegistry.controller(modelData.instrumentId)
            readonly property bool opened: view.workspace.opened[paneId] === true
            readonly property Item slot: view.leftPane === paneId ? leftSlot : view.rightPane === paneId ? rightSlot : null
            // The pane's InstrumentPanel (it registers itself once its tree is complete).
            readonly property InstrumentPanel panel: PanelRegistry.revision >= 0 && loader.status === Loader.Ready
                                                     ? PanelRegistry.panel(paneId) as InstrumentPanel : null

            parent: slot ? (slot as PaneSlot).content : stash
            anchors.fill: parent

            function load() {
                if (opened && loader.status === Loader.Null && host.modelData.panelUrl.toString().length > 0)
                    loader.setSource(host.modelData.panelUrl, { controller: host.controller })
            }
            onOpenedChanged: load()
            Component.onCompleted: load()

            Loader {
                id: loader
                anchors.fill: parent
                // Compiled and created in slices between frames: showing never blocks the UI.
                asynchronous: true
                // Commands waiting for a panel that cannot load are dropped.
                onStatusChanged: {
                    if (status === Loader.Error)
                        PanelRegistry.cancel(host.paneId)
                }
            }
            // The shell's state of the pane: focused (accent line under the header), closable in a
            // split (× = close that pane; the instrument keeps running), the "split hidden" note.
            Binding {
                target: host.panel
                when: host.panel !== null
                property: "focused"
                // Only a split marks the focused pane (one pane is always the focused one).
                value: view.splitShown && host.paneId === view.workspace.focused
            }
            Binding {
                target: host.panel
                when: host.panel !== null
                property: "closable"
                value: view.splitShown && host.slot !== null
            }
            Binding {
                target: host.panel
                when: host.panel !== null
                property: "note"
                value: view.workspace.split && !view.splitShown && host.slot !== null
                       ? qsTr("Split hidden — window too narrow") : ""
            }
            Connections {
                target: host.panel
                function onCloseRequested() {
                    view.workspace.closePane(host.paneId)
                }
            }
            // Pane-shaped placeholder while the panel is created.
            PanePlaceholder {
                anchors.fill: parent
                visible: loader.status === Loader.Loading
            }
            EmptyState {
                anchors.centerIn: parent
                visible: loader.status === Loader.Error
                iconName: "error"
                title: qsTr("This panel could not be loaded")
                text: host.modelData.panelUrl.toString()
            }
            // A click anywhere in the pane focuses it.
            PointHandler {
                onActiveChanged: {
                    if (active)
                        view.workspace.focus(host.paneId)
                }
            }
        }
    }

    // Header band and display surface, the shape of a pane.
    component PanePlaceholder: Rectangle {
        color: Theme.display
        Rectangle {
            width: parent.width
            height: Theme.paneHeaderHeight
            color: Theme.window
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.separator
            }
        }
    }
}
