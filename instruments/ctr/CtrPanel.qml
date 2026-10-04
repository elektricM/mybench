pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Counter/Timer panel (docs/instruments/ctr.md), round-2 layout (docs/UI.md "Round 2"):
// instrument bar in the pane header (Run, Single, Reset); display on the left (big readout,
// secondary value, trend or pulse diagram, statistics cards); right column with the mode picture
// buttons, the active mode's settings, the pulse-train and FREQOUT cards and a tool palette whose
// tools (Connections) float over the display. Options the device cannot do are hidden.
Item {
    id: root

    required property CtrController controller

    readonly property bool narrow: width < 900
    property string tool: ""

    readonly property bool measuring: controller.counterFunction === CtrController.Measure
    readonly property bool pulsing: controller.counterFunction === CtrController.PulseTrain
    readonly property bool counterOff: controller.counterFunction === CtrController.CounterOff
    readonly property int measurement: controller.measurement
    readonly property bool counting: measurement === CtrController.CountEdges
    readonly property bool encoder: controller.encoderMeasurement
    readonly property bool interval: controller.intervalMeasurement

    // DeviceHub capability query; reading the property makes bindings re-evaluate on a device change.
    function unsupported(feature: string): bool {
        return DeviceHub.unsupportedFeatures.includes(feature)
    }
    readonly property var measurementFeatures: [
        "", "", "", "counter.pulseWidth", "counter.semiPeriod", "counter.twoEdgeSeparation",
        "counter.linearEncoder", "counter.angularEncoder"
    ]

    // The modes of the counter: the measurements, then Pulse Train and "FREQOUT only".
    // glyph = 32 × 18 SVG path.
    readonly property var modes: [
        { fn: CtrController.Measure, m: CtrController.Frequency, text: qsTr("Frequency"),
          glyph: "M2 14h4V4h4v10h4V4h4v10h4V4h4v10h2" },
        { fn: CtrController.Measure, m: CtrController.Period, text: qsTr("Period"),
          glyph: "M2 14h5V6h7v8h7V6h7v8 M7 2h14 M7 1v3 M21 1v3" },
        { fn: CtrController.Measure, m: CtrController.PulseWidth, text: qsTr("Pulse Width"),
          glyph: "M2 15h9V6h10v9h9 M11 2h10 M11 1v3 M21 1v3" },
        { fn: CtrController.Measure, m: CtrController.SemiPeriod, text: qsTr("Semi-Period"),
          glyph: "M2 15h8V6h8v9h8V6h4 M10 2h8 M18 2h8 M10 1v3 M18 1v3 M26 1v3" },
        { fn: CtrController.Measure, m: CtrController.TwoEdgeSeparation, text: qsTr("Two-Edge"),
          glyph: "M2 8h7V2h21 M2 17h16v-6h12 M9 5h9" },
        { fn: CtrController.Measure, m: CtrController.CountEdges, text: qsTr("Edge Count"),
          glyph: "M2 16h6v-4h6v-4h6v-4h6v-3h4" },
        { fn: CtrController.Measure, m: CtrController.LinearEncoder, text: qsTr("Linear Encoder"), short: qsTr("Linear"),
          glyph: "M2 9h28 M6 4v10 M12 4v10 M18 4v10 M24 4v10" },
        { fn: CtrController.Measure, m: CtrController.AngularEncoder, text: qsTr("Angular Encoder"), short: qsTr("Angular"),
          glyph: "M16 2a7 7 0 1 0 0.01 0 M16 9l5-5" },
        { fn: CtrController.PulseTrain, m: -1, text: qsTr("Pulse Train"),
          glyph: "M2 15h4V4h3v11h7V4h3v11h7V4h3v11" },
        { fn: CtrController.CounterOff, m: -1, text: qsTr("FREQOUT Only"), short: qsTr("FREQOUT"),
          glyph: "M2 9h28" }
    ]
    function modeSupported(mode: var): bool {
        return mode.m < 0 || root.measurementFeatures[mode.m] === "" || !root.unsupported(root.measurementFeatures[mode.m])
    }
    function modeActive(mode: var): bool {
        return mode.fn === root.controller.counterFunction && (mode.m < 0 || mode.m === root.measurement)
    }
    function selectMode(mode: var) {
        if (mode.m >= 0)
            root.controller.measurement = mode.m
        root.controller.counterFunction = mode.fn
    }

    // FREQOUT: the achievable frequencies, low to high (the knob turns clockwise to higher).
    readonly property var freqOutValues: controller.freqOutChoices.map(t => Units.parse(t, "Hz"))
    readonly property bool freqOutAscending: freqOutValues.length < 2 || freqOutValues[0] < freqOutValues[1]
    readonly property var freqOutKnobValues: freqOutAscending ? freqOutValues : freqOutValues.slice().reverse()
    readonly property var freqOutKnobLabels: freqOutAscending ? controller.freqOutChoices : controller.freqOutChoices.slice().reverse()
    function knobToTable(index: int): int {
        return freqOutAscending ? index : freqOutValues.length - 1 - index
    }

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

    // A labelled row of a card or tool panel.
    component LabeledRow: ColumnLayout {
        property alias label: rowLabel.text
        Layout.fillWidth: true
        spacing: 3
        Text {
            id: rowLabel
            Layout.fillWidth: true
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            elide: Text.ElideRight
        }
    }
    component Caption: Text {
        Layout.fillWidth: true
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSizeCaption
        font.features: Theme.tabularFigures
        wrapMode: Text.WordWrap
    }
    // One statistic card at the bottom of the display.
    component StatCard: Rectangle {
        property alias label: stat.label
        property alias value: stat.value
        property alias valueColor: stat.color
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: stat.implicitHeight + 2 * Theme.spacingS
        radius: Theme.radiusM
        color: Theme.cardFill
        border.color: Theme.cardBorder
        CtrStat {
            id: stat
            anchors.fill: parent
            anchors.margins: Theme.spacingS
        }
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
        showRunControls: false
        runBar.showTimeout: false
        inspector: []

        headerContent: [
            RowLayout {
                spacing: Theme.spacingS
                RunButton {
                    objectName: "ctrRunButton"
                    running: root.controller.running
                    stateText: root.controller.running ? (root.controller.timedOut ? qsTr("No input") : panel.paneHeader.statusText) : ""
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    shortcutText: "⌘R"
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "ctrSingleButton"
                    visible: root.measuring
                    text: qsTr("Single")
                    Layout.preferredHeight: 52
                    enabled: root.controller.canRun
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.sampling === CtrController.Buffered
                                  ? qsTr("Measure %1 intervals, then stop").arg(root.controller.bufferedSamples)
                                  : qsTr("Take one reading, then stop")
                    onClicked: root.controller.runOnce()
                }
                ActionButton {
                    objectName: "ctrResetButton"
                    visible: root.measuring
                    text: root.narrow ? "" : root.counting || root.encoder ? qsTr("Reset Count") : qsTr("Reset Stats")
                    iconName: "reset"
                    toolTip: root.counting || root.encoder ? qsTr("Clear statistics and trend, count again from the initial value")
                                                           : qsTr("Clear statistics and trend")
                    onClicked: root.controller.resetMeasurement()
                }
            }
        ]

        Item {
            anchors.fill: parent

            // --- Display ----------------------------------------------------------------------
            ColumnLayout {
                id: display
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
                clip: true   // a very narrow pane cuts the display instead of running under the column
                spacing: Theme.spacingS

                // Readout(s)
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: root.counterOff
                    Layout.preferredHeight: readoutColumn.implicitHeight + Theme.spacingM
                    Layout.minimumHeight: readoutColumn.implicitHeight

                    ColumnLayout {
                        id: readoutColumn
                        anchors.centerIn: parent
                        width: Math.min(parent.width, 820)
                        spacing: Theme.spacingS

                        Readout {
                            objectName: "ctrValue"
                            visible: root.measuring
                            Layout.fillWidth: true
                            label: root.controller.functionTitle
                            valueText: root.controller.valueText
                            fontSize: Math.max(40, Math.min(96, display.width / 9))
                            stale: !root.controller.running
                            color: Theme.text
                            caption: root.controller.secondaryText
                        }

                        RowLayout {
                            visible: root.pulsing
                            Layout.fillWidth: true
                            spacing: Theme.spacingXl
                            Readout {
                                Layout.fillWidth: true
                                label: qsTr("Pulse Frequency")
                                value: root.controller.running && root.controller.outputFrequency > 0 ? root.controller.outputFrequency
                                                                                                      : root.controller.coercedPulseFrequency
                                unit: "Hz"
                                digits: 6
                                fontSize: Math.max(32, Math.min(72, display.width / 13))
                                stale: !root.controller.running
                                caption: qsTr("Period %1").arg(Units.format(1 / root.controller.coercedPulseFrequency, "s", 5))
                            }
                            Readout {
                                Layout.fillWidth: true
                                label: qsTr("Duty Cycle")
                                valueText: ((root.controller.running && root.controller.outputDutyCycle > 0 ? root.controller.outputDutyCycle
                                                                                                            : root.controller.coercedDutyCycle) * 100).toFixed(2) + " %"
                                fontSize: Math.max(32, Math.min(72, display.width / 13))
                                stale: !root.controller.running
                                caption: qsTr("High %1 · Low %2").arg(Units.format(root.controller.highTime, "s", 4))
                                                                  .arg(Units.format(root.controller.lowTime, "s", 4))
                            }
                        }

                        Readout {
                            visible: root.counterOff
                            Layout.fillWidth: true
                            label: qsTr("FREQOUT")
                            value: root.controller.freqOutEnabled ? (root.controller.freqOutActual > 0 ? root.controller.freqOutActual
                                                                                                        : root.controller.coercedFreqOut) : NaN
                            unit: "Hz"
                            digits: 6
                            fontSize: Math.max(40, Math.min(96, display.width / 9))
                            stale: !root.controller.running
                            caption: root.controller.freqOutEnabled ? qsTr("Square wave on DIO 4 · %1").arg(root.controller.freqOutDivider)
                                                                    : qsTr("Turn FREQOUT on in its card, or choose a counter mode")
                        }

                        Caption {
                            visible: root.pulsing
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
                        }
                        // FREQOUT actual value beside a counter mode (under the readouts).
                        Rectangle {
                            visible: root.controller.freqOutEnabled && !root.counterOff
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: freqOutRow.implicitWidth + 2 * Theme.spacingM
                            implicitHeight: freqOutRow.implicitHeight + 2 * Theme.spacingS
                            radius: Theme.radiusM
                            color: Theme.cardFill
                            border.color: Theme.cardBorder
                            RowLayout {
                                id: freqOutRow
                                anchors.centerIn: parent
                                spacing: Theme.spacingS
                                Led {
                                    on: root.controller.freqOutActual > 0
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
                                    color: Theme.text
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

                }

                // Trend of the readings (Measure).
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
                        color: Theme.signalCh0
                        pointsVisible: true
                        step: root.counting
                    }
                    PlotLine {
                        visible: !isNaN(root.controller.statMean) && root.controller.statCount > 1
                        value: isNaN(root.controller.statMean) ? 0 : root.controller.statMean
                        color: Theme.alpha(Theme.signalCh0, 0.6)
                        style: PlotElement.Dash
                        handle: PlotLine.End
                        label: qsTr("mean")
                        draggable: false
                    }
                }

                // Pulse diagram (Pulse Train).
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
                        color: Theme.signalCh0
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
                        color: Theme.alpha(Theme.signalCh0, 0.6)
                        style: PlotElement.Dash
                        handle: PlotLine.Start
                        label: qsTr("first pulse")
                        draggable: false
                    }
                }

                Text {
                    visible: root.controller.conflictText.length > 0
                    Layout.fillWidth: true
                    text: root.controller.conflictText
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }

                // Statistics cards (Measure).
                RowLayout {
                    objectName: "ctrStatistics"
                    visible: root.measuring
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    StatCard { label: qsTr("Min"); value: root.fmt(root.controller.statMinimum, false) }
                    StatCard { label: qsTr("Max"); value: root.fmt(root.controller.statMaximum, false) }
                    StatCard { label: qsTr("Mean"); value: root.fmt(root.controller.statMean, true) }
                    StatCard { label: "σ"; value: root.fmt(root.controller.statDeviation, true) }
                    StatCard {
                        label: root.controller.sampling === CtrController.Buffered && root.interval ? qsTr("Intervals") : qsTr("Count")
                        value: String(root.controller.statCount)
                        valueColor: Theme.textSecondary
                    }
                }
            }

            // --- Right column -----------------------------------------------------------------
            Rectangle {
                id: column
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: root.narrow ? 220 : 266
                color: Theme.window
                Rectangle { width: 1; height: parent.height; color: Theme.separator }

                Flickable {
                    id: columnFlick
                    anchors { fill: parent; leftMargin: 1 }
                    contentHeight: columnLayout.implicitHeight + 2 * Theme.spacingS
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { }

                    ColumnLayout {
                        id: columnLayout
                        x: Theme.spacingS
                        y: Theme.spacingS
                        width: columnFlick.width - 2 * Theme.spacingS
                        spacing: Theme.spacingS

                        // Mode: picture buttons.
                        ChannelCard {
                            objectName: "ctrModeCard"
                            Layout.fillWidth: true
                            title: qsTr("Counter")
                            subtitle: qsTr("ctr0")
                            color: Theme.accent
                            checkable: false
                            compact: root.narrow
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 4
                                rowSpacing: 4
                                Repeater {
                                    model: root.modes
                                    PictureButton {
                                        required property var modelData
                                        objectName: "ctrMode_" + modelData.text
                                        visible: root.modeSupported(modelData)
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        text: root.narrow && modelData.short ? modelData.short : modelData.text
                                        glyph: modelData.glyph
                                        ToolTip.visible: hovered && root.narrow && modelData.short !== undefined
                                        ToolTip.text: modelData.text
                                        checkable: false     // checked follows the controller
                                        checked: root.modeActive(modelData)
                                        onClicked: root.selectMode(modelData)
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
                        }

                        // The active measurement's settings.
                        ChannelCard {
                            objectName: "ctrSettingsCard"
                            visible: root.measuring
                            Layout.fillWidth: true
                            title: root.controller.functionTitle
                            subtitle: root.controller.terminals
                            color: Theme.signalCh0
                            checkable: false
                            compact: root.narrow

                            LabeledRow {
                                visible: root.measurement !== CtrController.SemiPeriod && !root.encoder
                                label: root.measurement === CtrController.TwoEdgeSeparation ? qsTr("First edge (DIO 2)")
                                     : root.measurement === CtrController.PulseWidth ? qsTr("Starting edge")
                                     : root.counting ? qsTr("Edges to count") : qsTr("Active edge")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: root.unsupported("counter.fallingEdge")
                                           ? [{ icon: "slope-rising", text: qsTr("Rising") }]
                                           : [{ icon: "slope-rising", text: qsTr("Rising") }, { icon: "slope-falling", text: qsTr("Falling") }]
                                    currentIndex: root.controller.activeEdge
                                    onActivated: index => root.controller.activeEdge = index
                                }
                            }
                            LabeledRow {
                                visible: root.measurement === CtrController.TwoEdgeSeparation
                                label: qsTr("Second edge (DIO 1)")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: [{ icon: "slope-rising", text: qsTr("Rising") }, { icon: "slope-falling", text: qsTr("Falling") }]
                                    currentIndex: root.controller.secondEdge
                                    onActivated: index => root.controller.secondEdge = index
                                }
                            }
                            LabeledRow {
                                visible: root.counting
                                label: qsTr("Direction")
                                SegmentedControl {
                                    id: directionControl
                                    Layout.fillWidth: true
                                    // Only the supported directions; `value` = CtrController.CountDirection.
                                    model: {
                                        const m = [{ text: qsTr("Up"), value: CtrController.CountUp }]
                                        if (!root.unsupported("counter.countDown"))
                                            m.push({ text: qsTr("Down"), value: CtrController.CountDown })
                                        if (!root.unsupported("counter.externalDirection"))
                                            m.push({ text: qsTr("Ext."), value: CtrController.CountExternal,
                                                     toolTip: qsTr("DIO 2 high counts up, low counts down") })
                                        return m
                                    }
                                    currentIndex: model.findIndex(e => e.value === root.controller.countDirection)
                                    onActivated: index => root.controller.countDirection = directionControl.model[index].value
                                }
                            }
                            LabeledRow {
                                visible: root.counting && !root.unsupported("counter.initialCount")
                                label: qsTr("Initial count")
                                SiField {
                                    Layout.fillWidth: true
                                    engineering: false
                                    decimals: 0
                                    stepSize: 1
                                    from: 0
                                    to: 4294967295
                                    value: root.controller.initialCount
                                    onValueModified: v => root.controller.initialCount = v
                                }
                            }
                            RowLayout {
                                visible: root.counting && !root.unsupported("counter.resetOnDio")
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Reset on DIO 1 rising edge")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    wrapMode: Text.WordWrap
                                }
                                ToggleSwitch {
                                    checked: root.controller.resetOnDio1
                                    onToggled: root.controller.resetOnDio1 = checked
                                }
                            }

                            // Encoders.
                            LabeledRow {
                                visible: root.encoder
                                label: qsTr("Decoding")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: [{ text: "X1", toolTip: qsTr("One count per cycle of A") },
                                            { text: "X2", toolTip: qsTr("Counts on both edges of A") },
                                            { text: "X4", toolTip: qsTr("Counts on every edge of A and B") },
                                            { text: qsTr("2-P"), toolTip: qsTr("Two-pulse: DIO 0 counts up, DIO 2 counts down") }]
                                    currentIndex: root.controller.decoding
                                    onActivated: index => root.controller.decoding = index
                                }
                            }
                            RowLayout {
                                visible: root.measurement === CtrController.LinearEncoder
                                Layout.fillWidth: true
                                LabeledRow {
                                    label: qsTr("Distance / pulse")
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
                                LabeledRow {
                                    Layout.preferredWidth: 56
                                    Layout.fillWidth: false
                                    label: qsTr("Unit")
                                    TextField {
                                        Layout.fillWidth: true
                                        text: root.controller.distanceUnit
                                        onEditingFinished: root.controller.distanceUnit = text
                                    }
                                }
                            }
                            LabeledRow {
                                visible: root.measurement === CtrController.AngularEncoder
                                label: qsTr("Pulses / revolution")
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
                            RowLayout {
                                visible: root.encoder
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Z index (DIO 1)")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                }
                                ToggleSwitch {
                                    checked: root.controller.zIndexEnabled
                                    onToggled: root.controller.zIndexEnabled = checked
                                }
                            }
                            LabeledRow {
                                visible: root.encoder && root.controller.zIndexEnabled
                                label: qsTr("Z index value")
                                SiField {
                                    Layout.fillWidth: true
                                    engineering: false
                                    decimals: 3
                                    unit: root.measurement === CtrController.AngularEncoder ? "°" : root.controller.distanceUnit
                                    value: root.controller.zIndexValue
                                    onValueModified: v => root.controller.zIndexValue = v
                                }
                            }

                            // Expected range (interval measurements): sets the timebase.
                            LabeledRow {
                                visible: root.interval
                                label: qsTr("Expected range")
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: root.narrow ? 1 : 3
                                    columnSpacing: 4
                                    rowSpacing: 4
                                    SiField {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        unit: root.measurement === CtrController.Frequency ? "Hz" : "s"
                                        from: root.measurement === CtrController.Frequency ? 0.01 : 10e-9
                                        to: root.measurement === CtrController.Frequency ? 50e6 : 1000
                                        value: root.measurement === CtrController.Frequency ? root.controller.frequencyMin : root.controller.timeMin
                                        toolTip: qsTr("Minimum")
                                        onValueModified: v => {
                                            if (root.measurement === CtrController.Frequency)
                                                root.controller.frequencyMin = v
                                            else
                                                root.controller.timeMin = v
                                        }
                                    }
                                    Text { visible: !root.narrow; text: "–"; color: Theme.textSecondary }
                                    SiField {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 1
                                        unit: root.measurement === CtrController.Frequency ? "Hz" : "s"
                                        from: root.measurement === CtrController.Frequency ? 0.01 : 10e-9
                                        to: root.measurement === CtrController.Frequency ? 50e6 : 1000
                                        value: root.measurement === CtrController.Frequency ? root.controller.frequencyMax : root.controller.timeMax
                                        toolTip: qsTr("Maximum")
                                        onValueModified: v => {
                                            if (root.measurement === CtrController.Frequency)
                                                root.controller.frequencyMax = v
                                            else
                                                root.controller.timeMax = v
                                        }
                                    }
                                }
                                Caption { text: root.controller.timebaseText }
                            }

                            // Acquisition.
                            LabeledRow {
                                label: qsTr("Acquisition")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: root.interval && !root.unsupported("counter.buffered")
                                           ? [{ text: qsTr("On Demand"), toolTip: qsTr("One reading every update interval (the latest measurement)") },
                                              { text: qsTr("Buffered"), toolTip: qsTr("Every period / interval is measured once (exact statistics)") }]
                                           : [{ text: qsTr("On Demand"), toolTip: qsTr("One reading every update interval (the latest measurement)") }]
                                    currentIndex: root.controller.sampling
                                    onActivated: index => root.controller.sampling = index
                                }
                            }
                            KnobField {
                                objectName: "ctrUpdateInterval"
                                visible: root.controller.sampling === CtrController.OnDemand
                                Layout.fillWidth: true
                                label: qsTr("Update interval")
                                unit: "s"
                                knobSize: 34
                                compact: root.narrow
                                from: 0.01
                                to: 10
                                logarithmic: true
                                digits: 3
                                defaultValue: 0.1
                                value: root.controller.updateInterval
                                onValueModified: v => root.controller.updateInterval = Number(v.toPrecision(3))
                            }
                            LabeledRow {
                                visible: root.controller.sampling === CtrController.Buffered
                                label: qsTr("Intervals per Single")
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
                        }

                        // Pulse train (ctr0 output on DIO 3).
                        ChannelCard {
                            objectName: "ctrPulseCard"
                            visible: root.pulsing
                            Layout.fillWidth: true
                            title: qsTr("Pulse Train")
                            subtitle: "DIO 3"
                            color: Theme.signalCh0
                            checkable: false
                            compact: root.narrow
                            facts: [qsTr("100 MHz ÷ %1").arg(Math.round(100e6 / root.controller.coercedPulseFrequency))]

                            SegmentedControl {
                                Layout.fillWidth: true
                                model: [qsTr("Freq & Duty"), qsTr("High & Low")]
                                currentIndex: root.controller.pulseDefinition
                                onActivated: index => root.controller.pulseDefinition = index
                            }
                            KnobField {
                                objectName: "ctrPulseFrequency"
                                visible: root.controller.pulseDefinition === CtrController.FrequencyAndDuty
                                Layout.fillWidth: true
                                label: qsTr("Frequency")
                                unit: "Hz"
                                digits: 5
                                compact: root.narrow
                                accentColor: Theme.signalCh0
                                from: root.controller.minimumPulseFrequency
                                to: root.controller.maximumPulseFrequency
                                logarithmic: true
                                defaultValue: 1000
                                value: root.controller.pulseFrequency
                                caption: qsTr("Actual %1").arg(Units.format(root.controller.coercedPulseFrequency, "Hz", 6))
                                onValueModified: v => root.controller.pulseFrequency = v
                            }
                            KnobField {
                                objectName: "ctrPulseDuty"
                                visible: root.controller.pulseDefinition === CtrController.FrequencyAndDuty
                                Layout.fillWidth: true
                                label: qsTr("Duty cycle")
                                unit: "%"
                                digits: 4
                                knobSize: 34
                                compact: root.narrow
                                accentColor: Theme.signalCh0
                                from: 0.01
                                to: 99.99
                                stepSize: 1
                                defaultValue: 50
                                value: root.controller.dutyCycle * 100
                                formatter: v => v.toFixed(2) + " %"
                                caption: qsTr("Actual %1 %").arg((root.controller.coercedDutyCycle * 100).toFixed(3))
                                onValueModified: v => root.controller.dutyCycle = v / 100
                            }
                            KnobField {
                                visible: root.controller.pulseDefinition === CtrController.HighAndLowTime
                                Layout.fillWidth: true
                                label: qsTr("High time")
                                unit: "s"
                                digits: 5
                                knobSize: 34
                                compact: root.narrow
                                accentColor: Theme.signalCh0
                                from: 20e-9
                                to: 40
                                logarithmic: true
                                value: root.controller.highTime
                                onValueModified: v => root.controller.setHighLowTimes(v, root.controller.lowTime)
                            }
                            KnobField {
                                visible: root.controller.pulseDefinition === CtrController.HighAndLowTime
                                Layout.fillWidth: true
                                label: qsTr("Low time")
                                unit: "s"
                                digits: 5
                                knobSize: 34
                                compact: root.narrow
                                accentColor: Theme.signalCh0
                                from: 20e-9
                                to: 40
                                logarithmic: true
                                value: root.controller.lowTime
                                onValueModified: v => root.controller.setHighLowTimes(root.controller.highTime, v)
                            }
                            LabeledRow {
                                visible: !root.unsupported("pulse.idleHigh")
                                label: qsTr("Idle state")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: [qsTr("Low"), qsTr("High")]
                                    currentIndex: root.controller.idleHigh ? 1 : 0
                                    onActivated: index => root.controller.idleHigh = index === 1
                                }
                            }
                            LabeledRow {
                                visible: !root.unsupported("pulse.initialDelay")
                                label: qsTr("Initial delay")
                                SiField {
                                    Layout.fillWidth: true
                                    unit: "s"
                                    from: 0
                                    to: 100
                                    value: root.controller.initialDelay
                                    onValueModified: v => root.controller.initialDelay = v
                                }
                            }
                            LabeledRow {
                                label: qsTr("Generation")
                                SegmentedControl {
                                    Layout.fillWidth: true
                                    model: [qsTr("Continuous"), qsTr("Finite")]
                                    currentIndex: root.controller.pulseGeneration
                                    onActivated: index => root.controller.pulseGeneration = index
                                }
                            }
                            LabeledRow {
                                visible: root.controller.pulseGeneration === CtrController.FinitePulses
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

                        // FREQOUT (DIO 4): switch in the header, knob over the achievable frequencies.
                        ChannelCard {
                            objectName: "ctrFreqOutCard"
                            Layout.fillWidth: true
                            title: qsTr("FREQOUT")
                            subtitle: "DIO 4"
                            color: Theme.signalCh1
                            compact: root.narrow
                            checked: root.controller.freqOutEnabled
                            onToggled: on => root.controller.freqOutEnabled = on
                            facts: [root.controller.freqOutDivider]
                            KnobField {
                                objectName: "ctrFreqOut"
                                Layout.fillWidth: true
                                label: qsTr("Frequency")
                                unit: "Hz"
                                compact: root.narrow
                                accentColor: Theme.signalCh1
                                values: root.freqOutKnobValues
                                labels: root.freqOutKnobLabels
                                currentIndex: Math.max(0, Math.min(root.freqOutValues.length - 1, root.knobToTable(Math.max(0, root.controller.freqOutIndex))))
                                caption: qsTr("Square wave; changes apply while running")
                                onIndexActivated: index => root.controller.selectFreqOut(root.knobToTable(index))
                            }
                        }

                        // Tools.
                        ToolPalette {
                            objectName: "ctrTools"
                            Layout.fillWidth: true
                            columns: 3
                            model: [{ id: "connections", text: qsTr("Connections"), icon: "pin" }]
                            currentId: root.tool
                            onActivated: id => root.tool = root.tool === id ? "" : id
                        }
                    }
                }
            }

            // Floating tool panels, over the right part of the display.
            ToolPanel {
                objectName: "ctrConnectionsPanel"
                visible: root.tool === "connections"
                anchors { right: column.left; top: parent.top; margins: Theme.spacingS }
                width: Math.min(display.width - Theme.spacingL, 560)
                z: 10
                title: qsTr("Connections")
                onCloseRequested: root.tool = ""
                PinMap {
                    Layout.fillWidth: true
                    pins: root.controller.pins
                }
                Caption {
                    visible: root.controller.conflictText.length > 0
                    text: root.controller.conflictText
                    color: Theme.warning
                }
            }
        }
    }
}
