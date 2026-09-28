pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Counter/Timer panel (docs/instruments/ctr.md): ctr0 measures (frequency, period, pulse width,
// semi-period, two-edge separation, edge count, encoders) or generates a pulse train; FREQOUT
// runs beside it. Display: big readout + statistics + trend (Measure), pulse diagram (Pulse
// Train), and the DIO pin map of the current settings.
Item {
    id: root

    required property CtrController controller

    readonly property bool measuring: controller.counterFunction === CtrController.Measure
    readonly property bool pulsing: controller.counterFunction === CtrController.PulseTrain
    readonly property bool counterOff: controller.counterFunction === CtrController.CounterOff
    readonly property int measurement: controller.measurement
    readonly property bool counting: measurement === CtrController.CountEdges
    readonly property bool encoder: controller.encoderMeasurement
    readonly property bool interval: controller.intervalMeasurement

    // Options the current device cannot do are greyed (DeviceHub capability query); the
    // property read inside makes every binding that calls this re-evaluate on a device change.
    function unsupported(feature: string): bool {
        return DeviceHub.unsupportedFeatures.includes(feature)
    }
    readonly property var measurementFeatures: [
        "", "", "", "counter.pulseWidth", "counter.semiPeriod", "counter.twoEdgeSeparation",
        "counter.linearEncoder", "counter.angularEncoder"
    ]

    // Statistics formatting in the measurement's unit.
    function fmt(v: real, spread: bool): string {
        if (isNaN(v))
            return "—"
        if (root.counting)
            return spread ? v.toFixed(2) : String(Math.round(v))
        if (root.encoder)
            return v.toFixed(3) + (root.controller.unit.length > 0 ? " " + root.controller.unit : "")
        return Units.format(v, root.controller.unit, 5)
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.controller.functionTitle
        displayInfo: {
            const parts = []
            if (root.controller.plannedLines !== 0)
                parts.push(root.controller.terminals)
            if (root.measuring && root.interval)
                parts.push(qsTr("Timebase %1").arg(Units.format(root.controller.timebase, "Hz", 3)))
            return parts.join("  ·  ")
        }
        runBar.showSingle: root.measuring
        runBar.singleToolTip: root.controller.sampling === CtrController.Buffered
                              ? qsTr("Measure %1 intervals, then stop").arg(root.controller.bufferedSamples)
                              : qsTr("Take one reading, then stop")
        runBar.showTimeout: root.measuring
        runBar.timeoutText: qsTr("No input")
        runBarControls: [
            ActionButton {
                visible: root.measuring
                text: root.counting || root.encoder ? qsTr("Reset count") : qsTr("Reset stats")
                iconName: "reset"
                compact: true
                toolTip: root.counting || root.encoder ? qsTr("Clear statistics and trend, count again from the initial value")
                                                       : qsTr("Clear statistics and trend")
                onClicked: root.controller.resetMeasurement()
            }
        ]

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingM

            // --- Readout area -----------------------------------------------------------------
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: !root.measuring
                Layout.preferredHeight: root.measuring ? readoutColumn.implicitHeight + Theme.spacingL : -1
                Layout.minimumHeight: readoutColumn.implicitHeight

                ColumnLayout {
                    id: readoutColumn
                    anchors.centerIn: parent
                    width: Math.min(parent.width, 760)
                    spacing: Theme.spacingS

                    // Measure: value + secondary quantity.
                    Readout {
                        visible: root.measuring
                        Layout.fillWidth: true
                        label: root.controller.functionTitle
                        valueText: root.controller.valueText
                        fontSize: Math.max(40, Math.min(84, root.width / 11))
                        stale: !root.controller.running
                        color: Theme.ch0
                        caption: root.controller.secondaryText
                    }

                    // Pulse train: actual frequency and duty cycle.
                    RowLayout {
                        visible: root.pulsing
                        Layout.fillWidth: true
                        spacing: Theme.spacingXl
                        Readout {
                            Layout.fillWidth: true
                            label: qsTr("Frequency")
                            value: root.controller.running && root.controller.outputFrequency > 0 ? root.controller.outputFrequency
                                                                                                  : root.controller.coercedPulseFrequency
                            unit: "Hz"
                            digits: 6
                            fontSize: Math.max(36, Math.min(68, root.width / 15))
                            color: Theme.math
                            stale: !root.controller.running
                            caption: qsTr("Period %1").arg(Units.format(1 / root.controller.coercedPulseFrequency, "s", 5))
                        }
                        Readout {
                            Layout.fillWidth: true
                            label: qsTr("Duty Cycle")
                            valueText: ((root.controller.running && root.controller.outputDutyCycle > 0 ? root.controller.outputDutyCycle
                                                                                                        : root.controller.coercedDutyCycle) * 100).toFixed(2) + " %"
                            fontSize: Math.max(36, Math.min(68, root.width / 15))
                            color: Theme.math
                            stale: !root.controller.running
                            caption: qsTr("High %1 · Low %2").arg(Units.format(root.controller.highTime, "s", 4))
                                                              .arg(Units.format(root.controller.lowTime, "s", 4))
                        }
                    }

                    // FREQOUT alone.
                    Readout {
                        visible: root.counterOff
                        Layout.fillWidth: true
                        label: qsTr("FREQOUT")
                        value: root.controller.freqOutEnabled ? (root.controller.freqOutActual > 0 ? root.controller.freqOutActual
                                                                                                    : root.controller.coercedFreqOut) : NaN
                        unit: "Hz"
                        digits: 6
                        fontSize: Math.max(40, Math.min(84, root.width / 11))
                        color: Theme.ch1
                        stale: !root.controller.running
                        caption: root.controller.freqOutEnabled ? qsTr("Square wave on DIO 4 · %1").arg(root.controller.freqOutDivider)
                                                                : qsTr("Choose a counter function or enable FREQOUT")
                    }

                    Text {
                        visible: root.pulsing
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: {
                            const parts = [root.controller.pulseGeneration === CtrController.FinitePulses
                                           ? qsTr("%1 pulses").arg(root.controller.pulseCount) : qsTr("Continuous"),
                                           qsTr("idle %1").arg(root.controller.idleHigh ? qsTr("high") : qsTr("low"))]
                            if (root.controller.initialDelay > 0)
                                parts.push(qsTr("delay %1").arg(Units.format(root.controller.initialDelay, "s", 4)))
                            if (root.controller.outputState.length > 0)
                                parts.push(root.controller.outputState)
                            return parts.join("  ·  ")
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeM
                    }
                }

                // FREQOUT beside a counter function.
                Rectangle {
                    visible: root.controller.freqOutEnabled && !root.counterOff
                    anchors.top: parent.top
                    anchors.right: parent.right
                    implicitWidth: freqOutRow.implicitWidth + 2 * Theme.spacingM
                    implicitHeight: freqOutRow.implicitHeight + 2 * Theme.spacingS
                    radius: Theme.radiusM
                    color: Theme.alpha(Theme.ch1, Theme.dark ? 0.10 : 0.07)
                    border.color: Theme.alpha(Theme.ch1, 0.45)
                    RowLayout {
                        id: freqOutRow
                        anchors.centerIn: parent
                        spacing: Theme.spacingS
                        Led {
                            on: root.controller.freqOutActual > 0
                            color: Theme.ch1
                            size: 8
                        }
                        Text {
                            text: qsTr("FREQOUT")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeS
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: Units.format(root.controller.freqOutActual > 0 ? root.controller.freqOutActual
                                                                                 : root.controller.coercedFreqOut, "Hz", 5)
                            color: Theme.ch1
                            font.pixelSize: Theme.fontSizeBody
                            font.features: Theme.tabularFigures
                        }
                        Text {
                            text: "DIO 4"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeS
                        }
                    }
                }
            }

            // --- Statistics -------------------------------------------------------------------
            Rectangle {
                visible: root.measuring
                Layout.fillWidth: true
                implicitHeight: statsRow.implicitHeight + 2 * Theme.spacingS
                radius: Theme.radiusM
                color: "transparent"
                border.color: Theme.displayBorder
                RowLayout {
                    id: statsRow
                    anchors.fill: parent
                    anchors.margins: Theme.spacingS
                    spacing: Theme.spacingS
                    CtrStat { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Min"); value: root.fmt(root.controller.statMinimum, false) }
                    CtrStat { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Max"); value: root.fmt(root.controller.statMaximum, false) }
                    CtrStat { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Mean"); value: root.fmt(root.controller.statMean, true) }
                    CtrStat { Layout.fillWidth: true; Layout.preferredWidth: 1; label: qsTr("Std Dev"); value: root.fmt(root.controller.statDeviation, true) }
                    CtrStat {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        label: root.controller.sampling === CtrController.Buffered && root.interval ? qsTr("Intervals") : qsTr("Readings")
                        value: root.controller.statCount > 0 ? String(root.controller.statCount) : "0"
                        color: Theme.textSecondary
                    }
                }
            }

            // --- Trend (Measure) / pulse diagram (Pulse Train) ----------------------------------
            BenchPlot {
                id: trendPlot
                visible: root.measuring
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                placeholderText: root.controller.running ? qsTr("Waiting for the first measurement…") : qsTr("Trend of the readings")
                hoverReadout: true
                xAxis { labelFormat: PlotAxis.Time; follow: true }
                yAxis { autoScale: true; unit: root.controller.unit; unitInLabels: root.controller.unit.length > 0 }
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                PlotTrace {
                    buffer: root.controller.trend
                    name: root.controller.functionTitle
                    unit: root.controller.unit
                    color: Theme.ch0
                    pointsVisible: true
                    step: root.counting
                }
                PlotLine {
                    visible: !isNaN(root.controller.statMean) && root.controller.statCount > 1
                    value: isNaN(root.controller.statMean) ? 0 : root.controller.statMean
                    color: Theme.alpha(Theme.ch0, 0.6)
                    style: PlotElement.Dash
                    handle: PlotLine.End
                    label: qsTr("mean")
                    draggable: false
                }
            }

            BenchPlot {
                visible: root.pulsing
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                interactive: false
                readonly property real period: 1 / root.controller.coercedPulseFrequency
                readonly property int shown: root.controller.pulseGeneration === CtrController.FinitePulses
                                             ? Math.min(root.controller.pulseCount, 4) : 4
                readonly property bool tail: root.controller.pulseGeneration === CtrController.FinitePulses && root.controller.pulseCount <= 4
                xAxis {
                    min: -Math.min(root.controller.initialDelay, period)
                    max: (shown + (tail ? 1 : 0)) * period
                    unit: "s"
                    unitInLabels: true
                }
                yAxis { min: -0.3; max: 1.3; labelsVisible: false; gridVisible: false; interactive: false }
                PlotTrace {
                    buffer: root.controller.pulsePreview
                    color: Theme.math
                    width: 2
                    fill: true
                    fillBaseline: 0
                    fillOpacity: 0.14
                }
                PlotLine { value: 1; color: Theme.alpha(Theme.textMuted, 0.5); style: PlotElement.Dot; handle: PlotLine.Start; label: "H"; draggable: false }
                PlotLine { value: 0; color: Theme.alpha(Theme.textMuted, 0.5); style: PlotElement.Dot; handle: PlotLine.Start; label: "L"; draggable: false }
                PlotLine {
                    orientation: PlotElement.Vertical
                    visible: root.controller.initialDelay > 0
                    value: 0
                    color: Theme.alpha(Theme.math, 0.6)
                    style: PlotElement.Dash
                    handle: PlotLine.Start
                    label: qsTr("first pulse")
                    draggable: false
                }
            }

            // --- Terminals ------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("TERMINALS")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.8
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: root.controller.conflictText.length > 0
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.controller.conflictText
                        color: Theme.warning
                        font.pixelSize: Theme.fontSizeS
                        elide: Text.ElideLeft
                    }
                }
                PinMap {
                    Layout.fillWidth: true
                    pins: root.controller.pins
                }
            }
        }

        inspector: [
            Section {
                title: qsTr("Counter")
                summary: root.measuring ? qsTr("Measure") : root.pulsing ? qsTr("Pulse Train") : qsTr("Off")
                SegmentedControl {
                    Layout.fillWidth: true
                    model: [{ text: qsTr("Measure"), toolTip: qsTr("ctr0 measures a signal") },
                            { text: qsTr("Pulse Train"), toolTip: qsTr("ctr0 generates pulses on DIO 3") },
                            { text: qsTr("Off"), toolTip: qsTr("ctr0 unused (FREQOUT only)") }]
                    currentIndex: root.controller.counterFunction
                    onActivated: index => root.controller.counterFunction = index
                }
            },

            // --- Measure --------------------------------------------------------------------
            Section {
                visible: root.measuring
                title: qsTr("Measurement Settings")
                accentColor: Theme.ch0
                summary: root.controller.terminals
                Field {
                    label: qsTr("Measurement")
                    ComboBox {
                        id: measurementBox
                        Layout.fillWidth: true
                        textRole: "text"
                        model: root.controller.measurementNames.map((name, i) => ({
                            text: name,
                            supported: root.measurementFeatures[i] === "" || !root.unsupported(root.measurementFeatures[i])
                        }))
                        currentIndex: root.controller.measurement
                        onActivated: index => root.controller.measurement = index
                        delegate: ItemDelegate {
                            required property var model
                            required property int index
                            width: ListView.view.width
                            text: model.supported ? model.text : qsTr("%1 (not supported)").arg(model.text)
                            enabled: model.supported
                            highlighted: measurementBox.highlightedIndex === index
                            checkable: true
                            checked: measurementBox.currentIndex === index
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: root.controller.unsupportedInUse.length > 0
                    text: qsTr("Not supported by this device: %1. Run is refused until you change them.").arg(root.controller.unsupportedInUse.join(", "))
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
                Field {
                    visible: root.measurement !== CtrController.SemiPeriod && !root.encoder
                    label: root.measurement === CtrController.TwoEdgeSeparation ? qsTr("First Edge (DIO 2)")
                         : root.measurement === CtrController.PulseWidth ? qsTr("Starting Edge")
                         : root.counting ? qsTr("Edges to Count") : qsTr("Active Edge")
                    hint: root.measurement === CtrController.PulseWidth ? qsTr("Rising measures the high time, Falling the low time") : ""
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ icon: "slope-rising-line", text: qsTr("Rising") },
                                { icon: "slope-falling-line", text: qsTr("Falling"), enabled: !root.unsupported("counter.fallingEdge"),
                                  toolTip: root.unsupported("counter.fallingEdge") ? qsTr("Not supported by this device") : "" }]
                        currentIndex: root.controller.activeEdge
                        onActivated: index => root.controller.activeEdge = index
                    }
                }
                Field {
                    visible: root.measurement === CtrController.TwoEdgeSeparation
                    label: qsTr("Second Edge (DIO 1)")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ icon: "slope-rising-line", text: qsTr("Rising") }, { icon: "slope-falling-line", text: qsTr("Falling") }]
                        currentIndex: root.controller.secondEdge
                        onActivated: index => root.controller.secondEdge = index
                    }
                }
                Field {
                    visible: root.counting
                    label: qsTr("Count Direction")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ text: qsTr("Up") },
                                { text: qsTr("Down"), enabled: !root.unsupported("counter.countDown"),
                                  toolTip: root.unsupported("counter.countDown") ? qsTr("Not supported by this device") : "" },
                                { text: qsTr("External"), enabled: !root.unsupported("counter.externalDirection"),
                                  toolTip: root.unsupported("counter.externalDirection") ? qsTr("Not supported by this device")
                                                                                          : qsTr("DIO 2 high counts up, low counts down") }]
                        currentIndex: root.controller.countDirection
                        onActivated: index => root.controller.countDirection = index
                    }
                }
                FieldRow {
                    visible: root.counting
                    Field {
                        label: qsTr("Initial Count")
                        disabled: root.unsupported("counter.initialCount")
                        SiField {
                            Layout.fillWidth: true
                            enabled: !root.unsupported("counter.initialCount")
                            engineering: false
                            decimals: 0
                            stepSize: 1
                            from: 0
                            to: 4294967295
                            value: root.controller.initialCount
                            onValueModified: v => root.controller.initialCount = v
                        }
                    }
                    Field {
                        label: qsTr("Reset on DIO 1")
                        hint: qsTr("A rising edge on DIO 1 sets the count to 0")
                        disabled: root.unsupported("counter.resetOnDio")
                        ToggleSwitch {
                            enabled: !root.unsupported("counter.resetOnDio")
                            checked: root.controller.resetOnDio1
                            onToggled: root.controller.resetOnDio1 = checked
                        }
                    }
                }
            },
            Section {
                visible: root.measuring && root.encoder
                title: qsTr("Encoder Settings")
                accentColor: Theme.ch0
                summary: ["X1", "X2", "X4", qsTr("Two-pulse")][root.controller.decoding]
                Field {
                    label: qsTr("Decoding")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ text: "X1", toolTip: qsTr("One count per cycle of A") },
                                { text: "X2", toolTip: qsTr("Counts on both edges of A") },
                                { text: "X4", toolTip: qsTr("Counts on every edge of A and B") },
                                { text: qsTr("2-Pulse"), toolTip: qsTr("DIO 0 counts up, DIO 2 counts down") }]
                        currentIndex: root.controller.decoding
                        onActivated: index => root.controller.decoding = index
                    }
                }
                FieldRow {
                    visible: root.measurement === CtrController.LinearEncoder
                    Field {
                        label: qsTr("Distance / Pulse")
                        SiField {
                            Layout.fillWidth: true
                            unit: root.controller.distanceUnit
                            from: 1e-9
                            to: 1e6
                            digits: 5
                            value: root.controller.distancePerPulse
                            onValueModified: v => root.controller.distancePerPulse = v
                        }
                    }
                    Field {
                        label: qsTr("Units")
                        TextField {
                            Layout.fillWidth: true
                            text: root.controller.distanceUnit
                            onEditingFinished: root.controller.distanceUnit = text
                        }
                    }
                }
                Field {
                    visible: root.measurement === CtrController.AngularEncoder
                    label: qsTr("Pulses / Revolution")
                    SiField {
                        Layout.fillWidth: true
                        engineering: false
                        decimals: 0
                        stepSize: 1
                        from: 1
                        to: 16777216
                        value: root.controller.pulsesPerRevolution
                        onValueModified: v => root.controller.pulsesPerRevolution = Math.round(v)
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Z Index (DIO 1)")
                        hint: qsTr("Reload the position when the index pulse arrives with A and B high")
                        ToggleSwitch {
                            checked: root.controller.zIndexEnabled
                            onToggled: root.controller.zIndexEnabled = checked
                        }
                    }
                    Field {
                        label: qsTr("Z Index Value")
                        SiField {
                            Layout.fillWidth: true
                            enabled: root.controller.zIndexEnabled
                            engineering: false
                            decimals: 3
                            unit: root.measurement === CtrController.AngularEncoder ? "°" : root.controller.distanceUnit
                            value: root.controller.zIndexValue
                            onValueModified: v => root.controller.zIndexValue = v
                        }
                    }
                }
            },
            Section {
                visible: root.measuring && root.interval
                title: qsTr("Expected Range")
                summary: Units.format(root.controller.timebase, "Hz", 3)
                FieldRow {
                    Field {
                        label: qsTr("Minimum")
                        SiField {
                            Layout.fillWidth: true
                            unit: root.measurement === CtrController.Frequency ? "Hz" : "s"
                            from: root.measurement === CtrController.Frequency ? 0.01 : 10e-9
                            to: root.measurement === CtrController.Frequency ? 50e6 : 1000
                            value: root.measurement === CtrController.Frequency ? root.controller.frequencyMin : root.controller.timeMin
                            onValueModified: v => {
                                if (root.measurement === CtrController.Frequency)
                                    root.controller.frequencyMin = v
                                else
                                    root.controller.timeMin = v
                            }
                        }
                    }
                    Field {
                        label: qsTr("Maximum")
                        SiField {
                            Layout.fillWidth: true
                            unit: root.measurement === CtrController.Frequency ? "Hz" : "s"
                            from: root.measurement === CtrController.Frequency ? 0.01 : 10e-9
                            to: root.measurement === CtrController.Frequency ? 50e6 : 1000
                            value: root.measurement === CtrController.Frequency ? root.controller.frequencyMax : root.controller.timeMax
                            onValueModified: v => {
                                if (root.measurement === CtrController.Frequency)
                                    root.controller.frequencyMax = v
                                else
                                    root.controller.timeMax = v
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.controller.timebaseText
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                visible: root.measuring
                title: qsTr("Acquisition")
                summary: root.controller.sampling === CtrController.Buffered ? qsTr("Buffered")
                                                                             : Units.format(root.controller.updateInterval, "s", 3)
                Field {
                    label: qsTr("Sampling")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [{ text: qsTr("On Demand"), toolTip: qsTr("One reading every update interval (the latest measurement)") },
                                { text: qsTr("Buffered"), enabled: root.interval && !root.unsupported("counter.buffered"),
                                  toolTip: root.unsupported("counter.buffered") ? qsTr("Not supported by this device")
                                         : root.interval ? qsTr("Every period / interval is measured once (exact statistics)")
                                                         : qsTr("Not available for counting measurements on NI myDAQ") }]
                        currentIndex: root.controller.sampling
                        onActivated: index => root.controller.sampling = index
                    }
                }
                Field {
                    visible: root.controller.sampling === CtrController.OnDemand
                    label: qsTr("Update Interval")
                    SiField {
                        Layout.fillWidth: true
                        unit: "s"
                        from: 0.01
                        to: 10
                        digits: 3
                        value: root.controller.updateInterval
                        onValueModified: v => root.controller.updateInterval = v
                    }
                }
                Field {
                    visible: root.controller.sampling === CtrController.Buffered
                    label: qsTr("Samples (Single)")
                    hint: qsTr("Intervals measured by Single; Run measures continuously")
                    SiField {
                        Layout.fillWidth: true
                        engineering: false
                        decimals: 0
                        stepSize: 100
                        from: 1
                        to: 1000000
                        value: root.controller.bufferedSamples
                        onValueModified: v => root.controller.bufferedSamples = Math.round(v)
                    }
                }
            },

            // --- Pulse train ----------------------------------------------------------------
            Section {
                visible: root.pulsing
                title: qsTr("Pulse Train Settings")
                accentColor: Theme.math
                summary: "DIO 3"
                Field {
                    label: qsTr("Define By")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Frequency & Duty"), qsTr("High & Low Time")]
                        currentIndex: root.controller.pulseDefinition
                        onActivated: index => root.controller.pulseDefinition = index
                    }
                }
                FieldRow {
                    visible: root.controller.pulseDefinition === CtrController.FrequencyAndDuty
                    Field {
                        label: qsTr("Frequency")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Hz"
                            digits: 5
                            from: root.controller.minimumPulseFrequency
                            to: root.controller.maximumPulseFrequency
                            value: root.controller.pulseFrequency
                            accentColor: Theme.math
                            onValueModified: v => root.controller.pulseFrequency = v
                        }
                    }
                    Field {
                        label: qsTr("Duty Cycle")
                        SiField {
                            Layout.fillWidth: true
                            unit: "%"
                            engineering: false
                            decimals: 2
                            stepSize: 1
                            from: 0.01
                            to: 99.99
                            value: root.controller.dutyCycle * 100
                            accentColor: Theme.math
                            onValueModified: v => root.controller.dutyCycle = v / 100
                        }
                    }
                }
                FieldRow {
                    visible: root.controller.pulseDefinition === CtrController.HighAndLowTime
                    Field {
                        label: qsTr("High Time")
                        SiField {
                            Layout.fillWidth: true
                            unit: "s"
                            digits: 5
                            from: 20e-9
                            to: 40
                            value: root.controller.highTime
                            accentColor: Theme.math
                            onValueModified: v => root.controller.setHighLowTimes(v, root.controller.lowTime)
                        }
                    }
                    Field {
                        label: qsTr("Low Time")
                        SiField {
                            Layout.fillWidth: true
                            unit: "s"
                            digits: 5
                            from: 20e-9
                            to: 40
                            value: root.controller.lowTime
                            accentColor: Theme.math
                            onValueModified: v => root.controller.setHighLowTimes(root.controller.highTime, v)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Actual %1 · %2 % (100 MHz ÷ %3)")
                              .arg(Units.format(root.controller.coercedPulseFrequency, "Hz", 6))
                              .arg((root.controller.coercedDutyCycle * 100).toFixed(3))
                              .arg(Math.round(100e6 / root.controller.coercedPulseFrequency))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
                FieldRow {
                    Field {
                        label: qsTr("Idle State")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Low"),
                                    { text: qsTr("High"), enabled: !root.unsupported("pulse.idleHigh"),
                                      toolTip: root.unsupported("pulse.idleHigh") ? qsTr("Not supported by this device") : "" }]
                            currentIndex: root.controller.idleHigh ? 1 : 0
                            onActivated: index => root.controller.idleHigh = index === 1
                        }
                    }
                    Field {
                        label: qsTr("Initial Delay")
                        disabled: root.unsupported("pulse.initialDelay")
                        SiField {
                            Layout.fillWidth: true
                            enabled: !root.unsupported("pulse.initialDelay")
                            unit: "s"
                            from: 0
                            to: 100
                            value: root.controller.initialDelay
                            onValueModified: v => root.controller.initialDelay = v
                        }
                    }
                }
                Field {
                    label: qsTr("Generation")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Continuous"), qsTr("Finite")]
                        currentIndex: root.controller.pulseGeneration
                        onActivated: index => root.controller.pulseGeneration = index
                    }
                }
                FieldRow {
                    visible: root.controller.pulseGeneration === CtrController.FinitePulses
                    Field {
                        label: qsTr("Pulses")
                        SiField {
                            Layout.fillWidth: true
                            engineering: false
                            decimals: 0
                            stepSize: 1
                            from: 1
                            to: 1e9
                            value: root.controller.pulseCount
                            onValueModified: v => root.controller.pulseCount = Math.round(v)
                        }
                    }
                }
            },

            // --- FREQOUT --------------------------------------------------------------------
            Section {
                title: qsTr("FREQOUT")
                accentColor: Theme.ch1
                summary: root.controller.freqOutEnabled ? Units.format(root.controller.coercedFreqOut, "Hz", 4) : qsTr("Off")
                headerTools: ToggleSwitch {
                    checked: root.controller.freqOutEnabled
                    accentColor: Theme.ch1
                    onToggled: root.controller.freqOutEnabled = checked
                }
                FieldRow {
                    Field {
                        label: qsTr("Frequency")
                        SiField {
                            Layout.fillWidth: true
                            unit: "Hz"
                            digits: 5
                            from: 6250
                            to: 20e6
                            value: root.controller.coercedFreqOut
                            accentColor: Theme.ch1
                            onValueModified: v => root.controller.freqOutFrequency = v
                        }
                    }
                    Field {
                        label: qsTr("Available")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.controller.freqOutChoices
                            currentIndex: root.controller.freqOutIndex
                            onActivated: index => root.controller.selectFreqOut(index)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Square wave on DIO 4 = %1. Changes apply while running.").arg(root.controller.freqOutDivider)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            }
        ]
    }
}
