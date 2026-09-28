pragma Singleton
import QtQuick

// Commands from the shell (shortcuts, palette) to the panel of an instrument: InstrumentPanel
// listens for its controller's id. Focus mode (app.md 6.5): `focusInstrument` is the id shown
// alone (display only), "" when off.
QtObject {
    property string focusInstrument: ""

    signal inspectorToggleRequested(string instrumentId)
    signal exportDataRequested(string instrumentId)
    signal exportImageRequested(string instrumentId)
    signal exportPdfRequested(string instrumentId)

    function toggleFocus(instrumentId: string) {
        focusInstrument = focusInstrument === instrumentId ? "" : instrumentId
    }
}
