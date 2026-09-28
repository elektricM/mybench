import QtQuick
import QtTest
import MyBench.Framework
import ".."

// CommandPalette: commands with an argument ask for the value first; instrument commands
// (InstrumentController.commands) run through invokeCommand.
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
    }
}
