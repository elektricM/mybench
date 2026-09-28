import QtQuick
import QtQuick.Controls
import MyBench.Controls

// The Multimeter's big number: the formatted reading ("1.235", "+Over", "OPEN", "---") with its
// unit, tabular figures so digits do not jump, dimmed while idle or held, and an optional
// secondary status on the right (Continuity "OPEN").
Item {
    id: root

    property string text: "---"
    property string unit
    property color color: Theme.ch1
    property bool stale: false
    property int fontSize: 96
    property string secondaryText

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    Accessible.role: Accessible.StaticText
    Accessible.name: root.text + " " + root.unit

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.text: qsTr("Measurement Display")

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Math.round(root.fontSize * 0.12)

        Text {
            id: number
            text: root.text
            color: root.color
            opacity: root.stale ? 0.6 : 1
            font.pixelSize: root.fontSize
            font.weight: Font.Light
            font.features: Theme.tabularFigures
            Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        }
        Text {
            visible: root.unit.length > 0
            anchors.baseline: number.baseline
            text: root.unit
            color: Theme.alpha(root.color, 0.8)
            opacity: root.stale ? 0.6 : 1
            font.pixelSize: Math.round(root.fontSize * 0.36)
        }
        Text {
            visible: root.secondaryText.length > 0
            anchors.baseline: number.baseline
            leftPadding: Math.round(root.fontSize * 0.2)
            text: root.secondaryText
            color: Theme.textSecondary
            font.pixelSize: Math.round(root.fontSize * 0.3)
            font.weight: Font.DemiBold
        }
    }
}
