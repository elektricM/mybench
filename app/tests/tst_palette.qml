import QtQuick
import QtTest
import MyBench.Framework
import MyBench.Controls
import MyBench.App

// CommandPalette: commands with an argument ask for the value first; instrument commands
// (InstrumentController.commands) for a panel that is still loading wait for it
// (PanelRegistry.whenReady) and run exactly once.
Item {
    id: host
    width: 800
    height: 600

    property var received: []

    CommandPalette {
        id: palette
        commands: () => [
            { title: "Plain", run: () => host.received.push("plain") },
            { title: "Set AO 0", argumentLabel: "Voltage (V)", run: arg => host.received.push("ao:" + arg) }
        ]
    }

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
    Item { id: fakePanel }

    TestCase {
        name: "Palette"
        when: windowShown

        function findField() {
            return findChild(palette.contentItem, "paletteField")
        }

        function test_argumentCommand() {
            host.received = []
            palette.open()
            tryCompare(palette, "opened", true)
            palette.refresh("set ao")
            compare(palette.results[0].title, "Set AO 0")
            palette.runCurrent()
            verify(palette.pending !== null)          // asks for the value, still open
            compare(palette.opened, true)
            findField().text = "0.5"
            palette.runCurrent()
            tryCompare(palette, "opened", false)
            tryVerify(() => host.received.length === 1)
            compare(host.received[0], "ao:0.5")
        }

        function test_plainCommandRunsAtOnce() {
            host.received = []
            palette.open()
            palette.refresh("plain")
            palette.runCurrent()
            tryVerify(() => host.received.length === 1)
            compare(host.received[0], "plain")
            verify(palette.pending === null)
        }

        function test_typingFilters() {
            palette.open()
            tryCompare(palette, "opened", true)
            keyClick(Qt.Key_P)
            keyClick(Qt.Key_L)
            compare(palette.results.length, 1)
            compare(palette.results[0].title, "Plain")
            keyClick(Qt.Key_Escape)
            tryCompare(palette, "opened", false)
        }

        function test_controllerCommands() {
            const c = InstrumentRegistry.controller("aa")
            const list = c.commands
            compare(list.length, 2)
            compare(list[0].argumentLabel, "Level (V)")
            c.invokeCommand("level", "250m")
            compare(c.level, 0.25)
            let triggered = ""
            const handler = (id, arg) => triggered = id
            c.commandTriggered.connect(handler)
            c.invokeCommand("poke")
            c.commandTriggered.disconnect(handler)
            compare(triggered, "poke")
        }

        // A command handled in QML (commandTriggered) for a panel that is not loaded yet runs once
        // the panel registers, exactly once (it was lost with Qt.callLater before).
        function test_commandForAnUnloadedPanelRunsOnce() {
            const bb = InstrumentRegistry.controller("bb")
            PanelRegistry.unregister("bb")
            let count = 0
            const handler = id => { if (id === "poke") ++count }
            bb.commandTriggered.connect(handler)
            const entry = actions.paletteCommands().find(c => c.title === "BB Instrument: Poke")
            verify(entry)
            entry.run(undefined)
            compare(ws.selected, "bb")                   // shown, its panel starts loading
            compare(count, 0)
            compare(PanelRegistry.pendingCount("bb"), 1)
            PanelRegistry.register("bb", fakePanel)      // the panel is ready
            compare(count, 1)
            PanelRegistry.register("bb", fakePanel)      // registering again does not repeat it
            compare(count, 1)
            entry.run(undefined)                          // loaded now: runs at once
            compare(count, 2)
            bb.commandTriggered.disconnect(handler)
            PanelRegistry.unregister("bb", fakePanel)
        }
    }
}
