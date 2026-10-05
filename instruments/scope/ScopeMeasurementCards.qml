pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Controls

// The measurement cards under the display (PicoScope style): one card per shown measurement and
// trace — the value large, then min / max / mean / σ over the last 100 records or all records —,
// one card for the cursors while they are on, and "+ Add" (opens the Measure panel). Same model
// API as a MeasurementTable (ScopeMeasurements: columns, rows, available, add/remove requests), so
// the cards and the Measure panel write the same measurement set.
Item {
    id: strip

    required property ScopeController controller
    property var columns: []            // [{ title, color }] = shown traces
    property var traces: []             // [{ index: 0 | 1 | 2, title, color }]
    property var rows: []               // [{ id, title, values: [text per column] }]
    property var available: []
    property string summaryText
    property int statistics: 1          // 0 off, 1 last 100 records, 2 all records
    property int sourceFilter: -1       // -1 all traces, else 0 / 1 / 2
    property var catalog: []            // ScopeChannel.measurementCatalog()

    signal addRequested(string id)
    signal removeRequested(string id)
    signal openMeasurePanel()

    readonly property var measurementRows: rows.filter(r => r.id.startsWith("m"))
    readonly property var cursorRow: rows.find(r => r.id === "cursorDeltaT") ?? null
    readonly property var cursorDeltaV: rows.find(r => r.id === "cursorDelta") ?? null
    readonly property var cursorRate: rows.find(r => r.id === "cursorRate") ?? null
    // Stable card keys "<rowId>/<traceIndex>" (a string: the cards keep their statistics while the
    // values update).
    readonly property string cardKeys: {
        const keys = []
        for (const r of measurementRows)
            for (let c = 0; c < traces.length; ++c)
                if ((sourceFilter < 0 || traces[c].index === sourceFilter)
                        && (traces[c].index !== 2 || r.id === "m" + ScopeChannel.Rms || r.id === "m" + ScopeChannel.PeakToPeak))
                    keys.push(r.id + "/" + traces[c].index + "/" + c)
        return keys.join("|")
    }

    // The scroll bar gets its own row under the cards: native styles (macOS) draw a permanent bar
    // that would otherwise cover the cards' last line.
    readonly property bool overflowing: flow.implicitWidth > width - 2 * Theme.spacingS
    readonly property real scrollBarSpace: overflowing ? hbar.implicitHeight : 0
    implicitHeight: flow.implicitHeight + 2 * Theme.spacingS + scrollBarSpace
    implicitWidth: 400

    function catalogEntry(flag: int): var {
        return catalog.find(e => e.flag === flag) ?? { label: "", unit: "V" }
    }

    // One row of cards; scrolls sideways when they do not fit (the display keeps its height).
    Flickable {
        id: scroller
        // The bottom scrollBarSpace stays free of cards: the attached bar sits there.
        anchors { fill: parent; margins: Theme.spacingS }
        contentWidth: flow.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: strip.overflowing
        ScrollBar.horizontal: ScrollBar {
            id: hbar
            objectName: "measurementScrollBar"
            policy: strip.overflowing ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
    Row {
        id: flow
        spacing: Theme.spacingS

        Repeater {
            model: strip.cardKeys.length > 0 ? strip.cardKeys.split("|") : []
            delegate: MeasurementCard {
                id: card
                required property string modelData
                readonly property var parts: modelData.split("/")
                readonly property string rowId: parts[0]
                readonly property int flag: Number(rowId.slice(1))
                readonly property int traceIndex: Number(parts[1])
                readonly property int column: Number(parts[2])
                readonly property var row: strip.rows.find(r => r.id === rowId) ?? null
                readonly property var trace: strip.traces[column] ?? { title: "", color: Theme.text }
                readonly property var entry: strip.catalogEntry(flag)
                readonly property string measureUnit: traceIndex === 2 ? strip.controller.math.unit : entry.unit
                property var samples: []
                property real total: 0
                property real total2: 0
                property real minimum: NaN
                property real maximum: NaN
                property int count: 0
                readonly property real mean: count > 0 ? total / count : NaN
                readonly property real sigma: count > 1 ? Math.sqrt(Math.max(0, total2 / count - mean * mean)) : NaN

                function push(v: real) {
                    if (!isFinite(v) || strip.statistics === 0)
                        return
                    if (strip.statistics === 1) {
                        const s = samples.concat([v]).slice(-100)
                        samples = s
                        count = s.length
                        total = s.reduce((a, b) => a + b, 0)
                        total2 = s.reduce((a, b) => a + b * b, 0)
                        minimum = Math.min(...s)
                        maximum = Math.max(...s)
                    } else {
                        count += 1
                        total += v
                        total2 += v * v
                        minimum = isNaN(minimum) ? v : Math.min(minimum, v)
                        maximum = isNaN(maximum) ? v : Math.max(maximum, v)
                    }
                }
                function reset() {
                    samples = []; total = 0; total2 = 0; minimum = NaN; maximum = NaN; count = 0
                }
                // Statistics as numbers: the card formats them (4 significant digits, σ 3); the
                // value keeps the controller's measurement format.
                function num(v: real): var {
                    return isFinite(v) ? v : ""
                }

                Connections {
                    target: strip
                    function onStatisticsChanged() { card.reset() }
                }
                Connections {
                    target: card.traceIndex === 2 ? strip.controller.math
                                                  : (card.traceIndex === 0 ? strip.controller.channel0 : strip.controller.channel1)
                    function onResultsChanged() {
                        if (card.traceIndex === 2)
                            card.push(card.flag === ScopeChannel.Rms ? strip.controller.math.rms
                                      : card.flag === ScopeChannel.PeakToPeak ? strip.controller.math.peakToPeak : NaN)
                        else
                            card.push((card.traceIndex === 0 ? strip.controller.channel0 : strip.controller.channel1).measurement(card.flag))
                    }
                }

                title: card.trace.title + " · " + (card.row ? card.row.title : "")
                color: card.trace.color
                value: card.row ? (card.row.values[card.column] || "—") : "—"
                unit: card.measureUnit
                stats: strip.statistics === 0 ? ({}) : ({ min: num(minimum), max: num(maximum), mean: num(mean), stdDev: num(sigma) })
                onRemoveRequested: strip.removeRequested(card.rowId)
            }
        }

        // Cursors A–B: Δt large, 1/Δt and ΔV beside it.
        MeasurementCard {
            objectName: "cursorCard"
            visible: strip.controller.cursorsOn
            title: qsTr("Cursors A–B") + (strip.cursorRate ? "  ·  1/Δt " + (strip.cursorRate.values[0] || "—") : "")
            color: Theme.signalCursor
            value: strip.cursorRow ? (strip.cursorRow.values[0] || "—") : "—"
            removable: false
        }

        // "+ Add": the Measure panel.
        Rectangle {
            objectName: "addMeasurement"
            width: 96
            height: 64
            radius: Theme.radiusM
            color: addHover.hovered ? Theme.hoverFill : "transparent"
            border.color: Theme.cardBorder
            HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: strip.openMeasurePanel() }
            Text {
                anchors.centerIn: parent
                text: qsTr("+ Add")
                color: Theme.accent
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
            }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Add a measurement")
        }
    }
}
}
