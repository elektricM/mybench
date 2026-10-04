import QtQuick
import QtQuick.Layouts
import QtTest
import MyBench.Framework
import MyBench.Controls

// InspectorDock (docs/UI.md §13.4, audit U9): the dock claims the focused panel's one Inspector
// item and hands it back; focus A → B → A restores A's parent and scroll position. Without any
// dock, a panel docks its inspector on its own right side (fallback).
Item {
    id: root
    width: 1400
    height: 600

    Component {
        id: panelComponent
        InstrumentPanel {
            width: 900
            height: 600
            inspector: [
                Section { sectionId: "first"; title: "First"; Item { implicitHeight: 500; Layout.fillWidth: true } },
                Section { sectionId: "second"; title: "Second"; Item { implicitHeight: 500; Layout.fillWidth: true } },
                Section { sectionId: "third"; title: "Third"; Item { implicitHeight: 500; Layout.fillWidth: true } }
            ]
        }
    }
    Component {
        id: barePanelComponent
        InstrumentPanel {
            width: 900
            height: 600
        }
    }
    Component {
        id: dockComponent
        InspectorDock {
            x: 1000
            width: 300
            height: 600
        }
    }

    TestCase {
        name: "InspectorDock"
        when: windowShown

        function test_fallbackDocksLocally() {
            const panel = createTemporaryObject(panelComponent, root, { controller: InstrumentRegistry.controller("alpha") })
            verify(!panel.inspectorClaimed)
            tryCompare(panel, "docked", true)                   // no dock: docked on the right
            compare(panel.inspectorContent.parent.objectName, "localInspectorSlot")
            verify(panel.inspectorContent.width >= Theme.inspectorMinWidth - 1)
            verify(panel.paneHeader.showInspectorToggle)
            panel.toggleInspector()
            compare(panel.docked, false)
            panel.toggleInspector()
            compare(panel.docked, true)
            panel.width = 600                                   // narrow: an overlay sheet, closed
            compare(panel.docked, false)
            compare(panel.overlayShown, false)
            panel.toggleInspector()
            compare(panel.overlayShown, true)
        }

        function test_focusChangeRestoresParentAndScroll() {
            const a = createTemporaryObject(panelComponent, root, { controller: InstrumentRegistry.controller("alpha") })
            const b = createTemporaryObject(panelComponent, root, { controller: InstrumentRegistry.controller("beta"), x: 0, y: 0 })
            const dock = createTemporaryObject(dockComponent, root)
            compare(PanelRegistry.dockCount, 1)
            dock.panel = a
            verify(a.inspectorClaimed)
            compare(a.inspectorContent.parent.objectName, "inspectorDockHost")
            compare(dock.claimedPanel, a)
            compare(dock.instrumentTitle, a.paneTitle)
            wait(50)
            // While a dock exists, no panel docks its inspector itself (an unfocused pane shows none).
            verify(!a.localInspector && !a.docked)
            verify(!b.inspectorClaimed && !b.localInspector && !b.docked)

            const inspectorA = a.inspectorContent
            tryVerify(() => inspectorA.flickable.contentHeight > inspectorA.height + 400)
            inspectorA.contentY = 400
            wait(50)

            dock.panel = b                                      // focus B
            compare(inspectorA.parent.objectName, "localInspectorSlot")
            verify(!a.inspectorClaimed)
            verify(b.inspectorClaimed)
            compare(b.inspectorContent.parent.objectName, "inspectorDockHost")
            wait(50)

            dock.panel = a                                      // and back to A
            compare(inspectorA.parent.objectName, "inspectorDockHost")
            verify(!b.inspectorClaimed)
            tryCompare(inspectorA, "contentY", 400)
            wait(100)
            compare(inspectorA.contentY, 400)

            dock.panel = null
            verify(!a.inspectorClaimed)
            compare(dock.claimedPanel, null)
        }

        // The dock goes away while it holds an inspector: the panel takes it back and docks it.
        function test_dockDestroyedReleases() {
            const a = createTemporaryObject(panelComponent, root, { controller: InstrumentRegistry.controller("alpha") })
            const dock = createTemporaryObject(dockComponent, root)
            dock.panel = a
            verify(a.inspectorClaimed)
            dock.destroy()
            tryCompare(PanelRegistry, "dockCount", 0)
            verify(!a.inspectorClaimed)
            tryCompare(a, "docked", true)
        }

        // The empty hint only shows without an instrument pane: not while the pane loads (panel
        // still null) nor for a panel without inspector; hasInspector follows `inspector`.
        function test_emptyHintAndHasInspector() {
            const dock = createTemporaryObject(dockComponent, root)
            const hint = findChild(dock, "dockEmptyHint")
            verify(hint.visible)
            dock.paneShown = true                    // the shell: a pane is shown, still loading
            verify(!hint.visible)
            dock.paneShown = false
            const bare = createTemporaryObject(barePanelComponent, root)
            verify(!bare.hasInspector)
            dock.panel = bare
            verify(!hint.visible)                    // a panel: never the hint
            const full = createTemporaryObject(panelComponent, root)
            verify(full.hasInspector)
            dock.destroy()                           // no dock: only a panel with sections docks locally
            tryVerify(() => PanelRegistry.dockCount === 0)
            tryVerify(() => full.localInspector)
            verify(!bare.localInspector)
            verify(!findChild(bare.paneHeader, "inspectorToggle").visible)
        }

        function test_helpPage() {
            const a = createTemporaryObject(panelComponent, root, { controller: InstrumentRegistry.controller("alpha") })
            const dock = createTemporaryObject(dockComponent, root)
            dock.panel = a
            dock.showHelp("app")
            compare(dock.page, "help")
            compare(dock.effectiveHelpTopic, "app")
            dock.page = "inspector"
            dock.helpTopic = ""
            compare(dock.effectiveHelpTopic, a.helpId)
        }

        // Collapsed sections are keyed by pane and section id (section/<paneId>/<sectionId>).
        function test_sectionKeys() {
            const a = createTemporaryObject(panelComponent, root, { controller: InstrumentRegistry.controller("alpha") })
            const sections = a.inspector
            compare(sections[0].stateKey, "section/alpha/first")
            compare(sections[2].stateKey, "section/alpha/third")
        }
    }
}
