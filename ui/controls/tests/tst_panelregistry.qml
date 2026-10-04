import QtQuick
import QtTest
import MyBench.Framework
import MyBench.Controls

// PanelRegistry (docs/UI.md §13.6, audit U10): a command for a pane that is not loaded yet runs
// exactly once when the pane registers; ShellActions forwards through it.
Item {
    id: root
    width: 900
    height: 600

    Component {
        id: plainItem
        Item {}
    }
    Component {
        id: panelComponent
        InstrumentPanel {
            width: 900
            height: 600
            controller: InstrumentRegistry.controller("gamma")
        }
    }

    TestCase {
        name: "PanelRegistry"
        when: windowShown

        SignalSpy {
            id: readySpy
            target: PanelRegistry
            signalName: "panelReady"
        }

        function test_whenReadyAfterRegister() {
            const item = createTemporaryObject(plainItem, root)
            PanelRegistry.register("reg-after", item)
            compare(PanelRegistry.panel("reg-after"), item)
            let calls = 0
            verify(PanelRegistry.whenReady("reg-after", p => { compare(p, item); ++calls }))
            compare(calls, 1)
            PanelRegistry.unregister("reg-after", item)
            compare(PanelRegistry.panel("reg-after"), null)
        }

        function test_whenReadyBeforeRegisterRunsOnce() {
            let calls = 0
            let received = null
            verify(!PanelRegistry.whenReady("reg-before", p => { received = p; ++calls }))
            verify(!PanelRegistry.whenReady("reg-before", () => ++calls))
            compare(calls, 0)
            compare(PanelRegistry.pendingCount("reg-before"), 2)
            readySpy.clear()
            const item = createTemporaryObject(plainItem, root)
            PanelRegistry.register("reg-before", item)
            compare(calls, 2)                  // both queued commands, once each
            compare(received, item)
            compare(readySpy.count, 1)
            compare(readySpy.signalArguments[0][0], "reg-before")
            compare(PanelRegistry.pendingCount("reg-before"), 0)
            PanelRegistry.unregister("reg-before", item)
            PanelRegistry.register("reg-before", item)   // registering again: nothing left to run
            compare(calls, 2)
            PanelRegistry.unregister("reg-before", item)
        }

        function test_unregisterKeepsNewerPanel() {
            const first = createTemporaryObject(plainItem, root)
            const second = createTemporaryObject(plainItem, root)
            PanelRegistry.register("reg-two", first)
            PanelRegistry.register("reg-two", second)
            PanelRegistry.unregister("reg-two", first)    // stale: ignored
            compare(PanelRegistry.panel("reg-two"), second)
            PanelRegistry.unregister("reg-two", second)
            compare(PanelRegistry.panel("reg-two"), null)
        }

        function test_cancel() {
            let calls = 0
            PanelRegistry.whenReady("reg-cancel", () => ++calls)
            PanelRegistry.cancel("reg-cancel")
            const item = createTemporaryObject(plainItem, root)
            PanelRegistry.register("reg-cancel", item)
            compare(calls, 0)
            PanelRegistry.unregister("reg-cancel", item)
        }

        // An InstrumentPanel registers itself when complete and unregisters when destroyed; a
        // shell command sent before it exists (the race of an asynchronously loaded panel) runs
        // exactly once, after the panel's tree is complete.
        function test_instrumentPanelRegisters() {
            compare(PanelRegistry.panel("gamma"), null)
            let calls = 0
            let complete = false
            PanelRegistry.whenReady("gamma", p => {
                ++calls
                complete = p.paneHeader !== null && p.inspectorContent !== null
            })
            ShellActions.inspectorToggleRequested("gamma")   // queued too
            ShellActions.exportDataRequested("unknown-id")   // unknown instrument: dropped
            compare(PanelRegistry.pendingCount("unknown-id"), 0)
            const panel = createTemporaryObject(panelComponent, root)
            compare(PanelRegistry.panel("gamma"), panel)
            compare(calls, 1)
            verify(complete)
            compare(panel.inspectorVisible, false)           // the toggle ran once
            panel.destroy()
            tryVerify(() => PanelRegistry.panel("gamma") === null)
            compare(calls, 1)
        }
    }
}
