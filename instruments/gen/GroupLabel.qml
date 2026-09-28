import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// Small group caption inside an inspector card, with a hairline (the ELVISmx group names:
// "Waveform Settings", "Sweep Settings"...).
RowLayout {
    id: root

    property string text

    Layout.fillWidth: true
    Layout.topMargin: Theme.spacingXs
    spacing: Theme.spacingS

    Text {
        text: root.text.toUpperCase()
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: Theme.divider
    }
}
