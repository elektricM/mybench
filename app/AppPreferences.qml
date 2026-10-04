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
    function setInspectorWidth(width: real) {
        view.inspectorWidth = Math.round(Math.max(Theme.inspectorMinWidth, Math.min(Theme.inspectorMaxWidth, width)))
    }
}
