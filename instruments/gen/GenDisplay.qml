pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// The generator display (round 5): ONE plot with both outputs in their channel colors, as on a
// bench generator. Time: both channels on one time axis (the controller gives them a shared
// window, two periods of the slowest function or one pass of the longest waveform); DC levels are
// flat lines with a draggable level handle, or a 60 s strip chart when every used output is DC.
// Frequency: both spectra (dBV). A channel whose output is off is drawn faded; an Off channel
// is not drawn. A bar above the plot holds the legend and the Time | Frequency choice.
ColumnLayout {
    id: root

    required property GenController controller
    readonly property var channels: [controller.channel0, controller.channel1]
    readonly property bool frequencyView: controller.displayMode === GenController.Frequency
    readonly property var shown: channels.filter(c => c.mode !== GenChannel.Off
                                                   && !(c.mode === GenChannel.Arbitrary && !c.hasWaveform))
    // The strip chart only when every output in use is a DC level (other waveforms need the time
    // window of their periods).
    readonly property bool dcChart: !frequencyView && shown.length > 0 && shown.every(c => c.mode === GenChannel.Dc)
    readonly property real historyEnd: {
        let end = 0
        for (const c of channels) {
            c.history.count   // dependency
            if (c.history.count > 0)
                end = Math.max(end, c.history.xMaximum())
        }
        return end
    }
    readonly property alias plot: genPlot

    function channelColor(index: int): color { return index === 0 ? Theme.signalAo0 : Theme.signalAo1 }
    function active(c: GenChannel): bool { return c.state === GenChannel.Running || c.state === GenChannel.Sweeping }

    spacing: Theme.spacingXs

    // --- Legend and view --------------------------------------------------------------------------
    RowLayout {
        objectName: "genDisplayBar"
        Layout.fillWidth: true
        Layout.leftMargin: genPlot.plotArea.x
        spacing: Theme.spacingM

        Repeater {
            model: root.channels
            delegate: Row {
                id: entry
                required property GenChannel modelData
                required property int index
                readonly property bool off: modelData.mode === GenChannel.Off
                readonly property color tone: root.channelColor(index)
                objectName: "previewLegend" + index
                spacing: 6
                opacity: off ? 0.6 : 1
                // Filled swatch: output on; hollow: output off.
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 14
                    height: 4
                    radius: 2
                    color: entry.modelData.outputOn ? entry.tone : "transparent"
                    border.width: entry.modelData.outputOn ? 0 : 1
                    border.color: entry.tone
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.modelData.name
                    color: entry.off ? Theme.textSecondary : entry.tone
                    font.pixelSize: Theme.fontSizeS
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text.length > 0
                    text: entry.off ? qsTr("Off")
                          : entry.modelData.mode === GenChannel.Arbitrary && !entry.modelData.hasWaveform ? qsTr("no waveform")
                          : ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS
                }
            }
        }
        Item { Layout.fillWidth: true }
        SegmentedControl {
            objectName: "previewMode"
            compact: true
            model: [ { text: qsTr("Time"), toolTip: qsTr("Amplitude (V) against time") },
                     { text: qsTr("Frequency"), toolTip: qsTr("Spectrum of each output (dBV)") } ]
            currentIndex: root.controller.displayMode
            onActivated: index => root.controller.displayMode = index
        }
    }

    // --- The plot ---------------------------------------------------------------------------------
    BenchPlot {
        id: genPlot
        objectName: "genPlot"
        Layout.fillWidth: true
        Layout.fillHeight: true
        hoverReadout: true
        placeholderText: root.shown.length === 0 ? qsTr("No output selected") : ""
        xAxis {
            min: root.frequencyView ? 0 : root.dcChart ? Math.max(0, root.historyEnd - root.controller.channel0.historySpan) : 0
            max: root.frequencyView ? Math.max(10, ...root.shown.map(c => c.spectrumSpan))
                 : root.dcChart ? Math.max(root.controller.channel0.historySpan, root.historyEnd)
                 : Math.max(root.controller.channel0.previewSpan, root.controller.channel1.previewSpan)
            unit: root.frequencyView ? "Hz" : "s"
            title: root.frequencyView ? qsTr("Frequency") : qsTr("Time")
            unitInLabels: true
        }
        yAxis {
            min: root.frequencyView ? -140
                 : root.dcChart ? -Math.max(...root.shown.map(c => c.outputRange))
                 : root.shown.length > 0 ? Math.min(...root.shown.map(c => c.previewMin)) : -1
            max: root.frequencyView ? 20
                 : root.dcChart ? Math.max(...root.shown.map(c => c.outputRange))
                 : root.shown.length > 0 ? Math.max(...root.shown.map(c => c.previewMax)) : 1
            unit: root.frequencyView ? "dBV" : "V"
            title: root.frequencyView ? qsTr("Magnitude") : qsTr("Amplitude")
        }

        // Time: what each output generates (or would), faded while its output is off.
        Instantiator {
            model: root.channels
            delegate: PlotTrace {
                required property GenChannel modelData
                required property int index
                objectName: "genTrace" + index
                plot: genPlot
                visible: !root.frequencyView && !root.dcChart && root.shown.indexOf(modelData) >= 0
                buffer: modelData.preview
                name: modelData.name
                unit: "V"
                color: root.active(modelData) ? root.channelColor(index) : Theme.alpha(root.channelColor(index), 0.45)
                width: 1.8
                step: modelData.previewStep
            }
        }
        // DC strip chart (every output in use is DC).
        Instantiator {
            model: root.channels
            delegate: PlotTrace {
                required property GenChannel modelData
                required property int index
                plot: genPlot
                visible: root.dcChart && modelData.mode === GenChannel.Dc
                buffer: modelData.history
                name: modelData.name
                unit: "V"
                color: root.channelColor(index)
                width: 1.8
                step: true
            }
        }
        // Frequency: the spectra.
        Instantiator {
            model: root.channels
            delegate: PlotTrace {
                required property GenChannel modelData
                required property int index
                plot: genPlot
                visible: root.frequencyView && root.shown.indexOf(modelData) >= 0
                buffer: modelData.spectrum
                name: modelData.name
                unit: "dBV"
                color: root.channelColor(index)
                width: 1.2
                fill: true
                fillOpacity: 0.14
            }
        }
        // DC level handles: drag to set the level.
        Instantiator {
            model: root.channels
            delegate: PlotLine {
                required property GenChannel modelData
                required property int index
                plot: genPlot
                visible: !root.frequencyView && modelData.mode === GenChannel.Dc
                value: modelData.dcLevel
                color: Theme.alpha(root.channelColor(index), 0.6)
                style: PlotElement.Dash
                handle: PlotLine.End
                label: Units.fixed(modelData.dcLevel, "V", 2)
                draggable: true
                onMoved: v => modelData.dcLevel = Math.round(v * 100) / 100
            }
        }
    }
}
