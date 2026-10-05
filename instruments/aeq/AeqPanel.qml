pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Audio Equalizer panel (docs/instruments/aeq.md), PicoScope-7 pattern: Run, Bypass and Flat on
// top; the display holds the tone response, the before/after spectrum (1/24 octave) or the
// waveforms with the band regions, and the console (band faders, volume, level meters) — the
// faders are the only place to edit the bands and the volume. The right column (InstrumentColumn)
// holds the Input and Output cards (one kind each — audio jack or AI/AO terminals — so the shared
// converters can never be mixed; then where the equalizer's L and R come from / go to) and the
// Streaming / Display tools.
Item {
    id: root

    required property AeqController controller

    // The bands follow the light spectrum: low = warm red-orange, mid = yellow, high = violet.
    // Their own colors (not the cursor, channel or accent colors), readable on both themes.
    readonly property color bassColor: Theme.displayDark ? "#EE6A5F" : "#C0392B"
    readonly property color midColor: Theme.signalMath
    readonly property color trebleColor: Theme.signalRef
    readonly property real topFrequency: Math.min(25000, 0.49 * controller.streamRate)
    readonly property bool narrow: width < Theme.instrumentCompactWidth
    readonly property bool wide: width - column.width >= 760    // console beside the plots
    property string tool                                       // the open tool panel ("" = none)

    // Short labels of the routing choices (controller order: AI 0, AI 1, audio L, audio R, none).
    readonly property var sourceShort: [qsTr("AI 0"), qsTr("AI 1"), qsTr("In L"), qsTr("In R"), qsTr("Off")]
    readonly property var sinkShort: [qsTr("AO 0"), qsTr("AO 1"), qsTr("Out L"), qsTr("Out R"), qsTr("Off")]
    function isAudioSource(s: int): bool { return s === AeqController.SourceAudioInLeft || s === AeqController.SourceAudioInRight }
    function isAudioSink(s: int): bool { return s === AeqController.SinkAudioOutLeft || s === AeqController.SinkAudioOutRight }
    readonly property bool audioIn: isAudioSource(controller.inputLeft) || isAudioSource(controller.inputRight)
    readonly property bool audioOut: isAudioSink(controller.outputLeft) || isAudioSink(controller.outputRight)
    readonly property var sourceChoices: audioIn ? [AeqController.SourceAudioInLeft, AeqController.SourceAudioInRight, AeqController.SourceNone]
                                                 : [AeqController.SourceAI0, AeqController.SourceAI1, AeqController.SourceNone]
    readonly property var sinkChoices: audioOut ? [AeqController.SinkAudioOutLeft, AeqController.SinkAudioOutRight, AeqController.SinkNone]
                                                : [AeqController.SinkAO0, AeqController.SinkAO1, AeqController.SinkNone]
    // Switching the kind never passes through a mixed routing: the right side is off meanwhile.
    function setInputKind(audio: bool) {
        controller.inputRight = AeqController.SourceNone
        controller.inputLeft = audio ? AeqController.SourceAudioInLeft : AeqController.SourceAI0
        controller.inputRight = audio ? AeqController.SourceAudioInRight : AeqController.SourceAI1
    }
    function setOutputKind(audio: bool) {
        controller.outputRight = AeqController.SinkNone
        controller.outputLeft = audio ? AeqController.SinkAudioOutLeft : AeqController.SinkAO0
        controller.outputRight = audio ? AeqController.SinkAudioOutRight : AeqController.SinkAO1
    }

    function meter(i: int): var {
        const m = root.controller.meters
        return i < m.length ? m[i] : { name: "", peak: -120, rms: -120, hold: -120, active: false }
    }
    function toggleTool(id: string) { tool = tool === id ? "" : id }

    // A labelled row inside a tool panel or card: the label above the control.
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
    // One side (L or R) of the routing: its label and the choices of the selected kind.
    component SideChoices: RowLayout {
        id: side
        property string sideName
        property var choices: []
        property var names: []
        property int current: 0
        readonly property string choiceText: choices.map(c => names[c]).join("|")   // for tests
        signal chosen(int choice)
        Layout.fillWidth: true
        spacing: Theme.spacingS
        Text {
            Layout.preferredWidth: 12
            text: side.sideName
            color: Theme.text
            font.pixelSize: Theme.fontSizeS
            font.weight: Font.DemiBold
            ToolTip.visible: sideHover.hovered
            ToolTip.text: side.sideName === "L" ? qsTr("Equalizer channel L") : qsTr("Equalizer channel R")
            HoverHandler { id: sideHover }
        }
        SegmentedControl {
            Layout.fillWidth: true
            model: side.choices.map(c => side.names[c])
            currentIndex: side.choices.indexOf(side.current)
            onActivated: index => side.chosen(side.choices[index])
        }
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        showRunControls: false
        runBar.showTimeout: false
        inspector: []

        headerContent: [
            RowLayout {
                objectName: "aeqBar"
                spacing: Theme.spacingM
                RunButton {
                    objectName: "aeqRunButton"
                    running: root.controller.running
                    stateText: panel.paneHeader.statusText
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "bypassButton"
                    text: qsTr("Bypass")
                    highlighted: root.controller.bypass
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Input passed through without equalization (volume still applies)")
                    onClicked: root.controller.bypass = !root.controller.bypass
                }
                Button {
                    objectName: "flatButton"
                    text: qsTr("Flat")
                    icon.source: Theme.icon("reset")
                    enabled: !root.controller.bypass
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Bass, Midtone and Treble to 0 dB")
                    onClicked: root.controller.flat()
                }
            }
        ]

        Item {
            anchors.fill: parent

            GridLayout {
                id: display
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
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
                        placeholderText: ""
                        xAxis { scale: PlotAxis.Log; min: 20; max: root.topFrequency; unit: "Hz"; unitInLabels: true; lowerLimit: 10; upperLimit: root.topFrequency * 1.5 }
                        yAxis { autoScale: root.controller.autoScaleSpectrum; min: -110; max: 10; unit: "dBV"; unitInLabels: true; limitToData: true; limitMargin: 0.5; minimumSpan: 3 }
                        // Band markers: the three equalizer bands behind the before/after spectrum.
                        PlotRegion { from: 20; to: 320; color: Theme.alpha(root.bassColor, root.controller.bypass ? 0.02 : 0.06) }
                        PlotRegion { from: 320; to: 2560; color: Theme.alpha(root.midColor, root.controller.bypass ? 0.02 : 0.06) }
                        PlotRegion { from: 2560; to: Math.min(20840, root.topFrequency); color: Theme.alpha(root.trebleColor, root.controller.bypass ? 0.02 : 0.06) }
                        PlotTrace {
                            visible: root.controller.showInput
                            name: qsTr("Before")
                            unit: "dBV"
                            buffer: root.controller.spectrumIn
                            color: Theme.alpha(Theme.textSecondary, 0.8)
                            width: 1
                            step: true
                            fill: true
                            fillOpacity: 0.12
                        }
                        PlotTrace {
                            name: qsTr("After")
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
                        placeholderText: ""
                        xAxis { min: 0; max: root.controller.timeSpan; unit: "s"; unitInLabels: true }
                        yAxis { autoScale: root.controller.autoScaleTime; min: -2; max: 2; unit: "V"; unitInLabels: true }
                        PlotTrace { visible: root.controller.showInput; name: qsTr("Input"); unit: "V"; buffer: root.controller.timeIn; color: Theme.alpha(Theme.textSecondary, 0.9); width: 1.2 }
                        PlotTrace { name: qsTr("Output"); unit: "V"; buffer: root.controller.timeOut; color: Theme.accent; width: 1.6 }
                    }
                }

                // Console: band faders, volume, meters (the only editors of the EQ).
                Rectangle {
                    objectName: "console"
                    Layout.fillHeight: root.wide
                    Layout.fillWidth: !root.wide
                    Layout.preferredWidth: consoleRow.implicitWidth + 2 * Theme.spacingM
                    Layout.preferredHeight: root.wide ? -1 : 200
                    radius: Theme.radiusL
                    color: "transparent"
                    border.color: Theme.displayBorder

                    RowLayout {
                        id: consoleRow
                        anchors.fill: parent
                        anchors.margins: Theme.spacingM
                        spacing: Theme.spacingXs

                        AeqFader {
                            objectName: "fader_bass"
                            Layout.fillHeight: true
                            label: qsTr("Bass")
                            accent: root.bassColor
                            value: root.controller.bass
                            enabled: !root.controller.bypass
                            onMoved: v => root.controller.bass = v
                        }
                        AeqFader {
                            objectName: "fader_mid"
                            Layout.fillHeight: true
                            label: qsTr("Midtone")
                            accent: root.midColor
                            value: root.controller.midtone
                            enabled: !root.controller.bypass
                            onMoved: v => root.controller.midtone = v
                        }
                        AeqFader {
                            objectName: "fader_treble"
                            Layout.fillHeight: true
                            label: qsTr("Treble")
                            accent: root.trebleColor
                            value: root.controller.treble
                            enabled: !root.controller.bypass
                            onMoved: v => root.controller.treble = v
                        }
                        Rectangle { Layout.fillHeight: true; Layout.topMargin: 20; Layout.bottomMargin: 20; Layout.leftMargin: 2; Layout.rightMargin: 2; implicitWidth: 1; color: Theme.displayBorder }
                        AeqFader {
                            objectName: "fader_volume"
                            Layout.fillHeight: true
                            label: qsTr("Volume")
                            from: 0
                            to: 1
                            stepSize: 0.01
                            resetValue: 0.5
                            centered: false
                            inputScale: 100
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

            // Right column (fixed width): the Input and Output cards, the tool palette pinned below.
            InstrumentColumn {
                id: column
                objectName: "aeqColumn"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                compact: root.narrow

                ChannelCard {
                    objectName: "inputCard"
                    Layout.fillWidth: true
                    title: qsTr("Input")
                    color: Theme.signalColor(root.audioIn ? "audioInL" : "ai0")
                    checkable: false
                    compact: root.narrow

                    SegmentedControl {
                        objectName: "inputKind"
                        Layout.fillWidth: true
                        model: [{ text: qsTr("Audio in"), toolTip: qsTr("AUDIO IN jack, left / right (±2 V)") },
                                { text: qsTr("AI 0 / AI 1"), toolTip: qsTr("Analog input terminals AI 0 / AI 1 (±2 V range)") }]
                        currentIndex: root.audioIn ? 0 : 1
                        onActivated: index => root.setInputKind(index === 0)
                    }
                    SideChoices {
                        objectName: "inputLeft"
                        sideName: "L"
                        choices: root.sourceChoices
                        names: root.sourceShort
                        current: root.controller.inputLeft
                        onChosen: choice => root.controller.inputLeft = choice
                    }
                    SideChoices {
                        objectName: "inputRight"
                        sideName: "R"
                        choices: root.sourceChoices
                        names: root.sourceShort
                        current: root.controller.inputRight
                        onChosen: choice => root.controller.inputRight = choice
                    }
                    // Simulator: the outputs loop back to these inputs (the routing above changes it).
                    Text {
                        objectName: "feedbackHint"
                        visible: root.controller.feedbackInSimulator
                        Layout.fillWidth: true
                        text: qsTr("Simulator: the outputs loop back here")
                        color: Theme.caution
                        font.pixelSize: Theme.fontSizeS
                        wrapMode: Text.WordWrap
                    }
                }

                ChannelCard {
                    objectName: "outputCard"
                    Layout.fillWidth: true
                    title: qsTr("Output")
                    color: Theme.signalColor(root.audioOut ? "audioOutL" : "ao0")
                    checkable: false
                    compact: root.narrow

                    SegmentedControl {
                        objectName: "outputKind"
                        Layout.fillWidth: true
                        model: [{ text: qsTr("Audio out"), toolTip: qsTr("AUDIO OUT jack, left / right (±2 V)") },
                                { text: qsTr("AO 0 / AO 1"), toolTip: qsTr("Analog output terminals AO 0 / AO 1 (±2 V used)") }]
                        currentIndex: root.audioOut ? 0 : 1
                        onActivated: index => root.setOutputKind(index === 0)
                    }
                    SideChoices {
                        objectName: "outputLeft"
                        sideName: "L"
                        choices: root.sinkChoices
                        names: root.sinkShort
                        current: root.controller.outputLeft
                        onChosen: choice => root.controller.outputLeft = choice
                    }
                    SideChoices {
                        objectName: "outputRight"
                        sideName: "R"
                        choices: root.sinkChoices
                        names: root.sinkShort
                        current: root.controller.outputRight
                        onChosen: choice => root.controller.outputRight = choice
                    }
                }

                tools: ToolPalette {
                    objectName: "aeqTools"
                    columns: 2
                    cellHeight: root.narrow ? 40 : 46
                    currentId: root.tool
                    model: [{ id: "streaming", text: qsTr("Streaming"), icon: "activity" },
                            { id: "display", text: qsTr("Display"), icon: "grid" }]
                    onActivated: id => root.toggleTool(id)
                }
            }

            // The open tool panel floats over the right part of the display, next to the column.
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: Math.min(Theme.toolPanelWidth, Math.max(230, (parent.width - column.width) * 0.4))
                z: 10

                ToolPanel {
                    objectName: "streamingPanel"
                    visible: root.tool === "streaming"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Streaming")
                    onCloseRequested: root.tool = ""
                    ToolRow {
                        label: qsTr("Sample rate")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: root.controller.rateNames
                                delegate: PictureButton {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    text: modelData
                                    checked: root.controller.rateIndex === index
                                    onClicked: root.controller.rateIndex = index
                                }
                            }
                        }
                    }
                    ToolRow {
                        label: qsTr("Output buffer · delay ≈ %1")
                                   .arg(Units.format(root.controller.latencySeconds + root.controller.blockSeconds, "s", 3))
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: root.controller.latencyNames
                                delegate: PictureButton {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    text: modelData
                                    checked: root.controller.latencyIndex === index
                                    onClicked: root.controller.latencyIndex = index
                                }
                            }
                        }
                    }
                    Text {
                        visible: root.controller.dropouts > 0
                        text: qsTr("Dropouts: %1").arg(root.controller.dropouts)
                        color: Theme.warning
                        font.pixelSize: Theme.fontSizeS
                    }
                }

                ToolPanel {
                    objectName: "displayPanel"
                    visible: root.tool === "display"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Display")
                    onCloseRequested: root.tool = ""
                    ToolRow {
                        label: qsTr("View")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Spectrum"), qsTr("Time")]
                            currentIndex: root.controller.view
                            onActivated: index => root.controller.view = index
                        }
                    }
                    ToolRow {
                        label: qsTr("Channel")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("L"), qsTr("R")]
                            currentIndex: root.controller.displayChannel
                            onActivated: index => root.controller.displayChannel = index
                        }
                    }
                    ToolRow {
                        visible: root.controller.view === AeqController.SpectrumView
                        label: qsTr("Averaging")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: root.controller.averagingNames
                                delegate: PictureButton {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    text: modelData
                                    checked: root.controller.averagingIndex === index
                                    onClicked: root.controller.averagingIndex = index
                                }
                            }
                        }
                    }
                    ToolRow {
                        visible: root.controller.view === AeqController.TimeView
                        label: qsTr("Time span")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: root.controller.timeSpanNames
                            currentIndex: root.controller.timeSpanIndex
                            onActivated: index => root.controller.timeSpanIndex = index
                        }
                    }
                    CheckBox {
                        text: qsTr("Autoscale")
                        checked: root.controller.view === AeqController.SpectrumView ? root.controller.autoScaleSpectrum : root.controller.autoScaleTime
                        onToggled: {
                            if (root.controller.view === AeqController.SpectrumView)
                                root.controller.autoScaleSpectrum = checked
                            else
                                root.controller.autoScaleTime = checked
                        }
                    }
                    CheckBox {
                        text: qsTr("Show input")
                        checked: root.controller.showInput
                        onToggled: root.controller.showInput = checked
                    }
                }
            }
        }
    }
}
