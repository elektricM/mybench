pragma Singleton
import QtQuick
import MyBench.Framework

// (alias, docs/UI.md §13.6) Commands from the shell (shortcuts, palette, plots) to the panel of an
// instrument. The per-pane requests forward to PanelRegistry.whenReady(), so a request for a pane
// that is still loading runs once it is ready. New code calls PanelRegistry directly.
// Focus mode: `focusInstrument` is the id shown alone (display only), "" when off.
QtObject {
    property string focusInstrument: ""

    // Open the event log (error banner / toast `Details`); the shell handles it.
    signal eventLogRequested()

    function forward(paneId: string, action: var) {
        if (paneId.length === 0 || !InstrumentRegistry.contains(paneId.split("/")[0]))
            return
        PanelRegistry.whenReady(paneId, action)
    }
    function inspectorToggleRequested(paneId: string) { forward(paneId, panel => panel.toggleInspector()) }
    function exportDataRequested(paneId: string) { forward(paneId, panel => panel.exportData()) }
    function exportImageRequested(paneId: string) { forward(paneId, panel => panel.exportImage(false)) }
    function exportPdfRequested(paneId: string) { forward(paneId, panel => panel.exportImage(true)) }
    function printRequested(paneId: string) { forward(paneId, panel => panel.printPanel()) }

    function toggleFocus(instrumentId: string) {
        focusInstrument = focusInstrument === instrumentId ? "" : instrumentId
    }

    // Opens (or focuses) another instrument's panel, preconfigured: `preset` is applied to its
    // controller first (InstrumentController.applyPreset), `beside` shows it in a split pane
    // next to the current panel. E.g. from the Scope's FFT:
    //     ShellActions.openInstrument("dsa", { frequencySpan: 20000 }, true)
    function openInstrument(instrumentId: string, preset: var, beside: bool): bool {
        return InstrumentRegistry.openInstrument(instrumentId, preset ?? {}, beside)
    }
}
