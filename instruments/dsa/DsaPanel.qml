pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Spectrum Analyzer panel. SKELETON: layout and section titles of docs/instruments/dsa.md with
// live spectrum and time plots (controller TraceBuffers); the dsa agent completes it.
Item {
    id: root

    required property DsaController controller

    InstrumentPanel {
        anchors.fill: parent
        controller: root.controller
        displayTitle: qsTr("Power Spectrum")
        displayInfo: root.controller.sampleRate > 0 ? qsTr("Sample Rate %1").arg(Units.format(root.controller.sampleRate, "S/s")) : ""

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingS
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXl
                Readout { alignment: Qt.AlignLeft; label: qsTr("Detected Fundamental Frequency"); value: root.controller.fundamentalFrequency; unit: "Hz"; fontSize: 20; color: Theme.ch0 }
                Readout { alignment: Qt.AlignLeft; label: qsTr("Fundamental Frequency Power"); value: root.controller.fundamentalPower; unit: "Vrms²"; fontSize: 20 }
                Item { Layout.fillWidth: true }
            }
            BenchPlot {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 3
                placeholderText: root.controller.running ? "" : qsTr("Press Run to acquire")
                xAxis { min: 0; max: root.controller.frequencySpan; unit: "Hz"; title: qsTr("Frequency") }
                yAxis { min: -120; max: 10; unit: "dBV"; title: qsTr("Magnitude") }
                PlotTrace {
                    id: spectrumTrace
                    buffer: root.controller.spectrum
                    name: qsTr("Power Spectrum")
                    unit: "dBV"
                    color: Theme.ch0
                    fill: true
                    fillOpacity: 0.18
                }
                PlotMarker {
                    visible: root.controller.fundamentalFrequency > 0
                    x: root.controller.fundamentalFrequency
                    y: NaN
                    trace: spectrumTrace
                    label: Units.format(root.controller.fundamentalFrequency, "Hz")
                    color: Theme.math
                    shape: PlotMarker.TriangleDown
                }
            }
            BenchPlot {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 1
                xAxis { unit: "s"; title: qsTr("Time"); autoScale: true; autoScaleMargin: 0 }
                yAxis { autoScale: true; unit: "V" }
                PlotTrace { buffer: root.controller.timeSignal; unit: "V"; color: Theme.ch1 }
            }
        }

        inspector: [
            Section {
                title: qsTr("Input Settings")
                FieldRow {
                    Field {
                        label: qsTr("Source Channel")
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["AI 0", "AI 1"]
                            currentIndex: root.controller.sourceChannel
                            onActivated: index => root.controller.sourceChannel = index
                        }
                    }
                    Field { label: qsTr("Voltage Range"); ComboBox { Layout.fillWidth: true; model: ["±10 V", "±2 V"] } }
                }
            },
            Section {
                title: qsTr("FFT Settings")
                FieldRow {
                    Field {
                        label: qsTr("Frequency Span")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Hz"
                            from: 10
                            to: 80000
                            value: root.controller.frequencySpan
                            onValueModified: v => root.controller.frequencySpan = v
                        }
                    }
                    Field {
                        label: qsTr("Resolution (lines)")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.resolutionLabels
                            currentIndex: root.controller.resolutionIndex
                            onActivated: index => root.controller.resolutionIndex = index
                        }
                    }
                }
                Field {
                    label: qsTr("Window")
                    ComboBox {
                        Layout.fillWidth: true
                        model: root.controller.windowLabels
                        currentIndex: root.controller.windowIndex
                        onActivated: index => root.controller.windowIndex = index
                    }
                }
            },
            Section {
                title: qsTr("Averaging")
                FieldRow {
                    Field { label: qsTr("Mode"); ComboBox { Layout.fillWidth: true; model: [qsTr("None"), qsTr("Vector"), qsTr("RMS"), qsTr("Peak Hold")]; currentIndex: 2 } }
                    Field { label: qsTr("Weighting"); ComboBox { Layout.fillWidth: true; model: [qsTr("Linear"), qsTr("Exponential")]; currentIndex: 1 } }
                }
                Field { label: qsTr("# of Averages"); SpinBox { Layout.fillWidth: true; from: 1; to: 1000; value: 5 } }
            },
            Section {
                title: qsTr("Trigger Settings")
                collapsed: true
                Field { label: qsTr("Type"); ComboBox { Layout.fillWidth: true; model: [qsTr("Immediate")]; enabled: false } }
            },
            Section {
                title: qsTr("Frequency Display")
                collapsed: true
                FieldRow {
                    Field { label: qsTr("Units"); ComboBox { Layout.fillWidth: true; model: ["dB", "Linear"] } }
                    Field { label: qsTr("Mode"); ComboBox { Layout.fillWidth: true; model: ["RMS", "Peak"] } }
                }
            },
            Section {
                title: qsTr("Scale Settings")
                collapsed: true
                Field { label: qsTr("Scale"); ComboBox { Layout.fillWidth: true; model: [qsTr("Auto"), qsTr("Manual")] } }
            },
            Section {
                title: qsTr("Cursor Settings")
                collapsed: true
                CheckBox { text: qsTr("Cursors On") }
            }
        ]
    }
}
