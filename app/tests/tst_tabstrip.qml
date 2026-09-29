import QtQuick
import QtTest
import MyBench.Framework
import ".."

// TabStrip: middle click closes an instrument tab (not Home); closing a tab of a running
// instrument stops it and returns its outputs to the safe state.
Item {
    id: host
    width: 900
    height: 60

    WorkspaceState { id: ws }
    TabStrip {
        id: strip
        anchors.fill: parent
        workspace: ws
    }

    TestCase {
        name: "TabStrip"
        when: windowShown

        function init() {
            ws.fromMap({})
        }

        function test_middleClickClosesTab() {
            ws.open("aa")
            ws.open("bb")
            compare(ws.tabs.length, 2)
            const tab = findChild(strip, "instrumentTab-aa")
            verify(tab)
            tryVerify(() => tab.x > 0)                // laid out
            mouseClick(tab, tab.width / 2, tab.height / 2, Qt.MiddleButton)
            compare(ws.tabs.length, 1)
            compare(ws.tabs[0].primary, "bb")
        }

        function test_leftClickDoesNotClose() {
            ws.open("aa")
            const tab = findChild(strip, "instrumentTab-aa")
            tryVerify(() => tab.x > 0)
            mouseClick(tab, tab.width / 2, tab.height / 2, Qt.LeftButton)
            compare(ws.tabs.length, 1)
        }

        function test_closingTabStopsRunningInstrument() {
            const aa = InstrumentRegistry.controller("aa")
            const safe = aa.safeStateCount
            ws.open("aa")
            aa.run()
            tryCompare(aa, "running", true)
            const tab = findChild(strip, "instrumentTab-aa")
            tryVerify(() => tab.x > 0)
            mouseClick(tab, tab.width / 2, tab.height / 2, Qt.MiddleButton)
            tryCompare(aa, "running", false)
            tryVerify(() => aa.safeStateCount > safe)
        }
    }
}
