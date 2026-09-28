pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// Dashboard tile of the Digital I/O: the 8 line LEDs. SKELETON.
Item {
    id: root

    required property DioController controller

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS
        RowLayout {
            spacing: 10
            Repeater {
                model: 8
                delegate: Column {
                    id: cell
                    required property int index
                    readonly property int line: 7 - index
                    spacing: 4
                    Led {
                        anchors.horizontalCenter: parent.horizontalCenter
                        size: 16
                        on: ((root.controller.lineStates >> cell.line) & 1) === 1
                        color: ((root.controller.outputMask >> cell.line) & 1) === 1 ? Theme.ch1 : Theme.ch0
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.line
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                    }
                }
            }
        }
        Text {
            text: "0x" + ("0" + root.controller.lineStates.toString(16).toUpperCase()).slice(-2)
                  + "   " + qsTr("outputs %1").arg(("0000000" + root.controller.outputMask.toString(2)).slice(-8))
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeM
            font.family: Theme.monoFamily
        }
        Item { Layout.fillHeight: true }
    }
}
