import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Oscilloscope: mini display + CH0 measurements. SKELETON.
Item {
    id: root

    required property ScopeController controller

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: height * 1.6
            radius: Theme.radiusM
            color: Theme.display
            border.width: 1
            border.color: Theme.displayBorder
            clip: true
            BenchPlot {
                anchors.fill: parent
                anchors.margins: 1
                graticule: Plot.Scope
                bare: true
                xAxis { min: 0; max: 10 * root.controller.timePerDiv; divisions: 10; labelsVisible: false }
                yAxis { min: -5; max: 5; divisions: 8; labelsVisible: false }
                PlotTrace { buffer: root.controller.ch0; visible: root.controller.ch0Enabled; color: Theme.ch0; width: 1.2; yScale: 1 / root.controller.voltsPerDiv0 }
                PlotTrace { buffer: root.controller.ch1; visible: root.controller.ch1Enabled; color: Theme.ch1; width: 1.2; yScale: 1 / root.controller.voltsPerDiv1 }
            }
        }
        Column {
            Layout.fillWidth: true
            spacing: 6
            Readout { width: parent.width; alignment: Qt.AlignLeft; label: "CH 0 RMS"; value: root.controller.ch0Rms; unit: "V"; digits: 3; fontSize: 24; color: Theme.ch0; stale: !root.controller.running }
            Readout { width: parent.width; alignment: Qt.AlignLeft; label: qsTr("Freq"); value: root.controller.ch0Frequency; unit: "Hz"; digits: 4; fontSize: 16; stale: !root.controller.running }
        }
    }
}
