import QtQuick
import QtQuick.Layouts
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Multimeter: function, the live reading with its unit, the range (AUTO),
// a continuity flash, and the recent trend. Updates while the DMM runs even when the panel is
// hidden.
Item {
    id: root

    required property DmmController controller

    readonly property var icons: ["dmm-vdc", "dmm-vac", "dmm-adc", "dmm-aac", "dmm-ohm", "dmm-diode-line", "dmm-continuity-line"]
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingXs + 2
            Icon {
                name: root.icons[root.controller.selectedFunction] ?? ""
                size: Theme.iconSizeS
                color: Theme.textSecondary
            }
            Text {
                Layout.fillWidth: true
                text: root.controller.functionName
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                elide: Text.ElideRight
            }
            Badge {
                text: root.controller.effectiveAutoRange ? qsTr("Auto %1").arg(root.controller.activeRangeLabel) : root.controller.selectedRangeLabel
                color: root.controller.effectiveAutoRange ? Theme.accent : Theme.textSecondary
                caps: false
            }
            Badge {
                visible: root.controller.hold
                text: qsTr("Hold")
                color: Theme.warning
            }
        }

        Row {
            spacing: 5
            Text {
                id: number
                text: root.controller.readingText
                color: root.controller.overload ? Theme.danger : Theme.ch1
                opacity: root.controller.running ? 1 : 0.6
                font.pixelSize: root.compact ? 26 : 34
                font.weight: Font.Light
                font.features: Theme.tabularFigures
            }
            Text {
                anchors.baseline: number.baseline
                text: root.controller.readingUnit
                color: Theme.textSecondary
                font.pixelSize: root.compact ? 12 : 15
            }
            Text {
                anchors.baseline: number.baseline
                visible: root.controller.nullOffset
                leftPadding: 4
                text: qsTr("REL")
                color: Theme.math
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
            }
        }

        BenchPlot {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.compact
            bare: true
            xAxis { labelFormat: PlotAxis.Time; follow: true; labelsVisible: false; gridVisible: false }
            yAxis { autoScale: true; minimumSpan: root.controller.trendMinimumSpan; labelsVisible: false; gridVisible: false }
            Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
            PlotTrace { buffer: root.controller.trend; color: Theme.ch1; width: 1.4 }
        }
    }
}
