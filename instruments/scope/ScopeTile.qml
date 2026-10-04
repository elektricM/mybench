import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Oscilloscope (content only; the shell draws title, state and Run/Stop): a
// bare mini trace on the display surface and the frequency and peak-to-peak of the first channel
// with results. Neutral text; the channel colors only draw the traces.
Item {
    id: root

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeChannel shown: ch0.enabled || !ch1.enabled ? ch0 : ch1
    readonly property bool stale: !controller.running

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            color: Theme.display
            radius: Theme.radiusControl
            border.width: Theme.borderWidth
            border.color: Theme.separator
            clip: true

            BenchPlot {
                id: miniPlot
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                bare: true
                placeholderText: ""
                // A few periods of the shown channel (the full screen while no frequency is known).
                readonly property real periods: root.shown.frequency > 0 ? 4 / root.shown.frequency : Infinity
                xAxis { min: 0; max: Math.min(root.controller.screenTime, miniPlot.periods); labelsVisible: false; gridVisible: false }
                yAxis { min: -5; max: 5; labelsVisible: false; gridVisible: false }
                PlotTrace {
                    buffer: root.controller.ch1
                    visible: root.ch1.enabled && root.controller.displayMode === ScopeController.YT
                    color: Theme.signalCh1
                    width: 1.2
                    yScale: 1 / root.ch1.voltsPerDiv
                    yOffset: root.ch1.position
                }
                PlotTrace {
                    buffer: root.controller.ch0
                    visible: root.ch0.enabled && root.controller.displayMode === ScopeController.YT
                    color: Theme.signalCh0
                    width: 1.2
                    yScale: 1 / root.ch0.voltsPerDiv
                    yOffset: root.ch0.position
                }
                PlotTrace {
                    buffer: root.controller.xyTrace
                    visible: root.controller.displayMode === ScopeController.XY && root.ch0.enabled && root.ch1.enabled
                    color: Theme.signalCh0
                    width: 1.2
                    xScale: 1 / root.ch0.voltsPerDiv
                    xOffset: root.ch0.position
                    yScale: 1 / root.ch1.voltsPerDiv
                    yOffset: root.ch1.position
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            spacing: Theme.spacingXs

            Readout {
                Layout.fillWidth: true
                size: "m"
                alignment: Qt.AlignLeft
                label: qsTr("CH%1 frequency").arg(root.shown.index)
                valueText: root.shown.hasResults && root.shown.frequency > 0
                           ? Units.format(root.shown.frequency, "Hz", 4) : "—"
                stale: root.stale
            }
            Readout {
                Layout.fillWidth: true
                size: "s"
                alignment: Qt.AlignLeft
                label: qsTr("Peak-to-peak")
                valueText: root.shown.hasResults ? Units.format(root.shown.peakToPeak, "V", 3) : "—"
                stale: root.stale
            }
            Item { Layout.fillHeight: true }
        }
    }
}
