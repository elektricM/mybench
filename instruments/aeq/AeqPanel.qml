pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Audio Equalizer panel (docs/instruments/aeq.md): tone response of the three bands, the
// input/output spectrum in 1/24-octave bands (or the waveforms), and a console with the band
// faders, volume and level meters.
Item {
    id: root

    required property AeqController controller

    readonly property color bassColor: Theme.ref
    readonly property color midColor: Theme.math
    readonly property color trebleColor: Theme.cursor2
    readonly property real topFrequency: Math.min(25000, 0.49 * controller.streamRate)
    readonly property bool wide: width >= 980

    function meter(i: int): var {
        const m = root.controller.meters
        return i < m.length ? m[i] : { name: "", peak: -120, rms: -120, hold: -120, active: false }
    }
    function signedDb(v: real): string {
        return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(v % 1 === 0 ? 0 : 1) + " dB"
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.controller.bypass ? qsTr("Bypass") : qsTr("Bass %1 · Mid %2 · Treble %3")
                                                                 .arg(root.signedDb(root.controller.bass))
                                                                 .arg(root.signedDb(root.controller.midtone))
                                                                 .arg(root.signedDb(root.controller.treble))
        displayInfo: qsTr("%1 · latency %2").arg(Units.format(root.controller.streamRate, "S/s", 3))
                                            .arg(Units.format(root.controller.latencySeconds + root.controller.blockSeconds, "s", 3))
                     + (root.controller.dropouts > 0 ? "  ·  " + qsTr("%n dropout(s)", "", root.controller.dropouts) : "")
        displayTools: [
            SegmentedControl {
                compact: true
                model: [qsTr("Spectrum"), qsTr("Time")]
                currentIndex: root.controller.view
                onActivated: index => root.controller.view = index
            },
            SegmentedControl {
                compact: true
                model: [{ text: "L", toolTip: qsTr("Left channel") }, { text: "R", toolTip: qsTr("Right channel") }]
                currentIndex: root.controller.displayChannel
                onActivated: index => root.controller.displayChannel = index
            }
        ]
        runBarControls: [
            ActionButton {
                text: qsTr("Bypass")
                compact: true
                tone: root.controller.bypass ? "subtle" : "neutral"
                color: Theme.warning
                toolTip: qsTr("Pass the input through without equalization (volume still applies)")
                onClicked: root.controller.bypass = !root.controller.bypass
            },
            ActionButton {
                text: qsTr("Flat")
                compact: true
                iconName: "reset"
                toolTip: qsTr("Bass, Midtone and Treble to 0 dB")
                onClicked: root.controller.flat()
            }
        ]

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            // Hints: invalid routing, simulator feedback path.
            Rectangle {
                id: hint
                visible: root.controller.routingError.length > 0 || root.controller.feedbackInSimulator
                Layout.fillWidth: true
                implicitHeight: hintRow.implicitHeight + 2 * Theme.spacingS
                radius: Theme.radiusM
                readonly property color tone: root.controller.routingError.length > 0 ? Theme.danger : Theme.simulated
                color: Theme.alpha(tone, Theme.dark ? 0.10 : 0.07)
                border.color: Theme.alpha(tone, 0.5)
                RowLayout {
                    id: hintRow
                    anchors.fill: parent
                    anchors.margins: Theme.spacingS
                    anchors.leftMargin: Theme.spacingM
                    spacing: Theme.spacingS
                    Icon {
                        name: root.controller.routingError.length > 0 ? "error" : "info"
                        color: hint.tone
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.controller.routingError.length > 0 ? root.controller.routingError
                              : qsTr("Simulator: the outputs are wired back to the chosen inputs, so the equalizer hears its own output (silence). Use AI 0 / AI 1, which carry test signals.")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeM
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        visible: root.controller.routingError.length === 0
                        text: qsTr("Use AI 0 / AI 1")
                        compact: true
                        onClicked: root.controller.useAnalogInputs()
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: root.wide ? 2 : 1
                columnSpacing: Theme.spacingM
                rowSpacing: Theme.spacingS

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: Theme.spacingS

                    // Tone response of the equalizer and its bands.
                    BenchPlot {
                        id: responsePlot
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(120, root.height * 0.24)
                        interactive: false
                        leftMargin: Math.max(implicitLeftMargin, spectrumPlot.implicitLeftMargin)
                        xAxis { scale: PlotAxis.Log; min: 20; max: root.topFrequency; unit: "Hz"; labelsVisible: false }
                        yAxis { min: -18; max: 18; divisions: 6; unit: "dB"; unitInLabels: true }
                        PlotRegion { from: 20; to: 320; color: Theme.alpha(root.bassColor, 0.07); label: qsTr("Bass"); edgesVisible: false }
                        PlotRegion { from: 320; to: 2560; color: Theme.alpha(root.midColor, 0.07); label: qsTr("Midtone"); edgesVisible: false }
                        PlotRegion { from: 2560; to: Math.min(20840, root.topFrequency); color: Theme.alpha(root.trebleColor, 0.07); label: qsTr("Treble"); edgesVisible: false }
                        PlotTrace { buffer: root.controller.responseBass; color: Theme.alpha(root.bassColor, 0.8); width: 1; visible: !root.controller.bypass }
                        PlotTrace { buffer: root.controller.responseMid; color: Theme.alpha(root.midColor, 0.8); width: 1; visible: !root.controller.bypass }
                        PlotTrace { buffer: root.controller.responseTreble; color: Theme.alpha(root.trebleColor, 0.8); width: 1; visible: !root.controller.bypass }
                        PlotTrace { buffer: root.controller.response; name: qsTr("Tone"); unit: "dB"; color: Theme.text; width: 2.2 }
                        PlotLine { value: 0; color: Theme.alpha(Theme.textMuted, 0.6); style: PlotElement.Dot; handle: PlotLine.NoHandle; draggable: false }
                    }

                    // Spectrum (1/24 octave) or waveforms.
                    BenchPlot {
                        id: spectrumPlot
                        visible: root.controller.view === AeqController.SpectrumView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 140
                        leftMargin: responsePlot.leftMargin
                        hoverReadout: true
                        legendVisible: true
                        placeholderText: root.controller.running ? qsTr("Collecting…") : qsTr("Press Run to stream the input to the output")
                        xAxis { scale: PlotAxis.Log; min: 20; max: root.topFrequency; unit: "Hz"; unitInLabels: true }
                        yAxis { min: -110; max: 10; unit: "dBV"; unitInLabels: true }
                        PlotTrace {
                            visible: root.controller.showInput
                            name: qsTr("Input")
                            unit: "dBV"
                            buffer: root.controller.spectrumIn
                            color: Theme.alpha(Theme.textSecondary, 0.8)
                            width: 1
                            step: true
                            fill: true
                            fillOpacity: 0.12
                        }
                        PlotTrace {
                            name: qsTr("Output")
                            unit: "dBV"
                            buffer: root.controller.spectrumOut
                            color: Theme.accent
                            width: 1.5
                            step: true
                            fill: true
                            fillOpacity: 0.18
                        }
                    }
                    BenchPlot {
                        visible: root.controller.view === AeqController.TimeView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 140
                        leftMargin: responsePlot.leftMargin
                        hoverReadout: true
                        legendVisible: true
                        placeholderText: root.controller.running ? qsTr("Collecting…") : qsTr("Press Run to stream the input to the output")
                        xAxis { min: 0; max: 0.02; unit: "s"; unitInLabels: true }
                        yAxis { autoScale: true; min: -1; max: 1; unit: "V"; unitInLabels: true }
                        PlotTrace { visible: root.controller.showInput; name: qsTr("Input"); unit: "V"; buffer: root.controller.timeIn; color: Theme.alpha(Theme.textSecondary, 0.9); width: 1.2 }
                        PlotTrace { name: qsTr("Output"); unit: "V"; buffer: root.controller.timeOut; color: Theme.accent; width: 1.6 }
                    }
                }

                // Console: band faders, volume, meters.
                Rectangle {
                    Layout.fillHeight: root.wide
                    Layout.fillWidth: !root.wide
                    Layout.preferredWidth: consoleRow.implicitWidth + 2 * Theme.spacingM
                    Layout.preferredHeight: root.wide ? -1 : 210
                    radius: Theme.radiusL
                    color: "transparent"
                    border.color: Theme.displayBorder

                    RowLayout {
                        id: consoleRow
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingXs

                        AeqFader {
                            Layout.fillHeight: true
                            label: qsTr("Bass")
                            accent: root.bassColor
                            value: root.controller.bass
                            enabled: !root.controller.bypass
                            onMoved: v => root.controller.bass = v
                        }
                        AeqFader {
                            Layout.fillHeight: true
                            label: qsTr("Midtone")
                            accent: root.midColor
                            value: root.controller.midtone
                            enabled: !root.controller.bypass
                            onMoved: v => root.controller.midtone = v
                        }
                        AeqFader {
                            Layout.fillHeight: true
                            label: qsTr("Treble")
                            accent: root.trebleColor
                            value: root.controller.treble
                            enabled: !root.controller.bypass
                            onMoved: v => root.controller.treble = v
                        }
                        Rectangle { Layout.fillHeight: true; Layout.topMargin: 20; Layout.bottomMargin: 20; Layout.leftMargin: 2; Layout.rightMargin: 2; implicitWidth: 1; color: Theme.displayBorder }
                        AeqFader {
                            Layout.fillHeight: true
                            label: qsTr("Volume")
                            from: 0
                            to: 1
                            stepSize: 0.01
                            resetValue: 0.5
                            centered: false
                            accent: Theme.accent
                            value: root.controller.volume
                            valueText: Math.round(value * 100) + " %"
                            onMoved: v => root.controller.volume = v
                        }
                        Rectangle { Layout.fillHeight: true; Layout.topMargin: 20; Layout.bottomMargin: 20; Layout.leftMargin: 2; Layout.rightMargin: 2; implicitWidth: 1; color: Theme.displayBorder }
                        Repeater {
                            model: [qsTr("IN"), qsTr("OUT")]
                            delegate: ColumnLayout {
                                id: meterGroup
                                required property int index
                                required property string modelData
                                Layout.fillHeight: true
                                Layout.leftMargin: index === 1 ? Theme.spacingXs : 0
                                spacing: 2
                                RowLayout {
                                    Layout.fillHeight: true
                                    spacing: 3
                                    Repeater {
                                        model: 2
                                        delegate: AeqMeter {
                                            required property int index
                                            Layout.fillHeight: true
                                            readonly property var m: root.meter(meterGroup.index * 2 + index)
                                            label: index === 0 ? "L" : "R"
                                            peak: m.peak
                                            rms: m.rms
                                            hold: m.hold
                                            active: m.active
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: 4
                                    Led {
                                        visible: meterGroup.index === 1
                                        on: root.controller.clipping
                                        color: Theme.danger
                                        size: 7
                                        glow: root.controller.clipping
                                        ToolTip.visible: clipHover.hovered
                                        ToolTip.text: qsTr("Output clipped at ±2 V in the last second")
                                        HoverHandler { id: clipHover }
                                    }
                                    Text {
                                        text: meterGroup.modelData
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeXs
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 0.8
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        inspector: [
            Section {
                title: qsTr("Input")
                summary: root.controller.sourceNames[root.controller.inputLeft] + " · " + root.controller.sourceNames[root.controller.inputRight]
                FieldRow {
                    Field {
                        label: qsTr("Input (L)")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.sourceNames
                            currentIndex: root.controller.inputLeft
                            onActivated: index => root.controller.inputLeft = index
                        }
                    }
                    Field {
                        label: qsTr("Input (R)")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.sourceNames
                            currentIndex: root.controller.inputRight
                            onActivated: index => root.controller.inputRight = index
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("±2 V inputs. With one input, it feeds both outputs.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                title: qsTr("Output")
                summary: root.controller.sinkNames[root.controller.outputLeft] + " · " + root.controller.sinkNames[root.controller.outputRight]
                FieldRow {
                    Field {
                        label: qsTr("Output (L)")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.sinkNames
                            currentIndex: root.controller.outputLeft
                            onActivated: index => root.controller.outputLeft = index
                        }
                    }
                    Field {
                        label: qsTr("Output (R)")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.sinkNames
                            currentIndex: root.controller.outputRight
                            onActivated: index => root.controller.outputRight = index
                        }
                    }
                }
                Text {
                    visible: root.controller.routingError.length > 0
                    Layout.fillWidth: true
                    text: root.controller.routingError
                    color: Theme.danger
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                title: qsTr("Equalizer")
                summary: root.controller.bypass ? qsTr("Bypass") : qsTr("Volume %1 %").arg(Math.round(root.controller.volume * 100))
                FieldRow {
                    Field {
                        label: qsTr("Bass")
                        trailing: "< 320 Hz"
                        SiField {
                            Layout.fillWidth: true
                            unit: "dB"
                            engineering: false
                            decimals: 1
                            stepSize: 1
                            from: -12
                            to: 12
                            accentColor: root.bassColor
                            value: root.controller.bass
                            onValueModified: v => root.controller.bass = v
                        }
                    }
                    Field {
                        label: qsTr("Midtone")
                        SiField {
                            Layout.fillWidth: true
                            unit: "dB"
                            engineering: false
                            decimals: 1
                            stepSize: 1
                            from: -12
                            to: 12
                            accentColor: root.midColor
                            value: root.controller.midtone
                            onValueModified: v => root.controller.midtone = v
                        }
                    }
                    Field {
                        label: qsTr("Treble")
                        SiField {
                            Layout.fillWidth: true
                            unit: "dB"
                            engineering: false
                            decimals: 1
                            stepSize: 1
                            from: -12
                            to: 12
                            accentColor: root.trebleColor
                            value: root.controller.treble
                            onValueModified: v => root.controller.treble = v
                        }
                    }
                }
                Field {
                    label: qsTr("Volume")
                    trailing: Math.round(root.controller.volume * 100) + " %"
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        stepSize: 0.01
                        value: root.controller.volume
                        onMoved: root.controller.volume = value
                    }
                }
                ToggleSwitch {
                    text: qsTr("Bypass")
                    checked: root.controller.bypass
                    onToggled: root.controller.bypass = checked
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("4th-order Bessel bands: Bass below 320 Hz, Midtone 320 Hz – 2.56 kHz, Treble 2.56 – 20.84 kHz, ±12 dB each.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                title: qsTr("Streaming")
                summary: Units.format(root.controller.streamRate, "S/s", 3)
                FieldRow {
                    Field {
                        label: qsTr("Sample Rate")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.rateNames
                            currentIndex: root.controller.rateIndex
                            onActivated: index => root.controller.rateIndex = index
                        }
                    }
                    Field {
                        label: qsTr("Output Buffer")
                        hint: qsTr("Silence queued before the first block: longer is safer, shorter answers faster")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.latencyNames
                            currentIndex: root.controller.latencyIndex
                            onActivated: index => root.controller.latencyIndex = index
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Input to output delay ≈ %1 (buffer + %2 blocks). Input and output run at the same rate.")
                              .arg(Units.format(root.controller.latencySeconds + root.controller.blockSeconds, "s", 3))
                              .arg(Units.format(root.controller.blockSeconds, "s", 2))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                title: qsTr("Display")
                collapsed: true
                summary: root.controller.view === AeqController.SpectrumView ? qsTr("1/24 octave") : qsTr("Time")
                Field {
                    label: qsTr("Spectrum Averaging")
                    ComboBox {
                        Layout.fillWidth: true
                        model: root.controller.averagingNames
                        currentIndex: root.controller.averagingIndex
                        onActivated: index => root.controller.averagingIndex = index
                    }
                }
                ToggleSwitch {
                    text: qsTr("Show input")
                    checked: root.controller.showInput
                    onToggled: root.controller.showInput = checked
                }
            }
        ]
    }
}
