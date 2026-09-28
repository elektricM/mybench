import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import ".."

// SessionGuard (app.md 11.2): Open / Open recent / Quit ask to save a modified named session
// first; the window title shows the session name and a modified marker (app.md 2.1).
// ShellKeys: Space runs/stops the focused instrument unless a control uses Space itself (6.2).
Item {
    id: host
    width: 400
    height: 300

    SessionGuard { id: guard }
    ShellKeys { id: keys }
    Button { id: button; text: "B" }
    Rectangle { id: plain; width: 10; height: 10; focus: true }

    SignalSpy { id: prompt; target: guard; signalName: "saveFirstRequested" }

    TestCase {
        name: "Session"
        when: windowShown
        property int ran: 0

        function namedModifiedSession() {
            const file = Qt.resolvedUrl("file://" + DataExport.temporaryFilePath("mybench"))
            verify(SessionManager.save(file))
            InstrumentRegistry.controller("aa").level = Math.random() + 1   // a setting changes
            tryCompare(SessionManager, "modified", true)
            return file
        }

        function test_untitledRunsAtOnce() {
            SessionManager.newSession()
            ran = 0
            prompt.clear()
            guard.run(() => ran++)
            compare(ran, 1)
            compare(prompt.count, 0)
        }

        function test_modifiedNamedAsksFirst() {
            namedModifiedSession()
            ran = 0
            prompt.clear()
            guard.run(() => ran++)
            compare(prompt.count, 1)
            compare(ran, 0)
            guard.cancel()
            compare(ran, 0)
            guard.run(() => ran++)
            guard.discard()
            compare(ran, 1)
        }

        function test_saveThenProceed() {
            namedModifiedSession()
            ran = 0
            guard.run(() => ran++)
            guard.save()
            compare(ran, 1)
            compare(SessionManager.modified, false)
        }

        function test_windowTitle() {
            SessionManager.newSession()
            compare(guard.windowTitle, AppInfo.name + " — " + SessionManager.currentName)
            namedModifiedSession()
            verify(guard.windowTitle.endsWith(" •"))
            verify(guard.windowTitle.indexOf(SessionManager.currentName) > 0)
        }

        function test_spaceNotStolenFromButtons() {
            button.forceActiveFocus()
            compare(keys.spaceAllowed(button), false)
            compare(keys.spaceAllowed(plain), true)
            compare(keys.spaceAllowed(null), true)
        }
    }
}
