pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Digital I/O panel. SKELETON: layout and section titles of docs/instruments/dio.md (Digital
// Reader + Digital Writer) with placeholder content.
Item {
    id: root

    required property DioController controller

    InstrumentPanel {
        anchors.fill: parent
        controller: root.controller
        displayTitle: qsTr("Line States")
        displayInfo: qsTr("Numeric Value 0x%1").arg(("0" + root.controller.lineStates.toString(16).toUpperCase()).slice(-2))

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingL

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.spacingXl
                spacing: Theme.spacingXl
                Repeater {
                    model: 8
                    delegate: ColumnLayout {
                        id: lineColumn
                        required property int index
                        readonly property int line: 7 - index
                        spacing: Theme.spacingS
                        Led {
                            Layout.alignment: Qt.AlignHCenter
                            size: 26
                            on: ((root.controller.lineStates >> lineColumn.line) & 1) === 1
                            color: ((root.controller.outputMask >> lineColumn.line) & 1) === 1 ? Theme.ch1 : Theme.ch0
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: lineColumn.line
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Font.DemiBold
                        }
                        Badge {
                            Layout.alignment: Qt.AlignHCenter
                            text: ((root.controller.outputMask >> lineColumn.line) & 1) ? qsTr("Out") : qsTr("In")
                            color: ((root.controller.outputMask >> lineColumn.line) & 1) ? Theme.ch1 : Theme.ch0
                        }
                    }
                }
            }

            BenchPlot {
                Layout.fillWidth: true
                Layout.fillHeight: true
                placeholderText: root.controller.running ? "" : qsTr("Press Run to record the line states")
                xAxis { unit: "s"; title: qsTr("Time"); follow: true }
                yAxis { min: 0; max: 8; divisions: 8; minorTicks: 0; inverted: true; labelsVisible: false; interactive: false; minorGridVisible: false }
                Component.onCompleted: xAxis.setRange(0, 10)
                PlotLogicTrace {
                    buffer: root.controller.timeline
                    lanes: [0, 1, 2, 3, 4, 5, 6, 7]
                    laneNames: ["DIO 0", "DIO 1", "DIO 2", "DIO 3", "DIO 4", "DIO 5", "DIO 6", "DIO 7"]
                    laneColors: [0, 1, 2, 3, 4, 5, 6, 7].map(line => ((root.controller.outputMask >> line) & 1) ? Theme.ch1 : Theme.ch0)
                }
            }
        }

        inspector: [
            Section {
                title: qsTr("Configuration Settings")
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Direction per line")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS + 1
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: Theme.spacingS
                    rowSpacing: Theme.spacingS
                    Repeater {
                        model: 8
                        delegate: ChannelChip {
                            id: chip
                            required property int index
                            Layout.fillWidth: true
                            text: "DIO " + index
                            color: Theme.ch1
                            checked: ((root.controller.outputMask >> index) & 1) === 1
                            toolTip: checked ? qsTr("Output — click to make it an input") : qsTr("Input — click to make it an output")
                            onToggled: root.controller.setLineDirection(chip.index, checked)
                        }
                    }
                }
            },
            Section {
                title: qsTr("Manual Pattern")
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    rowSpacing: Theme.spacingS
                    columnSpacing: 6
                    Repeater {
                        model: 8
                        delegate: ColumnLayout {
                            id: switchColumn
                            required property int index
                            readonly property int line: 7 - index
                            Layout.fillWidth: true
                            spacing: 2
                            Switch {
                                Layout.alignment: Qt.AlignHCenter
                                enabled: ((root.controller.outputMask >> switchColumn.line) & 1) === 1
                                checked: ((root.controller.outputValue >> switchColumn.line) & 1) === 1
                                onToggled: root.controller.toggleLine(switchColumn.line)
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: switchColumn.line
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeS
                            }
                        }
                    }
                }
            },
            Section {
                title: qsTr("Pattern")
                collapsed: true
                Field { label: qsTr("Pattern"); ComboBox { Layout.fillWidth: true; model: [qsTr("Manual"), qsTr("Ramp (0-255)"), qsTr("Alternating 1/0s"), qsTr("Walking 1s")] } }
            },
            Section {
                title: qsTr("Action")
                collapsed: true
                RowLayout {
                    ActionButton { text: qsTr("Toggle"); compact: true }
                    ActionButton { text: qsTr("Rotate"); compact: true }
                    ActionButton { text: qsTr("Shift"); compact: true }
                }
            }
        ]
    }
}
