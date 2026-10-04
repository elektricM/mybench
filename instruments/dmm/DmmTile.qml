import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Multimeter (content only; the shell draws title, state and Run/Stop): a
// bare mini trend on the display surface and the reading under its function name. Neutral text;
// the whole tile flashes while continuity is closed. Updates while the DMM runs even when the
// panel is hidden.
Item {
    id: root

    required property DmmController controller

    readonly property bool compact: height < 100

    Component.onCompleted: root.controller.autoStartIfEnabled()

    // Continuity: the whole tile glows while the probes are shorted.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -4
        radius: Theme.radiusM
        color: Theme.tint(Theme.success)
        visible: root.controller.continuityClosed && root.controller.running
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM

        Rectangle {
            visible: !root.compact
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            color: Theme.display
            radius: Theme.radiusControl
            border.width: Theme.borderWidth
            border.color: Theme.separator
            clip: true

            BenchPlot {
                anchors.fill: parent
                anchors.margins: Theme.spacingXs
                bare: true
                placeholderText: ""
                xAxis { labelFormat: PlotAxis.Time; follow: true; labelsVisible: false; gridVisible: false }
                yAxis { autoScale: true; minimumSpan: root.controller.trendMinimumSpan; labelsVisible: false; gridVisible: false }
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                PlotTrace { buffer: root.controller.trend; color: Theme.signalCh0; width: 1.4 }
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
                label: root.controller.functionName + (root.controller.nullOffset ? " · " + qsTr("Null") : "")
                valueText: root.controller.readingText + " " + root.controller.readingUnit
                overload: root.controller.overload
                unit: root.controller.readingUnit
                hold: root.controller.hold
                stale: !root.controller.running
            }
            Item { Layout.fillHeight: true }
        }
    }
}
