pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Waveform Generator panel. SKELETON: layout and section titles of docs/instruments/gen.md
// (FGEN + ARB + Waveform Editor merged) with placeholder content.
Item {
    id: root

    required property GenController controller

    readonly property var waveforms: [
        { icon: "wave-sine-line", toolTip: qsTr("Sine") },
        { icon: "wave-square-line", toolTip: qsTr("Square") },
        { icon: "wave-triangle-line", toolTip: qsTr("Triangle") }
    ]

    InstrumentPanel {
        anchors.fill: parent
        controller: root.controller
        displayTitle: qsTr("Output")
        displayInfo: root.controller.sampleRate > 0 && root.controller.running
                     ? qsTr("Update Rate %1").arg(Units.format(root.controller.sampleRate, "S/s")) : ""

        // --- Display ---------------------------------------------------------------------
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingM
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.spacingL
                text: root.controller.running ? qsTr("ON") : qsTr("OFF")
                color: root.controller.running ? Theme.success : Theme.textMuted
                font.pixelSize: 56
                font.weight: Font.Light
                font.letterSpacing: 4
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.controller.outputSummary
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeL
                font.features: Theme.tabularFigures
            }
            BenchPlot {
                id: previewPlot
                Layout.fillWidth: true
                Layout.fillHeight: true
                xAxis { unit: "s"; title: qsTr("Time") }
                yAxis { min: -10.5; max: 10.5; unit: "V"; title: qsTr("Amplitude") }
                Component.onCompleted: xAxis.setRange(root.controller.preview0.xMinimum(), root.controller.preview0.xMaximum())
                PlotTrace { buffer: root.controller.preview0; name: "AO 0"; unit: "V"; color: Theme.ch0; width: 2 }
                PlotTrace { buffer: root.controller.preview1; name: "AO 1"; unit: "V"; color: Theme.ch1; width: 2 }
                Connections {
                    target: root.controller.preview0
                    function onChanged() {
                        const b = root.controller.preview0
                        if (b.count > 0)
                            previewPlot.xAxis.setRange(b.xMinimum(), b.xMaximum())
                    }
                }
            }
        }

        // --- Inspector -------------------------------------------------------------------
        inspector: [
            Section {
                title: qsTr("AO 0 · Waveform Settings")
                accentColor: Theme.ch0
                summary: Units.format(root.controller.ch0Frequency, "Hz")
                headerTools: ToggleSwitch {
                    checked: root.controller.ch0Enabled
                    onToggled: root.controller.ch0Enabled = checked
                }
                SegmentedControl {
                    Layout.fillWidth: true
                    model: [qsTr("Function"), qsTr("Arbitrary")]
                    currentIndex: root.controller.ch0Mode
                    onActivated: index => root.controller.ch0Mode = index
                }
                SegmentedControl {
                    Layout.fillWidth: true
                    model: root.waveforms
                    accentColor: Theme.ch0
                    currentIndex: root.controller.ch0Waveform
                    onActivated: index => root.controller.ch0Waveform = index
                }
                RowLayout {
                    spacing: Theme.spacingM
                    Knob {
                        label: qsTr("Frequency")
                        from: 0.2
                        to: 20000
                        logarithmic: true
                        unit: "Hz"
                        minLabel: "200m"
                        maxLabel: "20k"
                        color: Theme.ch0
                        value: root.controller.ch0Frequency
                        onMoved: v => root.controller.ch0Frequency = v
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingS
                        Field {
                            label: qsTr("Frequency")
                            SiField {
                                Layout.fillWidth: true
                                unit: "Hz"
                                from: 0.2
                                to: 20000
                                value: root.controller.ch0Frequency
                                onValueModified: v => root.controller.ch0Frequency = v
                            }
                        }
                        Field {
                            label: qsTr("Amplitude")
                            SiField {
                                Layout.fillWidth: true
                                unit: "Vpp"
                                from: 0
                                to: 20
                                digits: 3
                                value: root.controller.ch0Amplitude
                                onValueModified: v => root.controller.ch0Amplitude = v
                            }
                        }
                    }
                }
                Field {
                    label: qsTr("DC Offset")
                    SiField {
                        Layout.fillWidth: true
                        unit: "V"
                        from: -10
                        to: 10
                        digits: 3
                        value: root.controller.ch0Offset
                        onValueModified: v => root.controller.ch0Offset = v
                    }
                }
            },
            Section {
                title: qsTr("AO 0 · Sweep Settings")
                accentColor: Theme.ch0
                collapsed: true
                FieldRow {
                    Field { label: qsTr("Start Frequency"); SiField { Layout.fillWidth: true; unit: "Hz"; value: 100 } }
                    Field { label: qsTr("Stop Frequency"); SiField { Layout.fillWidth: true; unit: "Hz"; value: 1000 } }
                }
                FieldRow {
                    Field { label: qsTr("Step"); SiField { Layout.fillWidth: true; unit: "Hz"; value: 100 } }
                    Field { label: qsTr("Step Interval"); SiField { Layout.fillWidth: true; unit: "s"; value: 1 } }
                }
            },
            Section {
                title: qsTr("AO 1 · Waveform Settings")
                accentColor: Theme.ch1
                summary: Units.format(root.controller.ch1Frequency, "Hz")
                collapsed: !root.controller.ch1Enabled
                headerTools: ToggleSwitch {
                    checked: root.controller.ch1Enabled
                    onToggled: root.controller.ch1Enabled = checked
                }
                SegmentedControl {
                    Layout.fillWidth: true
                    model: root.waveforms
                    accentColor: Theme.ch1
                    currentIndex: root.controller.ch1Waveform
                    onActivated: index => root.controller.ch1Waveform = index
                }
                FieldRow {
                    Field {
                        label: qsTr("Frequency")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Hz"
                            from: 0.2
                            to: 20000
                            value: root.controller.ch1Frequency
                            onValueModified: v => root.controller.ch1Frequency = v
                        }
                    }
                    Field {
                        label: qsTr("Amplitude")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Vpp"
                            digits: 3
                            value: root.controller.ch1Amplitude
                            onValueModified: v => root.controller.ch1Amplitude = v
                        }
                    }
                }
            },
            Section {
                title: qsTr("Arbitrary Waveform")
                collapsed: true
                Text { text: qsTr("Placeholder: waveform library and Waveform Editor."); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
            },
            Section {
                title: qsTr("Timing and Triggering Settings")
                collapsed: true
                Field { label: qsTr("Update Rate"); SiField { Layout.fillWidth: true; unit: "S/s"; value: 200000 } }
            }
        ]
    }
}
