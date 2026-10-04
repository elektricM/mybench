pragma ComponentBehavior: Bound
import QtQuick
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Live output preview of one channel: what it generates (or would generate). Function: two
// periods at the current settings (the real samples when they are coarse); Arbitrary: one pass of
// gain x waveform at the Update Rate (clipped); DC: a strip chart of the level (ELVISmx DC Level).
// Frequency display: the spectrum in dBV (harmonics of a function, FFT of an arbitrary waveform).
Item {
    id: root

    required property GenController controller
    required property GenChannel channel
    readonly property color channelColor: channel.index === 0 ? Theme.signalAo0 : Theme.signalAo1
    readonly property bool frequencyView: controller.displayMode === GenController.Frequency
    readonly property bool dcChart: channel.mode === GenChannel.Dc && !frequencyView
    readonly property bool active: channel.state === GenChannel.Running || channel.state === GenChannel.Sweeping
    readonly property real historyEnd: {
        root.channel.history.count   // dependency
        return root.channel.history.count > 0 ? root.channel.history.xMaximum() : 0
    }

    BenchPlot {
        id: plot
        anchors.fill: parent
        visible: root.channel.mode !== GenChannel.Off
        hoverReadout: true
        placeholderText: root.channel.mode === GenChannel.Arbitrary ? qsTr("No waveform loaded") : ""
        xAxis {
            min: root.frequencyView ? 0 : root.dcChart ? Math.max(0, root.historyEnd - root.channel.historySpan) : 0
            max: root.frequencyView ? root.channel.spectrumSpan
                 : root.dcChart ? Math.max(root.channel.historySpan, root.historyEnd) : root.channel.previewSpan
            unit: root.frequencyView ? "Hz" : "s"
            title: root.frequencyView ? qsTr("Frequency") : qsTr("Time")
            unitInLabels: true
        }
        yAxis {
            min: root.frequencyView ? -140 : root.dcChart ? -root.channel.outputRange : root.channel.previewMin
            max: root.frequencyView ? 20 : root.dcChart ? root.channel.outputRange : root.channel.previewMax
            unit: root.frequencyView ? "dBV" : "V"
            title: root.frequencyView ? qsTr("Magnitude") : qsTr("Amplitude")
        }
        PlotTrace {
            visible: !root.frequencyView && !root.dcChart
            buffer: root.channel.preview
            name: root.channel.name
            unit: "V"
            color: root.active ? root.channelColor : Theme.alpha(root.channelColor, 0.45)
            width: 1.8
            step: root.channel.previewStep
        }
        PlotTrace {
            visible: root.dcChart
            buffer: root.channel.history
            name: root.channel.name
            unit: "V"
            color: root.channelColor
            width: 1.8
            step: true
        }
        PlotTrace {
            visible: root.frequencyView
            buffer: root.channel.spectrum
            name: root.channel.name
            unit: "dBV"
            color: root.channelColor
            width: 1.2
            fill: true
            fillOpacity: 0.18
        }
        PlotLine {
            visible: root.dcChart
            value: root.channel.dcLevel
            color: Theme.alpha(root.channelColor, 0.6)
            style: PlotElement.Dash
            handle: PlotLine.End
            label: Units.fixed(root.channel.dcLevel, "V", 2)
            draggable: true
            onMoved: v => root.channel.dcLevel = Math.round(v * 100) / 100
        }
    }

    Text {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: plot.plotArea.x + 8
        anchors.topMargin: plot.plotArea.y + 6
        visible: plot.visible
        text: root.channel.name + (root.dcChart ? qsTr(" · level history") : root.frequencyView ? qsTr(" · spectrum")
                                   : root.channel.previewStep && root.channel.mode === GenChannel.Function ? qsTr(" · output samples") : "")
        color: Theme.alpha(root.channelColor, 0.9)
        font.pixelSize: Theme.fontSizeS
        font.weight: Font.DemiBold
    }

    Rectangle {
        anchors.fill: parent
        visible: root.channel.mode === GenChannel.Off
        radius: Theme.radiusM
        color: "transparent"
        border.width: 1
        border.color: Theme.displayBorder
        Row {
            anchors.centerIn: parent
            spacing: Theme.spacingS
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 4
                color: "transparent"
                border.width: 1.5
                border.color: root.channelColor
            }
            Text {
                text: qsTr("%1 is off · 0 V").arg(root.channel.name)
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
            }
        }
    }
}
