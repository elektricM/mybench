pragma Singleton
import QtQuick

// The instrument panes that are loaded, by pane id ("scope", "gen/editor") — docs/UI.md §13.6.
// InstrumentPanel registers itself once its whole tree is complete (its controller's QML
// handlers are connected) and unregisters when destroyed. Shell commands for a pane that may
// still be loading asynchronously go through whenReady(), which runs them exactly once:
//     workspace.show("scope")
//     PanelRegistry.whenReady("scope", panel => panel.exportData())
// InspectorDock counts itself in `dockCount`: while a dock exists, panels never dock their
// inspector locally (the dock shows the focused pane's inspector, the others show none).
QtObject {
    id: registry

    // Number of InspectorDocks alive.
    property int dockCount: 0
    // Increments at every register/unregister: bind to it to re-evaluate panel(id) lookups.
    readonly property int revision: registryState.revision

    signal panelReady(string paneId)
    signal panelRemoved(string paneId)

    readonly property QtObject privateState: QtObject {
        id: registryState
        property int revision: 0
        property var panels: ({})    // paneId -> InstrumentPanel
        property var pending: ({})   // paneId -> [function(panel)]
    }

    function register(paneId: string, panel: Item) {
        if (paneId.length === 0 || !panel)
            return
        registryState.panels[paneId] = panel
        registryState.revision += 1
        const queued = registryState.pending[paneId] ?? []
        delete registryState.pending[paneId]
        for (const fn of queued) {
            try {
                fn(panel)
            } catch (error) {
                console.warn("PanelRegistry: command for", paneId, "failed:", error)
            }
        }
        panelReady(paneId)
    }

    // `panel` (optional): only if it is still the registered one (a newer instance may have
    // replaced it).
    function unregister(paneId: string, panel: Item) {
        const current = registryState.panels[paneId]
        if (current === undefined || (panel && current && current !== panel))
            return
        delete registryState.panels[paneId]
        registryState.revision += 1
        panelRemoved(paneId)
    }

    // The loaded pane, or null.
    function panel(paneId: string): Item {
        const p = registryState.panels[paneId]
        return p ? p : null
    }

    // Runs fn(panel) now if the pane is loaded, otherwise once when it registers. Returns true
    // when it ran now.
    function whenReady(paneId: string, fn: var): bool {
        const p = panel(paneId)
        if (p) {
            fn(p)
            return true
        }
        const queued = registryState.pending[paneId] ?? []
        queued.push(fn)
        registryState.pending[paneId] = queued
        return false
    }

    // Drops the commands still waiting for `paneId` (e.g. the pane failed to load).
    function cancel(paneId: string) {
        delete registryState.pending[paneId]
    }

    function pendingCount(paneId: string): int {
        return (registryState.pending[paneId] ?? []).length
    }
}
