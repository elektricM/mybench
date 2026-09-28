pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Multimeter panel. SKELETON: layout and section titles of docs/instruments/dmm.md with
// placeholder content; the dmm agent fills it in.
Item {
    id: root

    required property DmmController controller

    readonly property var functions: [
        { icon: "dmm-vdc", toolTip: qsTr("DC Voltage") },
        { icon: "dmm-vac", toolTip: qsTr("AC Voltage") },
        { icon: "dmm-adc", toolTip: qsTr("DC Current") },
        { icon: "dmm-aac", toolTip: qsTr("AC Current") },
        { icon: "dmm-ohm", toolTip: qsTr("Resistance") },
        { icon: "dmm-diode-line", toolTip: qsTr("Diode") },
        { icon: "dmm-continuity-line", toolTip: qsTr("Continuity") },
        { icon: "dmm-capacitance-line", toolTip: qsTr("Capacitance (NI ELVIS only)"), enabled: false },
        { icon: "dmm-inductance-line", toolTip: qsTr("Inductance (NI ELVIS only)"), enabled: false }
    ]

    InstrumentPanel {
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.controller.functionName
        displayInfo: root.controller.rangeLabels[root.controller.rangeIndex] ?? ""

        // --- Display ---------------------------------------------------------------------
        BenchPlot {
            id: trendPlot
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height * 0.36
            placeholderText: qsTr("Trend")
            xAxis { labelFormat: PlotAxis.Time; follow: true }
            Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
            yAxis { autoScale: true; unit: root.controller.unit }
            PlotTrace { buffer: root.controller.trend; unit: root.controller.unit; color: Theme.ch0; pointsVisible: true }
        }
        Readout {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -trendPlot.height / 2
            width: Math.min(parent.width - 48, 640)
            label: root.controller.functionName
            value: root.controller.reading
            unit: root.controller.unit
            digits: 4
            fontSize: 72
            overload: root.controller.overload
            stale: !root.controller.running
            caption: (root.controller.autoRange ? qsTr("Auto range") : qsTr("Range %1").arg(root.controller.rangeLabels[root.controller.rangeIndex] ?? ""))
                     + (root.controller.nullOffset ? qsTr(" · Null offset") : "")
            showBar: true
            barBipolar: root.controller.selectedFunction <= DmmController.AcCurrent
            barValue: root.controller.rangeMax > 0 ? root.controller.reading / root.controller.rangeMax : 0
        }

        // --- Inspector -------------------------------------------------------------------
        inspector: [
            Section {
                title: qsTr("Measurement Settings")
                SegmentedControl {
                    Layout.fillWidth: true
                    model: root.functions
                    currentIndex: root.controller.selectedFunction
                    onActivated: index => root.controller.selectedFunction = index
                }
                Field {
                    label: qsTr("Mode")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Auto"), qsTr("Specify Range")]
                        currentIndex: root.controller.autoRange ? 0 : 1
                        onActivated: index => root.controller.autoRange = index === 0
                    }
                }
                Field {
                    label: qsTr("Range")
                    ComboBox {
                        Layout.fillWidth: true
                        enabled: !root.controller.autoRange
                        model: root.controller.rangeLabels
                        currentIndex: root.controller.rangeIndex
                        onActivated: index => root.controller.rangeIndex = index
                    }
                }
                ToggleSwitch {
                    text: qsTr("Null Offset")
                    checked: root.controller.nullOffset
                    onToggled: root.controller.nullOffset = checked
                }
            },
            Section {
                title: qsTr("Banana Jack Connections")
                summary: qsTr("HI · COM")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Connect the probes to the V/Ω HI and COM jacks (A HI for current).")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeM
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                title: qsTr("Statistics")
                collapsed: true
                summary: qsTr("min · max · avg · σ")
                Text {
                    text: qsTr("Placeholder: min / max / average / σ, Hold, Relative.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeM
                }
            },
            Section {
                title: qsTr("Trend & Logging")
                collapsed: true
                Text {
                    text: qsTr("Placeholder: trend chart and logging.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeM
                }
            }
        ]
    }
}
