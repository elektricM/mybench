import QtQuick
import QtTest
import MyBench.Framework
import MyBench.App

// WorkspaceState v2 (docs/UI.md §7, §8): Overview or one pane plus an optional pane beside it.
// Showing, splitting and closing never stop an instrument; v1 layouts (tabs, dashboard, help
// drawer) of older sessions and of the autosave migrate.
TestCase {
    name: "Workspace"

    WorkspaceState { id: ws }

    function init() {
        ws.fromMap({})
        ws.layoutClass = "wide"
        for (const id of ["aa", "bb", "cc"])
            InstrumentRegistry.controller(id).stop()
    }

    function closedCounts() {
        return ["aa", "bb", "cc"].map(id => InstrumentRegistry.controller(id).closedCount)
    }

    // Preferences ▸ General ▸ Reset Window Layout: equal split, default sidebar/inspector
    // visibility, inspector page; the shown instruments stay.
    function test_resetLayoutKeepsPanes() {
        ws.show("aa")
        ws.showBeside("bb")
        ws.setRatio(0.3)
        ws.setSidebarShown(false)
        ws.setInspectorShown(false)
        ws.showHelp("app")
        let changes = 0
        const count = () => ++changes
        ws.changed.connect(count)
        ws.resetLayout()
        ws.changed.disconnect(count)
        verify(changes > 0)
        compare(ws.ratio, 0.5)
        compare(ws.sidebarShown, true)
        compare(ws.inspectorShown, true)
        compare(ws.helpShown, false)
        compare(ws.selected, "aa")
        compare(ws.beside, "bb")
    }

    function test_emptyMapIsOverview() {
        compare(ws.selected, "")
        compare(ws.overview, true)
        compare(ws.split, false)
        compare(ws.focused, "")
    }

    function test_showReplacesTheFocusedPane() {
        ws.show("aa")
        compare(ws.selected, "aa")
        compare(ws.focused, "aa")
        ws.show("bb")
        compare(ws.selected, "bb")
        compare(ws.opened["aa"], true)
        compare(ws.opened["bb"], true)
        ws.show("unknown")
        compare(ws.selected, "bb")
    }

    function test_switchingNeverStops() {
        const aa = InstrumentRegistry.controller("aa")
        const before = closedCounts()
        ws.show("aa")
        aa.run()
        tryCompare(aa, "running", true)
        ws.show("bb")
        ws.showOverview()
        ws.show("cc")
        ws.showBeside("aa")
        ws.closeSplit()
        ws.cycle(1)
        compare(aa.running, true)
        compare(closedCounts(), before)
        aa.stop()
        tryCompare(aa, "running", false)
    }

    function test_besideAndCloseSplitKeepsRunning() {
        const bb = InstrumentRegistry.controller("bb")
        ws.show("aa")
        ws.showBeside("bb")
        compare(ws.split, true)
        compare(ws.selected, "aa")
        compare(ws.beside, "bb")
        compare(ws.focused, "bb")
        bb.run()
        tryCompare(bb, "running", true)
        const before = closedCounts()
        // The focused pane stays when the split closes.
        ws.focus("aa")
        ws.closeSplit()
        compare(ws.split, false)
        compare(ws.selected, "aa")
        compare(bb.running, true)
        compare(closedCounts(), before)
        bb.stop()
        tryCompare(bb, "running", false)
    }

    function test_closeSplitKeepsTheFocusedPane() {
        ws.show("aa")
        ws.showBeside("bb")
        ws.closeSplit()
        compare(ws.selected, "bb")
        compare(ws.beside, "")
        compare(ws.focused, "bb")
    }

    function test_showInASplitUsesTheFocusedPane() {
        ws.show("aa")
        ws.showBeside("bb")
        ws.show("cc")                      // focus is on the right pane
        compare(ws.selected, "aa")
        compare(ws.beside, "cc")
        ws.focus("aa")
        ws.show("bb")
        compare(ws.selected, "bb")
        compare(ws.beside, "cc")
        ws.show("cc")                      // already shown: focus only
        compare(ws.focused, "cc")
        compare(ws.selected, "bb")
    }

    function test_showBesideInASplitReplacesTheOtherPane() {
        ws.show("aa")
        ws.showBeside("bb")
        ws.focus("aa")
        ws.showBeside("cc")
        compare(ws.selected, "aa")
        compare(ws.beside, "cc")
        compare(ws.focused, "cc")
    }

    function test_overviewClearsTheSplitWithoutStopping() {
        ws.show("aa")
        ws.showBeside("bb")
        ws.showOverview()
        compare(ws.overview, true)
        compare(ws.split, false)
        ws.showBeside("cc")               // from Overview: a plain show
        compare(ws.selected, "cc")
        compare(ws.split, false)
    }

    function test_views() {
        verify(ws.isPane("cc/editor"))
        verify(!ws.isPane("cc/nothing"))
        compare(ws.paneOrder(), ["aa", "bb", "cc", "cc/editor"])
        ws.show("cc")
        ws.showBeside("cc/editor")
        compare(ws.beside, "cc/editor")
        compare(ws.instrumentOf(ws.beside), "cc")
        compare(ws.helpTopicOf("cc/editor"), "cc")
    }

    function test_toggleSplit() {
        ws.toggleSplit()                   // nothing on Overview
        compare(ws.split, false)
        ws.show("aa")
        ws.toggleSplit()
        compare(ws.beside, "bb")
        ws.toggleSplit()
        compare(ws.split, false)
    }

    function test_cycle() {
        ws.cycle(1)
        compare(ws.selected, "aa")
        ws.cycle(1)
        compare(ws.selected, "bb")
        ws.cycle(-1)
        ws.cycle(-1)
        compare(ws.overview, true)
        ws.cycle(-1)
        compare(ws.selected, "cc/editor")
    }

    function test_togglesPerLayoutClass() {
        compare(ws.sidebarShown, true)
        compare(ws.inspectorShown, true)
        ws.setInspectorShown(false)
        compare(ws.inspectorShown, false)
        ws.layoutClass = "narrow"
        compare(ws.inspectorShown, false)   // overlay by default, closed
        ws.setInspectorShown(true)
        ws.layoutClass = "wide"
        compare(ws.inspectorShown, false)   // wide keeps its own choice
        ws.layoutClass = "compact"
        compare(ws.sidebarShown, false)     // a drawer there
        ws.layoutClass = "medium"
        compare(ws.sidebarShown, true)
        compare(ws.inspectorShown, true)
    }

    function test_helpFollowsFocus() {
        ws.show("aa")
        ws.toggleHelp()
        compare(ws.dockPage, "help")
        compare(ws.helpTopic, "aa")
        ws.show("bb")
        compare(ws.helpTopic, "bb")
        ws.showBeside("cc/editor")
        compare(ws.helpTopic, "cc")
        ws.showOverview()
        compare(ws.helpTopic, "app")
        ws.toggleHelp()
        ws.toggleHelp()
        compare(ws.dockPage, "help")
        ws.toggleHelp()
        compare(ws.dockPage, "inspector")
    }

    function test_roundTrip() {
        ws.show("aa")
        ws.showBeside("cc/editor")
        ws.setRatio(0.35)
        ws.setInspectorShown(false)
        ws.layoutClass = "narrow"
        ws.setSidebarShown(false)
        ws.showHelp("bb")
        const map = ws.toMap()
        compare(map.version, 2)
        ws.fromMap({})
        compare(ws.overview, true)
        ws.fromMap(JSON.parse(JSON.stringify(map)))
        compare(ws.selected, "aa")
        compare(ws.beside, "cc/editor")
        compare(ws.focused, "cc/editor")
        compare(ws.ratio, 0.35)
        compare(ws.dockPage, "help")
        compare(ws.helpTopic, "bb")
        compare(ws.inspectorShownIn("wide"), false)
        compare(ws.sidebarHiddenIn("narrow"), true)
        compare(ws.inspectorShownIn("narrow"), true)
    }

    // --- v1 sessions and autosave -------------------------------------------------------------------
    function test_migratesV1Split() {
        ws.fromMap({ version: 1, tabs: [{ primary: "cc", secondary: "", ratio: 0.5 },
                                        { primary: "aa", secondary: "bb", ratio: 0.3 }],
                     current: 1, focused: "aa", dashboardPinned: true, dashboardTiles: ["aa"],
                     helpOpen: true, helpTopic: "bb" })
        compare(ws.selected, "aa")
        compare(ws.beside, "bb")
        compare(ws.ratio, 0.3)
        compare(ws.focused, "aa")
        compare(ws.dockPage, "help")
        compare(ws.helpTopic, "bb")
        const map = ws.toMap()
        compare(map.version, 2)
        verify(map.tabs === undefined)
        verify(map.dashboardPinned === undefined)
        verify(map.dashboardTiles === undefined)
        verify(map.helpOpen === undefined)
    }

    function test_migratesV1Home() {
        ws.fromMap({ version: 1, tabs: [{ primary: "aa", secondary: "", ratio: 0.5 }], current: -1,
                     focused: "aa", helpOpen: false })
        compare(ws.overview, true)
        compare(ws.focused, "")
        compare(ws.dockPage, "inspector")
    }

    function test_migratesV1UnknownInstruments() {
        // A session written by a build with other instruments: unknown ids are dropped.
        ws.fromMap({ tabs: [{ primary: "zz", secondary: "bb", ratio: 0.6 }], current: 0, focused: "zz" })
        compare(ws.selected, "bb")
        compare(ws.beside, "")
        compare(ws.focused, "bb")
        ws.fromMap({ version: 2, selected: "zz", beside: "yy", focused: "zz" })
        compare(ws.overview, true)
    }
}
