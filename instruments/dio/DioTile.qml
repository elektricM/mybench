pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls
import MyBench.Plot

// Dashboard tile of the Digital I/O: eight mini LEDs (DIO 7 ... DIO 0) with an IN/OUT mark and
// the input and output values in hex. With the Manual pattern, clicking an OUT LED flips it.
Item {
    id: root

    required property DioController controller

    readonly property bool compact: height < 100

    ColumnLayout {
        anchors.fill: parent
        spacing: root.compact ? 2 : Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: 8
                delegate: Item {
                    id: cell
                    required property int index
                    readonly property int line: 7 - index
                    readonly property bool isOut: (root.controller.outputMask >> line) & 1
                    readonly property bool isIn: (root.controller.inputMask >> line) & 1
                    readonly property bool level: isOut ? (root.controller.writtenValue >= 0 && ((root.controller.writtenLevels >> line) & 1))
                                                        : isIn && ((root.controller.lineStates >> line) & 1)
                    Layout.fillWidth: true
                    implicitHeight: column.implicitHeight

                    Column {
                        id: column
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 2
                        Led {
                            anchors.horizontalCenter: parent.horizontalCenter
                            size: root.compact ? 12 : 16
                            on: cell.level
                            color: Theme.ch1
                            opacity: cell.isIn || cell.isOut ? 1 : 0.35
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: cell.line
                            color: cell.isOut ? Theme.math : cell.isIn ? Theme.ch1 : Theme.textDisabled
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.DemiBold
                        }
                        Text {
                            visible: !root.compact
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: cell.isOut ? qsTr("OUT") : cell.isIn ? qsTr("IN") : "–"
                            color: Theme.textMuted
                            font.pixelSize: 8
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: cell.isOut && root.controller.pattern === DioController.Manual && root.controller.running
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: root.controller.toggleLine(cell.line)
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            Repeater {
                model: [
                    { title: qsTr("In"), shown: root.controller.inputMask !== 0, color: Theme.ch1,
                      value: root.controller.hasReading ? root.controller.inputValue : -1 },
                    { title: qsTr("Out"), shown: root.controller.outputMask !== 0, color: Theme.math,
                      value: root.controller.writtenValue }
                ]
                delegate: Row {
                    id: valueRow
                    required property var modelData
                    visible: modelData.shown
                    spacing: 4
                    Text {
                        anchors.baseline: valueText.baseline
                        text: valueRow.modelData.title
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                    }
                    Text {
                        id: valueText
                        text: valueRow.modelData.value < 0 ? "—" : "x " + valueRow.modelData.value.toString(16).toUpperCase()
                        color: valueRow.modelData.value < 0 ? Theme.textMuted : valueRow.modelData.color
                        font.pixelSize: root.compact ? Theme.fontSizeL : 20
                        font.weight: Font.Light
                        font.features: Theme.tabularFigures
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: root.controller.outputMask !== 0 && root.controller.pattern !== DioController.Manual
                text: root.controller.patternNames[root.controller.pattern]
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                elide: Text.ElideRight
            }
        }
        // Mini timeline (last 10 s).
        BenchPlot {
            id: mini
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.compact
            bare: true
            xAxis { follow: root.controller.timelineEnd > 10; labelsVisible: false; gridVisible: false }
            yAxis { min: 0; max: 8; inverted: true; labelsVisible: false; gridVisible: false }
            Component.onCompleted: xAxis.setRange(Math.max(0, root.controller.timelineEnd - 10), Math.max(10, root.controller.timelineEnd))
            PlotLogicTrace {
                buffer: root.controller.timeline
                lanes: [7, 6, 5, 4, 3, 2, 1, 0]
                laneColors: [7, 6, 5, 4, 3, 2, 1, 0].map(l => (root.controller.outputMask >> l) & 1 ? Theme.math
                                                         : (root.controller.inputMask >> l) & 1 ? Theme.ch1 : Theme.textMuted)
                labelsVisible: false
                width: 1
            }
        }
        Item { Layout.fillHeight: true; visible: !mini.visible }
    }
}
