pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Measurement rows under the scope graph (ELVISmx: "CH0 Meas:  RMS  Freq  Vp-p", "CH1 Meas:",
// the cursor line "C1  C2  dT") with the Timeout LED and the sample rate on the right.
Item {
    id: root

    required property ScopeController controller
    property real cursor1Value: NaN
    property real cursor2Value: NaN
    property real cursor1Position: NaN
    property real cursor2Position: NaN
    property var channelColors: [Theme.ch0, Theme.ch1, Theme.math]

    readonly property ScopeMath math: controller.math
    readonly property real deltaT: cursor2Position - cursor1Position
    // ELVISmx reads 0.00 V for a cursor linked to a disabled channel or before any record.
    function cursorVolts(v: real): real {
        return isNaN(v) ? 0 : v
    }

    implicitHeight: layout.implicitHeight

    component Reading: Row {
        id: cell
        property string label
        property string value
        property color valueColor: Theme.text
        spacing: 5
        width: Math.max(implicitWidth, 128)
        Text {
            anchors.baseline: valueText.baseline
            text: cell.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
        }
        Text {
            id: valueText
            text: cell.value
            color: cell.valueColor
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.Medium
            font.features: Theme.tabularFigures
        }
    }
    component RowTitle: Text {
        Layout.preferredWidth: 74
        Layout.alignment: Qt.AlignTop
        topPadding: 1
        font.pixelSize: Theme.fontSizeS
        font.weight: Font.DemiBold
        font.letterSpacing: 0.3
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: Theme.spacingM

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 4

            RowLayout {
                visible: root.controller.cursorsOn
                Layout.fillWidth: true
                spacing: Theme.spacingS
                RowTitle {
                    text: qsTr("Cursors")
                    color: Theme.text
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Reading {
                        visible: root.controller.cursor1.showValue
                        label: "C1"
                        value: root.controller.formatMeasurement(root.cursorVolts(root.cursor1Value), root.controller.cursor1.channel === 2 ? root.math.unit : "V")
                        valueColor: root.controller.cursor1.color.length > 0 ? root.controller.cursor1.color : Theme.cursor1
                    }
                    Reading {
                        visible: root.controller.cursor2.showValue
                        label: "C2"
                        value: root.controller.formatMeasurement(root.cursorVolts(root.cursor2Value), root.controller.cursor2.channel === 2 ? root.math.unit : "V")
                        valueColor: root.controller.cursor2.color.length > 0 ? root.controller.cursor2.color : Theme.cursor2
                    }
                    Reading {
                        visible: root.controller.showDeltaTime
                        label: "dT"
                        value: root.controller.formatMeasurement(root.deltaT, "s")
                    }
                    Reading {
                        visible: root.controller.showDeltaTime
                        label: "1/dT"
                        value: root.controller.formatMeasurement(Math.abs(root.deltaT) > 0 ? 1 / Math.abs(root.deltaT) : NaN, "Hz")
                    }
                    Reading {
                        visible: root.controller.cursor1.showValue && root.controller.cursor2.showValue
                        label: "ΔV"
                        value: root.controller.formatMeasurement(root.cursorVolts(root.cursor2Value) - root.cursorVolts(root.cursor1Value), "V")
                    }
                }
            }
            Repeater {
                model: 2
                delegate: RowLayout {
                    id: channelRow
                    required property int index
                    readonly property ScopeChannel channel: index === 0 ? root.controller.channel0 : root.controller.channel1
                    visible: channel.enabled && channel.displayMeasurements && channel.readouts.length > 0
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    RowTitle {
                        text: qsTr("CH%1 Meas").arg(channelRow.index)
                        color: root.channelColors[channelRow.index]
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: channelRow.channel.readouts
                            delegate: Reading {
                                required property var modelData
                                label: modelData.label
                                value: modelData.text
                                valueColor: root.channelColors[channelRow.index]
                            }
                        }
                    }
                }
            }
            RowLayout {
                visible: root.math.enabled
                Layout.fillWidth: true
                spacing: Theme.spacingS
                RowTitle {
                    text: qsTr("Math")
                    color: root.channelColors[2]
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Reading { label: qsTr("Fn"); value: root.math.functionName; valueColor: root.channelColors[2] }
                    Reading {
                        visible: !root.math.fft
                        label: qsTr("RMS")
                        value: root.controller.formatMeasurement(root.math.rms, root.math.unit)
                        valueColor: root.channelColors[2]
                    }
                    Reading {
                        visible: !root.math.fft
                        label: qsTr("p-p")
                        value: root.controller.formatMeasurement(root.math.peakToPeak, root.math.unit)
                        valueColor: root.channelColors[2]
                    }
                    Reading {
                        visible: root.math.fft
                        label: qsTr("Peak")
                        value: root.controller.formatMeasurement(root.math.peakFrequency, "Hz")
                        valueColor: root.channelColors[2]
                    }
                    Reading {
                        visible: root.math.fft
                        label: qsTr("Level")
                        value: isNaN(root.math.peakLevel) ? "?" : root.math.peakLevel.toFixed(2) + " dBV"
                        valueColor: root.channelColors[2]
                    }
                }
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignTop | Qt.AlignRight
            spacing: 4
            Led {
                Layout.alignment: Qt.AlignRight
                on: root.controller.timedOut
                color: Theme.success
                text: qsTr("Timeout")
                labelFirst: true
                ToolTip.visible: timeoutHover.hovered
                ToolTip.text: root.controller.timedOut ? qsTr("No trigger edge in the last acquisition") : qsTr("Timeout: off")
                HoverHandler { id: timeoutHover }
            }
        }
    }
}
