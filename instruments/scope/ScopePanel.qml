pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Oscilloscope panel (docs/instruments/scope.md). Display: scope graph with markers, cursors and
// measurement rows (ScopeDisplay). Inspector: the ELVISmx groups (Channel 0/1 Settings, Timebase,
// Trigger, Cursors Settings, Display Measurements, Graph Properties, Advanced Settings) plus the
// MYBENCH Math, Reference and Display cards. Run bar: Run (Run Continuously), Single (Run Once),
// Autoscale, status and the Timeout LED.
Item {
    id: root

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeMath math: controller.math
    readonly property bool edge: controller.triggerType === ScopeController.Edge
    readonly property int triggerChannel: controller.triggerSource === ScopeController.Chan1Source ? 1 : 0

    function channelName(index: int): string {
        return index === 2 ? qsTr("Math") : "CH " + index
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: {
            let t = root.controller.timeDivLabel + "/div"
            if (root.controller.rolling)
                return t + "  ·  " + qsTr("Roll")
            if (!root.edge)
                return t + "  ·  " + qsTr("Immediate")
            return t + "  ·  " + qsTr("Edge %1 CH %2 %3").arg(root.controller.triggerSlope === ScopeController.Rising ? "↑" : "↓")
                                                          .arg(root.triggerChannel)
                                                          .arg(Units.format(root.controller.triggerLevel, "V", 3))
        }
        displayInfo: (root.controller.running && root.controller.waitingForTrigger ? qsTr("*** Waiting for Trigger ***") + "   " : "")
                     + qsTr("Sample Rate: %1").arg(root.controller.formatRate(root.controller.running ? root.controller.sampleRate
                                                                                                     : root.controller.recordRate))
        runBar.showTimeout: true
        runBar.timeoutText: qsTr("Timeout")

        ScopeDisplay {
            anchors.fill: parent
            controller: root.controller
        }

        inspector: [
            ScopeChannelSection {
                controller: root.controller
                channel: root.ch0
                channelColor: root.ch0.style.lineColor.length > 0 ? root.ch0.style.lineColor : Theme.ch0
            },
            ScopeChannelSection {
                controller: root.controller
                channel: root.ch1
                channelColor: root.ch1.style.lineColor.length > 0 ? root.ch1.style.lineColor : Theme.ch1
                collapsed: !root.ch1.enabled
            },

            // ---- Timebase -------------------------------------------------------------------------
            Section {
                title: qsTr("Timebase")
                iconName: "clock"
                summary: root.controller.timeDivLabel + "/div"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingL
                    Knob {
                        // Bench-scope direction: clockwise = smaller Time/Div (faster), as the wheel.
                        readonly property int last: root.controller.timeDivLabels.length - 1
                        label: qsTr("Time/Div")
                        values: root.controller.timeDivValues.slice().reverse()
                        labels: root.controller.timeDivLabels.slice().reverse()
                        currentIndex: last - root.controller.timeDivIndex
                        showValue: false
                        diameter: 62
                        onIndexMoved: index => root.controller.timeDivIndex = last - index
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        ScopeCombo {
                            readonly property int last: root.controller.timeDivLabels.length - 1
                            model: root.controller.timeDivLabels.slice().reverse()
                            currentIndex: last - root.controller.timeDivIndex
                            Accessible.name: qsTr("Time/Div")
                            onActivated: index => root.controller.timeDivIndex = last - index
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("%1 · %2 samples").arg(Units.format(root.controller.plannedRate, "S/s", 4))
                                                         .arg(root.controller.plannedRecordLength)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeS
                            font.features: Theme.tabularFigures
                            elide: Text.ElideRight
                        }
                        ToggleSwitch {
                            text: qsTr("Roll mode")
                            enabled: root.controller.rollAvailable
                            checked: root.controller.rollMode
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("From 50 ms/div: the trace scrolls continuously, newest samples on the right (untriggered)")
                            onToggled: root.controller.rollMode = checked
                        }
                    }
                }
            },

            // ---- Trigger --------------------------------------------------------------------------
            Section {
                title: qsTr("Trigger")
                iconName: "zap"
                summary: root.edge ? qsTr("Edge") : qsTr("Immediate")
                FieldRow {
                    Field {
                        label: qsTr("Type")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Immediate"), toolTip: qsTr("Free-running: every record is displayed") },
                                    { text: qsTr("Edge"), toolTip: qsTr("Trigger on a slope crossing the level") }]
                            currentIndex: root.edge ? 1 : 0
                            onActivated: index => root.controller.triggerType = index === 1 ? ScopeController.Edge : ScopeController.Immediate
                        }
                    }
                    Field {
                        label: qsTr("Slope")
                        disabled: !root.edge
                        SegmentedControl {
                            Layout.fillWidth: true
                            enabled: root.edge
                            model: [{ icon: "slope-rising-line", toolTip: qsTr("Rising (positive) edge") },
                                    { icon: "slope-falling-line", toolTip: qsTr("Falling (negative) edge") }]
                            currentIndex: root.controller.triggerSlope
                            onActivated: index => root.controller.triggerSlope = index
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Source")
                        disabled: !root.edge
                        ScopeCombo {
                            enabled: root.edge
                            model: [qsTr("CH 0"), qsTr("CH 1")]
                            disabledIndices: {
                                let list = []
                                if (!root.ch0.enabled)
                                    list.push(0)
                                if (!root.ch1.enabled)
                                    list.push(1)
                                return list
                            }
                            disabledToolTip: qsTr("Enable the channel first")
                            currentIndex: root.controller.triggerSource
                            toolTip: qsTr("The trigger uses the signal of the chosen channel (which must be enabled)")
                            onActivated: index => {
                                if (isDisabled(index))
                                    currentIndex = Qt.binding(() => root.controller.triggerSource)
                                else
                                    root.controller.triggerSource = index
                            }
                        }
                    }
                    Field {
                        label: qsTr("Level (V)")
                        disabled: !root.edge
                        SiField {
                            Layout.fillWidth: true
                            enabled: root.edge
                            unit: "V"
                            from: -100
                            to: 100
                            stepSize: 0.1
                            digits: 4
                            value: root.controller.triggerLevel
                            toolTip: qsTr("Trigger level, -100 … 100 V (drag the T marker on the right edge of the graph)")
                            onValueModified: v => root.controller.triggerLevel = v
                        }
                    }
                }
                Field {
                    label: qsTr("Horizontal Position (%)")
                    disabled: !root.edge
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: root.edge
                        spacing: Theme.spacingS
                        Slider {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            stepSize: 1
                            value: root.controller.horizontalPosition
                            onMoved: root.controller.horizontalPosition = value
                        }
                        SiField {
                            Layout.preferredWidth: 78
                            engineering: false
                            decimals: 0
                            unit: "%"
                            from: 0
                            to: 100
                            stepSize: 1
                            value: root.controller.horizontalPosition
                            toolTip: qsTr("Where the trigger point sits on the screen (drag the T marker at the top)")
                            onValueModified: v => root.controller.horizontalPosition = v
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Mode")
                        disabled: !root.edge
                        SegmentedControl {
                            Layout.fillWidth: true
                            enabled: root.edge
                            model: [{ text: qsTr("Auto"), toolTip: qsTr("Free-run (untriggered) after the timeout, so the trace never freezes") },
                                    { text: qsTr("Normal"), toolTip: qsTr("Wait for the trigger indefinitely; the last triggered trace stays on screen") }]
                            currentIndex: root.controller.noTriggerMode === ScopeController.ShowUntriggered ? 0
                                          : root.controller.noTriggerMode === ScopeController.KeepLastTrace ? 1 : -1
                            onActivated: index => root.controller.noTriggerMode = index === 0 ? ScopeController.ShowUntriggered
                                                                                              : ScopeController.KeepLastTrace
                        }
                    }
                    Field {
                        label: qsTr("Timeout")
                        disabled: !root.edge
                        ScopeCombo {
                            readonly property var values: [0.1, 0.25, 0.5, 1, 2, 5, 10, 0]
                            enabled: root.edge
                            model: ["100 ms", "250 ms", "500 ms", "1 s", "2 s", "5 s", "10 s", qsTr("Never")]
                            currentIndex: Math.max(0, values.indexOf(root.controller.triggerTimeout))
                            toolTip: qsTr("How long a missing trigger is tolerated before Auto free-runs (or the waiting state shows). Rare events: use Normal with a long timeout.")
                            onActivated: index => root.controller.triggerTimeout = values[index]
                        }
                    }
                }
                Field {
                    label: qsTr("Holdoff")
                    disabled: !root.edge
                    Layout.fillWidth: true
                    SiField {
                        Layout.fillWidth: true
                        enabled: root.edge
                        unit: "s"
                        from: 0
                        to: 10
                        stepSize: 0.01
                        digits: 3
                        value: root.controller.triggerHoldoff
                        toolTip: qsTr("Minimum time between two displayed triggers: 0 … 10 s (slows the update, steadies bursts)")
                        onValueModified: v => root.controller.triggerHoldoff = v
                    }
                }
                Text {
                    visible: root.edge && root.controller.rolling
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Roll mode is on: the edge trigger is not used.")
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeS
                }
            },

            // ---- Cursors Settings -----------------------------------------------------------------
            Section {
                title: qsTr("Cursors Settings")
                iconName: "crosshair"
                collapsed: !root.controller.cursorsOn
                summary: root.controller.cursorsOn ? qsTr("On") : qsTr("Off")
                headerTools: ToggleSwitch {
                    checked: root.controller.cursorsOn
                    Accessible.name: qsTr("Cursors On")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Cursors On")
                    onToggled: root.controller.cursorsOn = checked
                }
                FieldRow {
                    Field {
                        label: "C1"
                        disabled: !root.controller.cursorsOn
                        ScopeCombo {
                            enabled: root.controller.cursorsOn
                            model: ["CH 0", "CH 1", qsTr("Math")]
                            disabledIndices: root.math.enabled && !root.math.fft ? [] : [2]
                            disabledToolTip: qsTr("Enable the math trace first")
                            currentIndex: root.controller.cursor1.channel
                            onActivated: index => root.controller.cursor1.channel = index
                        }
                    }
                    Field {
                        label: "C2"
                        disabled: !root.controller.cursorsOn
                        ScopeCombo {
                            enabled: root.controller.cursorsOn
                            model: ["CH 0", "CH 1", qsTr("Math")]
                            disabledIndices: root.math.enabled && !root.math.fft ? [] : [2]
                            disabledToolTip: qsTr("Enable the math trace first")
                            currentIndex: root.controller.cursor2.channel
                            onActivated: index => root.controller.cursor2.channel = index
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Drag C1/C2 on the graph; click one, then ← → to nudge.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        text: qsTr("Center")
                        compact: true
                        tone: "ghost"
                        enabled: root.controller.cursorsOn
                        onClicked: root.controller.resetCursors()
                    }
                }
                Divider { Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    ToggleSwitch {
                        text: qsTr("Level cursors")
                        checked: root.controller.levelCursorsOn
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Two horizontal lines (H1, H2) with their voltage difference; drag them on the graph")
                        onToggled: root.controller.levelCursorsOn = checked
                    }
                    Item { Layout.fillWidth: true }
                    ScopeCombo {
                        Layout.preferredWidth: 96
                        enabled: root.controller.levelCursorsOn
                        model: ["CH 0", "CH 1", qsTr("Math")]
                        disabledIndices: root.math.enabled && !root.math.fft ? [] : [2]
                        disabledToolTip: qsTr("Enable the math trace first")
                        currentIndex: root.controller.levelCursorChannel
                        onActivated: index => root.controller.levelCursorChannel = index
                    }
                }
            },

            // ---- Display Measurements -------------------------------------------------------------
            Section {
                title: qsTr("Display Measurements")
                iconName: "ruler"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingL
                    CheckBox {
                        text: "CH 0"
                        enabled: root.ch0.enabled
                        checked: root.ch0.displayMeasurements
                        onToggled: root.ch0.displayMeasurements = checked
                    }
                    CheckBox {
                        text: "CH 1"
                        enabled: root.ch1.enabled
                        checked: root.ch1.displayMeasurements
                        onToggled: root.ch1.displayMeasurements = checked
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Choose the readouts (RMS, Freq, Vp-p, mean, period, duty, rise/fall…) in Graph Properties › Measurements.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },

            // ---- Math (MYBENCH) -------------------------------------------------------------------
            Section {
                title: qsTr("Math")
                accentColor: root.math.style.lineColor.length > 0 ? root.math.style.lineColor : Theme.math
                collapsed: !root.math.enabled
                summary: root.math.enabled ? root.math.functionName : qsTr("Off")
                headerTools: ToggleSwitch {
                    accentColor: Theme.math
                    checked: root.math.enabled
                    Accessible.name: qsTr("Math trace")
                    onToggled: root.math.enabled = checked
                }
                Field {
                    label: qsTr("Function")
                    Layout.fillWidth: true
                    ScopeCombo {
                        model: ["CH 0 + CH 1", "CH 0 − CH 1", "CH 0 × CH 1", "−CH 0", "−CH 1", qsTr("FFT of CH 0"), qsTr("FFT of CH 1")]
                        currentIndex: root.math.operation
                        onActivated: index => root.math.operation = index
                    }
                }
                RowLayout {
                    visible: !root.math.fft
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    Field {
                        label: qsTr("Scale (%1/Div)").arg(root.math.unit)
                        Layout.fillWidth: true
                        ScopeCombo {
                            readonly property int last: root.math.scaleLabels.length - 1
                            model: root.math.scaleLabels.slice().reverse()
                            currentIndex: last - root.math.scaleIndex
                            onActivated: index => root.math.scaleIndex = last - index
                        }
                    }
                    Field {
                        label: qsTr("Position (Div)")
                        Layout.fillWidth: true
                        SiField {
                            Layout.fillWidth: true
                            engineering: false
                            decimals: 2
                            from: -5
                            to: 5
                            stepSize: 0.1
                            value: root.math.position
                            onValueModified: v => root.math.position = v
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    text: root.math.fft ? qsTr("Magnitude spectrum of the displayed record (Hanning window, dBV RMS) under the graph. For averaging, windows and distortion use the Spectrum Analyzer.")
                                        : qsTr("Computed on the displayed record; CH 0 and CH 1 must be enabled for + − ×.")
                }
            },

            // ---- Reference traces (MYBENCH) -------------------------------------------------------
            Section {
                title: qsTr("Reference Traces")
                accentColor: Theme.ref
                collapsed: true
                summary: (root.controller.refAAvailable ? "A" : "") + (root.controller.refBAvailable ? " B" : "")
                Repeater {
                    model: 2
                    delegate: RowLayout {
                        id: refRow
                        required property int index
                        readonly property bool available: index === 0 ? root.controller.refAAvailable : root.controller.refBAvailable
                        readonly property bool shown: index === 0 ? root.controller.refAVisible : root.controller.refBVisible
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Text {
                            Layout.preferredWidth: 44
                            text: refRow.index === 0 ? "REF A" : "REF B"
                            color: Theme.ref
                            font.pixelSize: Theme.fontSizeS
                            font.weight: Font.DemiBold
                        }
                        ActionButton {
                            text: qsTr("Save")
                            compact: true
                            iconName: "save"
                            enabled: root.controller.hasRecord
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Freeze a trace of the displayed record as the reference")
                            onClicked: saveMenu.popup()
                            Menu {
                                id: saveMenu
                                MenuItem { text: "CH 0"; enabled: root.ch0.enabled; onTriggered: root.controller.saveReference(refRow.index, 0) }
                                MenuItem { text: "CH 1"; enabled: root.ch1.enabled; onTriggered: root.controller.saveReference(refRow.index, 1) }
                                MenuItem { text: qsTr("Math"); enabled: root.math.enabled && !root.math.fft; onTriggered: root.controller.saveReference(refRow.index, 2) }
                            }
                        }
                        IconButton {
                            iconName: refRow.shown ? "eye" : "eye-off"
                            enabled: refRow.available
                            toolTip: refRow.shown ? qsTr("Hide") : qsTr("Show")
                            onClicked: {
                                if (refRow.index === 0)
                                    root.controller.refAVisible = !refRow.shown
                                else
                                    root.controller.refBVisible = !refRow.shown
                            }
                        }
                        IconButton {
                            iconName: "download"
                            enabled: refRow.available
                            toolTip: qsTr("Save the reference to a file…")
                            onClicked: {
                                refDialog.slot = refRow.index
                                refDialog.saving = true
                                refDialog.selectedFile = DataExport.suggestedFileUrl("scope-ref", "csv")
                                refDialog.open()
                            }
                        }
                        IconButton {
                            iconName: "upload"
                            toolTip: qsTr("Load a reference from a CSV/TSV file…")
                            onClicked: {
                                refDialog.slot = refRow.index
                                refDialog.saving = false
                                refDialog.open()
                            }
                        }
                        IconButton {
                            iconName: "trash"
                            enabled: refRow.available
                            toolTip: qsTr("Clear")
                            onClicked: root.controller.clearReference(refRow.index)
                        }
                        Item { Layout.fillWidth: true }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    text: root.controller.refAAvailable || root.controller.refBAvailable
                          ? qsTr("A: %1\nB: %2").arg(root.controller.refAAvailable ? root.controller.refALabel : "—")
                                                 .arg(root.controller.refBAvailable ? root.controller.refBLabel : "—")
                          : qsTr("References keep the scale and position of the trace they were saved from.")
                }
            },

            // ---- Display (MYBENCH) ----------------------------------------------------------------
            Section {
                title: qsTr("Display")
                iconName: "eye"
                collapsed: true
                summary: (root.controller.displayMode === ScopeController.XY ? "XY" : "YT")
                         + (root.controller.persistence > 0 ? " · " + qsTr("persistence") : "")
                FieldRow {
                    Field {
                        label: qsTr("Mode")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [{ text: "YT", toolTip: qsTr("Voltage versus time") },
                                    { text: "XY", toolTip: qsTr("CH 1 (Y) versus CH 0 (X)") }]
                            currentIndex: root.controller.displayMode
                            onActivated: index => root.controller.displayMode = index
                        }
                    }
                    Field {
                        label: qsTr("Persistence")
                        ScopeCombo {
                            readonly property var values: [0, 100, 250, 500, 1000, 2000, 5000]
                            model: [qsTr("Off"), "100 ms", "250 ms", "500 ms", "1 s", "2 s", "5 s"]
                            currentIndex: Math.max(0, values.indexOf(root.controller.persistence))
                            onActivated: index => root.controller.persistence = values[index]
                        }
                    }
                }
                Field {
                    label: qsTr("Averaging")
                    Layout.fillWidth: true
                    ScopeCombo {
                        readonly property var values: [1, 2, 4, 8, 16, 32, 64]
                        model: [qsTr("Off"), qsTr("2 records"), qsTr("4 records"), qsTr("8 records"),
                                qsTr("16 records"), qsTr("32 records"), qsTr("64 records")]
                        currentIndex: Math.max(0, values.indexOf(root.controller.averaging))
                        toolTip: qsTr("Running mean of the last triggered records of each channel: less noise on repetitive signals. Restarts when a scale, the time base or the trigger changes.")
                        onActivated: index => root.controller.averaging = values[index]
                    }
                }
            },

            ScopeGraphProperties {
                controller: root.controller
            },

            // ---- Advanced Settings ----------------------------------------------------------------
            Section {
                title: qsTr("Advanced Settings")
                iconName: "settings"
                collapsed: true
                Repeater {
                    model: 2
                    delegate: ColumnLayout {
                        id: advancedChannel
                        required property int index
                        readonly property ScopeChannel channel: index === 0 ? root.ch0 : root.ch1
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Text {
                            text: qsTr("Channel %1 Settings").arg(advancedChannel.index)
                            color: advancedChannel.index === 0 ? Theme.ch0 : Theme.ch1
                            font.pixelSize: Theme.fontSizeS
                            font.weight: Font.DemiBold
                        }
                        FieldRow {
                            Field {
                                label: qsTr("Input Range")
                                ScopeCombo {
                                    enabled: !advancedChannel.channel.audio
                                    model: [qsTr("Auto (%1)").arg(advancedChannel.channel.audio ? "±2 V"
                                                                  : advancedChannel.channel.voltsPerDiv >= 0.5 ? "±10 V" : "±2 V"),
                                            "±10 V", "±2 V"]
                                    currentIndex: advancedChannel.channel.audio ? 2 : advancedChannel.channel.inputRange
                                    toolTip: qsTr("Auto: ±10 V from 500 mV/div, ±2 V below (as ELVISmx). Audio inputs are always ±2 V.")
                                    onActivated: index => advancedChannel.channel.inputRange = index
                                }
                            }
                        }
                    }
                }
                Divider { Layout.fillWidth: true }
                Text {
                    text: qsTr("Trigger")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS
                    font.weight: Font.DemiBold
                }
                FieldRow {
                    Field {
                        label: qsTr("Hysteresis (div)")
                        SiField {
                            Layout.fillWidth: true
                            engineering: false
                            decimals: 2
                            from: 0
                            to: 2
                            stepSize: 0.05
                            value: root.controller.triggerHysteresis
                            toolTip: qsTr("0 = ELVISmx (no hysteresis). Larger values ignore noise around the level.")
                            onValueModified: v => root.controller.triggerHysteresis = v
                        }
                    }
                    Field {
                        label: qsTr("On timeout")
                        ScopeCombo {
                            model: [qsTr("Clear graph"), qsTr("Keep last trace"), qsTr("Free-run (Auto)")]
                            currentIndex: root.controller.noTriggerMode
                            toolTip: qsTr("What an Edge acquisition shows once the trigger timeout has passed without a crossing: an empty graph (ELVISmx), the last triggered trace (dimmed), or the untriggered record")
                            onActivated: index => root.controller.noTriggerMode = index
                        }
                    }
                }
                Divider { Layout.fillWidth: true }
                Text {
                    text: qsTr("Acquisition and measurements")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS
                    font.weight: Font.DemiBold
                }
                Field {
                    label: qsTr("Record length")
                    Layout.fillWidth: true
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ text: qsTr("Standard (2500 points)"), toolTip: qsTr("ELVISmx: at most 2501 samples per screen") },
                                { text: qsTr("High resolution"), toolTip: qsTr("Up to 100 000 samples per screen (200 kS/s up to 50 ms/div), for zooming and FFT") }]
                        currentIndex: root.controller.samplingMode
                        onActivated: index => root.controller.samplingMode = index
                    }
                }
                Field {
                    label: qsTr("Frequency method")
                    Layout.fillWidth: true
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ text: qsTr("Tone (ELVISmx)"), toolTip: qsTr("FFT single-tone estimate, shown when the tone exceeds 0.2 div") },
                                { text: qsTr("Edge counting"), toolTip: qsTr("Mid-level crossings with 10 % hysteresis: better for square and pulse signals") }]
                        currentIndex: root.controller.frequencyMethod
                        onActivated: index => root.controller.frequencyMethod = index
                    }
                }
                CheckBox {
                    text: qsTr("Autoscale sets the trigger level")
                    checked: root.controller.autoscaleSetsLevel
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("With Edge trigger, Autoscale also moves Level (V) to the middle of the trigger channel's signal")
                    onToggled: root.controller.autoscaleSetsLevel = checked
                }
            }
        ]

        runBarActions: [
            IconButton {
                iconName: "log"
                toolTip: qsTr("Log the displayed record to a text file (ELVISmx TXT layout)…")
                enabled: root.controller.hasRecord
                onClicked: {
                    logDialog.selectedFile = DataExport.suggestedFileUrl("scope", "txt")
                    logDialog.open()
                }
            }
        ]
        runBarControls: [
            ActionButton {
                text: qsTr("Autoscale")
                iconName: "autoscale"
                implicitHeight: Theme.controlHeightL
                enabled: !root.controller.autoscaling && !root.controller.busy
                         && (root.controller.running || root.controller.canRun)
                toolTip: qsTr("Set Volts/Div and Time/Div from the signals")
                onClicked: root.controller.autoscale()
            }
        ]
    }

    FileDialog {
        id: logDialog
        title: qsTr("Save Log File")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("TXT (*.txt)"), qsTr("All files (*)")]
        defaultSuffix: "txt"
        currentFolder: DataExport.lastFolder
        onAccepted: root.controller.exportElvisText(selectedFile)
    }
    FileDialog {
        id: refDialog
        property int slot: 0
        property bool saving: true
        title: saving ? qsTr("Save reference trace") : qsTr("Load reference trace")
        fileMode: saving ? FileDialog.SaveFile : FileDialog.OpenFile
        nameFilters: [qsTr("Data (*.csv *.tsv *.txt)"), qsTr("All files (*)")]
        defaultSuffix: "csv"
        currentFolder: DataExport.lastFolder
        onAccepted: {
            if (saving)
                root.controller.exportReference(slot, selectedFile)
            else
                root.controller.importReference(slot, selectedFile)
        }
    }
}
