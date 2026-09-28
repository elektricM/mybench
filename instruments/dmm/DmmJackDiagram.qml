pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import MyBench.Controls

// Our drawing of the myDAQ's three DMM banana jacks (HI for V/Ω/diode/continuity, COM, HI for
// current) with the two jacks to use highlighted and a probe arrow on each. `currentMode`
// selects the A jack instead of the V/Ω jack.
Item {
    id: root

    property bool currentMode: false

    // Jack models: left = V Ω HI (red), middle = COM (black), right = A HI (red).
    readonly property var jacks: [
        { label: "HI", caption: "V Ω", lead: qsTr("red lead"), hi: true, used: !currentMode },
        { label: "COM", caption: "", lead: qsTr("black lead"), hi: false, used: true },
        { label: "HI", caption: "A", lead: qsTr("red lead"), hi: true, used: currentMode }
    ]

    implicitWidth: 280
    implicitHeight: 118
    Accessible.role: Accessible.Graphic
    Accessible.name: currentMode ? qsTr("Connect the probes to COM and the A HI jack")
                                 : qsTr("Connect the probes to the V Ω HI jack and COM")

    Rectangle {
        id: plate
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        width: Math.min(parent.width, 260)
        height: 86
        radius: Theme.radiusL
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.borderStrong
    }

    Repeater {
        model: root.jacks
        delegate: Item {
            id: jack
            required property var modelData
            required property int index
            readonly property real centerX: plate.x + plate.width * (0.2 + 0.3 * index)
            readonly property color ringColor: modelData.hi ? (Theme.dark ? "#E5484D" : "#D13438")
                                                            : (Theme.dark ? "#3A3F4A" : "#2A2E36")
            readonly property bool used: modelData.used

            // Caption above the jack (function symbols).
            Text {
                x: jack.centerX - width / 2
                y: plate.y + 5
                text: jack.modelData.caption
                color: jack.used ? Theme.textSecondary : Theme.textDisabled
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
            }

            // The jack: colored insulating ring, metal socket, dark hole.
            Rectangle {
                id: ring
                x: jack.centerX - width / 2
                y: plate.y + 26
                width: 34
                height: 34
                radius: width / 2
                color: jack.used ? jack.ringColor : Theme.alpha(jack.ringColor, 0.28)
                border.width: jack.used ? 2 : 1
                border.color: jack.used ? Qt.lighter(jack.ringColor, 1.35) : Theme.alpha(jack.ringColor, 0.45)
                Rectangle {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    radius: 9
                    color: jack.used ? "#B9BEC7" : Theme.alpha("#B9BEC7", 0.35)
                    Rectangle {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: "#0B0C0F"
                    }
                }
                // Soft halo on the jacks to use.
                Rectangle {
                    visible: jack.used
                    anchors.centerIn: parent
                    width: parent.width + 10
                    height: width
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.alpha(Theme.accent, 0.55)
                }
            }

            Text {
                x: jack.centerX - width / 2
                y: ring.y + ring.height + 5
                text: jack.modelData.label
                color: jack.used ? Theme.text : Theme.textDisabled
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Bold
                font.letterSpacing: 0.5
            }

            // Probe arrow pointing at the jack.
            Shape {
                visible: jack.used
                x: jack.centerX - 6
                y: 0
                width: 12
                height: plate.y
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: 6; startY: 2
                    PathLine { x: 6; y: plate.y - 8 }
                }
                ShapePath {
                    strokeColor: "transparent"
                    fillColor: Theme.accent
                    startX: 0; startY: plate.y - 10
                    PathLine { x: 12; y: plate.y - 10 }
                    PathLine { x: 6; y: plate.y - 1 }
                    PathLine { x: 0; y: plate.y - 10 }
                }
            }
        }
    }
}
