import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// The small windows (docs/UI.md §10, §13.9): Preferences, About, Device Info and Simulator
// Settings each show a reusable pane; offscreen the pane is shown inside the main window.
Item {
    id: host
    width: 1000
    height: 700

    AppPreferences { id: prefs }
    WorkspaceState { id: ws }
    ShellDialogs {
        id: dialogs
        preferences: prefs
        workspace: ws
    }

    TestCase {
        name: "Windows"
        when: windowShown

        function check(objectName: string, paneType: var, open: var) {
            const window = findChild(dialogs, objectName)
            verify(window, objectName)
            verify(window.inline)                      // offscreen rule
            compare(window.shown, false)
            open()
            tryCompare(window, "shown", true)
            tryVerify(() => window.paneItem !== null)
            verify(window.paneItem instanceof paneType)
            verify(window.paneItem.width > 0 && window.paneItem.height > 0)
            window.close()
            tryCompare(window, "shown", false)
        }

        function test_preferences() {
            check("preferencesWindow", PreferencesPane, () => dialogs.showPreferences("tools"))
            dialogs.showPreferences("tools")
            const window = findChild(dialogs, "preferencesWindow")
            tryVerify(() => window.paneItem !== null)
            compare(window.paneItem.page, "tools")
            window.close()
        }
        function test_about() {
            check("aboutWindow", AboutPane, () => dialogs.showAbout())
        }
        function test_deviceInfo() {
            check("deviceInfoWindow", DeviceInfoPane, () => dialogs.showDeviceInfo())
        }
        function test_simulatorSettings() {
            check("simulatorWindow", SimulatorPane, () => dialogs.showSimulatorSettings())
        }
    }
}
