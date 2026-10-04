import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// Reset All Instruments to Defaults… (menu, palette, Overview "Reset All…"): asks with the verb
// "Reset All", then stops every running instrument, applies the safe states and restores the
// default settings; the layout is kept.
Item {
    id: host
    width: 900
    height: 600

    AppPreferences { id: prefs }
    WorkspaceState { id: ws }
    ShellDialogs {
        id: dialogs
        anchors.fill: parent
        preferences: prefs
        workspace: ws
    }
    AppActions {
        id: actions
        workspace: ws
        dialogs: dialogs
        preferences: prefs
    }

    TestCase {
        name: "ResetAll"
        when: windowShown

        function openConfirm(): var {
            actions.resetAllInstruments.trigger()
            const confirm = findChild(dialogs, "resetAllConfirm")
            verify(confirm)
            let dialog = null
            tryVerify(() => (dialog = findChild(confirm, "confirmDialog")) !== null && confirm.opened)
            return dialog
        }

        function test_actionText() {
            compare(actions.resetAllInstruments.text, "Reset All Instruments to Defaults…")
        }

        function test_resetAllStopsAndRestoresDefaults() {
            const aa = InstrumentRegistry.controller("aa")
            const bb = InstrumentRegistry.controller("bb")
            ws.show("aa")
            ws.showBeside("bb")
            aa.level = 0.5
            aa.run()
            tryCompare(aa, "running", true)
            const safeBefore = bb.safeStateCount

            // Cancel: nothing changes.
            let dialog = openConfirm()
            mouseClick(findChild(dialog.footer, "confirmCancelButton"))
            tryCompare(dialog, "visible", false)
            verify(aa.running)
            compare(aa.level, 0.5)

            dialog = openConfirm()
            const accept = findChild(dialog.footer, "confirmAcceptButton")
            compare(accept.text, "Reset All")
            verify(!accept.highlighted)                    // destructive: plain button
            mouseClick(accept)
            tryCompare(dialog, "visible", false)
            tryCompare(aa, "running", false)
            compare(aa.level, 0)
            verify(bb.safeStateCount > safeBefore)         // outputs to their safe state
            compare(ws.selected, "aa")                     // the layout is kept
            compare(ws.beside, "bb")
        }
    }
}
