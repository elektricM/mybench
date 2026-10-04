pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MyBench.Framework
import MyBench.Controls

// The shell's dialogs and small windows (docs/UI.md §10): native file dialogs, confirmations whose
// accept button carries the verb (ConfirmDialog: native only for a standard label such as Save),
// and the Preferences, About, Device Info and Simulator Settings windows (shown inside the main
// window offscreen, §13.9). Every entry point is a function:
//     dialogs.openSession()   dialogs.resetSettings("scope")   dialogs.showPreferences("tools")
Item {
    id: dialogs

    required property AppPreferences preferences
    required property WorkspaceState workspace
    // Set once the user confirmed quitting (the window then closes without asking again).
    property bool quitConfirmed: false
    readonly property alias sessionGuard: guard
    readonly property string windowTitle: guard.windowTitle

    signal quitAccepted()

    // --- Sessions ------------------------------------------------------------------------------------
    SessionGuard {
        id: guard
        onSaveFirstRequested: saveFirst.open()
    }
    function newSession() {
        if (SessionManager.modified)
            newSessionConfirm.open()
        else
            SessionManager.newSession()
    }
    function openSession() {
        guard.run(() => openDialog.open())
    }
    function openRecent(file: url) {
        guard.run(() => SessionManager.load(file))
    }
    function saveSession() {
        if (!SessionManager.saveCurrent())
            saveSessionAs()
    }
    function saveSessionAs() {
        saveDialog.selectedFile = SessionManager.suggestedFileUrl()
        saveDialog.open()
    }
    function revertToSaved() {
        if (guard.named)
            revertConfirm.open()
    }
    function resetAllInstruments() {
        resetAllConfirm.open()
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open Session")
        fileMode: FileDialog.OpenFile
        nameFilters: SessionManager.nameFilters
        onAccepted: SessionManager.load(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save Session")
        fileMode: FileDialog.SaveFile
        defaultSuffix: SessionManager.fileExtension
        nameFilters: SessionManager.nameFilters
        onAccepted: SessionManager.save(selectedFile)
    }
    MessageDialog {
        id: saveFirst
        text: qsTr("Do you want to save the changes to “%1”?").arg(SessionManager.currentName)
        informativeText: qsTr("Your changes are lost if you don't save them.")
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel
        onButtonClicked: (button, role) => {
            if (button === MessageDialog.Save)
                guard.save()
            else if (button === MessageDialog.Discard)
                guard.discard()
            else
                guard.cancel()
        }
    }
    ConfirmDialog {
        id: newSessionConfirm
        title: qsTr("Start a new session?")
        text: qsTr("Every instrument goes back to its default settings and the layout is reset. Unsaved changes of “%1” are lost.").arg(SessionManager.currentName)
        acceptText: qsTr("New Session")
        destructive: true
        onAccepted: SessionManager.newSession()
    }
    ConfirmDialog {
        id: revertConfirm
        title: qsTr("Revert to the saved “%1”?").arg(SessionManager.currentName)
        text: qsTr("The changes since the last save are lost.")
        acceptText: qsTr("Revert")
        destructive: true
        onAccepted: SessionManager.load(SessionManager.currentFile)
    }
    ConfirmDialog {
        id: resetAllConfirm
        title: qsTr("Reset all instruments?")
        text: qsTr("Every instrument goes back to its default settings and running instruments stop. The layout is kept.")
        acceptText: qsTr("Reset All")
        destructive: true
        onAccepted: SessionManager.revertAllToDefaults()
    }

    // --- Instruments ----------------------------------------------------------------------------------
    function resetSettings(paneId: string) {
        const controller = InstrumentRegistry.controller(paneId.split("/")[0])
        if (!controller)
            return
        resetSettingsConfirm.controller = controller
        resetSettingsConfirm.open()
    }
    ConfirmDialog {
        id: resetSettingsConfirm
        property InstrumentController controller: null
        title: qsTr("Reset the %1 settings?").arg(controller ? controller.title : "")
        text: qsTr("Every setting of this instrument goes back to its default.")
        acceptText: qsTr("Reset Settings")
        destructive: true
        onAccepted: {
            if (controller)
                controller.resetSettings()
        }
    }

    // --- Quit ----------------------------------------------------------------------------------------
    // Called from the window's onClosing: true when the window may close now. Otherwise the
    // needed questions are asked (unsaved documents, save the session, instruments running), and
    // quitAccepted() is emitted once everything is answered.
    function mayClose(): bool {
        if (quitConfirmed || AppInfo.screenshot)
            return true
        const mustConfirm = preferences.behaviour.confirmQuit && InstrumentRegistry.anyRunning
        const documents = InstrumentRegistry.unsavedDocuments()
        if (!guard.needsPrompt && !mustConfirm && documents.length === 0)
            return true
        if (documents.length > 0 && !unsavedDocumentsConfirm.answered) {
            unsavedDocumentsConfirm.documents = documents
            unsavedDocumentsConfirm.open()
            return false
        }
        guard.run(() => {
            if (preferences.behaviour.confirmQuit && InstrumentRegistry.anyRunning)
                quitConfirm.open()
            else
                dialogs.acceptQuit()
        })
        return false
    }
    function acceptQuit() {
        quitConfirmed = true
        quitAccepted()
    }
    ConfirmDialog {
        id: quitConfirm
        title: qsTr("Quit %1?").arg(AppInfo.name)
        text: qsTr("%n instrument(s) still running. Quitting stops them and puts the outputs in their safe state.", "",
                   InstrumentRegistry.runningCount)
        acceptText: qsTr("Quit")
        destructive: true
        onAccepted: dialogs.acceptQuit()
    }
    ConfirmDialog {
        id: unsavedDocumentsConfirm
        property var documents: []
        property bool answered: false
        title: qsTr("Quit without saving these documents?")
        text: documents.join("\n")
        acceptText: qsTr("Quit Without Saving")
        destructive: true
        onAccepted: {
            answered = true
            Qt.callLater(() => {
                if (dialogs.mayClose())
                    dialogs.acceptQuit()
            })
        }
    }

    // --- Windows ------------------------------------------------------------------------------------
    function showPreferences(page: string) {
        preferencesWindow.page = page ?? ""
        preferencesWindow.show()
    }
    function showAbout() {
        aboutWindow.show()
    }
    function showDeviceInfo() {
        deviceInfoWindow.show()
    }
    function showSimulatorSettings() {
        simulatorWindow.show()
    }

    PaneWindow {
        id: preferencesWindow
        property string page: ""
        objectName: "preferencesWindow"
        title: qsTr("Preferences")
        pane: Component {
            PreferencesPane {
                preferences: dialogs.preferences
                page: preferencesWindow.page
                onSimulatorSettingsRequested: dialogs.showSimulatorSettings()
            }
        }
    }
    PaneWindow {
        id: aboutWindow
        objectName: "aboutWindow"
        title: qsTr("About %1").arg(AppInfo.name)
        pane: Component { AboutPane {} }
    }
    PaneWindow {
        id: deviceInfoWindow
        objectName: "deviceInfoWindow"
        title: qsTr("Device Info")
        pane: Component { DeviceInfoPane {} }
    }
    PaneWindow {
        id: simulatorWindow
        objectName: "simulatorWindow"
        title: qsTr("Simulator Settings")
        pane: Component { SimulatorPane {} }
    }
}
