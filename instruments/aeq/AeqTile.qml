import QtQuick
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Audio Equalizer: band gains, output meters and the tone response.
Item {
    id: root

    required property AeqController controller

    function signedDb(v: real): string {
        return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v).toFixed(0)
    }
    function meter(i: int): var {
        const m = root.controller.meters
        return i < m.length ? m[i] : { peak: -120, rms: -120, hold: -120, active: false }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingM

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingXs
            Text {
                text: root.controller.bypass ? qsTr("Bypass")
                                             : qsTr("B %1 · M %2 · T %3 dB").arg(root.signedDb(root.controller.bass))
                                                   .arg(root.signedDb(root.controller.midtone)).arg(root.signedDb(root.controller.treble))
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
                font.features: Theme.tabularFigures
            }
            Text {
                text: qsTr("Volume %1 %").arg(Math.round(root.controller.volume * 100))
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusM
                color: Theme.display
                border.width: 1
                border.color: Theme.displayBorder
                clip: true
                BenchPlot {
                    anchors.fill: parent
                    anchors.margins: 1
                    bare: true
                    xAxis { scale: PlotAxis.Log; min: 20; max: 20000; gridVisible: false }
                    yAxis { min: -15; max: 15; gridVisible: false }
                    PlotTrace { buffer: root.controller.response; color: Theme.accent; width: 1.5 }
                }
            }
        }
        AeqMeter { Layout.fillHeight: true; label: "L"; peak: root.meter(2).peak; rms: root.meter(2).rms; hold: root.meter(2).hold; active: root.meter(2).active }
        AeqMeter { Layout.fillHeight: true; label: "R"; peak: root.meter(3).peak; rms: root.meter(3).rms; hold: root.meter(3).hold; active: root.meter(3).active }
    }
}
