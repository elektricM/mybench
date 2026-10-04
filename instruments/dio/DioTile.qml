pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls
import MyBench.Plot

// Overview tile of the Digital I/O (content only; the shell draws title, state and Run/Stop): eight
// mini LEDs (DIO 7 ... DIO 0; outputs in the accent color, like the panel), the input and output
// values and a bare mini timeline. With the Manual pattern, clicking an OUT LED flips it.
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
                    readonly property bool level: isOut ? (root.controller.hasWritten && ((root.controller.writtenLevels >> line) & 1))
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
                            color: cell.isOut ? Theme.accent : Theme.text
                            opacity: cell.isIn || cell.isOut ? 1 : 0.35
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: cell.line
                            color: cell.isOut ? Theme.accent : cell.isIn ? Theme.textSecondary : Theme.textDisabled
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.DemiBold
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
                    { title: qsTr("In"), shown: root.controller.inputMask !== 0, color: Theme.text,
                      output: false },
                    { title: qsTr("Out"), shown: root.controller.outputMask !== 0, color: Theme.accent,
                      output: true }
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
                        text: {
                            void root.controller.inputValue   // refresh on every new value
                            void root.controller.writtenValue
                            void root.controller.hasReading
                            const radix = valueRow.modelData.output ? root.controller.outputRadix : root.controller.inputRadix
                            return root.controller.radixLetter(radix) + " " + root.controller.numericValueText(valueRow.modelData.output)
                        }
                        color: valueRow.modelData.color
                        font.pixelSize: root.compact ? Theme.fontSizeL : 20
                        font.weight: Font.Light
                        font.features: Theme.tabularFigures
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
        // Mini timeline (last 10 s) on the display surface.
        Rectangle {
            id: miniFrame
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.compact
            color: Theme.display
            radius: Theme.radiusControl
            border.width: Theme.borderWidth
            border.color: Theme.separator
            clip: true
        BenchPlot {
            id: mini
            anchors.fill: parent
            anchors.margins: Theme.spacingXs
            bare: true
            placeholderText: ""
            xAxis { follow: root.controller.timelineEnd > 10; labelsVisible: false; gridVisible: false }
            yAxis { min: 0; max: 8; inverted: true; labelsVisible: false; gridVisible: false }
            Component.onCompleted: xAxis.setRange(Math.max(0, root.controller.timelineEnd - 10), Math.max(10, root.controller.timelineEnd))
            PlotLogicTrace {
                buffer: root.controller.timeline
                lanes: [7, 6, 5, 4, 3, 2, 1, 0]
                laneColors: [7, 6, 5, 4, 3, 2, 1, 0].map(l => (root.controller.outputMask >> l) & 1 ? Theme.accent
                                                         : (root.controller.inputMask >> l) & 1 ? Theme.text : Theme.textMuted)
                labelsVisible: false
                width: 1
            }
        }
        }
        Item { Layout.fillHeight: true; visible: !miniFrame.visible }
    }
}
