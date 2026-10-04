pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

GalleryPage {
    id: page

    title: qsTr("Readouts and Tables")
    description: qsTr("Values always carry their unit, a fixed number of significant digits and tabular figures, so they never change width. Over range reads \"OL\"; the last value of a stopped instrument is dimmed. Right-click a value to copy it; the tooltip shows the full precision.")

    readonly property InstrumentController demo: InstrumentRegistry.controller("demo")
    readonly property real reading: demo && demo.running ? (demo as DemoController).reading : 1.23456

    property var shownMeasurements: ["rms", "freq", "vpp"]
    readonly property var allMeasurements: [{ id: "rms", title: qsTr("RMS") }, { id: "freq", title: qsTr("Frequency") },
                                            { id: "vpp", title: qsTr("Peak-to-peak") }, { id: "mean", title: qsTr("Mean") },
                                            { id: "period", title: qsTr("Period") }, { id: "rise", title: qsTr("Rise time") }]
    readonly property var sampleValues: ({
        rms: ["707.1 mV", "353.6 mV"], freq: ["1.0000 kHz", "1.0000 kHz"], vpp: ["2.000 V", "1.000 V"],
        mean: ["1.2 mV", "−0.4 mV"], period: ["1.0000 ms", "1.0000 ms"], rise: ["", "61.8 µs"]
    })

    GallerySpec {
        title: qsTr("Readout sizes")
        note: qsTr("xl for the multimeter's main reading, l for a panel's primary value, m in tiles and side panels, s in dense lists. Labels are sentence case; a channel swatch replaces colored values.")
        RowLayout {
            spacing: Theme.spacingXl * 2
            Readout { size: "xl"; label: qsTr("DC voltage"); value: page.reading; unit: "V"; digits: 6; showBar: true; barValue: page.reading / 10; caption: qsTr("Range 10 V · Auto") }
            Readout { size: "l"; label: qsTr("Frequency"); value: 1000.04; unit: "Hz"; digits: 6 }
            Readout { size: "m"; label: "RMS"; swatchColor: Theme.signalCh0; value: 0.70711; unit: "V" }
            Readout { size: "s"; label: qsTr("Period"); swatchColor: Theme.signalCh1; value: 0.001; unit: "s" }
        }
        RowLayout {
            spacing: Theme.spacingXl * 2
            Readout { size: "l"; label: qsTr("Resistance"); overload: true; unit: "Ω" }
            Readout { size: "l"; label: qsTr("DC current"); value: 0.01234; unit: "A"; hold: true }
            Readout { size: "l"; label: qsTr("AC voltage"); value: 0.3536; unit: "V"; stale: true; caption: qsTr("Stopped") }
            Readout { size: "m"; label: qsTr("Duty cycle"); valueText: "50.0 %"; showBar: true; barValue: 0.5; barBipolar: false; barLabel: "%" }
        }
    }

    GallerySpec {
        title: qsTr("Measurement table")
        note: qsTr("Under a display: one row per measurement, one column per channel. + adds or removes measurements; a row's context menu copies or removes it. Below 600 px of window height the strip collapses to one summary line.")
        Rectangle {
            Layout.preferredWidth: 640
            implicitHeight: measurements.implicitHeight
            color: Theme.window
            border.width: Theme.borderWidth
            border.color: Theme.separator
            MeasurementTable {
                id: measurements
                anchors.fill: parent
                columns: [{ title: "CH0", color: Theme.signalCh0 }, { title: "CH1", color: Theme.signalCh1 }]
                rows: page.shownMeasurements.map(id => ({ id: id, title: page.allMeasurements.find(m => m.id === id).title,
                                                          values: page.sampleValues[id] }))
                available: page.allMeasurements
                onAddRequested: id => page.shownMeasurements = page.shownMeasurements.concat([id])
                onRemoveRequested: id => page.shownMeasurements = page.shownMeasurements.filter(m => m !== id)
            }
        }
        Rectangle {
            Layout.preferredWidth: 640
            implicitHeight: collapsedTable.implicitHeight
            color: Theme.window
            border.width: Theme.borderWidth
            border.color: Theme.separator
            MeasurementTable {
                id: collapsedTable
                anchors.fill: parent
                collapsed: true
                columns: measurements.columns
                rows: measurements.rows
            }
        }
    }

    GallerySpec {
        title: qsTr("Statistics table")
        note: qsTr("Running statistics per channel with Reset in the header (multimeter, data logger).")
        Rectangle {
            Layout.preferredWidth: 520
            implicitHeight: statistics.implicitHeight
            color: Theme.window
            border.width: Theme.borderWidth
            border.color: Theme.separator
            StatisticsTable {
                id: statistics
                anchors.fill: parent
                columns: [{ title: qsTr("DC voltage"), unit: "V", count: 1284, minimum: 1.23401, maximum: 1.23519,
                            peakToPeak: 0.00118, mean: 1.234567, stdDev: 0.000214, overloads: 0 },
                          { title: "AI 1", color: Theme.signalCh1, unit: "V", count: 1284, minimum: -0.0021, maximum: 0.0034,
                            peakToPeak: 0.0055, mean: 0.0004, stdDev: 0.0011, overloads: 2 }]
            }
        }
    }
}
