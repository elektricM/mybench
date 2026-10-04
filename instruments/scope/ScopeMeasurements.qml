pragma ComponentBehavior: Bound
import QtQuick
import MyBench.Controls

// The model of the Oscilloscope's measurement strip (a MeasurementTable): one column per shown
// trace (CH0, CH1, Math), one row per chosen measurement, then the cursor rows while cursors are on.
// The strip's "+" menu is the only place to choose measurements: add() / remove() write the
// controller (one set for every channel, ScopeController.setMeasurementShown), the cursor readouts
// (cursor showValue, showDeltaTime) and the level cursors.
//
// Row ids: "m<flag>" (ScopeChannel.Measurement), "cursor1", "cursor2", "cursorDelta" (ΔV),
// "cursorDeltaT", "cursorRate" (Δt, 1/Δt), "level1", "level2", "levelDelta".
QtObject {
    id: model

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeMath math: controller.math

    // Trace colors (the trace style's own color when the user picked one).
    property color ch0Color: Theme.signalCh0
    property color ch1Color: Theme.signalCh1
    property color mathColor: Theme.signalMath

    // [{ index: 0 | 1 | 2 (math), title, color }]
    readonly property var traces: {
        const list = []
        if (ch0.enabled)
            list.push({ index: 0, title: "CH0", color: ch0Color })
        if (ch1.enabled)
            list.push({ index: 1, title: "CH1", color: ch1Color })
        if (math.enabled)
            list.push({ index: 2, title: qsTr("Math"), color: mathColor })
        return list
    }
    readonly property var columns: traces.map(t => ({ title: t.title, color: t.color }))

    readonly property int shownMask: ch0.measurementMask | ch1.measurementMask
    readonly property bool cursorsOn: controller.cursorsOn

    function unitOf(index: int): string {
        return index === 2 ? math.unit : "V"
    }
    // Value of a trace at t (ELVISmx: 0.00 V without data).
    function valueAt(index: int, seconds: real, recordLength: int): real {
        const v = recordLength > 0 ? controller.valueAt(index, seconds) : NaN
        return isNaN(v) ? 0 : v
    }
    function measurementText(index: int, flag: int, readouts0: var, readouts1: var): string {
        if (index === 2) {
            // The math trace has RMS and peak-to-peak.
            if (flag === ScopeChannel.Rms)
                return controller.formatMeasurement(math.rms, math.unit)
            if (flag === ScopeChannel.PeakToPeak)
                return controller.formatMeasurement(math.peakToPeak, math.unit)
            return ""
        }
        const entry = (index === 0 ? readouts0 : readouts1).find(r => r.key === flag)
        return entry ? entry.text : ""
    }
    // A value of one trace column only (level cursors), "" elsewhere.
    function onlyIn(index: int, text: string): var {
        return traces.map(t => t.index === index ? text : "")
    }
    // A value that is the same for every trace (Δt, 1/Δt): shown once, in the first column.
    function once(text: string): var {
        return traces.map((t, i) => i === 0 ? text : "")
    }

    readonly property var rows: {
        // Re-evaluated with every record: readouts, math results and (recordLength) the cursor values.
        const readouts0 = ch0.readouts
        const readouts1 = ch1.readouts
        const recordLength = controller.recordLength
        const list = []
        for (const entry of controller.measurementCatalog) {
            if ((shownMask & entry.flag) === 0)
                continue
            list.push({ id: "m" + entry.flag, title: entry.label,
                        values: traces.map(t => measurementText(t.index, entry.flag, readouts0, readouts1)) })
        }
        if (cursorsOn) {
            const t1 = controller.cursor1.position
            const t2 = controller.cursor2.position
            const text = (index, v) => controller.formatMeasurement(v, unitOf(index))
            if (controller.cursor1.showValue)
                list.push({ id: "cursor1", title: "C1 · " + controller.formatMeasurement(t1, "s"),
                            values: traces.map(t => text(t.index, valueAt(t.index, t1, recordLength))) })
            if (controller.cursor2.showValue)
                list.push({ id: "cursor2", title: "C2 · " + controller.formatMeasurement(t2, "s"),
                            values: traces.map(t => text(t.index, valueAt(t.index, t2, recordLength))) })
            if (controller.showDeltaTime) {
                list.push({ id: "cursorDelta", title: qsTr("ΔV (C2 − C1)"),
                            values: traces.map(t => text(t.index, valueAt(t.index, t2, recordLength)
                                                                  - valueAt(t.index, t1, recordLength))) })
                const dt = t2 - t1
                list.push({ id: "cursorDeltaT", title: "Δt", values: once(controller.formatMeasurement(dt, "s")) })
                list.push({ id: "cursorRate", title: "1/Δt",
                            values: once(controller.formatMeasurement(Math.abs(dt) > 0 ? 1 / Math.abs(dt) : NaN, "Hz")) })
            }
        }
        if (controller.levelCursorsOn) {
            const index = controller.levelCursorChannel
            const unit = unitOf(index)
            list.push({ id: "level1", title: "H1", values: onlyIn(index, controller.formatMeasurement(controller.levelCursor1, unit)) })
            list.push({ id: "level2", title: "H2", values: onlyIn(index, controller.formatMeasurement(controller.levelCursor2, unit)) })
            list.push({ id: "levelDelta", title: qsTr("ΔH (H2 − H1)"),
                        values: onlyIn(index, controller.formatMeasurement(controller.levelCursor2 - controller.levelCursor1, unit)) })
        }
        return list
    }

    // The "+" menu: every measurement (checked = shown), and the cursor readouts while cursors are on.
    readonly property var available: {
        const list = controller.measurementCatalog.map(e => ({ id: "m" + e.flag, title: e.name }))
        if (cursorsOn) {
            list.push({ id: "cursor1", title: qsTr("Cursor C1 Values") })
            list.push({ id: "cursor2", title: qsTr("Cursor C2 Values") })
            list.push({ id: "cursorDelta", title: qsTr("Cursor Differences (ΔV, Δt, 1/Δt)") })
        }
        return list
    }

    // One line for short windows: the first trace's first measurements.
    readonly property string summaryText: {
        if (traces.length === 0 || rows.length === 0)
            return ""
        return traces[0].title + "  " + rows.slice(0, 3).map(r => r.title + " " + (r.values[0] || "—")).join(" · ")
    }

    function add(id: string) {
        if (id.startsWith("m"))
            controller.setMeasurementShown(Number(id.slice(1)), true)
        else if (id === "cursor1")
            controller.cursor1.showValue = true
        else if (id === "cursor2")
            controller.cursor2.showValue = true
        else if (id === "cursorDelta")
            controller.showDeltaTime = true
    }
    function remove(id: string) {
        if (id.startsWith("m"))
            controller.setMeasurementShown(Number(id.slice(1)), false)
        else if (id === "cursor1")
            controller.cursor1.showValue = false
        else if (id === "cursor2")
            controller.cursor2.showValue = false
        else if (id.startsWith("cursor"))
            controller.showDeltaTime = false
        else if (id.startsWith("level"))
            controller.levelCursorsOn = false
    }
}
