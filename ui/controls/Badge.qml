import QtQuick

// Small tinted label ("SIMULATED", "RUNNING", "AI", "×3").
//     Badge { text: qsTr("SIMULATED"); color: Theme.simulated }
Item {
    id: root

    property string text
    property color color: Theme.accent
    property bool solid: false
    property bool caps: true
    property int fontSize: Theme.fontSizeXs
    property string iconName

    implicitWidth: row.implicitWidth + 12
    implicitHeight: Math.max(16, label.implicitHeight + 4)

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusXs + 1
        color: root.solid ? root.color : Theme.tint(root.color)
        border.width: root.solid ? 0 : 1
        border.color: Theme.alpha(root.color, Theme.dark ? 0.35 : 0.3)
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 4
        Icon {
            visible: root.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            size: 10
            color: label.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: root.caps ? root.text.toUpperCase() : root.text
            color: root.solid ? Theme.onColor(root.color) : root.color
            font.pixelSize: root.fontSize
            font.weight: Font.DemiBold
            font.letterSpacing: root.caps ? 0.6 : 0
        }
    }
}
