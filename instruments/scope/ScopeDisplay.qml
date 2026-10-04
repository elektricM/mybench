pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// The Oscilloscope's display: the 10 × 10 division graph (YT) or the XY graph, the read-only scale
// legend of the shown traces under it, and the spectrum view of the FFT section below when it is
// on. Direct manipulation (scope.md design notes): drag a channel's zero marker (left edge) for its
// position, the trigger level marker (right edge) and the horizontal position marker (top edge);
// the wheel over the graph steps Time/div, Shift + wheel the Scale of the nearest channel. Plot
// tools, cursors toggle and the context menu come from BenchPlot.
Item {
    id: root

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeMath math: controller.math
    readonly property bool xyMode: controller.displayMode === ScopeController.XY
    readonly property bool edge: controller.triggerType === ScopeController.Edge && !controller.rolling
    readonly property bool fftShown: controller.fftShown
    readonly property int fftChannel: controller.fftSource === ScopeController.FftChannel1 ? 1 : 0
    // The trigger uses the chosen channel while it is enabled, else the other one (controller rule).
    readonly property int triggerChannel: {
        const wanted = controller.triggerSource === ScopeController.Chan1Source ? 1 : 0
        return (wanted === 0 ? ch0 : ch1).enabled ? wanted : 1 - wanted
    }
    readonly property alias ytPlot: ytPlot
    readonly property alias xyPlot: xyPlot
    readonly property alias fftPlot: fftPlot
    readonly property BenchPlot timePlot: xyMode ? xyPlot : ytPlot

    // Bound to this (not to controller.fftMaxFrequency, notified with every record) so the spectrum
    // keeps the user's zoom until the rate really changes.
    readonly property real fftNyquist: Math.max(1, controller.fftMaxFrequency)
    // The kept trace is drawn faded while Normal mode waits for a trigger.
    readonly property bool dimmed: controller.running && controller.waitingForTrigger
                                   && controller.noTriggerMode === ScopeController.KeepLastTrace
    // Persistence: any scale, position, timebase or trigger change drops the afterglow of the old
    // setting (the traces clear their ghosts while persistence is 0 for a moment).
    property bool persistenceHold: false
    readonly property int persistenceEpoch: controller.persistenceEpoch
    readonly property int persistence: persistenceHold ? 0 : controller.persistence
    onPersistenceEpochChanged: {
        if (controller.persistence > 0) {
            persistenceHold = true
            persistenceRelease.restart()
        }
    }
    Timer {
        id: persistenceRelease
        interval: 60
        onTriggered: root.persistenceHold = false
    }

    readonly property string idleText: !controller.running && !controller.hasRecord ? qsTr("Press Run or Space to start") : ""

    // Trace colors: the trace style's color when the user picked one, else the signal palette.
    function styleColor(style: TraceStyle, fallback: color): color {
        return style && style.lineColor.valid ? style.lineColor : fallback
    }
    function traceColor(index: int): color {
        return index === 0 ? styleColor(ch0.style, Theme.signalCh0)
             : index === 1 ? styleColor(ch1.style, Theme.signalCh1)
                           : styleColor(math.style, Theme.signalMath)
    }
    function faded(c: color): color {
        return root.dimmed ? Theme.alpha(c, 0.4) : c
    }
    function traceFor(index: int): PlotTrace {
        return index === 0 ? trace0 : index === 1 ? trace1 : mathTrace
    }
    function markerShape(pointStyle: int): int {
        switch (pointStyle) {
        case TraceStyle.FilledCircle: return PlotMarker.Circle
        case TraceStyle.Square:
        case TraceStyle.FilledSquare: return PlotMarker.Square
        case TraceStyle.Diamond:
        case TraceStyle.FilledDiamond: return PlotMarker.Diamond
        default: return PlotMarker.Ring
        }
    }

    // One legend entry: swatch, name and the read-only scale.
    component LegendEntry: Row {
        id: entry
        property color swatch
        property string name
        property string detail
        spacing: Theme.spacingXs + 2
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 2
            color: entry.swatch
        }
        Text {
            text: entry.name
            color: Theme.displayText
            font.pixelSize: Theme.fontSizeCaption
            font.weight: Font.Medium
        }
        Text {
            text: entry.detail
            color: Theme.displayTextSecondary
            font.pixelSize: Theme.fontSizeCaption
            font.features: Theme.tabularFigures
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 3

            // ---- YT graph -----------------------------------------------------------------------
            BenchPlot {
                id: ytPlot
                objectName: "ytPlot"
                anchors.fill: parent
                visible: !root.xyMode
                hoverReadout: true
                graticule: Plot.Scope
                leftMargin: root.fftShown ? fftPlot.plotArea.x : -1
                rightMargin: root.fftShown ? fftPlot.width - fftPlot.plotArea.x - fftPlot.plotArea.width : -1
                placeholderText: root.idleText
                cursorsAvailable: true
                cursorsEnabled: root.controller.cursorsOn
                onCursorsToggled: on => root.controller.cursorsOn = on
                xAxis {
                    min: 0
                    max: root.controller.screenTime
                    divisions: 10
                    unit: "s"
                    unitInLabels: true
                    labelsVisible: false
                    lowerLimit: 0
                    upperLimit: root.controller.screenTime
                }
                yAxis { min: -5; max: 5; divisions: 10; labelsVisible: false; interactive: false }

                // Wheel = Time/div, Shift + wheel = Scale of the channel nearest the pointer (wheel
                // up = smaller value, as a bench scope's knob turned clockwise).
                WheelHandler {
                    id: wheelTime
                    acceptedModifiers: Qt.NoModifier
                    property real accumulated: 0
                    onWheel: event => {
                        accumulated += event.angleDelta.y
                        const steps = Math.trunc(accumulated / 120)
                        if (steps !== 0) {
                            accumulated -= steps * 120
                            root.controller.stepTimeDiv(-steps)
                        }
                    }
                }
                WheelHandler {
                    id: wheelVolts
                    acceptedModifiers: Qt.ShiftModifier
                    property real accumulated: 0
                    onWheel: event => {
                        // Shift turns the wheel horizontal on some systems.
                        accumulated += event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        const steps = Math.trunc(accumulated / 120)
                        if (steps === 0)
                            return
                        accumulated -= steps * 120
                        const area = ytPlot.plotArea
                        const divisions = 5 - (wheelVolts.point.position.y - area.y) / area.height * 10
                        const channel = root.controller.channelNearestTo(divisions)
                        if (channel >= 0)
                            root.controller.stepChannelScale(channel, -steps)
                    }
                }

                PlotTrace {
                    id: refATrace
                    name: "REF A"
                    unit: "V"
                    buffer: root.controller.refA
                    visible: root.controller.refAAvailable && root.controller.refAVisible
                    color: Theme.signalRef
                    userStyle: root.controller.refAStyle
                    yScale: 1 / root.controller.refAScale
                    yOffset: root.controller.refAPosition
                }
                PlotTrace {
                    id: refBTrace
                    name: "REF B"
                    unit: "V"
                    buffer: root.controller.refB
                    visible: root.controller.refBAvailable && root.controller.refBVisible
                    color: Theme.signalRef
                    userStyle: root.controller.refBStyle
                    yScale: 1 / root.controller.refBScale
                    yOffset: root.controller.refBPosition
                }
                PlotTrace {
                    id: mathTrace
                    name: root.math.functionName
                    unit: root.math.unit
                    buffer: root.controller.mathTrace
                    visible: root.math.enabled
                    color: root.faded(Theme.signalMath)
                    userStyle: root.math.style
                    yScale: 1 / root.math.voltsPerDiv
                    yOffset: root.math.position
                    persistence: root.persistence
                }
                PlotTrace {
                    id: trace1
                    name: "CH1"
                    unit: "V"
                    buffer: root.controller.ch1
                    visible: root.ch1.enabled
                    color: root.faded(Theme.signalCh1)
                    userStyle: root.ch1.style
                    yScale: 1 / root.ch1.voltsPerDiv
                    yOffset: root.ch1.position
                    persistence: root.persistence
                }
                PlotTrace {
                    id: trace0
                    name: "CH0"
                    unit: "V"
                    buffer: root.controller.ch0
                    visible: root.ch0.enabled
                    color: root.faded(Theme.signalCh0)
                    userStyle: root.ch0.style
                    yScale: 1 / root.ch0.voltsPerDiv
                    yOffset: root.ch0.position
                    persistence: root.persistence
                }

                // Zero markers at the left edge: drag = the trace's position.
                PlotLine {
                    visible: root.math.enabled
                    value: root.math.position
                    handle: PlotLine.Start
                    label: "M"
                    color: root.traceColor(2)
                    lineMode: PlotLine.WhileDragging
                    onMoved: v => root.math.position = v
                }
                PlotLine {
                    visible: root.ch1.enabled
                    value: root.ch1.position
                    handle: PlotLine.Start
                    label: "1"
                    color: root.traceColor(1)
                    lineMode: PlotLine.WhileDragging
                    onMoved: v => root.ch1.position = v
                }
                PlotLine {
                    visible: root.ch0.enabled
                    value: root.ch0.position
                    handle: PlotLine.Start
                    label: "0"
                    color: root.traceColor(0)
                    lineMode: PlotLine.WhileDragging
                    onMoved: v => root.ch0.position = v
                }
                // Trigger level (right edge, in volts of the trigger channel) and horizontal
                // position (top edge), in the trigger channel's color.
                PlotLine {
                    objectName: "triggerLevelMarker"
                    visible: root.edge
                    trace: root.traceFor(root.triggerChannel)
                    value: root.controller.triggerLevel
                    handle: PlotLine.End
                    label: "T"
                    color: root.traceColor(root.triggerChannel)
                    style: PlotElement.Dash
                    lineMode: PlotLine.WhileDragging
                    onMoved: v => root.controller.triggerLevel = Math.round(v * 1000) / 1000
                }
                PlotLine {
                    objectName: "triggerPositionMarker"
                    visible: root.edge
                    orientation: PlotElement.Vertical
                    value: root.controller.horizontalPosition / 100 * root.controller.screenTime
                    handle: PlotLine.Start
                    label: "T"
                    color: root.traceColor(root.triggerChannel)
                    lineMode: PlotLine.WhileDragging
                    onMoved: v => root.controller.horizontalPosition = Math.round(v / root.controller.screenTime * 100)
                }

                // Cursors C1 / C2: values are in the measurement strip (not on the plot).
                ScopeCursorItem {
                    id: cursor1
                    cursor: root.controller.cursor1
                    shown: root.controller.cursorsOn
                    trace: root.traceFor(root.controller.cursor1.channel)
                }
                ScopeCursorItem {
                    id: cursor2
                    cursor: root.controller.cursor2
                    shown: root.controller.cursorsOn
                    trace: root.traceFor(root.controller.cursor2.channel)
                }
                PlotLine {
                    visible: cursor1.crosshair
                    trace: cursor1.trace
                    value: cursor1.value
                    handle: PlotLine.NoHandle
                    draggable: false
                    color: cursor1.color
                    style: cursor1.style
                    width: cursor1.lineWidth
                }
                PlotLine {
                    visible: cursor2.crosshair
                    trace: cursor2.trace
                    value: cursor2.value
                    handle: PlotLine.NoHandle
                    draggable: false
                    color: cursor2.color
                    style: cursor2.style
                    width: cursor2.lineWidth
                }
                PlotMarker {
                    visible: cursor1.marked
                    x: cursor1.position
                    trace: cursor1.trace
                    color: cursor1.color
                    shape: root.markerShape(cursor1.cursor.style.pointStyle)
                    size: 8 + 2 * cursor1.lineWidth
                }
                PlotMarker {
                    visible: cursor2.marked
                    x: cursor2.position
                    trace: cursor2.trace
                    color: cursor2.color
                    shape: root.markerShape(cursor2.cursor.style.pointStyle)
                    size: 8 + 2 * cursor2.lineWidth
                }

                // Level cursors: two horizontal lines on one trace (values in the strip).
                PlotLine {
                    visible: root.controller.levelCursorsOn
                    trace: root.traceFor(root.controller.levelCursorChannel)
                    value: root.controller.levelCursor1
                    handle: PlotLine.End
                    label: "H1"
                    color: Theme.signalCursor
                    style: PlotElement.Dash
                    lineMode: PlotLine.Always
                    onMoved: v => root.controller.levelCursor1 = v
                }
                PlotLine {
                    visible: root.controller.levelCursorsOn
                    trace: root.traceFor(root.controller.levelCursorChannel)
                    value: root.controller.levelCursor2
                    handle: PlotLine.End
                    label: "H2"
                    color: Theme.signalCursor
                    style: PlotElement.Dash
                    lineMode: PlotLine.Always
                    onMoved: v => root.controller.levelCursor2 = v
                }
            }

            // ---- XY graph: CH0 on X, CH1 on Y ---------------------------------------------------
            BenchPlot {
                id: xyPlot
                objectName: "xyPlot"
                // Square, so circles stay circles (Lissajous figures).
                // (The YT plot fills the same area with the same margins.)
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height - (ytPlot.height - ytPlot.plotArea.height)
                                              + (ytPlot.width - ytPlot.plotArea.width))
                height: parent.height
                visible: root.xyMode
                hoverReadout: true
                graticule: Plot.Scope
                placeholderText: root.ch0.enabled && root.ch1.enabled ? root.idleText
                                                                     : qsTr("XY needs Channel 0 and Channel 1")
                xAxis { min: -5; max: 5; divisions: 10; labelsVisible: false }
                yAxis { min: -5; max: 5; divisions: 10; labelsVisible: false }
                PlotTrace {
                    name: qsTr("CH1 vs CH0")
                    buffer: root.controller.xyTrace
                    visible: root.ch0.enabled && root.ch1.enabled
                    color: Theme.signalCh0
                    userStyle: root.ch0.style
                    xScale: 1 / root.ch0.voltsPerDiv
                    xOffset: root.ch0.position
                    yScale: 1 / root.ch1.voltsPerDiv
                    yOffset: root.ch1.position
                    persistence: root.persistence
                }
            }
        }

        // ---- Scale legend of the shown traces (read-only; settings are in the inspector) ------------
        Item {
            id: legend
            objectName: "scaleLegend"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.lineHeight + Theme.spacingS
            Row {
                x: root.timePlot.x + root.timePlot.plotArea.x
                width: root.timePlot.plotArea.width
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingL
                clip: true
                LegendEntry {
                    visible: root.ch0.enabled
                    swatch: root.traceColor(0)
                    name: root.xyMode ? qsTr("X  CH0") : "CH0"
                    detail: root.ch0.scaleLabel + "/div" + (root.ch0.audio ? "  AC" : "")
                }
                LegendEntry {
                    visible: root.ch1.enabled
                    swatch: root.traceColor(1)
                    name: root.xyMode ? qsTr("Y  CH1") : "CH1"
                    detail: root.ch1.scaleLabel + "/div" + (root.ch1.audio ? "  AC" : "")
                }
                LegendEntry {
                    visible: root.math.enabled && !root.xyMode
                    swatch: root.traceColor(2)
                    name: qsTr("Math")
                    detail: root.math.functionName + "  " + root.math.scaleLabels[root.math.scaleIndex] + "/div"
                }
                LegendEntry {
                    visible: refATrace.visible && !root.xyMode
                    swatch: refATrace.userStyle.lineColor.valid ? refATrace.userStyle.lineColor : Theme.signalRef
                    name: "REF A"
                    detail: root.controller.refALabel
                }
                LegendEntry {
                    visible: refBTrace.visible && !root.xyMode
                    swatch: refBTrace.userStyle.lineColor.valid ? refBTrace.userStyle.lineColor : Theme.signalRef
                    name: "REF B"
                    detail: root.controller.refBLabel
                }
            }
        }

        // ---- Spectrum of the FFT section's channel ----------------------------------------------------
        Rectangle {
            visible: root.fftShown
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.borderWidth
            color: Theme.separator
        }
        BenchPlot {
            id: fftPlot
            objectName: "fftPlot"
            visible: root.fftShown
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 2
            hoverReadout: true
            xAxis {
                min: 0
                max: root.fftNyquist
                unit: "Hz"
                unitInLabels: true
                lowerLimit: 0
                upperLimit: root.fftNyquist
                minimumSpan: root.fftNyquist / 200
            }
            yAxis { min: -140; max: 20; unit: "dBV"; limitToData: true; limitMargin: 0.5; minimumSpan: 3 }
            PlotTrace {
                name: qsTr("Spectrum CH%1").arg(root.fftChannel)
                unit: "dBV"
                buffer: root.controller.fftTrace
                color: root.traceColor(root.fftChannel)
                width: 1.2
                fill: true
                fillOpacity: 0.12
            }
            PlotMarker {
                visible: !isNaN(root.math.peakFrequency)
                x: root.math.peakFrequency
                y: root.math.peakLevel
                shape: PlotMarker.TriangleDown
                color: root.traceColor(root.fftChannel)
                label: Units.format(root.math.peakFrequency, "Hz", 4) + "  " + root.math.peakLevel.toFixed(1) + " dBV"
            }
        }
        Item {
            objectName: "fftLegend"
            visible: root.fftShown
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.lineHeight + Theme.spacingS
            LegendEntry {
                x: fftPlot.plotArea.x
                anchors.verticalCenter: parent.verticalCenter
                swatch: root.traceColor(root.fftChannel)
                name: qsTr("CH%1 spectrum").arg(root.fftChannel)
                detail: qsTr("Hann window · dBV RMS")
            }
        }
    }
}
