import QtQuick
import MyBench.Controls

// How to wire a Bode measurement: AO 0 drives the circuit and AI 0, AI 1 reads the circuit's
// output. Shown in the Connections tool and, small, in a corner of the empty plot.
Item {
    id: wiring

    readonly property color lineColor: Theme.textSecondary

    implicitWidth: 300
    implicitHeight: 92

    component Chip: Rectangle {
        id: chip
        property alias text: chipText.text
        property color tone: Theme.text
        width: 56
        height: 24
        radius: Theme.radiusM
        color: Theme.alpha(tone, 0.14)
        border.width: 1
        border.color: tone
        Text {
            id: chipText
            anchors.centerIn: parent
            color: chip.tone
            font.pixelSize: Theme.fontSizeS
            font.weight: Font.DemiBold
        }
    }

    Chip { id: ao0Chip; x: 0; y: 12; text: qsTr("AO 0"); tone: Theme.signalColor("ao0") }
    Chip { id: ai0Chip; x: 0; y: 64; text: qsTr("AI 0"); tone: Theme.signalColor("ai0") }
    Rectangle {   // AO 0 → circuit
        x: ao0Chip.width; y: ao0Chip.y + ao0Chip.height / 2; width: dut.x - x; height: 1
        color: wiring.lineColor
    }
    Rectangle {   // tap down to AI 0
        x: ao0Chip.width + 24; y: ao0Chip.y + ao0Chip.height / 2; width: 1; height: ai0Chip.y + ai0Chip.height / 2 - y
        color: wiring.lineColor
    }
    Rectangle {
        x: ai0Chip.width; y: ai0Chip.y + ai0Chip.height / 2; width: 25; height: 1
        color: wiring.lineColor
    }
    Rectangle {
        x: ao0Chip.width + 22; y: ao0Chip.y + ao0Chip.height / 2 - 2; width: 5; height: 5; radius: 2.5
        color: wiring.lineColor
    }
    Rectangle {
        id: dut
        x: 122; y: 4
        width: 84; height: 40
        radius: Theme.radiusM
        color: "transparent"
        border.width: 1
        border.color: Theme.textSecondary
        Text {
            anchors.centerIn: parent
            text: qsTr("Circuit")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
        }
    }
    Rectangle {   // circuit → AI 1
        x: dut.x + dut.width; y: ao0Chip.y + ao0Chip.height / 2; width: ai1Chip.x - x; height: 1
        color: wiring.lineColor
    }
    Chip { id: ai1Chip; x: 244; y: 12; text: qsTr("AI 1"); tone: Theme.signalColor("ai1") }
}
