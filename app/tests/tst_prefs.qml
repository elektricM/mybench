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

    SignalSpy { id: resetSpy; target: prefs; signalName: "windowLayoutResetRequested" }
    // Reads a preferences file back the way any QSettings user does (category = group).
    Component {
        id: appearanceReaderComponent
        Settings { category: "appearance" }
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

        // Appearance ▸ Controls: Native | MYBENCH under the key appearance/controls (shell.cpp reads
        // it at start: tst_framework storedControlsStyleReadsThePreference); the note shows while
        // the choice differs from the style of this run.
        function test_controlsStyleChoice() {
            pane.page = "appearance"
            const control = findChild(pane, "controlsStyleControl")
            const note = findChild(pane, "controlsRestartNote")
            verify(control)
            verify(note)
            const inUse = prefs.controlsInUse
            prefs.setControls(inUse)
            verify(!note.visible)
            mouseClick(control, control.width * 0.75, control.height / 2)
            compare(prefs.appearance.controls, "mybench")
            compare(control.currentIndex, 1)
            compare(note.visible, inUse !== "mybench")
            mouseClick(control, control.width * 0.25, control.height / 2)
            compare(prefs.appearance.controls, "native")
            compare(control.currentIndex, 0)
            compare(note.visible, inUse !== "native")
            prefs.setControls(inUse)
        }

        function test_controlsKeyAndDefault() {
            const fresh = createTemporaryObject(freshPrefsComponent, host)
            verify(fresh)
            compare(fresh.appearance.controls, "native")
            fresh.setControls("mybench")
            const reader = createTemporaryObject(appearanceReaderComponent, host,
                                                 { location: fresh.appearance.location })
            // Settings stores changed properties on a short timer.
            tryVerify(() => {
                fresh.appearance.sync()
                reader.sync()
                return reader.value("controls") === "mybench"
            }, 3000)
            fresh.setControls("anything")
            compare(fresh.appearance.controls, "native")
        }

        // General ▸ Reset Window Layout: layout preferences back to defaults, the window is told.
        function test_resetWindowLayout() {
            const button = findChild(pane, "resetWindowLayoutButton")
            verify(button)
            prefs.setInspectorWidth(Theme.inspectorWidth + 60)
            prefs.view.statusBarShown = false
            resetSpy.clear()
            mouseClick(button)
            compare(resetSpy.count, 1)
            compare(prefs.view.inspectorWidth, Theme.inspectorWidth)
            compare(prefs.view.statusBarShown, true)
            compare(prefs.view.menuBarShown, true)
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
