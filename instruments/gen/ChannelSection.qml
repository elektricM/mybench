pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Inspector card of one output channel (AO 0 / AO 1): Mode (Off, Function, Arbitrary, DC), the
// Output switch, and the settings of the mode, grouped with the ELVISmx group names
// ("Waveform Settings", "Sweep Settings", "Timing and Triggering Settings", "Voltage").
Section {
    id: root

    required property GenController controller
    required property GenChannel channel
    readonly property color channelColor: channel.index === 0 ? Theme.ch0 : Theme.ch1
    readonly property bool isFunction: channel.mode === GenChannel.Function
    readonly property bool isArbitrary: channel.mode === GenChannel.Arbitrary
    readonly property bool isDc: channel.mode === GenChannel.Dc
    property bool fineTuning: false
    property real fineCenter: 1000

    title: channel.name
    accentColor: channelColor
    summary: channel.outputOn ? channel.readoutText : channel.summary

    headerTools: [
        Led {
            on: root.channel.state === GenChannel.Running || root.channel.state === GenChannel.Sweeping
                || root.channel.state === GenChannel.Done || root.channel.state === GenChannel.Paused
            color: root.channel.state === GenChannel.Done || root.channel.state === GenChannel.Paused ? Theme.warning : Theme.success
            size: 8
            ToolTip.visible: ledHover.hovered
            ToolTip.text: root.channel.state === GenChannel.Done ? qsTr("Done: holding the last sample")
                          : root.channel.state === GenChannel.Paused ? qsTr("Paused")
                          : on ? qsTr("Generating") : qsTr("Output off (0 V)")
            HoverHandler { id: ledHover }
        },
        ToggleSwitch {
            accentColor: root.channelColor
            enabled: root.channel.mode !== GenChannel.Off
            checked: root.channel.outputOn
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Output (Run/Stop of %1)").arg(root.channel.name)
            onToggled: root.controller.setOutput(root.channel.index, checked)
        }
    ]

    SegmentedControl {
        Layout.fillWidth: true
        compact: true
        accentColor: root.channelColor
        model: [
            { text: qsTr("Off"), toolTip: qsTr("No output (0 V)") },
            { text: qsTr("Function"), toolTip: qsTr("Sine, triangle or square wave, sweep") },
            { text: qsTr("Arbitrary"), toolTip: qsTr("Play a waveform file or an editor document") },
            { text: qsTr("DC"), enabled: !root.channel.audio,
              toolTip: root.channel.audio ? qsTr("Not available on the AC-coupled audio outputs") : qsTr("Constant DC level") }
        ]
        currentIndex: root.channel.mode
        onActivated: index => root.channel.mode = index
    }

    Text {
        visible: root.channel.errorText.length > 0
        Layout.fillWidth: true
        text: root.channel.errorText
        color: Theme.danger
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    Text {
        visible: root.channel.mode === GenChannel.Off
        Layout.fillWidth: true
        text: qsTr("The output rests at 0 V. Choose Function, Arbitrary or DC.")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    // --- Function ------------------------------------------------------------------------------
    GroupLabel { visible: root.isFunction; text: qsTr("Waveform Settings") }

    SegmentedControl {
        visible: root.isFunction
        Layout.fillWidth: true
        accentColor: root.channelColor
        model: [
            { icon: "wave-sine-line", text: qsTr("Sine"), toolTip: qsTr("Sine") },
            { icon: "wave-triangle-line", text: qsTr("Triangle"), toolTip: qsTr("Triangle") },
            { icon: "wave-square-line", text: qsTr("Square"), toolTip: qsTr("Square") }
        ]
        currentIndex: root.channel.shape
        onActivated: index => root.channel.shape = index
    }

    RowLayout {
        visible: root.isFunction
        Layout.fillWidth: true
        spacing: Theme.spacingM
        Knob {
            id: frequencyKnob
            label: qsTr("Frequency")
            diameter: 74
            from: root.fineTuning ? Math.max(0.2, root.fineCenter / 1.25) : 0.2
            to: root.fineTuning ? Math.min(20000, root.fineCenter * 1.25) : 20000
            logarithmic: true
            unit: "Hz"
            digits: 4
            showValue: false
            minLabel: root.fineTuning ? Units.format(from, "", 3) : "200m"
            maxLabel: root.fineTuning ? Units.format(to, "", 3) : "20k"
            color: root.channelColor
            enabled: !root.channel.sweeping
            value: root.channel.frequency
            onMoved: v => root.channel.frequency = root.fineTuning ? v : Number(v.toPrecision(3))
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            Field {
                Layout.fillWidth: true
                label: qsTr("Frequency")
                SiField {
                    Layout.fillWidth: true
                    unit: "Hz"
                    from: 0.2
                    to: 20000
                    digits: 5
                    accentColor: root.channelColor
                    enabled: !root.channel.sweeping
                    toolTip: qsTr("0.2 Hz to 20 kHz. Type 1.5k; arrows or the wheel tune the digit at the cursor.")
                    value: root.channel.frequency
                    onValueModified: v => root.channel.frequency = v
                }
            }
            SegmentedControl {
                Layout.fillWidth: true
                compact: true
                model: [ { text: qsTr("Coarse"), toolTip: qsTr("The knob spans 0.2 Hz to 20 kHz") },
                         { text: qsTr("Fine"), toolTip: qsTr("The knob spans ±25 % around the current frequency") } ]
                currentIndex: root.fineTuning ? 1 : 0
                onActivated: index => {
                    root.fineCenter = root.channel.frequency
                    root.fineTuning = index === 1
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Generates %1").arg(formatDisplay(root.channel.expectedFrequency))
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                font.features: Theme.tabularFigures
                elide: Text.ElideRight
                ToolTip.visible: generatesHover.hovered
                ToolTip.text: root.controller.engineMode === GenController.Regenerate
                              ? qsTr("Frequency of the looped buffer (Regenerate engine): whole samples per period at up to 200 kS/s")
                              : qsTr("Exact frequency of the streaming synthesizer")
                HoverHandler { id: generatesHover }
                function formatDisplay(f: real): string {
                    const prefixes = [[1e6, "MHz"], [1e3, "kHz"], [1, "Hz"], [1e-3, "mHz"]]
                    for (const p of prefixes) {
                        if (Math.round(f / p[0] * 1e4) / 1e4 >= 1 || p[0] === 1e-3)
                            return (f / p[0]).toFixed(4) + " " + p[1]
                    }
                    return ""
                }
            }
        }
    }

    FieldRow {
        visible: root.isFunction
        Layout.fillWidth: true
        Field {
            label: qsTr("Amplitude")
            // ELVISmx-like: knob with its end labels, the value field below.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Knob {
                    Layout.alignment: Qt.AlignHCenter
                    diameter: 48
                    from: 0
                    to: root.channel.audio ? 4 : 10
                    stepSize: 0.1
                    showValue: false
                    minLabel: "0.0"
                    maxLabel: root.channel.audio ? "4.0" : "10.0"
                    color: root.channelColor
                    ToolTip.visible: hovered
                    ToolTip.text: root.channel.audio ? qsTr("0.0 to 4.0 Vpp") : qsTr("0.0 to 10.0 Vpp")
                    value: root.channel.amplitude
                    onMoved: v => root.channel.amplitude = Math.round(v * 100) / 100
                }
                SiField {
                    Layout.fillWidth: true
                    unit: "Vpp"
                    showArrows: false
                    engineering: false
                    decimals: 2
                    stepSize: 0.01
                    from: 0
                    to: root.channel.audio ? 4 : 10
                    toolTip: qsTr("Amplitude, peak to peak")
                    value: root.channel.amplitude
                    onValueModified: v => root.channel.amplitude = v
                }
            }
        }
        Field {
            label: qsTr("DC Offset")
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Knob {
                    Layout.alignment: Qt.AlignHCenter
                    diameter: 48
                    from: -5
                    to: 5
                    stepSize: 0.1
                    showValue: false
                    minLabel: "-5.0"
                    maxLabel: "5.0"
                    color: root.channelColor
                    enabled: !root.channel.audio
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("-5.0 to 5.0 V")
                    value: root.channel.offset
                    onMoved: v => root.channel.offset = Math.round(v * 50) / 50
                }
                SiField {
                    Layout.fillWidth: true
                    unit: "V"
                    showArrows: false
                    engineering: false
                    decimals: 2
                    stepSize: 0.02
                    from: -5
                    to: 5
                    enabled: !root.channel.audio
                    toolTip: root.channel.audio ? qsTr("The audio outputs are AC coupled: no DC offset") : qsTr("DC Offset")
                    value: root.channel.offset
                    onValueModified: v => root.channel.offset = v
                }
            }
        }
    }

    FieldRow {
        visible: root.isFunction
        Layout.fillWidth: true
        Field {
            label: qsTr("Duty Cycle")
            disabled: root.channel.shape !== GenChannel.Square
            SiField {
                Layout.fillWidth: true
                unit: "%"
                engineering: false
                decimals: 0
                stepSize: 1
                from: 0
                to: 100
                enabled: root.channel.shape === GenChannel.Square
                value: root.channel.dutyCycle
                onValueModified: v => root.channel.dutyCycle = v
            }
        }
        Field {
            label: qsTr("Phase")
            hint: qsTr("Phase of this channel's waveform (degrees), e.g. 90 for a quadrature pair")
            SiField {
                Layout.fillWidth: true
                unit: "°"
                engineering: false
                decimals: 1
                stepSize: 1
                from: -180
                to: 180
                value: root.channel.phase
                onValueModified: v => root.channel.phase = v
            }
        }
    }

    GroupLabel { visible: root.isFunction; text: qsTr("Sweep Settings") }

    FieldRow {
        visible: root.isFunction
        Layout.fillWidth: true
        Field {
            label: qsTr("Start Frequency")
            SiField {
                Layout.fillWidth: true
                unit: "Hz"; from: 0.2; to: 20000; digits: 5
                enabled: !root.channel.sweeping
                value: root.channel.sweepStart
                onValueModified: v => root.channel.sweepStart = v
            }
        }
        Field {
            label: qsTr("Stop Frequency")
            SiField {
                Layout.fillWidth: true
                unit: "Hz"; from: 0.2; to: 20000; digits: 5
                enabled: !root.channel.sweeping
                value: root.channel.sweepStop
                onValueModified: v => root.channel.sweepStop = v
            }
        }
    }
    FieldRow {
        visible: root.isFunction
        Layout.fillWidth: true
        Field {
            label: qsTr("Step")
            SiField {
                Layout.fillWidth: true
                unit: "Hz"; from: 0; to: 20000; digits: 5
                enabled: !root.channel.sweeping
                value: root.channel.sweepStep
                onValueModified: v => root.channel.sweepStep = v
            }
        }
        Field {
            label: qsTr("Step Interval")
            SiField {
                Layout.fillWidth: true
                unit: "ms"; engineering: false; decimals: 0; stepSize: 1; from: 1; to: 300000
                enabled: !root.channel.sweeping
                toolTip: qsTr("1 to 300 000 ms")
                value: root.channel.sweepInterval
                onValueModified: v => root.channel.sweepInterval = v
            }
        }
    }
    CheckBox {
        visible: root.isFunction
        Layout.fillWidth: true
        text: qsTr("Whole 250 ms steps (as ELVISmx)")
        enabled: !root.channel.sweeping
        checked: root.channel.sweepBlockTiming
        ToolTip.visible: hovered
        ToolTip.text: qsTr("On: each step lasts the Step Interval rounded up to a multiple of 250 ms, like ELVISmx. Off: exactly the Step Interval.")
        onToggled: root.channel.sweepBlockTiming = checked
    }
    RowLayout {
        visible: root.isFunction
        Layout.fillWidth: true
        spacing: Theme.spacingS
        ActionButton {
            text: root.channel.sweeping ? qsTr("Stop Sweep") : qsTr("Sweep")
            iconName: root.channel.sweeping ? "stop" : "sweep"
            tone: root.channel.sweeping ? "danger" : "subtle"
            color: root.channelColor
            compact: true
            enabled: root.channel.sweeping || !root.channel.outputOn
            toolTip: root.channel.outputOn && !root.channel.sweeping
                     ? qsTr("Stop the output first: a sweep starts from a stopped channel")
                     : qsTr("Single linear sweep from Start to Stop Frequency, then the output stops")
            onClicked: root.channel.sweeping ? root.controller.setOutput(root.channel.index, false)
                                             : root.controller.startSweep(root.channel.index)
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            visible: root.channel.sweeping
            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: root.channel.sweepProgress
            }
            Text {
                text: qsTr("Step %1 of %2").arg(root.channel.sweepStepIndex).arg(root.channel.sweepStepCount)
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                font.features: Theme.tabularFigures
            }
        }
    }

    // --- Arbitrary ------------------------------------------------------------------------------
    GroupLabel { visible: root.isArbitrary; text: qsTr("Waveform Settings") }

    Rectangle {
        visible: root.isArbitrary
        Layout.fillWidth: true
        Layout.preferredHeight: 64
        radius: Theme.radiusM
        color: Theme.display
        border.width: 1
        border.color: Theme.displayBorder
        BenchPlot {
            anchors.fill: parent
            anchors.margins: 4
            anchors.bottomMargin: 18
            bare: true
            visible: root.channel.hasWaveform
            xAxis { min: 0; max: Math.max(1, root.channel.thumbnail.count - 1) }
            yAxis { autoScale: true; minimumSpan: 0.1 }
            PlotTrace { buffer: root.channel.thumbnail; color: root.channelColor; width: 1.2 }
        }
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            text: root.channel.waveformName
            horizontalAlignment: root.channel.hasWaveform ? Text.AlignLeft : Text.AlignHCenter
            color: root.channel.hasWaveform ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.fontSizeS
            elide: Text.ElideMiddle
        }
        HoverHandler { id: thumbHover }
        ToolTip.visible: thumbHover.hovered
        ToolTip.text: root.channel.waveformDetails
    }

    RowLayout {
        visible: root.isArbitrary
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        ActionButton {
            text: qsTr("Open")
            iconName: "folder-open"
            compact: true
            toolTip: qsTr("Load a waveform file (.mbw, .wav, .csv, .txt, .bin, .wdt, .wfm)")
            onClicked: openDialog.open()
        }
        ActionButton {
            text: qsTr("Library")
            iconName: "list"
            compact: true
            toolTip: qsTr("Ready-made waveforms of the 21 library functions")
            onClicked: libraryMenu.popup()
            Menu {
                id: libraryMenu
                Repeater {
                    model: root.controller.libraryNames
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        onTriggered: root.controller.loadLibraryWaveform(root.channel.index, index)
                    }
                }
            }
        }
        IconButton {
            iconName: "clock"
            size: Theme.controlHeightS
            toolTip: qsTr("Recent files")
            enabled: root.controller.recentFiles.length > 0
            onClicked: recentMenu.popup()
            Menu {
                id: recentMenu
                Repeater {
                    model: root.controller.recentFiles
                    delegate: MenuItem {
                        required property string modelData
                        text: modelData.split(/[\\/]/).pop()
                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                        onTriggered: root.controller.loadWaveformPath(root.channel.index, modelData)
                    }
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Clear recent files"); onTriggered: root.controller.clearRecentFiles() }
            }
        }
        Item { Layout.fillWidth: true }
        IconButton {
            iconName: "edit"
            size: Theme.controlHeightS
            toolTip: qsTr("Edit in the Waveform Editor")
            onClicked: root.controller.editChannel(root.channel.index)
        }
        IconButton {
            iconName: "more-horizontal"
            size: Theme.controlHeightS
            toolTip: qsTr("More")
            onClicked: arbMenu.popup()
            Menu {
                id: arbMenu
                MenuItem {
                    text: qsTr("Export waveform…")
                    icon.source: Theme.icon("download")
                    enabled: root.channel.hasWaveform
                    onTriggered: exportDialog.open()
                }
                MenuItem {
                    text: qsTr("Clear waveform")
                    icon.source: Theme.icon("trash")
                    enabled: root.channel.hasWaveform && !root.channel.outputOn
                    onTriggered: root.controller.clearWaveform(root.channel.index)
                }
            }
        }
    }

    FieldRow {
        visible: root.isArbitrary
        Layout.fillWidth: true
        Field {
            label: qsTr("Gain")
            SiField {
                Layout.fillWidth: true
                engineering: false
                decimals: 2
                stepSize: 0.01
                from: -10
                to: 10
                toolTip: qsTr("-10.00 to 10.00; samples beyond the output range are clipped")
                value: root.channel.gain
                onValueModified: v => root.channel.gain = v
            }
        }
        Field {
            label: qsTr("Waveform Points")
            Text {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeight
                verticalAlignment: Text.AlignVCenter
                text: root.channel.waveformPoints + (root.channel.hasWaveform ? "  ·  " + Units.format(root.channel.waveformDuration, "s", 3) : "")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.features: Theme.tabularFigures
            }
        }
    }
    Text {
        visible: root.isArbitrary && root.channel.clippedSamples > 0
        Layout.fillWidth: true
        text: qsTr("%1 samples exceed ±%2 V after gain and are clipped.").arg(root.channel.clippedSamples).arg(root.channel.outputRange)
        color: Theme.warning
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    GroupLabel { visible: root.isArbitrary; text: qsTr("Timing and Triggering Settings") }

    FieldRow {
        visible: root.isArbitrary
        Layout.fillWidth: true
        Field {
            label: qsTr("Update Rate")
            SiField {
                Layout.fillWidth: true
                unit: "S/s"
                from: 0.1
                to: DeviceHub.maxAoRate > 0 ? DeviceHub.maxAoRate : 200000
                digits: 4
                toolTip: root.controller.linkUpdateRates ? qsTr("0.1 S/s to 200 kS/s, shared by both channels (Link update rates)")
                                                         : qsTr("0.1 S/s to 200 kS/s")
                value: root.channel.updateRate
                onValueModified: v => root.channel.updateRate = v
            }
        }
    }
    FieldRow {
        visible: root.isArbitrary
        Layout.fillWidth: true
        Field {
            label: qsTr("Generation Mode")
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("Run Continuously"), qsTr("Run Once")]
                enabled: !root.channel.outputOn
                currentIndex: root.channel.generationMode
                onActivated: index => root.channel.generationMode = index
            }
        }
        Field {
            label: qsTr("Interpolation")
            hint: qsTr("Hold: each sample is held until the next update (a DAC at the Update Rate). Linear: straight lines between samples (streaming engine).")
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("Hold"), qsTr("Linear")]
                currentIndex: root.channel.interpolation
                onActivated: index => root.channel.interpolation = index
            }
        }
    }

    CheckBox {
        visible: root.isArbitrary
        Layout.fillWidth: true
        text: qsTr("Return to 0 V after Run Once")
        enabled: !root.channel.outputOn
        checked: root.channel.zeroAfterRunOnce
        ToolTip.visible: hovered
        ToolTip.text: qsTr("On: after one pass the output returns to 0 V and shows OFF (as ELVISmx). Off: it holds the last sample (Done) until stopped.")
        onToggled: root.channel.zeroAfterRunOnce = checked
    }

    // --- DC -------------------------------------------------------------------------------------------
    GroupLabel { visible: root.isDc; text: qsTr("Voltage") }

    RowLayout {
        visible: root.isDc
        Layout.fillWidth: true
        spacing: Theme.spacingS
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Slider {
                id: levelSlider
                Layout.fillWidth: true
                from: -root.channel.outputRange
                to: root.channel.outputRange
                stepSize: 0.01
                snapMode: Slider.SnapAlways
                value: root.channel.dcLevel
                onMoved: root.channel.dcLevel = Math.round(value * 100) / 100
            }
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 12
                Repeater {
                    model: 5
                    delegate: Text {
                        required property int index
                        readonly property real tick: -root.channel.outputRange + index * root.channel.outputRange / 2
                        x: levelSlider.leftPadding + levelSlider.availableWidth * index / 4 - width / 2
                        text: Number(tick.toFixed(1)).toString()
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }
        }
        SiField {
            Layout.preferredWidth: 96
            Layout.alignment: Qt.AlignTop
            unit: "V"
            engineering: false
            decimals: 2
            stepSize: 0.01
            toolTip: qsTr("Level in 0.01 V steps; arrows and the wheel tune the digit at the cursor")
            from: -root.channel.outputRange
            to: root.channel.outputRange
            value: root.channel.dcLevel
            onValueModified: v => root.channel.dcLevel = v
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open waveform for %1").arg(root.channel.name)
        currentFolder: root.controller.lastWaveformFolder
        nameFilters: [qsTr("Waveforms (*.mbw *.wav *.csv *.txt *.bin *.wdt *.wfm)"), qsTr("MYBENCH waveform (*.mbw)"),
                      qsTr("WAV audio (*.wav)"), qsTr("Text data (*.csv *.txt)"), qsTr("Binary 16-bit (*.bin)"),
                      qsTr("LabVIEW waveform (*.wdt)"), qsTr("NI Waveform Editor document (*.wfm)"), qsTr("All files (*)")]
        onAccepted: root.controller.loadWaveform(root.channel.index, selectedFile)
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export %1 waveform").arg(root.channel.name)
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)"), qsTr("WAV audio (*.wav)")]
        defaultSuffix: "csv"
        onAccepted: root.controller.exportChannel(root.channel.index, selectedFile)
    }
}
