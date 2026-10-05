import QtQuick
import QtCore
import MyBench.Framework
import MyBench.Controls

// The application preferences (QSettings), one instance per window, shared by the shell, the
// menus and the Preferences pane. Keys under "preferences/" are also read by the framework
// (restoreSession at startup, resumeRunning when a session opens).
QtObject {
    id: prefs

    // --theme and --dark-displays win until the user picks another value.
    property bool themeFromCommandLine: AppInfo.theme.length > 0
    property bool darkDisplaysFromCommandLine: AppInfo.flag("dark-displays")
    readonly property string themeMode: themeFromCommandLine ? AppInfo.theme : appearance.themeMode
    readonly property bool alwaysDarkDisplays: darkDisplaysFromCommandLine || appearance.alwaysDarkDisplays

    // Typed groups (qmllint sees their keys).
    component AppearanceSettings: Settings {
        category: "appearance"
        property string themeMode: "dark"            // "system" | "light" | "dark"; dark by default
        property bool alwaysDarkDisplays: false
        // "native" (the platform's Qt style) | "mybench" (MyBench.Style); read by the shell at
        // start (shell.cpp storedControlsStyle), so a change applies after a restart.
        property string controls: "native"
    }
    component BehaviourSettings: Settings {
        category: "preferences"
        property bool restoreSession: true
        property bool resumeRunning: false
        property bool confirmQuit: true
    }
    component ViewSettings: Settings {
        category: "view"
        property bool statusBarShown: true
        property bool menuBarShown: true            // Windows and Linux (Ctrl+M)
        property int inspectorWidth: Theme.inspectorWidth
    }
    property AppearanceSettings appearance: AppearanceSettings {}
    property BehaviourSettings behaviour: BehaviourSettings {}
    property ViewSettings view: ViewSettings {}

    function setThemeMode(mode: string) {
        appearance.themeMode = mode
        themeFromCommandLine = false
    }
    function setAlwaysDarkDisplays(on: bool) {
        appearance.alwaysDarkDisplays = on
        darkDisplaysFromCommandLine = false
    }
    // The controls style of this run: "mybench" for MyBench.Style, else "native".
    readonly property string controlsInUse: AppInfo.nativeControls ? "native" : "mybench"
    readonly property bool controlsRestartNeeded: appearance.controls !== controlsInUse
    function setControls(style: string) {
        appearance.controls = style === "mybench" ? "mybench" : "native"
    }

    // General ▸ Reset Window Layout: the layout settings go back to their defaults here; the
    // window (Main.qml) resets its size, split and sidebar/inspector toggles on the signal.
    signal windowLayoutResetRequested()
    function resetWindowLayout() {
        view.inspectorWidth = Theme.inspectorWidth
        view.statusBarShown = true
        view.menuBarShown = true
        windowLayoutResetRequested()
    }

    function setInspectorWidth(width: real) {
        view.inspectorWidth = Math.round(Math.max(Theme.inspectorMinWidth, Math.min(Theme.inspectorMaxWidth, width)))
    }
}
