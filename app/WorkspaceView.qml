pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The workspace content: Home, or the current tab's panel(s) side by side. Each instrument's
// panel is created the first time it is shown and then kept (moved between the split slots and
// a hidden stash), so switching tabs never loses view state.
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
                asynchronous: false
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: loader.status === Loader.Loading
                visible: running
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
