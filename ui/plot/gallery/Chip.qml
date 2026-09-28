// Settings chip: colored dot, label and value ("CH0  500 mV/div").
import QtQuick

Rectangle {
    id: chip
    required property GalleryTheme theme
    property color accent: theme.muted
    property string label
    property string value
    property bool dot: true
    implicitWidth: row.implicitWidth + 20
    implicitHeight: 28
    radius: 7
    color: theme.card
    border.color: theme.border
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 7
        Rectangle {
            visible: chip.dot
            width: 8
            height: 8
            radius: 4
            color: chip.accent
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: chip.label
            color: chip.dot ? chip.theme.text : chip.accent
            font.pixelSize: 12
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: chip.value.length > 0
            text: chip.value
            color: chip.theme.muted
            font.pixelSize: 12
            font.features: { "tnum": 1 }
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
