import QtQuick
import MyBench.Framework

// Save-first guard of the session actions (app.md 11.2): Open, Open recent and Quit ask to save
// a modified *named* session first (Save / Don't save / Cancel); an untitled session is handled
// by the New-session confirmation. Also the window title (app.md 2.1: name + modified marker).
//     SessionGuard { id: guard; onSaveFirstRequested: saveFirstDialog.open() }
//     guard.run(() => SessionManager.load(file))
QtObject {
    id: guard

    // The action waiting for the user's answer.
    property var pending: null
    readonly property bool named: SessionManager.currentFile.toString().length > 0
    readonly property bool needsPrompt: SessionManager.modified && named
    readonly property string windowTitle: AppInfo.name + " — " + SessionManager.currentName
                                          + (SessionManager.modified ? " •" : "")

    signal saveFirstRequested()

    function run(action: var) {
        if (!needsPrompt) {
            action()
            return
        }
        pending = action
        saveFirstRequested()
    }
    function proceed() {
        const action = pending
        pending = null
        if (action)
            action()
    }
    function save() {
        if (SessionManager.saveCurrent())
            proceed()
        else
            pending = null
    }
    function discard() {
        proceed()
    }
    function cancel() {
        pending = null
    }
}
