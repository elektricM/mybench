pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Spectrum Analyzer panel (docs/instruments/dsa.md; ELVISmx Dynamic Signal Analyzer) with the
// 1/3-octave analysis mode (ELVISmx featured app "Octave Analyzer"). Inspector cards keep the
// ELVISmx groups; the Analysis and View cards hold the MYBENCH extensions. View only: every
// setting, conversion and measurement lives in DsaController.
Item {
    id: root

    required property DsaController controller

    readonly property bool octave: controller.octave
    readonly property bool manualScale: controller.scaleMode === DsaController.ManualScale

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.octave
                      ? qsTr("1/3 Octave · %1").arg(root.controller.sourceName)
                      : qsTr("%1 · %2 · %3 span · %4 lines · %5 window")
                            .arg(root.controller.spectrumDisplay === DsaController.PowerSpectralDensityDisplay ? qsTr("Power Spectral Density") : qsTr("Power Spectrum"))
                            .arg(root.controller.sourceName)
                            .arg(Units.format(root.controller.frequencySpan, "Hz", 4))
                            .arg(root.controller.lines)
                            .arg(root.controller.windowLabels[root.controller.windowIndex])
        displayInfo: qsTr("Sample Rate: %1").arg(Units.format(root.controller.running ? root.controller.sampleRate : root.controller.expectedSampleRate, "S/s", 4))

        Item {
            anchors.fill: parent
            DsaSpectrumView {
                anchors.fill: parent
                visible: !root.octave
                controller: root.controller
            }
            DsaOctaveView {
                anchors.fill: parent
                visible: root.octave
                controller: root.controller
            }
        }

        runBarControls: [
            ActionButton {
                text: panel.runBar.compact ? "" : qsTr("Restart")
                iconName: "refresh"
                compact: false
                implicitHeight: Theme.controlHeightL
                enabled: root.controller.restartEnabled
                toolTip: root.octave ? qsTr("Restart the band averaging") : qsTr("Averaging: Restart")
                onClicked: root.controller.restartAveraging()
            }
        ]

        inspector: [
            Section {
                title: qsTr("Analysis")
                iconName: "bar-chart"
                summary: root.octave ? qsTr("1/3 octave") : qsTr("FFT")
                SegmentedControl {
                    Layout.fillWidth: true
                    model: [
                        { text: qsTr("Spectrum (FFT)"), toolTip: qsTr("ELVISmx Dynamic Signal Analyzer") },
                        { text: qsTr("1/3 Octave"), toolTip: qsTr("ELVISmx Octave Analyzer: third-octave bands at 50 kS/s") }
                    ]
                    currentIndex: root.octave ? 1 : 0
                    onActivated: index => root.controller.analysisMode = index === 1 ? DsaController.OctaveAnalysis : DsaController.FftAnalysis
                }
            },
            Section {
                title: qsTr("Input Settings")
                accentColor: Theme.ch0
                summary: root.controller.sourceName
                FieldRow {
                    Field {
                        label: qsTr("Source Channel")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.sourceLabels
                            currentIndex: root.controller.sourceChannel
                            onActivated: index => root.controller.sourceChannel = index
                        }
                    }
                    Field {
                        label: qsTr("Voltage Range")
                        disabled: root.controller.audioSource
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.rangeLabels
                            enabled: !root.controller.audioSource
                            currentIndex: root.controller.effectiveRangeIndex
                            onActivated: index => root.controller.voltageRangeIndex = index
                        }
                    }
                }
            },
            // ------------------------------------------------------------------ FFT mode
            Section {
                visible: !root.octave
                title: qsTr("FFT Settings")
                summary: Units.format(root.controller.frequencySpan, "Hz", 4) + " · " + root.controller.lines
                FieldRow {
                    Field {
                        label: qsTr("Frequency Span")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Hz"
                            from: 10
                            to: root.controller.maximumSpan
                            digits: 5
                            stepSize: 1
                            toolTip: qsTr("10 Hz … %1; sample rate = 2 × span").arg(Units.format(root.controller.maximumSpan, "Hz"))
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
                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1 · df %2 · record %3")
                          .arg(Units.format(root.controller.expectedSampleRate, "S/s", 4))
                          .arg(Units.format(root.controller.lineSpacing, "Hz", 4))
                          .arg(Units.format(2 * root.controller.lines / root.controller.expectedSampleRate, "s", 3))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS + 1
                    font.features: Theme.tabularFigures
                    elide: Text.ElideRight
                }
            },
            Section {
                visible: !root.octave
                title: qsTr("Averaging")
                summary: ["None", "Vector", "RMS", "Peak Hold"][root.controller.averagingMode]
                         + (root.controller.averagesEnabled ? " · " + root.controller.averages : "")
                FieldRow {
                    Field {
                        label: qsTr("Mode")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("None"), qsTr("Vector"), qsTr("RMS"), qsTr("Peak Hold")]
                            currentIndex: root.controller.averagingMode
                            onActivated: index => root.controller.averagingMode = index
                        }
                    }
                    Field {
                        label: qsTr("Weighting")
                        disabled: !root.controller.weightingEnabled
                        ComboBox {
                            Layout.fillWidth: true
                            enabled: root.controller.weightingEnabled
                            model: [qsTr("Linear"), qsTr("Exponential")]
                            currentIndex: root.controller.weighting
                            onActivated: index => root.controller.weighting = index
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("# of Averages")
                        disabled: !root.controller.averagesEnabled
                        SpinBox {
                            Layout.fillWidth: true
                            from: 1
                            to: 10000
                            editable: true
                            enabled: root.controller.averagesEnabled
                            value: root.controller.averages
                            onValueModified: root.controller.averages = value
                        }
                    }
                    Field {
                        label: " "
                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Restart")
                            iconName: "refresh"
                            enabled: root.controller.restartEnabled
                            toolTip: qsTr("Restart the averaging")
                            onClicked: root.controller.restartAveraging()
                        }
                    }
                }
            },
            Section {
                visible: !root.octave
                title: qsTr("Trigger Settings")
                collapsed: true
                summary: qsTr("Immediate")
                FieldRow {
                    Field {
                        label: qsTr("Type")
                        disabled: true
                        ComboBox { Layout.fillWidth: true; enabled: false; model: [qsTr("Immediate"), qsTr("Digital"), qsTr("Edge")] }
                    }
                    Field {
                        label: qsTr("Slope")
                        disabled: true
                        SegmentedControl {
                            Layout.fillWidth: true
                            enabled: false
                            model: [{ icon: "slope-rising-line", toolTip: qsTr("Rising") }, { icon: "slope-falling-line", toolTip: qsTr("Falling") }]
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Source")
                        disabled: true
                        ComboBox { Layout.fillWidth: true; enabled: false; model: [qsTr("TRIG"), qsTr("SYNC"), qsTr("Source Channel")] }
                    }
                    Field {
                        label: qsTr("Level (V)")
                        disabled: true
                        SiField { Layout.fillWidth: true; enabled: false; engineering: false; decimals: 1; unit: "V"; from: -10; to: 10; value: 0 }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Immediate only on NI myDAQ: acquisition starts at once (the TRIG/SYNC inputs exist only on NI ELVIS).")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS + 1
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                visible: !root.octave
                title: qsTr("Frequency Display")
                summary: root.controller.magnitudeUnit
                FieldRow {
                    Field {
                        label: qsTr("Units")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Linear"), qsTr("dB"), qsTr("dBm")]
                            currentIndex: root.controller.units
                            onActivated: index => root.controller.units = index
                        }
                    }
                    Field {
                        label: qsTr("Mode")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("RMS"), qsTr("Peak")]
                            currentIndex: root.controller.magnitudeMode
                            onActivated: index => root.controller.magnitudeMode = index
                        }
                    }
                }
                Field {
                    label: qsTr("Display Mode")
                    ComboBox {
                        Layout.fillWidth: true
                        model: [qsTr("Power Spectrum"), qsTr("Power Spectral Density")]
                        currentIndex: root.controller.spectrumDisplay
                        onActivated: index => root.controller.spectrumDisplay = index
                    }
                }
            },
            Section {
                visible: !root.octave
                title: qsTr("Scale Settings")
                summary: root.manualScale ? qsTr("Manual") : qsTr("Auto")
                Field {
                    label: qsTr("Scale")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Auto"), qsTr("Manual")]
                        currentIndex: root.manualScale ? 1 : 0
                        onActivated: index => root.controller.scaleMode = index === 1 ? DsaController.ManualScale : DsaController.AutoScale
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Maximum")
                        disabled: !root.manualScale
                        SiField {
                            Layout.fillWidth: true
                            engineering: false
                            decimals: 3
                            stepSize: root.controller.logarithmicUnits ? 1 : 0.01
                            enabled: root.manualScale
                            value: root.manualScale ? root.controller.scaleMaximum : root.controller.spectrumMaximum
                            onValueModified: v => root.controller.scaleMaximum = v
                        }
                    }
                    Field {
                        label: qsTr("Minimum")
                        disabled: !root.manualScale
                        SiField {
                            Layout.fillWidth: true
                            engineering: false
                            decimals: 3
                            stepSize: root.controller.logarithmicUnits ? 1 : 0.01
                            enabled: root.manualScale
                            value: root.manualScale ? root.controller.scaleMinimum : root.controller.spectrumMinimum
                            onValueModified: v => root.controller.scaleMinimum = v
                        }
                    }
                }
            },
            Section {
                visible: !root.octave
                title: qsTr("Cursor Settings")
                summary: root.controller.cursorsOn ? qsTr("C%1").arg(root.controller.cursorSelect + 1) : qsTr("Off")
                CheckBox {
                    text: qsTr("Cursors On")
                    checked: root.controller.cursorsOn
                    onToggled: root.controller.cursorsOn = checked
                }
                FieldRow {
                    Field {
                        label: "C1"
                        disabled: !root.controller.cursorsOn
                        ComboBox {
                            Layout.fillWidth: true
                            enabled: root.controller.cursorsOn
                            model: [qsTr("Spectrum"), qsTr("Time")]
                            currentIndex: root.controller.cursor1Plot
                            onActivated: index => root.controller.cursor1Plot = index
                        }
                    }
                    Field {
                        label: "C2"
                        disabled: !root.controller.cursorsOn
                        ComboBox {
                            Layout.fillWidth: true
                            enabled: root.controller.cursorsOn
                            model: [qsTr("Spectrum"), qsTr("Time")]
                            currentIndex: root.controller.cursor2Plot
                            onActivated: index => root.controller.cursor2Plot = index
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Cursor Select")
                        disabled: !root.controller.cursorsOn
                        SegmentedControl {
                            Layout.fillWidth: true
                            enabled: root.controller.cursorsOn
                            model: ["C 1", "C 2"]
                            currentIndex: root.controller.cursorSelect
                            onActivated: index => root.controller.cursorSelect = index
                        }
                    }
                    Field {
                        label: qsTr("Position")
                        disabled: !root.controller.cursorsOn
                        RowLayout {
                            spacing: Theme.spacingXs
                            IconButton {
                                iconName: "chevron-left"
                                bordered: true
                                enabled: root.controller.cursorsOn
                                toolTip: qsTr("Left: one line / sample")
                                onClicked: root.controller.stepCursor(-1)
                            }
                            IconButton {
                                iconName: "chevron-right"
                                bordered: true
                                enabled: root.controller.cursorsOn
                                toolTip: qsTr("Right: one line / sample")
                                onClicked: root.controller.stepCursor(1)
                            }
                        }
                    }
                }
                Field {
                    label: qsTr("Peak search")
                    disabled: !root.controller.cursorsOn || !root.controller.hasResult
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        component PeakButton: ActionButton {
                            Layout.fillWidth: true
                            compact: true
                            enabled: root.controller.cursorsOn && root.controller.hasResult
                        }
                        PeakButton { text: qsTr("Peak"); toolTip: qsTr("Selected cursor to the highest peak"); onClicked: root.controller.searchPeak(DsaController.HighestPeak) }
                        PeakButton { text: qsTr("Next"); toolTip: qsTr("Next lower peak"); onClicked: root.controller.searchPeak(DsaController.NextPeak) }
                        PeakButton { text: "◂"; toolTip: qsTr("Nearest peak on the left"); onClicked: root.controller.searchPeak(DsaController.PeakLeft) }
                        PeakButton { text: "▸"; toolTip: qsTr("Nearest peak on the right"); onClicked: root.controller.searchPeak(DsaController.PeakRight) }
                    }
                }
            },
            Section {
                visible: !root.octave
                title: qsTr("View")
                iconName: "layers"
                collapsed: false
                summary: ""
                ToggleSwitch {
                    text: qsTr("Harmonic markers (F, H2 … H10)")
                    checked: root.controller.harmonicMarkers
                    onToggled: root.controller.harmonicMarkers = checked
                }
                ToggleSwitch {
                    text: qsTr("Harmonics and peaks tables")
                    checked: root.controller.analysisTables
                    onToggled: root.controller.analysisTables = checked
                }
                ToggleSwitch {
                    text: qsTr("Max hold")
                    checked: root.controller.maxHold
                    onToggled: root.controller.maxHold = checked
                }
                ToggleSwitch {
                    text: qsTr("Min hold")
                    checked: root.controller.minHold
                    onToggled: root.controller.minHold = checked
                }
                ToggleSwitch {
                    text: qsTr("Logarithmic frequency axis")
                    checked: root.controller.logFrequency
                    onToggled: root.controller.logFrequency = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    ToggleSwitch {
                        text: qsTr("Spectrogram")
                        checked: root.controller.waterfallVisible
                        onToggled: root.controller.waterfallVisible = checked
                    }
                    Item { Layout.fillWidth: true }
                    SpinBox {
                        Layout.preferredWidth: 110
                        enabled: root.controller.waterfallVisible
                        from: 50
                        to: 1000
                        stepSize: 50
                        value: root.controller.waterfallHistory
                        onValueModified: root.controller.waterfallHistory = value
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: qsTr("Spectra kept in the spectrogram")
                    }
                }
            },
            // ------------------------------------------------------------------ Octave mode
            Section {
                visible: root.octave
                title: qsTr("Octave Settings")
                summary: ["Slow", "Fast", "Impulse", "Custom"][root.controller.octaveAveraging]
                Field {
                    label: qsTr("Mode (time weighting)")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [
                            { text: qsTr("Slow"), toolTip: qsTr("1 s") },
                            { text: qsTr("Fast"), toolTip: qsTr("125 ms") },
                            { text: qsTr("Impulse"), toolTip: qsTr("35 ms rise, 1.5 s decay") },
                            { text: qsTr("Custom"), toolTip: qsTr("Time Constant below") }
                        ]
                        currentIndex: root.controller.octaveAveraging
                        onActivated: index => root.controller.octaveAveraging = index
                    }
                }
                Field {
                    label: qsTr("Time Constant")
                    disabled: root.controller.octaveAveraging !== DsaController.CustomAveraging
                    SiField {
                        Layout.fillWidth: true
                        engineering: false
                        decimals: 0
                        unit: "ms"
                        from: 0
                        to: 10000
                        stepSize: 1
                        enabled: root.controller.octaveAveraging === DsaController.CustomAveraging
                        value: root.controller.octaveTimeConstant
                        onValueModified: v => root.controller.octaveTimeConstant = v
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Low Band")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.octaveBandLabels
                            currentIndex: root.controller.octaveLowBand
                            onActivated: index => root.controller.octaveLowBand = index
                        }
                    }
                    Field {
                        label: qsTr("High Band")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.octaveBandLabels
                            currentIndex: root.controller.octaveHighBand
                            onActivated: index => root.controller.octaveHighBand = index
                        }
                    }
                }
                Field {
                    label: qsTr("Weighting")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [
                            { text: qsTr("Z"), toolTip: qsTr("None (Linear)") },
                            { text: "A" }, { text: "B" }, { text: "C" }
                        ]
                        currentIndex: root.controller.octaveWeighting
                        onActivated: index => root.controller.octaveWeighting = index
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("50 kS/s, blocks of 1000 samples. The lowest band needs about %1 to settle.")
                          .arg(Units.format(root.controller.octaveSettlingTime, "s", 2))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS + 1
                    wrapMode: Text.WordWrap
                }
            }
        ]
    }
}
