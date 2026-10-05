import QtQuick
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// Adaptive layout (docs/UI.md §8): window widths → sidebar and inspector forms; a split compacts
// the sidebar when that reaches Theme.splitMinWidth of pane area, else it is suspended; the
// --sidebar/--inspector overrides; the user's toggles per layout class.
TestCase {
    name: "Layout"

    WorkspaceState {
        id: ws
        layoutClass: layout.layoutClass
    }
    ShellLayout {
        id: layout
        workspace: ws
    }

    function init() {
        ws.fromMap({})
        ws.show("aa")
        layout.sidebarOverride = ""
        layout.inspectorOverride = ""
        layout.preferredInspectorWidth = Theme.inspectorWidth
        layout.panelInspectorEmpty = false
    }

    function test_classes_data() {
        return [
            { tag: "1440", width: 1440, cls: "wide", sidebar: Theme.sidebarWidth, drawer: false, docked: true, dock: Theme.inspectorWidth },
            { tag: "1280", width: 1280, cls: "wide", sidebar: Theme.sidebarWidth, drawer: false, docked: true, dock: Theme.inspectorWidth },
            { tag: "1100", width: 1100, cls: "medium", sidebar: Theme.sidebarCompactWidth, drawer: false, docked: true, dock: 280 },
            { tag: "900", width: 900, cls: "narrow", sidebar: Theme.sidebarCompactWidth, drawer: false, docked: false, dock: 0 },
            { tag: "640", width: 640, cls: "compact", sidebar: 0, drawer: true, docked: false, dock: 0 }
        ]
    }
    function test_classes(data) {
        layout.width = data.width
        compare(layout.layoutClass, data.cls)
        compare(layout.sidebarWidth, data.sidebar)
        compare(layout.sidebarDrawer, data.drawer)
        compare(layout.inspectorDocked, data.docked)
        compare(layout.dockWidth, data.dock)
        compare(layout.inspectorSheet, false)   // overlay classes: closed until toggled
    }

    function test_overlayInspectorStaysOpenUntilToggled() {
        layout.width = 900
        ws.setInspectorShown(true)
        verify(layout.inspectorSheet)
        verify(!layout.inspectorDocked)
        layout.width = 950
        verify(layout.inspectorSheet)
        ws.setInspectorShown(false)
        verify(!layout.inspectorSheet)
    }

    function test_splitAutoCompactsTheSidebar() {
        layout.width = 1280
        ws.showBeside("bb")
        verify(ws.split)
        // 1280 - 220 - 300 < 900, 1280 - 56 - 300 >= 900
        verify(layout.sidebarAutoCompact)
        compare(layout.sidebarWidth, Theme.sidebarCompactWidth)
        verify(!layout.splitSuspended)
        verify(layout.paneArea >= Theme.splitMinWidth)
        // Closing the split expands it again.
        ws.closeSplit()
        verify(!layout.sidebarAutoCompact)
        compare(layout.sidebarWidth, Theme.sidebarWidth)
    }

    function test_splitAtDefaultSizeKeepsTheSidebar() {
        layout.width = 1440
        ws.showBeside("bb")
        verify(!layout.sidebarAutoCompact)          // 1440 - 220 - 300 = 920
        verify(!layout.splitSuspended)
    }

    function test_splitSuspended() {
        layout.width = 1100                           // medium: 1100 - 56 - 280 < 900
        ws.showBeside("bb")
        verify(layout.splitSuspended)
        verify(ws.split)                              // the split is kept
        ws.setInspectorShown(false)                   // more room: 1100 - 56 >= 900
        verify(!layout.splitSuspended)
        layout.width = 900                            // narrow: always suspended
        verify(layout.splitSuspended)
        layout.width = 1440
        verify(!layout.splitSuspended)
    }

    function test_overrides() {
        layout.width = 1440
        layout.sidebarOverride = "compact"
        compare(layout.sidebarWidth, Theme.sidebarCompactWidth)
        layout.sidebarOverride = "hidden"
        compare(layout.sidebarWidth, 0)
        layout.width = 700
        layout.sidebarOverride = "full"
        compare(layout.sidebarWidth, Theme.sidebarWidth)
        verify(!layout.sidebarDrawer)
        layout.inspectorOverride = "hidden"
        layout.width = 1440
        verify(!layout.inspectorDocked)
    }

    function test_togglesPerClass() {
        layout.width = 1440
        ws.setSidebarShown(false)
        compare(layout.sidebarWidth, 0)
        layout.width = 1100
        compare(layout.sidebarWidth, Theme.sidebarCompactWidth)   // medium keeps its own choice
        layout.width = 1440
        compare(layout.sidebarWidth, 0)
        ws.setSidebarShown(true)
    }

    function test_overviewHasNoInspectorUnlessHelp() {
        layout.width = 1440
        ws.showOverview()
        verify(!layout.inspectorDocked)
        ws.showHelp("app")
        verify(layout.inspectorDocked)
    }

    Component {
        id: layoutComponent
        ShellLayout { workspace: ws; width: 1440 }
    }
    Component {
        id: panelComponent
        InstrumentPanel {
            property bool withInspector: false
            hasInspector: withInspector
        }
    }
    // While the focused pane's panel loads (no panel registered yet) the dock column stays away:
    // it appears once a panel with an inspector is there and never flashes for one without.
    function test_noDockWhilePanelLoads() {
        const fresh = createTemporaryObject(layoutComponent, this)
        verify(fresh)
        verify(!ws.overview)
        compare(fresh.focusedPanel, null)
        verify(fresh.panelInspectorEmpty)
        verify(!fresh.inspectorDocked)
        compare(fresh.dockWidth, 0)
        const without = createTemporaryObject(panelComponent, this)
        fresh.focusedPanel = without
        verify(!fresh.inspectorDocked)
        const withInspector = createTemporaryObject(panelComponent, this, { withInspector: true })
        fresh.focusedPanel = withInspector
        verify(fresh.inspectorDocked)
        compare(fresh.dockWidth, Theme.inspectorWidth)
        fresh.focusedPanel = null
        verify(!fresh.inspectorDocked)
    }

    // A panel without inspector (controls beside the display): no dock column, no dock width, the
    // pane gets the room; Help still opens the column on request, Settings hides it again.
    function test_panelWithoutInspectorHidesTheDock() {
        layout.width = 1440
        verify(layout.inspectorDocked)
        layout.panelInspectorEmpty = true
        verify(!layout.inspectorDocked)
        compare(layout.dockWidth, 0)
        compare(layout.paneArea, 1440 - layout.sidebarWidth)
        ws.showHelp("app")
        verify(layout.inspectorDocked)
        ws.showSettings()
        verify(!layout.inspectorDocked)
        layout.width = 900                       // overlay classes: no sheet either
        layout.inspectorOverride = "shown"
        verify(!layout.inspectorSheet)
    }

    function test_mediumInspectorWidth() {
        layout.preferredInspectorWidth = Theme.inspectorMaxWidth
        layout.width = 1440
        compare(layout.inspectorWidth, Theme.inspectorMaxWidth)
        layout.width = 1100
        compare(layout.inspectorWidth, 280)
    }
}
