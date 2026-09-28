import QtQuick
import QtQuick.Layouts
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Multimeter: the live reading and its recent trend. SKELETON.
Item {
    id: root

    required property DmmController controller

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs
        Readout {
            Layout.fillWidth: true
            alignment: Qt.AlignLeft
            label: root.controller.functionName
            value: root.controller.reading
            unit: root.controller.unit
            overload: root.controller.overload
            stale: !root.controller.running
            fontSize: root.height < 90 ? 26 : 34
        }
        BenchPlot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            bare: true
            xAxis { labelFormat: PlotAxis.Time; follow: true; labelsVisible: false; gridVisible: false }
            Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 30, Date.now() / 1000)
            yAxis { min: -root.controller.rangeMax; max: root.controller.rangeMax; labelsVisible: false; gridVisible: false }   // full scale
            PlotTrace { buffer: root.controller.trend; color: Theme.ch0; width: 1.2 }
        }
    }
}
