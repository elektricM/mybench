pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Bode Analyzer panel (docs/instruments/bode.md), PicoScope-7 pattern: Sweep button, Keep as
// Reference and the Start/Stop/Amplitude knobs on top; gain and phase plots sharing one frequency
// axis on the left; the InstrumentColumn on the right with the Sweep card (steps, mode, polarity,
// averages) and the tool palette (Graph, Cursors, References, Connections); result cards and the
// cursor readout (C1, C2, their difference and slope) in one row below. All state and logic live
// in BodeController.
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
    // The cursor pair: C1 and C2 in the shared cursor colors.
    readonly property color cursor1Color: Theme.cursor1
    readonly property color cursor2Color: Theme.cursor2
    // Stability margins (GM, PM): a caution tone, distinct from the cursors and the −3 dB markers.
    readonly property color marginColor: Theme.caution
    readonly property bool narrow: width < Theme.instrumentCompactWidth   // compact knobs and column
    readonly property bool referencesShown: controller.references.some(r => r.visible)
    property string tool                            // the open tool panel ("" = none)
    // Steps per decade on the stepped knob; a restored odd value stays selectable.
    readonly property var stepValues: {
        const base = [1, 2, 5, 10, 20, 25, 50, 100, 200, 500, 1000]
        const v = controller.stepsPerDecade
        return base.indexOf(v) >= 0 ? base : base.concat([v]).sort((a, b) => a - b)
    }
    readonly property string progressText: {
        const c = controller
        if (!c.running || c.remeasuring || c.totalPoints <= 0)
            return ""
        const left = c.remainingSeconds > 0 ? qsTr(" · ≈ %1 left").arg(duration(c.remainingSeconds)) : ""
        return qsTr("%1/%2").arg(Math.min(c.pointCount + 1, c.totalPoints)).arg(c.totalPoints) + left
    }
    readonly property bool measurementLocked: controller.running
    readonly property real leftMargin: Math.max(gainPlot.implicitLeftMargin, phasePlot.implicitLeftMargin)

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
        showRunControls: false
        runBar.showTimeout: false
        inspector: []
        // No display title: the wiring shows as a small diagram while there is no sweep; every
        // setting and result is shown once, in its control or card.

        Item {
            anchors.fill: parent

            ColumnLayout {
                id: plots
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
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
                            label: qsTr("−3 dB")
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
                        label: qsTr("0 dB")
                    }
                    PlotMarker {
                        visible: root.controller.showMarginMarkers && isFinite(root.controller.phaseCrossoverFrequency)
                        x: root.controller.phaseCrossoverFrequency
                        trace: gainTrace
                        shape: PlotMarker.TriangleDown
                        size: 10
                        color: root.marginColor
                        labelPosition: PlotMarker.Above
                        label: qsTr("GM")
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
                        objectName: "gainCursor1"
                        visible: root.controller.cursorsOn && root.hasPoints
                        position: root.controller.cursorFrequency
                        trace: gainTrace
                        snap: true
                        label: "C1"
                        color: root.cursor1Color
                        onMoved: position => root.controller.setCursorFrequency(position)
                    }
                    PlotCursor {
                        objectName: "gainCursor2"
                        visible: gainCursor.visible
                        position: root.controller.cursor2Frequency
                        trace: gainTrace
                        snap: true
                        label: "C2"
                        color: root.cursor2Color
                        onMoved: position => root.controller.setCursor2Frequency(position)
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
                        color: Theme.alpha(root.marginColor, 0.6)
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
                        color: root.marginColor
                        guides: true
                        labelPosition: PlotMarker.Left
                        label: qsTr("PM")
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
                        label: "C1"
                        color: root.cursor1Color
                        onMoved: position => root.controller.setCursorFrequency(position)
                    }
                    PlotCursor {
                        visible: gainCursor.visible
                        position: root.controller.cursor2Frequency
                        trace: phaseTrace
                        snap: true
                        label: "C2"
                        color: root.cursor2Color
                        onMoved: position => root.controller.setCursor2Frequency(position)
                    }
                }
            }

            // Empty plot (no sweep, no reference shown): how to wire the circuit, small, in the gain
            // plot's top-left corner. Always available in the Connections tool.
            Rectangle {
                id: wiringHint
                objectName: "wiringDiagram"
                x: plots.x + root.leftMargin + Theme.spacingS
                y: plots.y + Theme.spacingS
                visible: !root.hasPoints && !root.controller.running && !root.referencesShown
                width: hintDiagram.implicitWidth + 2 * Theme.spacingS
                height: hintDiagram.implicitHeight + 2 * Theme.spacingS
                radius: Theme.radiusL
                color: Theme.alpha(Theme.display, 0.9)
                border.width: 1
                border.color: Theme.displayBorder
                BodeWiring {
                    id: hintDiagram
                    x: Theme.spacingS
                    y: Theme.spacingS
                }
            }

            // Right column (fixed width): the Sweep card, the tool palette pinned at the bottom.
            InstrumentColumn {
                id: column
                objectName: "bodeColumn"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                compact: root.narrow

                ChannelCard {
                    objectName: "sweepCard"
                    Layout.fillWidth: true
                    title: qsTr("Sweep")
                    color: Theme.signalColor("ao0")
                    checkable: false
                    compact: root.narrow

                    KnobField {
                        objectName: "stepsKnob"
                        Layout.fillWidth: true
                        knobSize: 34
                        compact: root.narrow
                        enabled: !root.measurementLocked
                        label: qsTr("Steps / decade")
                        accentColor: Theme.signalColor("ao0")
                        values: root.stepValues
                        labels: root.stepValues.map(v => String(v))
                        currentIndex: Math.max(0, root.stepValues.indexOf(root.controller.stepsPerDecade))
                        caption: root.controller.settingsValid ? qsTr("%1 points").arg(root.controller.plannedPoints) : ""
                        onIndexActivated: index => root.controller.stepsPerDecade = root.stepValues[index]
                    }
                    RowLayout {
                        objectName: "modeButtons"
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            // Standard: play speed (one chevron); Fast: fast-forward (two chevrons).
                                                        model: [{ text: qsTr("Standard"), tip: qsTr("Each point first measures both inputs briefly on ±10 V to choose their ranges"),
                                      glyph: "M12 3l7 6-7 6" },
                                    { text: qsTr("Fast"), tip: qsTr("No pre-measurement: ranges follow the previous point, clipped records are measured again"),
                                      glyph: "M8 3l7 6-7 6 M16 3l7 6-7 6" }]
                            delegate: PictureButton {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                enabled: !root.measurementLocked
                                text: modelData.text
                                glyph: modelData.glyph
                                checked: (root.controller.sweepMode === BodeController.Fast ? 1 : 0) === index
                                ToolTip.visible: hovered
                                ToolTip.text: modelData.tip
                                onClicked: root.controller.sweepMode = index === 1 ? BodeController.Fast : BodeController.Standard
                            }
                        }
                    }
                    RowLayout {
                        objectName: "polarityButtons"
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            // Polarity of the response: a sine as measured, or flipped (inverting amplifier).
                            model: [{ text: qsTr("Normal"), tip: qsTr("Response as measured"),
                                      glyph: "M2 9h28 M2 9c4-10 9-10 14 0s9 10 14 0" },
                                    { text: qsTr("Inverted"), tip: qsTr("Flips the response's sign: inverting amplifiers read about 0°"),
                                      glyph: "M2 9h28 M2 9c4 10 9 10 14 0s9-10 14 0" }]
                            delegate: PictureButton {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                accentColor: root.phaseColor
                                text: modelData.text
                                glyph: modelData.glyph
                                checked: (root.controller.polarity === BodeController.Inverted ? 1 : 0) === index
                                ToolTip.visible: hovered
                                ToolTip.text: modelData.tip
                                onClicked: root.controller.polarity = index === 1 ? BodeController.Inverted : BodeController.Normal
                            }
                        }
                    }
                    KnobField {
                        objectName: "averagesKnob"
                        Layout.fillWidth: true
                        knobSize: 34
                        compact: root.narrow
                        enabled: !root.measurementLocked
                        label: qsTr("Averages")
                        accentColor: Theme.signalColor("ao0")
                        from: 1
                        to: 16
                        stepSize: 1
                        value: root.controller.averagesPerPoint
                        formatter: v => String(Math.round(v))
                        toolTip: qsTr("Records averaged at each frequency (complex average)")
                        onValueModified: v => root.controller.averagesPerPoint = Math.round(v)
                    }
                }

                tools: ToolPalette {
                    objectName: "bodeTools"
                    columns: root.narrow ? 4 : 2
                    cellHeight: root.narrow ? 40 : 46
                    currentId: root.tool
                    model: [{ id: "graph", text: root.narrow ? "" : qsTr("Graph"), toolTip: qsTr("Graph"), icon: "grid" },
                            { id: "cursors", text: root.narrow ? "" : qsTr("Cursors"), toolTip: qsTr("Cursors and markers"), icon: "crosshair" },
                            { id: "references", text: root.narrow ? "" : qsTr("References"), toolTip: qsTr("References"), icon: "layers" },
                            { id: "connections", text: root.narrow ? "" : qsTr("Wiring"), toolTip: qsTr("Connections"), icon: "activity" }]
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
                    objectName: "graphPanel"
                    visible: root.tool === "graph"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Graph")
                    onCloseRequested: root.tool = ""
                    ToolRow {
                        label: qsTr("Gain")
                        SegmentedControl {
                            objectName: "mappingButtons"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("dB"), toolTip: qsTr("Logarithmic mapping") },
                                    { text: qsTr("Ratio"), toolTip: qsTr("Linear mapping") }]
                            currentIndex: root.linearGain ? 1 : 0
                            onActivated: index => root.controller.mapping = index === 1 ? BodeController.Linear : BodeController.Logarithmic
                        }
                    }
                    ToolRow {
                        label: qsTr("Frequency")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [qsTr("Log"), qsTr("Linear")]
                            currentIndex: root.controller.logFrequencyAxis ? 0 : 1
                            onActivated: index => root.controller.logFrequencyAxis = index === 0
                        }
                    }
                    CheckBox {
                        text: qsTr("Autoscale")
                        checked: root.controller.autoscale
                        onToggled: root.controller.autoscale = checked
                    }
                    CheckBox {
                        text: qsTr("Unwrap phase")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Removes the 360° jumps between points (off: ±180°)")
                        checked: root.controller.unwrapPhase
                        onToggled: root.controller.unwrapPhase = checked
                    }
                }

                // Cursors (C1 / C2) and the automatic markers in one tool.
                ToolPanel {
                    objectName: "cursorsPanel"
                    visible: root.tool === "cursors"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Cursors")
                    onCloseRequested: root.tool = ""
                    Switch {
                        objectName: "cursorsSwitch"
                        Layout.fillWidth: true
                        text: qsTr("Cursors C1 and C2")
                        checked: root.controller.cursorsOn
                        onToggled: root.controller.cursorsOn = checked
                    }
                    // C1 steps point by point (drag either cursor on the plot); Re-measure replaces C1's point.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        enabled: root.controller.cursorsOn
                        Rectangle { implicitWidth: 10; implicitHeight: 10; radius: 3; color: root.cursor1Color }
                        Text {
                            text: qsTr("C1")
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeS
                            font.weight: Font.DemiBold
                        }
                        Item { Layout.fillWidth: true }
                        IconButton {
                            iconName: "chevron-left"
                            enabled: root.controller.cursorIndex > 0
                            toolTip: qsTr("Previous point")
                            onClicked: root.controller.cursorLeft()
                        }
                        IconButton {
                            iconName: "chevron-right"
                            enabled: root.controller.cursorIndex < root.controller.pointCount - 1
                            toolTip: qsTr("Next point")
                            onClicked: root.controller.cursorRight()
                        }
                        IconButton {
                            iconName: "refresh"
                            enabled: root.controller.cursorIndex >= 0 && root.controller.canRun && !root.controller.paused
                            toolTip: qsTr("Measure C1's frequency again and replace that point")
                            onClicked: root.controller.remeasureCursorPoint()
                        }
                    }
                    ToolRow {
                        label: qsTr("Markers")
                        CheckBox {
                            text: qsTr("−3 dB")
                            checked: root.controller.showCutoffMarkers
                            onToggled: root.controller.showCutoffMarkers = checked
                        }
                        CheckBox {
                            text: qsTr("0 dB crossover")
                            checked: root.controller.showUnityGainMarker
                            onToggled: root.controller.showUnityGainMarker = checked
                        }
                        CheckBox {
                            text: qsTr("Gain / phase margin")
                            checked: root.controller.showMarginMarkers
                            onToggled: root.controller.showMarginMarkers = checked
                        }
                    }
                }

                ToolPanel {
                    objectName: "referencesPanel"
                    visible: root.tool === "references"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("References")
                    onCloseRequested: root.tool = ""
                    // Kept with the header's Keep as Reference button.
                    Text {
                        visible: root.controller.references.length === 0
                        text: qsTr("None kept yet")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeS
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
                                onEditingFinished: root.controller.renameReference(referenceRow.modelData.slot, text)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("%1 points%2").arg(referenceRow.modelData.points)
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
                                toolTip: qsTr("Export…")
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

                ToolPanel {
                    objectName: "connectionsPanel"
                    visible: root.tool === "connections"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Connections")
                    onCloseRequested: root.tool = ""
                    BodeWiring {
                        Layout.alignment: Qt.AlignHCenter
                        scale: Math.min(1, (parent ? parent.width : 300) / implicitWidth)
                        transformOrigin: Item.TopLeft
                        Layout.preferredHeight: implicitHeight * scale
                        Layout.preferredWidth: implicitWidth * scale
                    }
                }
            }
        }

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
                    ToolTip.text: qsTr("Keep as Reference")
                    onClicked: root.controller.keepAsReference()
                }
                KnobField {
                    objectName: "startKnob"
                    compact: root.narrow
                    enabled: !root.measurementLocked
                    label: qsTr("Start")
                    unit: "Hz"
                    accentColor: root.controller.settingsValid ? root.gainColor : Theme.negative
                    from: 1
                    to: 20000
                    logarithmic: true
                    value: root.controller.startFrequency
                    caption: root.controller.settingsValid ? "" : qsTr("Start > Stop")
                    onValueModified: v => root.controller.startFrequency = v
                }
                KnobField {
                    objectName: "stopKnob"
                    compact: root.narrow
                    enabled: !root.measurementLocked
                    label: qsTr("Stop")
                    unit: "Hz"
                    accentColor: root.gainColor
                    from: 1
                    to: 20000
                    logarithmic: true
                    value: root.controller.stopFrequency
                    caption: root.controller.settingsValid && !root.controller.running ? qsTr("≈ %1").arg(root.duration(root.controller.plannedSeconds)) : ""
                    onValueModified: v => root.controller.stopFrequency = v
                }
                KnobField {
                    objectName: "amplitudeKnob"
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
                    onValueModified: v => root.controller.peakAmplitude = Math.round(v * 100) / 100
                }
            }
        ]

        // One row of result cards (wraps only in a narrow pane), then the cursor readout: C1, C2
        // and C2 − C1 with the slope when the cursors are on, else the last measured point.
        measurements: [
            Flow {
                id: cards
                objectName: "bodeCards"
                readonly property string summaryText: root.controller.cutoffFrequencies.length > 0
                    ? qsTr("f−3dB %1").arg(Units.format(root.controller.cutoffFrequencies[0], "Hz", 3)) : ""
                readonly property real readoutWidth: 360
                readonly property real cardWidth: Math.max(120, Math.floor((width - 2 * padding - 6 * spacing - readoutWidth) / 6))
                width: parent ? parent.width : 0
                spacing: Theme.spacingS
                padding: Theme.spacingS
                component Card: MeasurementCard {
                    removable: false
                    width: cards.cardWidth
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
                    title: qsTr("0 dB at f")
                    color: root.gainColor
                    value: isFinite(root.controller.unityGainFrequency) && root.controller.unityGainFrequency > 0
                           ? Units.format(root.controller.unityGainFrequency, "Hz", 3) : "—"
                }
                Card {
                    objectName: "cardGainMargin"
                    title: qsTr("Gain margin")
                    color: root.marginColor
                    value: isFinite(root.controller.gainMargin) ? Units.decibels(root.controller.gainMargin, 1) : "—"
                }
                Card {
                    objectName: "cardGainMarginFrequency"
                    title: qsTr("GM at f")
                    color: root.marginColor
                    value: isFinite(root.controller.phaseCrossoverFrequency) && root.controller.phaseCrossoverFrequency > 0
                           ? Units.format(root.controller.phaseCrossoverFrequency, "Hz", 3) : "—"
                }
                Card {
                    objectName: "cardPhaseMargin"
                    title: qsTr("Phase margin")
                    color: root.marginColor
                    value: isFinite(root.controller.phaseMargin) ? root.controller.phaseMargin.toFixed(1) + "°" : "—"
                }
                Card {
                    objectName: "cardMaxGain"
                    title: qsTr("Max gain")
                    color: root.gainColor
                    value: root.hasPoints && isFinite(root.controller.maxGainDb) ? Units.decibels(root.controller.maxGainDb, 2) : "—"
                }
                BodeReadout {
                    objectName: "cardCursor"
                    width: cards.readoutWidth
                    controller: root.controller
                    linearGain: root.linearGain
                    cursor1Color: root.cursor1Color
                    cursor2Color: root.cursor2Color
                }
            }
        ]
    }

    ReferenceExportDialog {
        id: referenceDialog
        controller: root.controller
    }
}
