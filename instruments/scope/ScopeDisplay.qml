pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Display area of the Oscilloscope: the 10 × 10 division scope graph (YT) or the XY graph, the
// optional FFT pane of the math channel, the draggable markers (channel zero levels, trigger level
// and horizontal position), the cursors, and the measurement rows under the graph.
Item {
    id: root

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeMath math: controller.math
    readonly property bool xyMode: controller.displayMode === ScopeController.XY
    readonly property bool edge: controller.triggerType === ScopeController.Edge && !controller.rolling
    readonly property bool fftShown: math.enabled && math.fft
    readonly property bool mathShown: math.enabled && !math.fft
    readonly property int triggerChannel: {
        const wanted = controller.triggerSource === ScopeController.Chan1Source ? 1 : 0
        const c = wanted === 0 ? ch0 : ch1
        return c.enabled ? wanted : 1 - wanted
    }
    readonly property bool customBackground: controller.background !== ScopeController.ThemeBackground
    readonly property bool whiteBackground: controller.background === ScopeController.White
    readonly property alias ytPlot: ytPlot
    readonly property bool dimmed: controller.running && controller.waitingForTrigger
                                   && controller.noTriggerMode === ScopeController.KeepLastTrace

    // PlotMarker shape of a ScopeTraceStyle.PointStyle (the marker has fewer shapes).
    function markerShape(pointStyle: int): int {
        switch (pointStyle) {
        case ScopeTraceStyle.FilledCircle: return PlotMarker.Circle
        case ScopeTraceStyle.Square:
        case ScopeTraceStyle.FilledSquare: return PlotMarker.Square
        case ScopeTraceStyle.Diamond:
        case ScopeTraceStyle.FilledDiamond: return PlotMarker.Diamond
        default: return PlotMarker.Ring
        }
    }
    function channelColor(index: int): color {
        const style = index === 0 ? ch0.style : index === 1 ? ch1.style : math.style
        if (style.lineColor.length > 0)
            return style.lineColor
        return index === 0 ? Theme.ch0 : index === 1 ? Theme.ch1 : Theme.math
    }
    function traceFor(index: int): PlotTrace {
        return index === 0 ? trace0 : index === 1 ? trace1 : mathTrace
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: Theme.spacingS

                // ---- YT graph -------------------------------------------------------------------
                BenchPlot {
                    id: ytPlot
                    visible: !root.xyMode
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.verticalStretchFactor: 3
                    graticule: Plot.Scope
                    colorScheme: root.customBackground ? (root.whiteBackground ? Plot.Light : Plot.Dark)
                                                      : (Theme.dark ? Plot.Dark : Plot.Light)
                    backgroundColor: root.customBackground ? (root.whiteBackground ? "#ffffff" : "#000000") : Theme.display
                    placeholderText: root.controller.running ? (root.controller.waitingForTrigger ? qsTr("Waiting for trigger…") : "")
                                                             : qsTr("Press Run to acquire")
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

                    // Wheel = Time/Div, Shift + wheel = Volts/Div of the channel nearest the pointer
                    // (direction: wheel up zooms in).
                    WheelHandler {
                        id: wheelTime
                        acceptedModifiers: Qt.NoModifier
                        onWheel: event => {
                            wheelTime.step(event.angleDelta.y, steps => root.controller.stepTimeDiv(steps))
                        }
                        property real accumulated: 0
                        function step(delta: real, apply: var) {
                            accumulated += delta
                            const steps = Math.trunc(accumulated / 120)
                            if (steps !== 0) {
                                accumulated -= steps * 120
                                apply(-steps)
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

                    ScopeTrace {
                        id: refATrace
                        buffer: root.controller.refA
                        visible: root.controller.refAAvailable && root.controller.refAVisible
                        name: "REF A"
                        unit: "V"
                        traceStyle: root.controller.refAStyle
                        defaultColor: Theme.ref
                        yScale: 1 / root.controller.refAScale
                        yOffset: root.controller.refAPosition
                    }
                    ScopeTrace {
                        id: refBTrace
                        buffer: root.controller.refB
                        visible: root.controller.refBAvailable && root.controller.refBVisible
                        name: "REF B"
                        unit: "V"
                        traceStyle: root.controller.refBStyle
                        defaultColor: Qt.lighter(Theme.ref, 1.35)
                        yScale: 1 / root.controller.refBScale
                        yOffset: root.controller.refBPosition
                    }
                    ScopeTrace {
                        id: mathTrace
                        dimmed: root.dimmed
                        buffer: root.controller.mathTrace
                        visible: root.mathShown
                        name: root.math.functionName
                        unit: root.math.unit
                        traceStyle: root.math.style
                        defaultColor: Theme.math
                        yScale: 1 / root.math.voltsPerDiv
                        yOffset: root.math.position
                        persistence: root.controller.persistence
                    }
                    ScopeTrace {
                        id: trace1
                        dimmed: root.dimmed
                        buffer: root.controller.ch1
                        visible: root.ch1.enabled
                        name: "CH 1"
                        unit: "V"
                        traceStyle: root.ch1.style
                        defaultColor: Theme.ch1
                        yScale: 1 / root.ch1.voltsPerDiv
                        yOffset: root.ch1.position
                        persistence: root.controller.persistence
                    }
                    ScopeTrace {
                        id: trace0
                        dimmed: root.dimmed
                        buffer: root.controller.ch0
                        visible: root.ch0.enabled
                        name: "CH 0"
                        unit: "V"
                        traceStyle: root.ch0.style
                        defaultColor: Theme.ch0
                        yScale: 1 / root.ch0.voltsPerDiv
                        yOffset: root.ch0.position
                        persistence: root.controller.persistence
                    }

                    // Channel zero markers (left edge): drag = Vertical Position.
                    PlotLine {
                        visible: root.mathShown
                        value: root.math.position
                        handle: PlotLine.Start
                        label: "M"
                        color: root.channelColor(2)
                        lineMode: PlotLine.WhileDragging
                        onMoved: v => root.math.position = v
                    }
                    PlotLine {
                        visible: root.ch1.enabled
                        value: root.ch1.position
                        handle: PlotLine.Start
                        label: "1"
                        color: root.channelColor(1)
                        lineMode: PlotLine.WhileDragging
                        onMoved: v => root.ch1.position = v
                    }
                    PlotLine {
                        visible: root.ch0.enabled
                        value: root.ch0.position
                        handle: PlotLine.Start
                        label: "0"
                        color: root.channelColor(0)
                        lineMode: PlotLine.WhileDragging
                        onMoved: v => root.ch0.position = v
                    }
                    // Trigger level (right edge, volts of the trigger channel) and horizontal
                    // position (top edge).
                    PlotLine {
                        visible: root.edge
                        trace: root.traceFor(root.triggerChannel)
                        value: root.controller.triggerLevel
                        handle: PlotLine.End
                        label: "T"
                        color: Theme.danger   // ELVISmx: the trigger level line is red
                        style: PlotElement.Dash
                        lineMode: PlotLine.Always
                        onMoved: v => root.controller.triggerLevel = Math.round(v * 1000) / 1000
                    }
                    PlotLine {
                        visible: root.edge
                        orientation: PlotElement.Vertical
                        value: root.controller.horizontalPosition / 100 * root.controller.screenTime
                        handle: PlotLine.Start
                        label: "T"
                        color: root.channelColor(root.triggerChannel)
                        lineMode: PlotLine.WhileDragging
                        onMoved: v => root.controller.horizontalPosition = Math.round(v / root.controller.screenTime * 100)
                    }

                    // Cursors C1 / C2 (Graph Properties > Cursors: style, width, point style).
                    PlotCursor {
                        id: cursor1
                        visible: root.controller.cursorsOn && root.controller.cursor1.cursorStyle !== ScopeCursor.NoCursor
                        label: "C1"
                        position: root.controller.cursor1.position
                        trace: root.traceFor(root.controller.cursor1.channel)
                        color: root.controller.cursor1.color.length > 0 ? root.controller.cursor1.color : Theme.cursor1
                        style: root.controller.cursor1.lineStyle
                        readoutVisible: root.controller.cursor1.showValue
                        snap: true
                        onMoved: p => root.controller.cursor1.position = p
                    }
                    PlotCursor {
                        id: cursor2
                        visible: root.controller.cursorsOn && root.controller.cursor2.cursorStyle !== ScopeCursor.NoCursor
                        label: "C2"
                        position: root.controller.cursor2.position
                        trace: root.traceFor(root.controller.cursor2.channel)
                        color: root.controller.cursor2.color.length > 0 ? root.controller.cursor2.color : Theme.cursor2
                        style: root.controller.cursor2.lineStyle
                        readoutVisible: root.controller.cursor2.showValue
                        snap: true
                        onMoved: p => root.controller.cursor2.position = p
                    }
                    // Full crosshair: the horizontal line through the cursor's value.
                    PlotLine {
                        visible: root.controller.cursorsOn && root.controller.cursor1.cursorStyle === ScopeCursor.FullCrosshair
                                 && !isNaN(cursor1.value)
                        trace: cursor1.trace
                        value: cursor1.value
                        handle: PlotLine.NoHandle
                        draggable: false
                        color: cursor1.color
                        style: cursor1.style
                        width: root.controller.cursor1.lineWidth
                    }
                    PlotLine {
                        visible: root.controller.cursorsOn && root.controller.cursor2.cursorStyle === ScopeCursor.FullCrosshair
                                 && !isNaN(cursor2.value)
                        trace: cursor2.trace
                        value: cursor2.value
                        handle: PlotLine.NoHandle
                        draggable: false
                        color: cursor2.color
                        style: cursor2.style
                        width: root.controller.cursor2.lineWidth
                    }
                    // A wider cursor line is drawn over the (thin) one of the cursor itself.
                    PlotLine {
                        visible: cursor1.visible && root.controller.cursor1.lineWidth > 1
                        orientation: PlotElement.Vertical
                        value: cursor1.position
                        handle: PlotLine.NoHandle
                        draggable: false
                        color: cursor1.color
                        style: cursor1.style
                        width: root.controller.cursor1.lineWidth
                    }
                    PlotLine {
                        visible: cursor2.visible && root.controller.cursor2.lineWidth > 1
                        orientation: PlotElement.Vertical
                        value: cursor2.position
                        handle: PlotLine.NoHandle
                        draggable: false
                        color: cursor2.color
                        style: cursor2.style
                        width: root.controller.cursor2.lineWidth
                    }
                    // Marker at the cursor's point (Small Crosshair, or any Point Style).
                    PlotMarker {
                        visible: root.controller.cursorsOn && cursor1.visible && !isNaN(cursor1.value)
                                 && (root.controller.cursor1.pointStyle !== ScopeTraceStyle.NoPoints
                                     || root.controller.cursor1.cursorStyle === ScopeCursor.SmallCrosshair)
                        x: cursor1.position
                        trace: cursor1.trace
                        color: cursor1.color
                        shape: root.markerShape(root.controller.cursor1.pointStyle)
                        size: 8 + 2 * root.controller.cursor1.lineWidth
                    }
                    PlotMarker {
                        visible: root.controller.cursorsOn && cursor2.visible && !isNaN(cursor2.value)
                                 && (root.controller.cursor2.pointStyle !== ScopeTraceStyle.NoPoints
                                     || root.controller.cursor2.cursorStyle === ScopeCursor.SmallCrosshair)
                        x: cursor2.position
                        trace: cursor2.trace
                        color: cursor2.color
                        shape: root.markerShape(root.controller.cursor2.pointStyle)
                        size: 8 + 2 * root.controller.cursor2.lineWidth
                    }

                    // ---- Overlays ---------------------------------------------------------------
                    Row {
                        x: ytPlot.plotArea.x + 8
                        y: ytPlot.plotArea.y + ytPlot.plotArea.height - height - 8
                        spacing: 6
                        Repeater {
                            model: 2
                            delegate: ChannelChip {
                                id: chip
                                required property int index
                                readonly property ScopeChannel channel: index === 0 ? root.ch0 : root.ch1
                                compact: true
                                text: channel.name
                                color: root.channelColor(index)
                                checked: channel.enabled
                                detail: channel.enabled ? channel.scaleLabel + "/div" + (channel.audio ? "  AC" : "") : ""
                                toolTip: channel.sourceName + (channel.enabled ? "" : qsTr(" (off, click to enable)"))
                                onToggled: {
                                    if (!root.controller.setChannelEnabled(index, checked))
                                        checked = Qt.binding(() => chip.channel.enabled)
                                }
                            }
                        }
                        ChannelChip {
                            visible: root.mathShown
                            compact: true
                            checkable: false
                            text: qsTr("Math")
                            color: root.channelColor(2)
                            detail: root.math.functionName + "  " + root.math.scaleLabels[root.math.scaleIndex] + "/div"
                        }
                        ChannelChip {
                            visible: refATrace.visible
                            compact: true
                            checkable: false
                            text: "REF A"
                            color: Theme.ref
                            detail: root.controller.refALabel
                        }
                        ChannelChip {
                            visible: refBTrace.visible
                            compact: true
                            checkable: false
                            text: "REF B"
                            color: refBTrace.color
                            detail: root.controller.refBLabel
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: parent.width - ytPlot.plotArea.x - ytPlot.plotArea.width + 22
                        y: ytPlot.plotArea.y + ytPlot.plotArea.height - height - 8
                        spacing: 6
                        layoutDirection: Qt.RightToLeft
                        Badge {
                            visible: root.controller.rolling
                            text: qsTr("Roll")
                            color: Theme.accent
                        }
                        Badge {
                            visible: root.controller.autoscaling
                            text: qsTr("Autoscaling")
                            color: Theme.accent
                            iconName: "autoscale"
                        }
                        ActionButton {
                            visible: ytPlot.xAxis.zoomed
                            compact: true
                            tone: "subtle"
                            iconName: "zoom-fit"
                            text: qsTr("Reset zoom")
                            onClicked: ytPlot.resetView()
                        }
                    }
                }

                // ---- XY graph -------------------------------------------------------------------
                BenchPlot {
                    id: xyPlot
                    visible: root.xyMode
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.verticalStretchFactor: 3
                    graticule: Plot.Scope
                    colorScheme: ytPlot.colorScheme
                    backgroundColor: ytPlot.backgroundColor
                    placeholderText: root.ch0.enabled && root.ch1.enabled ? (root.controller.running ? "" : qsTr("Press Run to acquire"))
                                                                         : qsTr("XY mode needs CH 0 and CH 1 enabled")
                    xAxis { min: -5; max: 5; divisions: 10; labelsVisible: false }
                    yAxis { min: -5; max: 5; divisions: 10; labelsVisible: false }
                    ScopeTrace {
                        buffer: root.controller.xyTrace
                        visible: root.ch0.enabled && root.ch1.enabled
                        name: "CH 1 vs CH 0"
                        traceStyle: root.ch0.style
                        defaultColor: Theme.ch0
                        xScale: 1 / root.ch0.voltsPerDiv
                        xOffset: root.ch0.position
                        yScale: 1 / root.ch1.voltsPerDiv
                        yOffset: root.ch1.position
                        persistence: root.controller.persistence
                    }
                    Row {
                        x: xyPlot.plotArea.x + 8
                        y: xyPlot.plotArea.y + xyPlot.plotArea.height - height - 8
                        spacing: 6
                        ChannelChip { compact: true; checkable: false; text: "X  CH 0"; color: root.channelColor(0); detail: root.ch0.scaleLabel + "/div" }
                        ChannelChip { compact: true; checkable: false; text: "Y  CH 1"; color: root.channelColor(1); detail: root.ch1.scaleLabel + "/div" }
                    }
                }

                // ---- FFT of the math channel ----------------------------------------------------
                BenchPlot {
                    id: fftPlot
                    visible: root.fftShown
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.verticalStretchFactor: 2
                    colorScheme: ytPlot.colorScheme
                    backgroundColor: ytPlot.backgroundColor
                    placeholderText: root.controller.running ? "" : qsTr("FFT of the displayed record")
                    xAxis {
                        min: 0
                        max: Math.max(1, root.controller.fftMaxFrequency)
                        unit: "Hz"
                        unitInLabels: true
                        lowerLimit: 0
                    }
                    yAxis { min: -140; max: 20; unit: "dBV"; title: qsTr("Magnitude") }
                    PlotTrace {
                        id: fftTrace
                        buffer: root.controller.fftTrace
                        name: root.math.functionName
                        unit: "dBV"
                        color: root.channelColor(2)
                        width: 1.2
                        fill: true
                        fillOpacity: 0.18
                    }
                    PlotMarker {
                        visible: !isNaN(root.math.peakFrequency)
                        x: root.math.peakFrequency
                        y: root.math.peakLevel
                        shape: PlotMarker.TriangleDown
                        color: root.channelColor(2)
                        label: Units.format(root.math.peakFrequency, "Hz", 4) + "  " + root.math.peakLevel.toFixed(1) + " dBV"
                    }
                }
            }
        }

        ScopeReadouts {
            Layout.fillWidth: true
            controller: root.controller
            cursor1Value: cursor1.value
            cursor2Value: cursor2.value
            cursor1Position: cursor1.position
            cursor2Position: cursor2.position
            channelColors: [root.channelColor(0), root.channelColor(1), root.channelColor(2)]
        }
    }
}
