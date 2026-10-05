pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Spectrum Analyzer panel (docs/instruments/dsa.md; ELVISmx Dynamic Signal Analyzer) with the
// 1/3-octave analysis mode (ELVISmx featured app "Octave Analyzer"). Layout: instrument bar in
// the pane header (Run, Single, Span and Resolution knobs, Analysis), display on the left (with
// the averaging progress and Restart while they apply), the shared InstrumentColumn on the right
// (Input card; tool palette pinned at the bottom, a tool opens a floating panel over the display),
// measurement cards at the bottom. Every setting exists once; settings that do not apply are
// hidden. View only: settings, conversions and measurements live in DsaController.
Item {
    id: root

    required property DsaController controller

    readonly property bool octave: controller.octave
    readonly property bool manualScale: controller.scaleMode === DsaController.ManualScale
    // Same rule as every panel: compact column and knobs below the compact pane width (a header
    // that does not fit wraps to a second row).
    readonly property bool narrow: width < Theme.instrumentCompactWidth
    // Window-shape pictograms (SVG paths on 32×18), in DsaController.windowLabels order: None,
    // Hanning, Hamming, Blackman-Harris, Exact Blackman, Blackman, Flat Top, 4 and 7 Term
    // B-Harris, Low Sidelobe. Narrower bells for the windows with lower side lobes.
    readonly property var windowGlyphs: [
        "M2 16 V3 H30 V16",
        "M2 16 C9 16 10 2 16 2 S23 16 30 16",
        "M2 16 V13 C9 13 10 2 16 2 S23 13 30 13 V16",
        "M2 16 C11 16 12 2 16 2 S21 16 30 16",
        "M2 16 C10 16 11 2 16 2 S22 16 30 16",
        "M2 16 C10 16 11.5 2 16 2 S22 16 30 16",
        "M2 15 C7 17 9 2 13 2 H19 C23 2 25 17 30 15",
        "M2 16 C11.5 16 12.5 2 16 2 S20.5 16 30 16",
        "M2 16 C12.5 16 13.5 2 16 2 S19.5 16 30 16",
        "M2 16 C11 16 12 3 16 2 S21 16 30 16"
    ]
    // Input pictograms (32×18): AI = a signal into a terminal; Audio = a 3.5 mm jack plug.
    readonly property string analogGlyph: "M2 9c2.5-7 5-7 7.5 0s5 7 7.5 0 M17 9h6 M23 9a3 3 0 1 0 6 0a3 3 0 1 0 -6 0"
    readonly property string audioGlyph: "M2 6h8v6H2z M10 8h12 M10 10h12 M15 7v4 M19 7v4 M22 8c4 0 6 0.4 6 1s-2 1-6 1"
    // Averaging-mode pictograms: a noisy line (None), an arrow (Vector), a smooth line (RMS),
    // a noisy line under its held maximum (Peak Hold).
    readonly property var averagingGlyphs: [
        "M2 10l3-4 3 7 3-6 3 5 3-4 3 6 3-5 3 3 3-2",
        "M3 14L27 5 M21 4.5l6 0.5-3.5 5",
        "M2 10c4 0 5-3 9-3s5 4 10 4 5-2 9-2",
        "M2 15l3-4 3 3 3-6 3 5 3-3 3 4 3-5 3 3 M2 4h28"
    ]
    readonly property color sourceColor: Theme.signalColor(["ai0", "ai1", "audioInL", "audioInR"][controller.sourceChannel] ?? "ai0")
    // Open tool panel ("" = none) and the time record (a Display option, off by default).
    property string tool: ""
    property bool showTime: false
    readonly property string octaveUnit: "dBV" + (controller.octaveWeightingSuffix.length > 0 ? " " + controller.octaveWeightingSuffix : "")

    // Span: 1-2-5 steps up to the source's maximum (sample rate = 2 × span).
    readonly property var spanValues: {
        const steps = [500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000]
        const list = steps.filter(v => v <= controller.maximumSpan)
        if (list.length === 0 || list[list.length - 1] < controller.maximumSpan)
            list.push(controller.maximumSpan)
        // A span set elsewhere (typed, restored, ELVISmx default 40 kHz) stays selectable as is.
        if (!list.some(v => Math.abs(v - controller.frequencySpan) < 1e-6 * v))
            list.push(controller.frequencySpan)
        return list.sort((x, y) => x - y)
    }
    readonly property var lineValues: [200, 400, 800, 1600, 3200]

    function nearest(list: var, v: real): int {
        let best = 0
        for (let i = 1; i < list.length; ++i) {
            if (Math.abs(Math.log(list[i] / v)) < Math.abs(Math.log(list[best] / v)))
                best = i
        }
        return best
    }
    function toggleTool(id: string) { tool = tool === id ? "" : id }

    // A labelled row inside a tool panel: the label above the control.
    component ToolRow: ColumnLayout {
        id: toolRow
        property string label
        default property alias content: rowBody.data
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        Text {
            text: toolRow.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
        }
        ColumnLayout {
            id: rowBody
            Layout.fillWidth: true
            spacing: Theme.spacingXs
        }
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        showRunControls: false
        runBar.showTimeout: false
        inspector: []
        // No display title: the analysis, source, window and averaging are shown once, in their
        // controls; the axis title names the unit (power or density).

        headerContent: [
            RowLayout {
                objectName: "dsaBar"
                spacing: Theme.spacingM

                RunButton {
                    objectName: "dsaRunButton"
                    running: root.controller.running
                    stateText: panel.paneHeader.statusText
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    onToggled: root.controller.toggle()
                }
                // Single: idle = one record (or one complete average); running = finish after it.
                Button {
                    objectName: "dsaSingleButton"
                    text: qsTr("Single")
                    icon.source: Theme.icon("step")
                    implicitHeight: Theme.controlHeightL
                    enabled: root.controller.canRun
                             || (root.controller.state === InstrumentController.Running
                                 && root.controller.runMode === InstrumentController.Continuous)
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.running ? qsTr("Stop after this record (or average)")
                                                          : qsTr("Acquire one record (or one complete average), then stop")
                    onClicked: root.controller.runOnce()
                }
                Rectangle { visible: !root.narrow; implicitWidth: 1; Layout.fillHeight: true; Layout.margins: 6; color: Theme.separator }
                KnobField {
                    objectName: "spanKnob"
                    visible: !root.octave
                    Layout.preferredWidth: root.narrow ? 150 : 200
                    compact: root.narrow
                    label: qsTr("Span")
                    unit: "Hz"
                    accentColor: root.sourceColor
                    values: root.spanValues
                    currentIndex: root.nearest(root.spanValues, root.controller.frequencySpan)
                    formatter: v => Units.format(v, "Hz", 3)
                    caption: qsTr("%1 S/s").arg(Units.format(root.controller.expectedSampleRate, "", 3, false))
                    toolTip: qsTr("0 Hz … span; the sample rate is twice the span")
                    onIndexActivated: index => root.controller.frequencySpan = root.spanValues[index]
                }
                KnobField {
                    objectName: "resolutionKnob"
                    visible: !root.octave
                    Layout.preferredWidth: root.narrow ? 150 : 210
                    compact: root.narrow
                    label: qsTr("Resolution")
                    accentColor: root.sourceColor
                    values: root.lineValues
                    currentIndex: root.controller.resolutionIndex
                    formatter: v => qsTr("%1 lines").arg(v)
                    caption: qsTr("df %1 · record %2")
                             .arg(Units.format(root.controller.lineSpacing, "Hz", 3))
                             .arg(Units.format(2 * root.controller.lines / root.controller.expectedSampleRate, "s", 3))
                    onIndexActivated: index => root.controller.resolutionIndex = index
                }
                SegmentedControl {
                    objectName: "analysisMode"
                    visible: !root.narrow         // narrow pane: in the column (Analysis card)
                    model: [
                        { text: qsTr("Spectrum"), toolTip: qsTr("FFT spectrum (ELVISmx Dynamic Signal Analyzer)") },
                        { text: qsTr("Octave"), toolTip: qsTr("1/3-octave bands at 50 kS/s (ELVISmx Octave Analyzer)") }
                    ]
                    currentIndex: root.octave ? 1 : 0
                    onActivated: index => root.controller.analysisMode = index === 1 ? DsaController.OctaveAnalysis : DsaController.FftAnalysis
                }
            }
        ]

        Item {
            anchors.fill: parent

            DsaSpectrumView {
                id: spectrumView
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
                visible: !root.octave
                controller: root.controller
                traceColor: root.sourceColor
                showTime: root.showTime
            }
            DsaOctaveView {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
                visible: root.octave
                controller: root.controller
                traceColor: root.sourceColor
            }

            // Right column (shared, fixed width): Input card; tool palette pinned at the bottom.
            InstrumentColumn {
                id: column
                objectName: "dsaColumn"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                compact: root.narrow

                // Narrow pane: the analysis choice moves here so the header keeps the knobs.
                ChannelCard {
                    objectName: "analysisCard"
                    visible: root.narrow
                    Layout.fillWidth: true
                    title: qsTr("Analysis")
                    color: Theme.signalMath
                    checkable: false
                    compact: true
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ text: qsTr("Spectrum"), toolTip: qsTr("FFT spectrum (ELVISmx Dynamic Signal Analyzer)") },
                                { text: qsTr("Octave"), toolTip: qsTr("1/3-octave bands at 50 kS/s (ELVISmx Octave Analyzer)") }]
                        currentIndex: root.octave ? 1 : 0
                        onActivated: index => root.controller.analysisMode = index === 1 ? DsaController.OctaveAnalysis : DsaController.FftAnalysis
                    }
                }
                ChannelCard {
                    objectName: "inputCard"
                    Layout.fillWidth: true
                    title: qsTr("Input")
                    color: root.sourceColor
                    checkable: false
                    compact: root.narrow
                    facts: root.controller.audioSource ? [qsTr("±2 V fixed")] : []
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 4
                        columnSpacing: 4
                        Repeater {
                            model: root.controller.sourceLabels
                            delegate: PictureButton {
                                required property string modelData
                                required property int index
                                objectName: "source_" + index
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                text: modelData
                                glyph: index < 2 ? root.analogGlyph : root.audioGlyph
                                checked: root.controller.sourceChannel === index
                                accentColor: Theme.signalColor(["ai0", "ai1", "audioInL", "audioInR"][index])
                                ToolTip.visible: hovered
                                ToolTip.text: [qsTr("Analog input AI 0"), qsTr("Analog input AI 1"),
                                               qsTr("Audio input, left"), qsTr("Audio input, right")][index]
                                onClicked: root.controller.sourceChannel = index
                            }
                        }
                    }
                    SegmentedControl {
                        objectName: "rangeButtons"
                        Layout.fillWidth: true
                        visible: !root.controller.audioSource
                        model: root.controller.rangeLabels
                        currentIndex: root.controller.effectiveRangeIndex
                        onActivated: index => root.controller.voltageRangeIndex = index
                    }
                }

                tools: ToolPalette {
                    objectName: "dsaTools"
                    columns: 3
                    cellHeight: root.narrow ? 40 : 52
                    currentId: root.tool
                    model: root.octave
                           ? [{ id: "octave", text: qsTr("Octave"), icon: "bar-chart" },
                              { id: "export", text: qsTr("Export"), icon: "export" }]
                           : [{ id: "window", text: qsTr("Window"), icon: "activity" },
                              { id: "averaging", text: qsTr("Averaging"), icon: "layers" },
                              { id: "display", text: qsTr("Display"), icon: "grid" },
                              { id: "cursors", text: qsTr("Cursors"), icon: "crosshair" },
                              { id: "export", text: qsTr("Export"), icon: "export" }]
                    onActivated: id => root.toggleTool(id)
                }
            }

            // The open tool panel floats over the right part of the display, next to the column.
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: Math.min(Theme.toolPanelWidth, Math.max(Theme.instrumentColumnCompactWidth, (parent.width - column.width) * 0.4))
                z: 10

                ToolPanel {
                    objectName: "windowPanel"
                    visible: root.tool === "window" && !root.octave
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Window")
                    onCloseRequested: root.tool = ""
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 4
                        columnSpacing: 4
                        Repeater {
                            model: root.controller.windowLabels
                            delegate: PictureButton {
                                required property string modelData
                                required property int index
                                objectName: "window" + index
                                Layout.fillWidth: true
                                text: modelData
                                glyph: root.windowGlyphs[index] ?? ""
                                checked: root.controller.windowIndex === index
                                onClicked: root.controller.windowIndex = index
                            }
                        }
                    }
                }

                ToolPanel {
                    objectName: "averagingPanel"
                    visible: root.tool === "averaging" && !root.octave
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Averaging")
                    onCloseRequested: root.tool = ""
                    ToolRow {
                        label: qsTr("Mode")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: [qsTr("None"), qsTr("Vector"), qsTr("RMS"), qsTr("Peak Hold")]
                                delegate: PictureButton {
                                    required property string modelData
                                    required property int index
                                    objectName: "averaging" + index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    text: modelData
                                    glyph: root.averagingGlyphs[index]
                                    checked: root.controller.averagingMode === index
                                    onClicked: root.controller.averagingMode = index
                                }
                            }
                        }
                    }
                    ToolRow {
                        visible: root.controller.weightingEnabled
                        label: qsTr("Weighting")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Linear"), qsTr("Exponential")]
                            currentIndex: root.controller.weighting
                            onActivated: index => root.controller.weighting = index
                        }
                    }
                    KnobField {
                        visible: root.controller.averagesEnabled
                        Layout.fillWidth: true
                        knobSize: 34
                        label: qsTr("Averages")
                        from: 1
                        to: 10000
                        stepSize: 1
                        logarithmic: true
                        value: root.controller.averages
                        formatter: v => String(Math.round(v))
                        onValueModified: v => root.controller.averages = Math.round(v)
                    }
                }

                ToolPanel {
                    objectName: "displayPanel"
                    visible: root.tool === "display" && !root.octave
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Display")
                    onCloseRequested: root.tool = ""
                    ToolRow {
                        label: qsTr("Units")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("V"), qsTr("dB"), qsTr("dBm")]
                            currentIndex: root.controller.units
                            onActivated: index => root.controller.units = index
                        }
                        SegmentedControl {
                            Layout.fillWidth: true
                            visible: root.controller.units !== DsaController.DbmUnits
                            model: [qsTr("RMS"), qsTr("Peak")]
                            currentIndex: root.controller.magnitudeMode
                            onActivated: index => root.controller.magnitudeMode = index
                        }
                    }
                    ToolRow {
                        label: qsTr("Spectrum")
                        SegmentedControl {
                            objectName: "spectrumDisplay"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Power"), toolTip: qsTr("Power Spectrum") },
                                    { text: qsTr("Density"), toolTip: qsTr("Power Spectral Density (per √Hz)") }]
                            currentIndex: root.controller.spectrumDisplay === DsaController.PowerSpectralDensityDisplay ? 1 : 0
                            onActivated: index => root.controller.spectrumDisplay = index === 1 ? DsaController.PowerSpectralDensityDisplay
                                                                                               : DsaController.PowerSpectrumDisplay
                        }
                    }
                    ToolRow {
                        label: qsTr("Vertical scale")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Auto"), qsTr("Manual")]
                            currentIndex: root.manualScale ? 1 : 0
                            onActivated: index => root.controller.scaleMode = index === 1 ? DsaController.ManualScale : DsaController.AutoScale
                        }
                        RowLayout {
                            visible: root.manualScale
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs
                            SiField {
                                Layout.fillWidth: true
                                engineering: false
                                decimals: 2
                                stepSize: root.controller.logarithmicUnits ? 1 : 0.01
                                toolTip: qsTr("Minimum")
                                value: root.controller.scaleMinimum
                                onValueModified: v => root.controller.scaleMinimum = v
                            }
                            Text { text: "…"; color: Theme.textSecondary }
                            SiField {
                                Layout.fillWidth: true
                                engineering: false
                                decimals: 2
                                stepSize: root.controller.logarithmicUnits ? 1 : 0.01
                                toolTip: qsTr("Maximum")
                                value: root.controller.scaleMaximum
                                onValueModified: v => root.controller.scaleMaximum = v
                            }
                        }
                    }
                    Switch { text: qsTr("Logarithmic frequency axis"); checked: root.controller.logFrequency; onToggled: root.controller.logFrequency = checked }
                    Switch { text: qsTr("Max hold"); checked: root.controller.maxHold; onToggled: root.controller.maxHold = checked }
                    Switch { text: qsTr("Min hold"); checked: root.controller.minHold; onToggled: root.controller.minHold = checked }
                    Switch { objectName: "timeSwitch"; text: qsTr("Time record"); checked: root.showTime; onToggled: root.showTime = checked }
                    Switch { text: qsTr("Spectrogram"); checked: root.controller.waterfallVisible; onToggled: root.controller.waterfallVisible = checked }
                    KnobField {
                        visible: root.controller.waterfallVisible
                        Layout.fillWidth: true
                        knobSize: 34
                        label: qsTr("Spectrogram depth")
                        values: [50, 100, 200, 300, 500, 1000]
                        currentIndex: root.nearest([50, 100, 200, 300, 500, 1000], root.controller.waterfallHistory)
                        formatter: v => qsTr("%1 spectra").arg(v)
                        onIndexActivated: index => root.controller.waterfallHistory = [50, 100, 200, 300, 500, 1000][index]
                    }
                }

                ToolPanel {
                    objectName: "cursorsPanel"
                    visible: root.tool === "cursors" && !root.octave
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Cursors")
                    onCloseRequested: root.tool = ""
                    // Every marker and cursor control lives here (once): harmonic markers and their
                    // table, then the C1 / C2 cursor pair with its peak search.
                    Switch { objectName: "harmonicMarkersSwitch"; text: qsTr("Harmonic markers"); checked: root.controller.harmonicMarkers; onToggled: root.controller.harmonicMarkers = checked }
                    Switch { text: qsTr("Harmonics and peaks table"); checked: root.controller.analysisTables; onToggled: root.controller.analysisTables = checked }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.separator }
                    Switch { objectName: "cursorsSwitch"; text: qsTr("Cursors C1 · C2"); checked: root.controller.cursorsOn; onToggled: root.controller.cursorsOn = checked }
                    ToolRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("Selected cursor")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: ["C1", "C2"]
                            currentIndex: root.controller.cursorSelect
                            onActivated: index => root.controller.cursorSelect = index
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Button { Layout.fillWidth: true; icon.source: Theme.icon("chevron-left"); ToolTip.visible: hovered; ToolTip.text: qsTr("One line left"); onClicked: root.controller.stepCursor(-1) }
                            Button { Layout.fillWidth: true; icon.source: Theme.icon("chevron-right"); ToolTip.visible: hovered; ToolTip.text: qsTr("One line right"); onClicked: root.controller.stepCursor(1) }
                        }
                    }
                    ToolRow {
                        visible: root.controller.cursorsOn && root.showTime
                        label: qsTr("Cursor plots (C1 · C2)")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Spectrum"), qsTr("Time")]
                            currentIndex: root.controller.cursor1Plot
                            onActivated: index => root.controller.cursor1Plot = index
                        }
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Spectrum"), qsTr("Time")]
                            currentIndex: root.controller.cursor2Plot
                            onActivated: index => root.controller.cursor2Plot = index
                        }
                    }
                    ToolRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("Peak search")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 4
                            component PeakButton: Button {
                                Layout.fillWidth: true
                                enabled: root.controller.hasResult
                            }
                            PeakButton { text: qsTr("Highest"); onClicked: root.controller.searchPeak(DsaController.HighestPeak) }
                            PeakButton { text: qsTr("Next lower"); onClicked: root.controller.searchPeak(DsaController.NextPeak) }
                            PeakButton { text: qsTr("◂ Left"); onClicked: root.controller.searchPeak(DsaController.PeakLeft) }
                            PeakButton { text: qsTr("Right ▸"); onClicked: root.controller.searchPeak(DsaController.PeakRight) }
                        }
                    }
                }

                ToolPanel {
                    objectName: "octavePanel"
                    visible: root.tool === "octave" && root.octave
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Octave")
                    onCloseRequested: root.tool = ""
                    ToolRow {
                        label: qsTr("Time weighting")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [
                                { text: qsTr("Slow"), toolTip: qsTr("1 s") },
                                { text: qsTr("Fast"), toolTip: qsTr("125 ms") },
                                { text: qsTr("Impulse"), toolTip: qsTr("35 ms rise, 1.5 s decay") },
                                { text: qsTr("Custom"), toolTip: qsTr("Your time constant") }
                            ]
                            currentIndex: root.controller.octaveAveraging
                            onActivated: index => root.controller.octaveAveraging = index
                        }
                    }
                    KnobField {
                        visible: root.controller.octaveAveraging === DsaController.CustomAveraging
                        Layout.fillWidth: true
                        knobSize: 34
                        label: qsTr("Time constant")
                        unit: "s"
                        from: 0.001
                        to: 10
                        logarithmic: true
                        value: Math.max(0.001, root.controller.octaveTimeConstant / 1000)
                        onValueModified: v => root.controller.octaveTimeConstant = v * 1000
                    }
                    ToolRow {
                        label: qsTr("Bands")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            ComboBox {
                                Layout.fillWidth: true
                                model: root.controller.octaveBandLabels
                                currentIndex: root.controller.octaveLowBand
                                onActivated: index => root.controller.octaveLowBand = index
                            }
                            Text { text: "…"; color: Theme.textSecondary }
                            ComboBox {
                                Layout.fillWidth: true
                                model: root.controller.octaveBandLabels
                                currentIndex: root.controller.octaveHighBand
                                onActivated: index => root.controller.octaveHighBand = index
                            }
                        }
                    }
                    ToolRow {
                        label: qsTr("Frequency weighting")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Flat"), toolTip: qsTr("No weighting") }, { text: "A" }, { text: "B" }, { text: "C" }]
                            currentIndex: root.controller.octaveWeighting
                            onActivated: index => root.controller.octaveWeighting = index
                        }
                    }
                }

                ToolPanel {
                    objectName: "exportPanel"
                    visible: root.tool === "export"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Export")
                    onCloseRequested: root.tool = ""
                    Button { Layout.fillWidth: true; text: qsTr("Export Data…"); icon.source: Theme.icon("download"); onClicked: { root.tool = ""; panel.exportData() } }
                    Button { Layout.fillWidth: true; text: qsTr("Export Image…"); icon.source: Theme.icon("image"); onClicked: { root.tool = ""; panel.exportImage(false) } }
                    Button { Layout.fillWidth: true; text: qsTr("Export PDF…"); icon.source: Theme.icon("file"); onClicked: { root.tool = ""; panel.exportImage(true) } }
                    Button { Layout.fillWidth: true; text: qsTr("Print…"); icon.source: Theme.icon("printer"); onClicked: { root.tool = ""; panel.printPanel() } }
                }
            }
        }

        measurements: [
            Flow {
                objectName: "dsaCards"
                // Short windows collapse the strip to this line (InstrumentPanel).
                readonly property string summaryText: root.controller.hasResult && !root.octave
                    ? qsTr("%1 · THD %2 %").arg(Units.format(root.controller.fundamentalFrequency, "Hz", 5)).arg(root.controller.thd.toFixed(2))
                    : ""
                width: parent ? parent.width : 0
                spacing: Theme.spacingS
                padding: Theme.spacingS
                // A result card with min / max / mean / σ over the last 100 results of this run.
                component Card: MeasurementCard {
                    id: statCard
                    property real sample: NaN                    // the numeric result, NaN = none
                    property var format: v => Units.format(v, "", 4, false)
                    property var statFormat: format                // the statistics (4 digits)
                    property var samples: []
                    function push() {
                        if (root.controller.hasResult && isFinite(sample))
                            samples = samples.concat([sample]).slice(-100)
                    }
                    function statsText(): var {
                        const n = samples.length
                        if (n < 2)
                            return ({})
                        const mean = samples.reduce((a, b) => a + b, 0) / n
                        const variance = samples.reduce((a, b) => a + (b - mean) * (b - mean), 0) / n
                        // SI units: numbers, formatted by the card (4 digits, σ 3); %, dB, V² here.
                        const f = unit.length > 0 ? v => v : statFormat
                        return ({ min: f(Math.min(...samples)), max: f(Math.max(...samples)),
                                  mean: f(mean), stdDev: f(Math.sqrt(variance)) })
                    }
                    // A new run starts new statistics (`running` notifies through stateChanged).
                    readonly property bool runNow: root.controller.running
                    onRunNowChanged: if (runNow) samples = []
                    Connections {
                        target: root.controller
                        function onResultsChanged() { statCard.push() }
                    }
                    removable: false
                    width: Math.max(implicitWidth, root.narrow ? 160 : 190)
                    value: root.controller.hasResult && isFinite(sample) ? format(sample) : "—"
                    stats: statsText()
                }
                Card {
                    objectName: "cardFundamental"
                    visible: !root.octave
                    title: qsTr("Fundamental")
                    color: root.sourceColor
                    sample: root.controller.fundamentalFrequency
                    unit: "Hz"
                    format: v => Units.format(v, "Hz", 6)
                }
                Card {
                    objectName: "cardPower"
                    visible: !root.octave
                    title: qsTr("Fundamental power")
                    color: root.sourceColor
                    sample: root.controller.fundamentalPower
                    // A power in V² (no SI prefix: mV² would read as 1e-3 V²).
                    format: v => (v === 0 || Math.abs(v) >= 0.01 ? v.toFixed(3) : v.toExponential(2)) + " " + root.controller.powerUnit
                }
                Card {
                    objectName: "cardThd"
                    visible: !root.octave
                    title: qsTr("THD")
                    color: Theme.signalMath
                    sample: root.controller.thd
                    format: v => v.toPrecision(4) + " %"
                    statFormat: v => v.toPrecision(3) + " %"
                }
                Card {
                    objectName: "cardSinad"
                    visible: !root.octave
                    title: qsTr("SINAD")
                    color: Theme.signalMath
                    sample: root.controller.sinad
                    format: v => v.toFixed(2) + " dB"
                }
                Card {
                    objectName: "cardSnr"
                    visible: !root.octave
                    title: qsTr("SNR")
                    color: Theme.signalMath
                    sample: root.controller.snr
                    format: v => v.toFixed(2) + " dB"
                }
                Card {
                    objectName: "cardPeak"
                    visible: !root.octave
                    title: qsTr("Peak")
                    color: root.sourceColor
                    sample: root.controller.vpk
                    unit: "V"
                    format: v => Units.format(v, "V", 4) + "pk"
                }
                DsaCursorCard {
                    objectName: "cardCursors"
                    visible: root.controller.cursorsOn && !root.octave
                    width: Math.max(implicitWidth, root.narrow ? 200 : 260)
                    c1: root.controller.cursor1Text
                    c2: root.controller.cursor2Text
                    delta: root.controller.cursorDeltaText
                }
                // 1/3-octave results.
                MeasurementCard {
                    objectName: "cardOctaveOverall"
                    visible: root.octave
                    removable: false
                    width: Math.max(implicitWidth, root.narrow ? 160 : 190)
                    title: qsTr("Overall level")
                    color: root.sourceColor
                    value: root.controller.hasOctaveResult ? root.controller.octaveOverall.toFixed(1) + " " + root.octaveUnit : "—"
                }
                MeasurementCard {
                    objectName: "cardOctaveLoudest"
                    visible: root.octave
                    removable: false
                    width: Math.max(implicitWidth, root.narrow ? 160 : 190)
                    title: root.controller.hasOctaveResult ? qsTr("Loudest band · %1").arg(root.controller.octaveLoudestBand) : qsTr("Loudest band")
                    color: Theme.signalMath
                    value: root.controller.hasOctaveResult ? root.controller.octaveLoudestLevel.toFixed(1) + " " + root.octaveUnit : "—"
                }
            }
        ]
    }
}
