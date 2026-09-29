pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import MyBench.Framework
import MyBench.Controls

// Preferences (app.md 12). Keys under "preferences/" are read by the framework too.
Dialog {
    id: dialog

    // "system" (default), "light" or "dark"; the shell stores it and drives Theme.mode.
    property string themeMode: "system"
    signal themeModeRequested(string mode)

    anchors.centerIn: Overlay.overlay
    width: 480
    modal: true
    title: qsTr("Preferences")
    standardButtons: Dialog.Close

    Settings {
        id: prefs
        category: "preferences"
        property bool restoreSession: true
        property bool resumeRunning: false
        property bool confirmQuit: true
    }

    component PreferenceRow: RowLayout {
        id: row
        property string title
        property string detail
        property alias checked: toggle.checked
        signal toggled(bool checked)
        Layout.fillWidth: true
        spacing: Theme.spacingM
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: row.title; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.detail
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
                wrapMode: Text.WordWrap
            }
        }
        ToggleSwitch {
            id: toggle
            onToggled: row.toggled(checked)
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingL
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Appearance"); color: Theme.text; font.pixelSize: Theme.fontSizeBody }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("System follows the light or dark setting and the accent color of your desktop.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeM
                    wrapMode: Text.WordWrap
                }
            }
            SegmentedControl {
                objectName: "themeModeControl"
                readonly property var modes: ["system", "light", "dark"]
                model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                currentIndex: Math.max(0, modes.indexOf(dialog.themeMode))
                onActivated: index => dialog.themeModeRequested(modes[index])
            }
        }
        PreferenceRow {
            title: qsTr("Restore the last session on start")
            detail: qsTr("Instrument settings and the workspace layout come back at the next start.")
            checked: prefs.restoreSession
            onToggled: checked => prefs.restoreSession = checked
        }
        PreferenceRow {
            title: qsTr("Resume running instruments when opening a session")
            detail: qsTr("Off by default: outputs never start unexpectedly.")
            checked: prefs.resumeRunning
            onToggled: checked => prefs.resumeRunning = checked
        }
        PreferenceRow {
            title: qsTr("Confirm quit while instruments run")
            checked: prefs.confirmQuit
            onToggled: checked => prefs.confirmQuit = checked
        }
        ActionButton {
            tone: "ghost"
            iconName: "info"
            text: qsTr("About %1…").arg(AppInfo.name)
            onClicked: Launcher.showAbout()
        }
    }
}
