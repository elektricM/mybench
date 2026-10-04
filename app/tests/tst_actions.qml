import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// AppActions (docs/UI.md §9): one Action per command feeds the menu bar, the palette and the
// keyboard. No shortcut is used twice, Space is never a menu key, instrument cycling uses the
// standard Next/Previous keys (no Ctrl+Tab, which is ⌘Tab on macOS), and the menu bar shows the
// actions.
Item {
    id: host
    width: 800
    height: 600

    AppPreferences { id: prefs }
    WorkspaceState { id: ws }
    ShellDialogs {
        id: dialogs
        preferences: prefs
        workspace: ws
    }
    AppActions {
        id: actions
        workspace: ws
        dialogs: dialogs
        preferences: prefs
    }
    AppMenuBar {
        id: menuBar
        actions: actions
    }

    readonly property bool mac: AppInfo.platform === "macos"

    TestCase {
        name: "Actions"
        when: windowShown

        function init() {
            ws.fromMap({})
        }

        function test_noDuplicateShortcuts() {
            const seen = {}
            let count = 0
            const all = actions.allActions()
            verify(all.length > 40)
            for (const action of all) {
                for (const text of KeyText.all(action.keys)) {
                    verify(seen[text] === undefined, text + ": " + action.text + " and " + seen[text])
                    seen[text] = action.text
                    ++count
                }
            }
            for (const shortcut of actions.windowShortcuts) {
                if (!shortcut.enabled)
                    continue
                for (const sequence of shortcut.sequences) {
                    const text = KeyText.portable(sequence)
                    verify(seen[text] === undefined, text + " is also " + seen[text])
                    seen[text] = "window shortcut"
                }
            }
            verify(count > 25)
            // The documented keys (Ctrl = ⌘ on macOS).
            compare(actions.runStop.keysText, "Ctrl+R")
            compare(actions.single.keysText, "Ctrl+Shift+R")
            compare(actions.stopAll.keysText, "Ctrl+.")
            compare(actions.exportData.keysText, "Ctrl+E")
            compare(actions.exportImage.keysText, "Ctrl+Shift+E")
            compare(actions.showOverview.keysText, "Ctrl+0")
            compare(actions.paneActions[0].keysText, "Ctrl+1")
            compare(actions.toggleSplit.keysText, "Ctrl+\\")
            compare(actions.commandPalette.keysText, "Ctrl+K")
            compare(actions.toggleSidebar.keysText, KeyText.portable(host.mac ? "Meta+Ctrl+S" : "Ctrl+Alt+S"))
            compare(actions.toggleInspector.keysText, KeyText.portable(host.mac ? "Alt+Ctrl+I" : "Ctrl+I"))
        }

        function test_spaceIsNeverAMenuKey() {
            for (const action of actions.allActions()) {
                for (const text of KeyText.all(action.keys))
                    verify(text !== "Space" && text !== "Shift+Space", action.text)
            }
            // Space and Shift+Space are window shortcuts, gated by ShellKeys.spaceAllowed().
            const sequences = Array.from(actions.windowShortcuts).map(s => s.sequences[0])
            verify(sequences.indexOf("Space") >= 0)
            verify(sequences.indexOf("Shift+Space") >= 0)
        }

        function test_instrumentCyclingUsesStandardKeys() {
            compare(actions.nextInstrument.keys, StandardKey.NextChild)
            compare(actions.previousInstrument.keys, StandardKey.PreviousChild)
            for (const action of actions.allActions()) {
                const keys = action.keys === undefined ? "" : String(action.keys)
                verify(keys.indexOf("Tab") < 0 || typeof action.keys === "number", action.text)
            }
            // The platform decides (macOS: ⌘} / ⌘{, as ⌘Tab belongs to the system; elsewhere
            // Ctrl+Tab): the action shows the platform's main binding.
            compare(actions.nextInstrument.keysText, KeyText.portable(StandardKey.NextChild))
            compare(actions.fullScreenAction.keys, StandardKey.FullScreen)
        }

        function test_shortcutsActivate() {
            host.forceActiveFocus()
            keyClick(Qt.Key_1, Qt.ControlModifier)
            compare(ws.selected, "aa")
            keyClick(Qt.Key_2, Qt.ControlModifier)
            compare(ws.selected, "bb")
            keyClick(Qt.Key_Backslash, Qt.ControlModifier)
            compare(ws.split, true)
            keyClick(Qt.Key_0, Qt.ControlModifier)
            verify(ws.overview)
            ws.show("aa")
            const aa = InstrumentRegistry.controller("aa")
            keyClick(Qt.Key_R, Qt.ControlModifier)
            tryCompare(aa, "running", true)
            keyClick(Qt.Key_Period, Qt.ControlModifier)
            tryCompare(aa, "running", false)
        }

        function test_spaceRunsTheFocusedPane() {
            ws.show("bb")
            host.forceActiveFocus()
            const bb = InstrumentRegistry.controller("bb")
            keyClick(Qt.Key_Space)
            tryCompare(bb, "running", true)
            keyClick(Qt.Key_Space)
            tryCompare(bb, "running", false)
        }

        function test_menus() {
            const titles = []
            for (let i = 0; i < menuBar.count; ++i)
                titles.push(menuBar.menuAt(i).title.replace("&", ""))
            const expected = ["File", "Edit", "View", "Instrument", "Device", "Tools"]
            if (host.mac)
                expected.push("Window")
            expected.push("Help")
            compare(titles, expected)
            // The Instrument menu lists Overview and every pane (⌘1…).
            const instrument = menuBar.menuAt(3)
            const texts = []
            for (let i = 0; i < instrument.count; ++i) {
                const item = instrument.itemAt(i) as MenuItem
                texts.push(item ? item.text : "-")
            }
            compare(texts.slice(0, 7), ["Overview", "-", "AA Instrument", "BB Instrument", "CC Instrument", "CC Editor", "-"])
        }

        function test_paletteEntriesComeFromActions() {
            ws.show("aa")
            const titles = actions.paletteCommands().map(c => c.title)
            for (const title of ["Show Overview", "Show AA Instrument", "Show BB Instrument Beside", "Run BB Instrument",
                                 "AA Instrument: Poke", "AA Instrument: Set level", "Export Data…", "Preferences…",
                                 "Appearance: Dark", "Show Beside"])
                verify(titles.indexOf(title) >= 0, title)
            verify(titles.indexOf("Stop All") < 0)          // nothing runs: disabled actions are left out
        }
    }
}
