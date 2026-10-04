pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Bode Analyzer panel (docs/instruments/bode.md): gain and phase plots sharing one frequency
// axis, the ELVISmx readout row, markers and reference curves; inspector cards with the ELVISmx
// groups (Measurement / Graph / Cursor Settings) and the MYBENCH extensions (Sweep, Markers,
// References). All state and logic live in BodeController.
Item {
    id: root

    required property BodeController controller

    readonly property bool hasPoints: controller.pointCount > 0
    readonly property bool linearGain: controller.mapping === BodeController.Linear
    readonly property string gainUnit: linearGain ? "" : "dB"
    // Gain follows the stimulus channel's color, phase the response's (the old "ELVISmx colors"
    // option is gone; a restored value is ignored).
    readonly property color gainColor: Theme.signalColor("ai0")
    readonly property color phaseColor: Theme.signalColor("ai1")
    readonly property color cursorColor: Theme.signalCursor
    readonly property bool narrow: width < 1240     // knobs fold away (the inspector still takes room)
    readonly property string progressText: {
        const c = controller
        if (!c.running || c.remeasuring || c.totalPoints <= 0)
            return ""
        const left = c.remainingSeconds > 0 ? qsTr(" · ≈ %1 left").arg(duration(c.remainingSeconds)) : ""
        return qsTr("%1/%2").arg(Math.min(c.pointCount + 1, c.totalPoints)).arg(c.totalPoints) + left
    }
    readonly property bool measurementLocked: controller.running
    readonly property bool cursorValid: controller.cursorsOn && controller.cursorIndex >= 0
    readonly property real leftMargin: Math.max(gainPlot.implicitLeftMargin, phasePlot.implicitLeftMargin)

    // "2 decimals" ELVISmx readout format; -Inf / Inf spelled out.
    function fixed2(value: real): string {
        if (isNaN(value))
            return "—"
        if (!isFinite(value))
            return value < 0 ? "-Inf" : "Inf"
        return value.toFixed(2)
    }
    function duration(seconds: real): string {
        if (!(seconds > 0))
            return ""
        if (seconds < 60)
            return qsTr("%1 s").arg(Math.max(1, Math.round(seconds)))
        const minutes = Math.floor(seconds / 60)
        if (minutes < 60)
            return qsTr("%1 min %2 s").arg(minutes).arg(Math.round(seconds - 60 * minutes))
        return qsTr("%1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60)
    }
    function referenceInfo(slot: int): var {
        const list = controller.references
        for (let i = 0; i < list.length; ++i)
            if (list[i].slot === slot)
                return list[i]
        return null
    }
    function referenceColor(colorIndex: int): color {
        const order = [3, 4, 5, 6, 7, 2]
        return gainPlot.paletteColor(order[colorIndex % order.length])
    }
    function gainText(dbValue: real, ratio: real): string {
        return linearGain ? ratio.toFixed(3) : Units.decibels(dbValue)
    }

    // Autoscale off: both plots jump to the ELVISmx fixed ranges (kept when zooming afterwards).
    function applyFixedRanges() {
        gainPlot.yAxis.min = controller.fixedGainMin
        gainPlot.yAxis.max = controller.fixedGainMax
        phasePlot.yAxis.min = controller.fixedPhaseMin
        phasePlot.yAxis.max = controller.fixedPhaseMax
    }
    Connections {
        target: root.controller
        function onAutoscaleChanged() { if (!root.controller.autoscale) root.applyFixedRanges() }
        function onStarted() { if (!root.controller.autoscale) root.applyFixedRanges() }
    }
    Component.onCompleted: if (!controller.autoscale) applyFixedRanges()

    PlotAxis {
        id: frequencyAxis
        min: root.controller.startFrequency
        max: Math.max(root.controller.stopFrequency, root.controller.startFrequency * 1.0001)
        scale: root.controller.logFrequencyAxis ? PlotAxis.Log : PlotAxis.Linear
        unit: "Hz"
        title: qsTr("Frequency")
        lowerLimit: root.controller.logFrequencyAxis ? 0.5 : 0
        upperLimit: 40000
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: qsTr("Connections: AI 0 – AO 0   AI 1 – Signal")
        displayInfo: {
            const c = root.controller
            if (c.running && !c.remeasuring && c.totalPoints > 0)
                return qsTr("Point %1 of %2").arg(Math.min(c.pointCount + 1, c.totalPoints)).arg(c.totalPoints)
            if (c.pointCount > 0)
                return qsTr("%1 points · %2 – %3").arg(c.pointCount).arg(Units.format(c.dataMinFrequency, "Hz", 3))
                                                  .arg(Units.format(c.dataMaxFrequency, "Hz", 3))
            return ""
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // --- Gain -------------------------------------------------------------------------
            BenchPlot {
                id: gainPlot
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 3
                xAxis: frequencyAxis
                xLabelsVisible: false
                leftMargin: root.leftMargin
                rightMargin: 22
                hoverReadout: true
                placeholderText: root.hasPoints || root.controller.running ? ""
                                 : root.controller.settingsValid
                                   ? qsTr("Press Run to sweep %1 – %2 · %3 points · about %4")
                                         .arg(Units.format(root.controller.startFrequency, "Hz", 3))
                                         .arg(Units.format(root.controller.stopFrequency, "Hz", 3))
                                         .arg(root.controller.plannedPoints)
                                         .arg(root.duration(root.controller.plannedSeconds))
                                   : qsTr("Start Frequency must not be higher than Stop Frequency")
                yAxis {
                    min: root.linearGain ? 0 : -40
                    max: root.linearGain ? 2 : 10
                    title: root.linearGain ? qsTr("Gain (Linear)") : qsTr("Gain")
                    unit: root.gainUnit
                    autoScale: root.controller.autoscale && root.hasPoints
                    autoScaleMargin: 0.05
                    limitToData: root.hasPoints
                    limitMargin: 1
                    minimumSpan: root.linearGain ? 0.01 : 1
                    labelFormat: root.linearGain ? PlotAxis.Engineering : PlotAxis.Fixed
                    tickSpacing: 44
                }

                Instantiator {
                    model: root.controller.maxReferences
                    delegate: PlotTrace {
                        required property int index
                        readonly property var info: root.referenceInfo(index)
                        plot: gainPlot
                        z: -1
                        buffer: root.controller.referenceGain(index)
                        visible: info !== null && info.visible
                        name: info ? info.name : ""
                        color: info ? root.referenceColor(info.colorIndex) : Theme.ref
                        width: 1.4
                        style: PlotElement.Dash
                        unit: root.gainUnit
                    }
                }
                PlotTrace {
                    id: gainTrace
                    buffer: root.controller.gain
                    name: qsTr("Gain")
                    unit: root.gainUnit
                    color: root.gainColor
                    width: 2
                    pointsVisible: true
                    pointSize: 5
                    pointShape: PlotTrace.FilledCircle
                }
                PlotTrace {
                    buffer: root.controller.clippedGain
                    name: qsTr("Clipped")
                    color: Theme.danger
                    lineVisible: false
                    pointsVisible: true
                    pointSize: 9
                    legendVisible: false
                }
                Instantiator {
                    model: root.controller.showCutoffMarkers ? root.controller.cutoffFrequencies : []
                    delegate: PlotMarker {
                        required property real modelData
                        plot: gainPlot
                        x: modelData
                        trace: gainTrace
                        shape: PlotMarker.Ring
                        size: 11
                        color: Theme.math
                        guides: true
                        // Left of the point: the cursor's value chip is drawn on the right.
                        labelPosition: PlotMarker.Left
                        label: qsTr("−3 dB  %1").arg(Units.format(modelData, "Hz", 3))
                    }
                }
                PlotMarker {
                    visible: root.controller.showUnityGainMarker && isFinite(root.controller.unityGainFrequency)
                    x: root.controller.unityGainFrequency
                    trace: gainTrace
                    shape: PlotMarker.Diamond
                    size: 10
                    color: Theme.ref
                    guides: true
                    labelPosition: PlotMarker.Left
                    label: qsTr("0 dB  %1").arg(Units.format(root.controller.unityGainFrequency, "Hz", 3))
                }
                PlotMarker {
                    visible: root.controller.showMarginMarkers && isFinite(root.controller.phaseCrossoverFrequency)
                    x: root.controller.phaseCrossoverFrequency
                    trace: gainTrace
                    shape: PlotMarker.TriangleDown
                    size: 10
                    color: Theme.cursor2
                    labelPosition: PlotMarker.Above
                    label: qsTr("GM %1").arg(Units.decibels(root.controller.gainMargin, 1))
                }
                PlotLine {
                    visible: root.controller.showUnityGainMarker && !root.linearGain
                    value: 0
                    color: Theme.alpha(Theme.ref, 0.6)
                    style: PlotElement.Dash
                    width: 1
                    draggable: false
                    handle: PlotLine.NoHandle
                }
                PlotCursor {
                    id: gainCursor
                    visible: root.controller.cursorsOn && root.hasPoints
                    position: root.controller.cursorFrequency
                    trace: gainTrace
                    snap: true
                    label: "C"
                    color: root.cursorColor
                    onMoved: position => root.controller.setCursorFrequency(position)
                }
            }

            // --- Phase ------------------------------------------------------------------------
            BenchPlot {
                id: phasePlot
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: 2.6
                xAxis: frequencyAxis
                leftMargin: root.leftMargin
                rightMargin: 22
                hoverReadout: true
                yAxis {
                    min: -180
                    max: 180
                    divisions: root.controller.autoscale && root.hasPoints ? 0 : 8   // 90° steps on the fixed -360...360 range
                    title: qsTr("Phase")
                    unit: "deg"
                    autoScale: root.controller.autoscale && root.hasPoints
                    autoScaleMargin: 0.05
                    limitToData: root.hasPoints
                    limitMargin: 1
                    minimumSpan: 5
                    labelFormat: PlotAxis.Fixed
                    tickSpacing: 40
                }

                Instantiator {
                    model: root.controller.maxReferences
                    delegate: PlotTrace {
                        required property int index
                        readonly property var info: root.referenceInfo(index)
                        plot: phasePlot
                        z: -1
                        buffer: root.controller.referencePhase(index)
                        visible: info !== null && info.visible
                        name: info ? info.name : ""
                        color: info ? root.referenceColor(info.colorIndex) : Theme.ref
                        width: 1.4
                        style: PlotElement.Dash
                        unit: "°"
                    }
                }
                PlotTrace {
                    id: phaseTrace
                    buffer: root.controller.phase
                    name: qsTr("Phase")
                    unit: "°"
                    color: root.phaseColor
                    width: 2
                    pointsVisible: true
                    pointSize: 5
                    pointShape: PlotTrace.FilledCircle
                }
                PlotTrace {
                    buffer: root.controller.clippedPhase
                    name: qsTr("Clipped")
                    color: Theme.danger
                    lineVisible: false
                    pointsVisible: true
                    pointSize: 9
                    legendVisible: false
                }
                PlotLine {
                    visible: root.controller.showMarginMarkers
                    value: -180
                    color: Theme.alpha(Theme.cursor2, 0.6)
                    style: PlotElement.Dash
                    width: 1
                    draggable: false
                    handle: PlotLine.NoHandle
                }
                PlotMarker {
                    visible: root.controller.showMarginMarkers && isFinite(root.controller.unityGainFrequency)
                    x: root.controller.unityGainFrequency
                    trace: phaseTrace
                    shape: PlotMarker.TriangleUp
                    size: 10
                    color: Theme.cursor2
                    guides: true
                    labelPosition: PlotMarker.Left
                    label: qsTr("PM %1°").arg(root.controller.phaseMargin.toFixed(1))
                }
                Instantiator {
                    model: root.controller.showCutoffMarkers ? root.controller.cutoffFrequencies : []
                    delegate: PlotMarker {
                        required property real modelData
                        plot: phasePlot
                        x: modelData
                        trace: phaseTrace
                        shape: PlotMarker.Ring
                        size: 11
                        color: Theme.math
                        labelPosition: PlotMarker.Left
                        label: phaseTrace.valueAt(modelData).toFixed(1) + "°"
                    }
                }
                PlotCursor {
                    visible: gainCursor.visible
                    position: root.controller.cursorFrequency
                    trace: phaseTrace
                    snap: true
                    label: "C"
                    color: root.cursorColor
                    onMoved: position => root.controller.setCursorFrequency(position)
                }
            }

            // --- Readout row: replaced by the measurement cards under the display ---------------
            Rectangle {
                visible: false
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingS
                implicitHeight: readoutRow.implicitHeight + 2 * Theme.spacingS
                radius: Theme.radiusM
                color: Theme.alpha(Theme.text, Theme.dark ? 0.03 : 0.025)
                border.width: 1
                border.color: Theme.displayBorder

                RowLayout {
                    id: readoutRow
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingM
                    anchors.rightMargin: Theme.spacingM
                    spacing: Theme.spacingXl

                    Readout {
                        alignment: Qt.AlignLeft
                        implicitWidth: 160
                        label: root.cursorValid ? qsTr("C · Frequency (Hz)") : qsTr("Frequency (Hz)")
                        valueText: root.fixed2(root.controller.readoutFrequency)
                        fontSize: 20
                        color: root.cursorValid ? root.cursorColor : Theme.text
                        stale: !root.controller.readoutValid
                    }
                    Readout {
                        alignment: Qt.AlignLeft
                        implicitWidth: 100
                        label: qsTr("Phase (deg)")
                        valueText: root.fixed2(root.controller.readoutPhase)
                        fontSize: 20
                        color: root.phaseColor
                        stale: !root.controller.readoutValid
                    }
                    Readout {
                        alignment: Qt.AlignLeft
                        implicitWidth: 90
                        label: qsTr("Gain")
                        valueText: root.fixed2(root.controller.readoutGain)
                        fontSize: 20
                        color: root.gainColor
                        stale: !root.controller.readoutValid
                    }
                    Readout {
                        alignment: Qt.AlignLeft
                        implicitWidth: 100
                        label: qsTr("Gain (dB)")
                        valueText: root.fixed2(root.controller.readoutGainDb)
                        fontSize: 20
                        color: root.gainColor
                        stale: !root.controller.readoutValid
                    }
                    Badge {
                        visible: root.controller.readoutClipped
                        text: qsTr("Clipped")
                        color: Theme.danger
                    }
                    Item { Layout.fillWidth: true }
                    // Marker summary (MYBENCH).
                    ColumnLayout {
                        spacing: 2
                        visible: root.hasPoints && panel.width > 900
                        Text {
                            visible: root.controller.showCutoffMarkers
                            Layout.alignment: Qt.AlignRight
                            text: root.controller.cutoffFrequencies.length > 0
                                  ? qsTr("−3 dB  %1").arg(root.controller.cutoffFrequencies.map(f => Units.format(f, "Hz", 3)).join(" · "))
                                  : qsTr("−3 dB  not crossed")
                            color: root.controller.cutoffFrequencies.length > 0 ? Theme.math : Theme.textMuted
                            font.pixelSize: Theme.fontSizeM
                            font.features: Theme.tabularFigures
                        }
                        Text {
                            visible: root.controller.showMarginMarkers
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("PM %1 · GM %2")
                                  .arg(isFinite(root.controller.phaseMargin) ? root.controller.phaseMargin.toFixed(1) + "°" : "—")
                                  .arg(isFinite(root.controller.gainMargin) ? Units.decibels(root.controller.gainMargin, 1) : "—")
                            color: Theme.cursor2
                            font.pixelSize: Theme.fontSizeM
                            font.features: Theme.tabularFigures
                        }
                    }
                }
            }
        }

        showRunControls: false
        runBar.showTimeout: false
        headerContent: [
            RowLayout {
                objectName: "bodeBar"
                spacing: Theme.spacingM
                RunButton {
                    objectName: "bodeRunButton"
                    runText: qsTr("Sweep")
                    running: root.controller.running
                    stateText: root.progressText.length > 0 ? root.progressText : panel.paneHeader.statusText
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "keepReferenceButton"
                    text: root.narrow ? "" : qsTr("Keep as Reference")
                    icon.source: Theme.icon("layers")
                    enabled: root.hasPoints && !root.controller.running
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Store this sweep as a reference curve to compare with the next one")
                    onClicked: root.controller.keepAsReference()
                }
                KnobField {
                    objectName: "startKnob"
                    Layout.preferredWidth: root.narrow ? 140 : 180
                    compact: root.narrow
                    enabled: !root.measurementLocked
                    label: qsTr("Start")
                    unit: "Hz"
                    accentColor: root.controller.settingsValid ? root.gainColor : Theme.negative
                    from: 1
                    to: 20000
                    logarithmic: true
                    value: root.controller.startFrequency
                    caption: root.controller.settingsValid ? qsTr("%1 points").arg(root.controller.plannedPoints) : qsTr("Start > Stop")
                    onValueModified: v => root.controller.startFrequency = v
                }
                KnobField {
                    objectName: "stopKnob"
                    Layout.preferredWidth: root.narrow ? 140 : 180
                    compact: root.narrow
                    enabled: !root.measurementLocked
                    label: qsTr("Stop")
                    unit: "Hz"
                    accentColor: root.gainColor
                    from: 1
                    to: 20000
                    logarithmic: true
                    value: root.controller.stopFrequency
                    caption: root.controller.settingsValid ? qsTr("about %1").arg(root.duration(root.controller.plannedSeconds)) : ""
                    onValueModified: v => root.controller.stopFrequency = v
                }
                KnobField {
                    objectName: "amplitudeKnob"
                    Layout.preferredWidth: root.narrow ? 140 : 170
                    compact: root.narrow
                    enabled: !root.measurementLocked
                    label: qsTr("Amplitude")
                    unit: "Vpk"
                    accentColor: Theme.signalColor("ao0")
                    from: 0
                    to: 5
                    stepSize: 0.01
                    value: root.controller.peakAmplitude
                    formatter: v => v.toFixed(2) + " Vpk"
                    caption: qsTr("sine on AO 0")
                    onValueModified: v => root.controller.peakAmplitude = Math.round(v * 100) / 100
                }
            }
        ]

        measurements: [
            Flow {
                objectName: "bodeCards"
                readonly property string summaryText: root.controller.cutoffFrequencies.length > 0
                    ? qsTr("f−3dB %1").arg(Units.format(root.controller.cutoffFrequencies[0], "Hz", 3)) : ""
                width: parent ? parent.width : 0
                spacing: Theme.spacingS
                padding: Theme.spacingS
                component Card: MeasurementCard {
                    removable: false
                    width: root.narrow ? 150 : 180
                }
                Card {
                    objectName: "cardCutoff"
                    title: qsTr("f−3dB")
                    color: root.gainColor
                    value: root.controller.cutoffFrequencies.length > 0
                           ? root.controller.cutoffFrequencies.map(f => Units.format(f, "Hz", 3)).join(" · ") : "—"
                }
                Card {
                    objectName: "cardUnity"
                    title: qsTr("0 dB crossover")
                    color: root.gainColor
                    value: isFinite(root.controller.unityGainFrequency) && root.controller.unityGainFrequency > 0
                           ? Units.format(root.controller.unityGainFrequency, "Hz", 3) : "—"
                }
                Card {
                    objectName: "cardGainMargin"
                    title: qsTr("Gain margin")
                    color: root.gainColor
                    value: isFinite(root.controller.gainMargin) ? Units.decibels(root.controller.gainMargin, 1) : "—"
                }
                Card {
                    objectName: "cardPhaseMargin"
                    title: qsTr("Phase margin")
                    color: root.phaseColor
                    value: isFinite(root.controller.phaseMargin) ? root.controller.phaseMargin.toFixed(1) + "°" : "—"
                }
                Card {
                    objectName: "cardMaxGain"
                    title: qsTr("Max gain")
                    color: root.gainColor
                    value: root.hasPoints && isFinite(root.controller.maxGainDb) ? Units.decibels(root.controller.maxGainDb, 2) : "—"
                }
                Card {
                    objectName: "cardCursor"
                    width: root.narrow ? 220 : 280
                    title: root.cursorValid ? qsTr("Cursor") : qsTr("Last point")
                    color: root.cursorValid ? root.cursorColor : Theme.textSecondary
                    value: root.controller.readoutValid
                           ? qsTr("%1 · %2 dB · %3°").arg(Units.format(root.controller.readoutFrequency, "Hz", 4))
                                 .arg(root.fixed2(root.controller.readoutGainDb)).arg(root.fixed2(root.controller.readoutPhase))
                           : "—"
                }
            }
        ]

        inspector: [
            Section {
                title: qsTr("Measurement Settings")
                summary: qsTr("%1 pts").arg(root.controller.plannedPoints)
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Stimulus is measured on AI 0, response on AI 1 (the generator AO 0 drives the circuit).")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: !root.controller.settingsValid
                    Layout.fillWidth: true
                    text: qsTr("Start Frequency must not be higher than Stop Frequency.")
                    color: Theme.danger
                    font.pixelSize: Theme.fontSizeS + 1
                    wrapMode: Text.WordWrap
                }
                FieldRow {
                    Field {
                        label: qsTr("Steps")
                        trailing: qsTr("per decade")
                        SpinBox {
                            Layout.fillWidth: true
                            from: 1
                            to: 1000
                            editable: true
                            enabled: !root.measurementLocked
                            value: root.controller.stepsPerDecade
                            onValueModified: root.controller.stepsPerDecade = value
                        }
                    }
                }
                Field {
                    label: qsTr("Op-Amp Signal Polarity")
                    hint: qsTr("Inverted flips the response's sign so inverting amplifiers read about 0°. It only changes the calculation: switch it at any time.")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Normal"), qsTr("Inverted")]
                        currentIndex: root.controller.polarity === BodeController.Inverted ? 1 : 0
                        onActivated: index => root.controller.polarity = index === 1 ? BodeController.Inverted : BodeController.Normal
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.controller.settingsValid
                          ? qsTr("%1 points · about %2").arg(root.controller.plannedPoints).arg(root.duration(root.controller.plannedSeconds))
                          : ""
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS + 1
                    font.features: Theme.tabularFigures
                }
            },
            Section {
                title: qsTr("Graph Settings")
                summary: (root.linearGain ? qsTr("Gain ratio") : qsTr("Gain dB")) + (root.controller.autoscale ? qsTr(" · Auto") : "")
                FieldRow {
                    Field {
                        label: qsTr("Mapping")
                        hint: qsTr("Gain axis: Logarithmic shows dB, Linear the plain ratio. The phase plot is not affected.")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Linear"), qsTr("Logarithmic")]
                            currentIndex: root.linearGain ? 0 : 1
                            onActivated: index => root.controller.mapping = index === 0 ? BodeController.Linear : BodeController.Logarithmic
                        }
                    }
                    Field {
                        label: " "
                        CheckBox {
                            text: qsTr("Autoscale")
                            checked: root.controller.autoscale
                            onToggled: root.controller.autoscale = checked
                        }
                    }
                }
                Field {
                    label: qsTr("Frequency axis")
                    hint: qsTr("Logarithmic or linear frequency axis")
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: [qsTr("Logarithmic"), qsTr("Linear")]
                        currentIndex: root.controller.logFrequencyAxis ? 0 : 1
                        onActivated: index => root.controller.logFrequencyAxis = index === 0
                    }
                }
                ToggleSwitch {
                    text: qsTr("Unwrap phase")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Removes the 360° jumps between consecutive points (off: phase wrapped to ±180°)")
                    checked: root.controller.unwrapPhase
                    onToggled: root.controller.unwrapPhase = checked
                }
            },
            Section {
                title: qsTr("Cursor Settings")
                summary: !root.controller.cursorsOn ? qsTr("Off") : root.cursorValid ? Units.format(root.controller.cursorFrequency, "Hz", 4) : qsTr("On")
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    CheckBox {
                        text: qsTr("Cursors On")
                        checked: root.controller.cursorsOn
                        onToggled: root.controller.cursorsOn = checked
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("Position")
                        color: root.controller.cursorsOn ? Theme.textSecondary : Theme.textDisabled
                        font.pixelSize: Theme.fontSizeS + 1
                    }
                    IconButton {
                        iconName: "chevron-left"
                        bordered: true
                        size: Theme.controlHeight
                        enabled: root.controller.cursorsOn && root.controller.cursorIndex > 0
                        toolTip: qsTr("Left: previous point")
                        onClicked: root.controller.cursorLeft()
                    }
                    IconButton {
                        iconName: "chevron-right"
                        bordered: true
                        size: Theme.controlHeight
                        enabled: root.controller.cursorsOn && root.controller.cursorIndex < root.controller.pointCount - 1
                        toolTip: qsTr("Right: next point")
                        onClicked: root.controller.cursorRight()
                    }
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Re-measure point at cursor")
                    iconName: "refresh"
                    enabled: root.controller.cursorsOn && root.controller.cursorIndex >= 0 && root.controller.canRun
                             && !root.controller.paused
                    toolTip: qsTr("Measures the cursor's frequency again and replaces that point")
                    onClicked: root.controller.remeasureCursorPoint()
                }
            },
            Section {
                title: qsTr("Sweep")
                iconName: "sweep"
                summary: root.controller.sweepMode === BodeController.Fast ? qsTr("Fast") : qsTr("Standard")
                Field {
                    label: qsTr("Mode")
                    SegmentedControl {
                        Layout.fillWidth: true
                        enabled: !root.measurementLocked
                        model: [
                            { text: qsTr("Standard"), toolTip: qsTr("A short range pre-measurement at every point") },
                            { text: qsTr("Fast"), toolTip: qsTr("No pre-measurement; ranges adapt from the previous point") }
                        ]
                        currentIndex: root.controller.sweepMode === BodeController.Fast ? 1 : 0
                        onActivated: index => root.controller.sweepMode = index === 1 ? BodeController.Fast : BodeController.Standard
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.controller.sweepMode === BodeController.Fast
                          ? qsTr("No range pre-measurement: the circuit settles for 3 periods, the input ranges adapt from the previous point and clipped records are measured again.")
                          : qsTr("Each point first measures both inputs briefly (80 ms) on ±10 V to choose their ranges.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS + 1
                    wrapMode: Text.WordWrap
                }
                Field {
                    label: qsTr("Averages per point")
                    hint: qsTr("MYBENCH extension: records averaged at each frequency (complex average)")
                    SpinBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 16
                        enabled: !root.measurementLocked
                        value: root.controller.averagesPerPoint
                        onValueModified: root.controller.averagesPerPoint = value
                    }
                }
            },
            Section {
                title: qsTr("Markers")
                iconName: "marker"
                summary: root.controller.cutoffFrequencies.length > 0 ? qsTr("−3 dB %1").arg(Units.format(root.controller.cutoffFrequencies[0], "Hz", 3)) : ""
                ToggleSwitch {
                    text: qsTr("−3 dB points")
                    checked: root.controller.showCutoffMarkers
                    onToggled: root.controller.showCutoffMarkers = checked
                }
                ToggleSwitch {
                    text: qsTr("Unity gain (0 dB crossover)")
                    checked: root.controller.showUnityGainMarker
                    onToggled: root.controller.showUnityGainMarker = checked
                }
                ToggleSwitch {
                    text: qsTr("Gain and phase margins")
                    checked: root.controller.showMarginMarkers
                    onToggled: root.controller.showMarginMarkers = checked
                }
                GridLayout {
                    Layout.fillWidth: true
                    visible: root.hasPoints
                    columns: 2
                    columnSpacing: Theme.spacingM
                    rowSpacing: 3
                    component Key: Text {
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS + 1
                    }
                    component Value: Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeS + 1
                        font.features: Theme.tabularFigures
                    }
                    Key { text: qsTr("Maximum gain") }
                    Value { text: isFinite(root.controller.maxGainDb) ? Units.decibels(root.controller.maxGainDb) : "—" }
                    Key { text: qsTr("−3 dB") }
                    Value { text: root.controller.cutoffFrequencies.length > 0 ? root.controller.cutoffFrequencies.map(f => Units.format(f, "Hz", 4)).join(", ") : "—" }
                    Key { text: qsTr("0 dB crossover") }
                    Value { text: isFinite(root.controller.unityGainFrequency) ? Units.format(root.controller.unityGainFrequency, "Hz", 4) : "—" }
                    Key { text: qsTr("Phase margin") }
                    Value { text: isFinite(root.controller.phaseMargin) ? root.controller.phaseMargin.toFixed(1) + "°" : "—" }
                    Key { text: qsTr("Gain margin") }
                    Value {
                        text: isFinite(root.controller.gainMargin)
                              ? qsTr("%1 at %2").arg(Units.decibels(root.controller.gainMargin, 1)).arg(Units.format(root.controller.phaseCrossoverFrequency, "Hz", 3))
                              : "—"
                    }
                }
            },
            Section {
                title: qsTr("References")
                iconName: "layers"
                summary: root.controller.references.length > 0 ? qsTr("%1 kept").arg(root.controller.references.length) : ""
                ActionButton {
                    Layout.fillWidth: true
                    text: qsTr("Keep as reference")
                    iconName: "plus"
                    enabled: root.hasPoints && !root.controller.running && root.controller.references.length < root.controller.maxReferences
                    onClicked: root.controller.keepAsReference()
                }
                Text {
                    visible: root.controller.references.length === 0
                    Layout.fillWidth: true
                    text: qsTr("Keep a finished sweep to draw it under the next ones (up to %1).").arg(root.controller.maxReferences)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS + 1
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: root.controller.references
                    delegate: RowLayout {
                        id: referenceRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Rectangle {
                            implicitWidth: 14
                            implicitHeight: 3
                            radius: 1.5
                            color: root.referenceColor(referenceRow.modelData.colorIndex)
                            opacity: referenceRow.modelData.visible ? 1 : 0.35
                        }
                        TextField {
                            Layout.fillWidth: true
                            text: referenceRow.modelData.name
                            font.pixelSize: Theme.fontSizeM
                            onEditingFinished: root.controller.renameReference(referenceRow.modelData.slot, text)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("%1 points%2 · click to rename").arg(referenceRow.modelData.points)
                                              .arg(referenceRow.modelData.inverted ? qsTr(", inverted") : "")
                        }
                        IconButton {
                            iconName: referenceRow.modelData.visible ? "eye" : "eye-off"
                            size: 24
                            iconSize: Theme.iconSizeS
                            toolTip: referenceRow.modelData.visible ? qsTr("Hide") : qsTr("Show")
                            onClicked: root.controller.setReferenceVisible(referenceRow.modelData.slot, !referenceRow.modelData.visible)
                        }
                        IconButton {
                            iconName: "download"
                            size: 24
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("Export this reference…")
                            onClicked: {
                                referenceDialog.slot = referenceRow.modelData.slot
                                referenceDialog.selectedFile = DataExport.suggestedFileUrl("bode-reference", "csv")
                                referenceDialog.open()
                            }
                        }
                        IconButton {
                            iconName: "trash"
                            size: 24
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("Delete")
                            onClicked: root.controller.removeReference(referenceRow.modelData.slot)
                        }
                    }
                }
            }
        ]
    }

    ReferenceExportDialog {
        id: referenceDialog
        controller: root.controller
    }
}
