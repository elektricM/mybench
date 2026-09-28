import QtQuick
import QtTest
import MyBench.Framework
import ".."

// WorkspaceState: closing the last view of an instrument notifies its controller.
TestCase {
    name: "Workspace"

    WorkspaceState { id: ws }

    function init() {
        ws.fromMap({})
    }

    function test_closeTabNotifiesController() {
        const aa = InstrumentRegistry.controller("aa")
        const before = aa.closedCount
        ws.open("aa")
        ws.open("bb")
        compare(ws.tabs.length, 2)
        ws.closeTab(ws.indexOf("aa"))
        compare(aa.closedCount, before + 1)
        compare(aa.safeStateCount > 0, true)
    }

    function test_closeSplitPaneNotifiesOnlyThatInstrument() {
        const aa = InstrumentRegistry.controller("aa")
        const bb = InstrumentRegistry.controller("bb")
        ws.open("aa")
        ws.openBeside("bb")
        const a0 = aa.closedCount
        const b0 = bb.closedCount
        ws.closeInstrument("bb")
        compare(bb.closedCount, b0 + 1)
        compare(aa.closedCount, a0)
        // Moving a panel into a split is not a close.
        ws.openBeside("cc")
        ws.unsplit(ws.currentIndex)
        compare(InstrumentRegistry.controller("cc").closedCount, 0)
    }

    function test_openBesideASplitKeepsTheDisplacedPanel() {
        const b0 = InstrumentRegistry.controller("bb").closedCount
        ws.open("aa")
        ws.openBeside("bb")
        ws.openBeside("cc")
        verify(ws.indexOf("bb") >= 0)          // bb still has a tab (it may be running)
        compare(ws.currentTab.secondary, "cc")
        compare(InstrumentRegistry.controller("bb").closedCount, b0)
    }

    function test_closeTabOfASplitClosesBoth() {
        const aa = InstrumentRegistry.controller("aa")
        const cc = InstrumentRegistry.controller("cc")
        ws.open("aa")
        ws.openBeside("cc")
        const a0 = aa.closedCount
        const c0 = cc.closedCount
        ws.closeTab(ws.currentIndex)
        compare(aa.closedCount, a0 + 1)
        compare(cc.closedCount, c0 + 1)
    }
}
