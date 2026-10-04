import QtQuick

// State as a small dot and short text (docs/UI.md §1 "Calm color", §13.8): running/positive green,
// waiting/paused/caution amber, error/negative red, neutral grey, stopped an empty ring. Steady:
// no glow, no pulse.
//     StatusIndicator { status: "running"; text: qsTr("Running · 200 kS/s") }
//     StatusIndicator { status: controller.hasError ? "error" : "stopped"; dotOnly: true }
Item {
    id: root

    property string status: "neutral"   // running waiting paused error stopped positive caution negative neutral
    property string text
    property bool dotOnly: false
    property int fontSize: Theme.fontSizeSmall
    property color textColor: Theme.textSecondary
    readonly property color dotColor: status === "running" || status === "positive" ? Theme.positive
                                    : status === "waiting" || status === "paused" || status === "caution" ? Theme.caution
                                    : status === "error" || status === "negative" ? Theme.negative
                                    : Theme.textTertiary
    readonly property bool hollow: status === "stopped"

    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(dot.height, label.visible ? label.implicitHeight : 0)
    Accessible.role: Accessible.StaticText
    Accessible.name: text.length > 0 ? text : status

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs + 2
        Rectangle {
            id: dot
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: root.hollow ? "transparent" : root.dotColor
            border.width: root.hollow ? 1.5 : 0
            border.color: root.dotColor
        }
        Text {
            id: label
            visible: !root.dotOnly && root.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.textColor
            font.pixelSize: root.fontSize
            font.features: Theme.tabularFigures
        }
    }
}
