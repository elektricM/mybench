pragma Singleton
import QtQuick
import MyBench.Framework

// Commands from the shell (shortcuts, palette) to the panel of an instrument: InstrumentPanel
// listens for its controller's id. Focus mode (app.md 6.5): `focusInstrument` is the id shown
// alone (display only), "" when off.
QtObject {
    property string focusInstrument: ""

    signal inspectorToggleRequested(string instrumentId)
    signal exportDataRequested(string instrumentId)
    signal exportImageRequested(string instrumentId)
    signal exportPdfRequested(string instrumentId)
    signal printRequested(string instrumentId)
    // Open the event log (error banner / toast `Details`); the shell handles it.
    signal eventLogRequested()

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
