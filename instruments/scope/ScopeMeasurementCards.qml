pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Controls

// The measurement cards under the display (PicoScope style): first one card per cursor pair while
// it is on (C1–C2: Δt, 1/Δt, ΔV, C1, C2; H1–H2: ΔV), then one card per shown measurement and
// trace — the value large, then min / max / mean / σ over the last 100 records or all records —,
// and "+ Add" (opens the Measure panel). Same model
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
    function traceName(index: int): string {
        return index === 2 ? qsTr("Math") : "CH" + index
    }
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

        // Time cursors C1–C2: Δt large; 1/Δt, ΔV, and the values at C1 and C2 on their traces.
        ScopeCursorCard {
            objectName: "cursorCard"
            visible: strip.controller.cursorsOn
            readonly property var c1: strip.controller.cursor1
            readonly property var c2: strip.controller.cursor2
            // Re-read with every record (the rows follow the records). First in the strip:
            // what the user is pointing at right now..
            readonly property var readings: {
                void strip.rows
                const v1 = strip.controller.valueAt(c1.channel, c1.position)
                const v2 = strip.controller.valueAt(c2.channel, c2.position)
                return { v1: v1, v2: v2, dt: c2.position - c1.position }
            }
            function unitOf(index: int): string {
                return index === 2 ? strip.controller.math.unit : "V"
            }
            function fmt(v: real, unit: string): string {
                return isFinite(v) ? strip.controller.formatMeasurement(v, unit) : ""
            }
            title: c1.channel === c2.channel ? qsTr("on %1").arg(strip.traceName(c1.channel)) : ""
            value: fmt(readings.dt, "s")
            items: [{ name: "1/Δt", text: Math.abs(readings.dt) > 0 ? fmt(1 / Math.abs(readings.dt), "Hz") : "" },
                    { name: "ΔV", text: unitOf(c1.channel) === unitOf(c2.channel) ? fmt(readings.v2 - readings.v1, unitOf(c1.channel)) : "" },
                    { name: "C1", color: Theme.cursor1, text: fmt(readings.v1, unitOf(c1.channel)) },
                    { name: "C2", color: Theme.cursor2, text: fmt(readings.v2, unitOf(c2.channel)) }]
        }
        // Level cursors H1–H2 on one trace: ΔV large, the two levels beside it.
        ScopeCursorCard {
            objectName: "levelCursorCard"
            visible: strip.controller.levelCursorsOn
            readonly property string unit: strip.controller.levelCursorChannel === 2 ? strip.controller.math.unit : "V"
            names: ["H1", "H2"]
            title: qsTr("on %1").arg(strip.traceName(strip.controller.levelCursorChannel))
            value: strip.controller.formatMeasurement(strip.controller.levelCursor2 - strip.controller.levelCursor1, unit)
            items: [{ name: "H1", color: Theme.cursor1, text: strip.controller.formatMeasurement(strip.controller.levelCursor1, unit) },
                    { name: "H2", color: Theme.cursor2, text: strip.controller.formatMeasurement(strip.controller.levelCursor2, unit) }]
        }

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
