import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls
import ".."

// Preferences: the appearance option (System / Light / Dark) lives here, not in the top bar.
Item {
    id: host
    width: 800
    height: 600

    PreferencesDialog {
        id: dialog
        parent: Overlay.overlay
    }
    SignalSpy { id: spy; target: dialog; signalName: "themeModeRequested" }
    TopBar {
        id: bar
        width: 800
        workspace: WorkspaceState {}
    }

    TestCase {
        name: "Preferences"
        when: windowShown

        function test_themeModeChoices() {
            dialog.open()
            tryCompare(dialog, "opened", true)
            const control = findChild(dialog.contentItem, "themeModeControl")
            verify(control)
            compare(control.count, 3)
            compare(control.currentIndex, 0)               // System is the default
            mouseClick(control, control.width * 0.9, control.height / 2)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], "dark")
            mouseClick(control, control.width * 0.5, control.height / 2)
            compare(spy.signalArguments[1][0], "light")
            dialog.themeMode = "dark"
            compare(control.currentIndex, 2)
            dialog.close()
        }

        function test_topBarHasNoThemeToggle() {
            verify(!("themeToggleRequested" in bar))
        }
    }
}
