pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Oscilloscope panel. SKELETON: layout and section titles of docs/instruments/scope.md; a live
// scope plot fed by the controller's TraceBuffers; the scope agent completes it.
Item {
    id: root

    required property ScopeController controller

    InstrumentPanel {
        anchors.fill: parent
        controller: root.controller
        displayTitle: (root.controller.ch0Enabled ? "CH 0  " : "") + (root.controller.ch1Enabled ? "CH 1  " : "")
                      + root.controller.timeDivLabels[root.controller.timeDivIndex] + "/div"
        displayInfo: root.controller.sampleRate > 0 ? qsTr("Sample Rate %1").arg(Units.format(root.controller.sampleRate, "S/s")) : ""
        runBar.showTimeout: true

        // --- Display ---------------------------------------------------------------------
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingS
            BenchPlot {
                id: plot
                Layout.fillWidth: true
                Layout.fillHeight: true
                graticule: Plot.Scope
                placeholderText: root.controller.running ? "" : qsTr("Press Run to acquire")
                xAxis { min: 0; max: 10 * root.controller.timePerDiv; divisions: 10; unit: "s"; unitInLabels: true }
                yAxis { min: -5; max: 5; divisions: 10; labelsVisible: false; interactive: false }
                PlotTrace {
                    id: trace0
                    buffer: root.controller.ch0
                    visible: root.controller.ch0Enabled
                    name: "CH 0"
                    unit: "V"
                    color: Theme.ch0
                    yScale: 1 / root.controller.voltsPerDiv0
                }
                PlotTrace {
                    buffer: root.controller.ch1
                    visible: root.controller.ch1Enabled
                    name: "CH 1"
                    unit: "V"
                    color: Theme.ch1
                    yScale: 1 / root.controller.voltsPerDiv1
                }
                PlotLine {
                    visible: root.controller.ch0Enabled
                    value: 0
                    handle: PlotLine.Start
                    label: "0"
                    color: Theme.ch0
                    lineMode: PlotLine.Never
                    draggable: false
                }
                PlotLine {
                    visible: root.controller.triggerType === ScopeController.Edge
                    trace: trace0
                    value: root.controller.triggerLevel
                    handle: PlotLine.End
                    label: "T"
                    color: Theme.math
                    onMoved: v => root.controller.triggerLevel = v
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                ChannelChip {
                    text: "CH 0"
                    color: Theme.ch0
                    checked: root.controller.ch0Enabled
                    detail: "RMS " + Units.format(root.controller.ch0Rms, "V", 3) + "   Freq " + Units.format(root.controller.ch0Frequency, "Hz", 4)
                            + "   Vp-p " + Units.format(root.controller.ch0PeakToPeak, "V", 3)
                    onToggled: root.controller.ch0Enabled = checked
                }
                ChannelChip {
                    text: "CH 1"
                    color: Theme.ch1
                    checked: root.controller.ch1Enabled
                    detail: root.controller.ch1Enabled ? "RMS " + Units.format(root.controller.ch1Rms, "V", 3) : ""
                    onToggled: root.controller.ch1Enabled = checked
                }
                Item { Layout.fillWidth: true }
            }
        }

        // --- Inspector -------------------------------------------------------------------
        inspector: [
            Section {
                title: qsTr("Channel 0 Settings")
                accentColor: Theme.ch0
                summary: "AI 0 · " + root.controller.voltsDivLabels[root.controller.voltsDivIndex0] + "/div"
                headerTools: ToggleSwitch {
                    checked: root.controller.ch0Enabled
                    onToggled: root.controller.ch0Enabled = checked
                }
                FieldRow {
                    Field { label: qsTr("Source"); ComboBox { Layout.fillWidth: true; model: ["AI 0", "AudioInput Left"] } }
                    Field { label: qsTr("Coupling"); ComboBox { Layout.fillWidth: true; model: ["DC"]; enabled: false } }
                }
                RowLayout {
                    spacing: Theme.spacingL
                    Knob {
                        label: qsTr("Volts/Div")
                        values: root.controller.voltsDivLabels
                        labels: root.controller.voltsDivLabels
                        currentIndex: root.controller.voltsDivIndex0
                        color: Theme.ch0
                        onIndexMoved: index => root.controller.voltsDivIndex0 = index
                    }
                    Knob {
                        label: qsTr("Vertical Position")
                        from: -5
                        to: 5
                        value: 0
                        unit: "div"
                        color: Theme.ch0
                    }
                }
            },
            Section {
                title: qsTr("Channel 1 Settings")
                accentColor: Theme.ch1
                summary: "AI 1 · " + root.controller.voltsDivLabels[root.controller.voltsDivIndex1] + "/div"
                collapsed: !root.controller.ch1Enabled
                headerTools: ToggleSwitch {
                    checked: root.controller.ch1Enabled
                    onToggled: root.controller.ch1Enabled = checked
                }
                Field {
                    label: qsTr("Volts/Div")
                    ComboBox {
                        Layout.fillWidth: true
                        model: root.controller.voltsDivLabels
                        currentIndex: root.controller.voltsDivIndex1
                        onActivated: index => root.controller.voltsDivIndex1 = index
                    }
                }
            },
            Section {
                title: qsTr("Timebase")
                summary: root.controller.timeDivLabels[root.controller.timeDivIndex] + "/div"
                RowLayout {
                    spacing: Theme.spacingL
                    Knob {
                        label: qsTr("Time/Div")
                        values: root.controller.timeDivLabels
                        labels: root.controller.timeDivLabels
                        currentIndex: root.controller.timeDivIndex
                        onIndexMoved: index => root.controller.timeDivIndex = index
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        model: root.controller.timeDivLabels
                        currentIndex: root.controller.timeDivIndex
                        onActivated: index => root.controller.timeDivIndex = index
                    }
                }
            },
            Section {
                title: qsTr("Trigger")
                summary: root.controller.triggerType === ScopeController.Edge ? qsTr("Edge") : qsTr("Immediate")
                FieldRow {
                    Field {
                        label: qsTr("Type")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Immediate"), qsTr("Edge")]
                            currentIndex: root.controller.triggerType
                            onActivated: index => root.controller.triggerType = index
                        }
                    }
                    Field {
                        label: qsTr("Slope")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [{ icon: "slope-rising-line", toolTip: qsTr("Rising") },
                                    { icon: "slope-falling-line", toolTip: qsTr("Falling") }]
                        }
                    }
                }
                Field {
                    label: qsTr("Level")
                    SiField {
                        Layout.fillWidth: true
                        unit: "V"
                        from: -10
                        to: 10
                        digits: 3
                        value: root.controller.triggerLevel
                        onValueModified: v => root.controller.triggerLevel = v
                    }
                }
            },
            Section {
                title: qsTr("Cursors Settings")
                collapsed: true
                CheckBox { text: qsTr("Cursors On") }
            },
            Section {
                title: qsTr("Display Measurements")
                collapsed: true
                RowLayout {
                    CheckBox { text: "CH 0"; checked: true }
                    CheckBox { text: "CH 1" }
                }
            },
            Section {
                title: qsTr("Math")
                accentColor: Theme.math
                collapsed: true
                Text { text: qsTr("Placeholder: CH0 ± CH1, ×, FFT, XY."); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
            }
        ]

        runBarControls: [
            ActionButton { text: qsTr("Autoscale"); iconName: "autoscale"; implicitHeight: Theme.controlHeightL }
        ]
    }
}
