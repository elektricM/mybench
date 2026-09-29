import QtQuick
import QtTest
import MyBench.Framework
import ".."

// NavRail: the rail is "open / focus" with a running indicator; its tooltips say what a click does.
Item {
    id: host
    width: 200
    height: 500

    WorkspaceState { id: ws }
    NavRail {
        id: rail
        anchors.fill: parent
        workspace: ws
    }

    TestCase {
        name: "NavRail"
        when: windowShown

        function init() {
            ws.fromMap({})
            InstrumentRegistry.controller("aa").stop()
        }

        function test_tooltipSaysWhatClickDoes() {
            const button = findChild(rail, "railButton-aa")
            verify(button)
            verify(button.toolTip.indexOf("Open") === 0)
            ws.open("aa")
            ws.open("bb")
            verify(button.toolTip.indexOf("Show") === 0)         // has a tab, not shown
            ws.open("aa")
            verify(button.toolTip.indexOf("(shown)") > 0)
        }

        function test_clickOpensAndFocuses() {
            const button = findChild(rail, "railButton-bb")
            mouseClick(button)
            compare(ws.focused, "bb")
            compare(ws.tabs.length, 1)
            mouseClick(button)                        // again: still one tab, no toggle-close
            compare(ws.tabs.length, 1)
        }

        function test_runningIsIndicated() {
            const aa = InstrumentRegistry.controller("aa")
            const button = findChild(rail, "railButton-aa")
            aa.run()
            tryCompare(aa, "running", true)
            tryVerify(() => button.toolTip.indexOf("running") > 0)
            verify(button.stateColor.a > 0)
            aa.stop()
            tryCompare(aa, "running", false)
        }
    }
}
