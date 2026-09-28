import QtQuick
import QtTest
import MyBench.Framework
import ".."

// The help drawer follows the focused panel while it is open (app.md 2.5).
TestCase {
    name: "HelpFollow"

    WorkspaceState { id: ws }
    HelpFollower { workspace: ws }

    function init() {
        ws.fromMap({})
    }

    function test_followsFocusWhileOpen() {
        ws.open("aa")
        ws.toggleHelp()
        compare(ws.helpOpen, true)
        compare(ws.helpTopic, "aa")
        ws.open("bb")
        compare(ws.helpTopic, "bb")
        ws.openBeside("cc")
        compare(ws.helpTopic, "cc")
        ws.focused = "bb"
        compare(ws.helpTopic, "bb")
        ws.showHome()
        compare(ws.helpTopic, "app")
    }

    function test_doesNothingWhileClosed() {
        ws.open("aa")
        compare(ws.helpOpen, false)
        const topic = ws.helpTopic
        ws.open("bb")
        compare(ws.helpTopic, topic)
    }
}
