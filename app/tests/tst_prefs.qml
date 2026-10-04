import QtQuick
import QtQuick.Controls
import QtCore
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// Preferences (docs/UI.md §10): a reusable pane with General / Appearance / Instruments / Device /
// Tools. The appearance mode (System / Light / Dark) and "Always use dark displays" live here, and
// the dark-displays preference drives ThemeSync and so Theme.alwaysDarkDisplays.
Item {
    id: host
    width: 900
    height: 600

    AppPreferences { id: prefs }
    ThemeSync {
        mode: prefs.themeMode
        alwaysDarkDisplays: prefs.alwaysDarkDisplays
    }
    PreferencesPane {
        id: pane
        anchors.fill: parent
        preferences: prefs
    }

    // Preferences read from an empty settings file (a first start).
    Component {
        id: freshPrefsComponent
        AppPreferences {
            // writableLocation() is already a file:// URL
            appearance.location: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                 + "/mybench-fresh-prefs-" + Date.now() + ".ini"
        }
    }

    TestCase {
        name: "Preferences"
        when: windowShown

        function init() {
            prefs.setThemeMode("system")
            prefs.setAlwaysDarkDisplays(false)
            pane.page = ""
        }

        function test_pages() {
            const list = findChild(pane, "preferencesPages")
            verify(list)
            compare(list.count, 5)
            mouseClick(findChild(pane, "preferencesPage-tools"))
            compare(pane.page, "tools")
            verify(findChild(pane, "toolsList").visible)
            pane.page = "appearance"
            compare(list.currentIndex, 1)
        }

        function test_themeModeChoices() {
            pane.page = "appearance"
            const control = findChild(pane, "themeModeControl")
            verify(control)
            compare(control.count, 3)
            compare(control.currentIndex, 0)               // System (set by init())
            mouseClick(control, control.width * 0.9, control.height / 2)
            compare(prefs.themeMode, "dark")
            compare(Theme.mode, "dark")
            mouseClick(control, control.width * 0.5, control.height / 2)
            compare(prefs.themeMode, "light")
            prefs.setThemeMode("dark")
            compare(control.currentIndex, 2)
        }

        // Dark is the default appearance of a first start (System and Light stay selectable;
        // --theme overrides it for one run).
        function test_darkByDefault() {
            const fresh = createTemporaryObject(freshPrefsComponent, host)
            verify(fresh)
            compare(fresh.appearance.themeMode, "dark")
            if (!fresh.themeFromCommandLine)
                compare(fresh.themeMode, "dark")
        }

        function test_darkDisplays() {
            pane.page = "appearance"
            const row = findChild(pane, "darkDisplaysSwitch")
            verify(row)
            compare(row.checked, false)
            compare(Theme.alwaysDarkDisplays, false)
            prefs.setThemeMode("light")
            verify(!Theme.displayDark)
            prefs.setAlwaysDarkDisplays(true)
            compare(row.checked, true)
            compare(Theme.alwaysDarkDisplays, true)
            verify(Theme.displayDark)                       // plots stay dark in light mode
            prefs.setAlwaysDarkDisplays(false)
            compare(Theme.alwaysDarkDisplays, false)
        }

        function test_generalSwitchesWriteSettings() {
            const row = findChild(pane, "restoreSessionSwitch")
            verify(row)
            const before = prefs.behaviour.restoreSession
            row.toggled(!before)
            compare(prefs.behaviour.restoreSession, !before)
            row.toggled(before)
        }
    }
}
