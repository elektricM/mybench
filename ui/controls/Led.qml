import QtQuick

// v1 compatibility (use StatusIndicator): a flat state dot with an optional label; no glow.
//     Led { on: controller.timedOut; color: Theme.caution; text: qsTr("Timeout") }
Item {
    id: root

    property bool on: false
    property color color: Theme.positive
    property real size: 8
    property string text
    property color textColor: on ? Theme.text : Theme.textTertiary
    property int fontSize: Theme.fontSizeSmall
    property bool glow: false           // v1, ignored
    property bool labelFirst: false

    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(size, label.visible ? label.implicitHeight : 0)
    Accessible.role: Accessible.Indicator
    Accessible.name: text
    Accessible.checked: on

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs + 2
        layoutDirection: root.labelFirst ? Qt.RightToLeft : Qt.LeftToRight
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: root.size
            height: root.size
            radius: width / 2
            color: root.on ? root.color : "transparent"
            border.width: root.on ? 0 : 1.5
            border.color: root.on ? root.color : Theme.alpha(Theme.text, 0.3)
        }
        Text {
            id: label
            visible: root.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.textColor
            font.pixelSize: root.fontSize
        }
    }
}
