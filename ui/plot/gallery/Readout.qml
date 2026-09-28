// Measurement readout: small label over a large tabular value.
import QtQuick

Rectangle {
    id: readout
    required property GalleryTheme theme
    property string label
    property string value
    property color accent: theme.text
    implicitWidth: Math.max(labelText.implicitWidth, valueText.implicitWidth) + 28
    implicitHeight: 54
    radius: 9
    color: theme.card
    border.color: theme.border
    Column {
        anchors.verticalCenter: parent.verticalCenter
        x: 14
        spacing: 2
        Text {
            id: labelText
            text: readout.label
            color: readout.theme.muted
            font.pixelSize: 11
            font.weight: Font.Medium
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.4
        }
        Text {
            id: valueText
            text: readout.value
            color: readout.accent
            font.pixelSize: 17
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
        }
    }
}
