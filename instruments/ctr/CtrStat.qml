import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// One statistic under the Counter/Timer readout: caption above a tabular value.
ColumnLayout {
    id: root

    property string label
    property string value
    property color color: Theme.text

    spacing: 2

    Text {
        Layout.fillWidth: true
        text: root.label.toUpperCase()
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.DemiBold
        font.letterSpacing: 0.8
        horizontalAlignment: Text.AlignHCenter
    }
    Text {
        Layout.fillWidth: true
        text: root.value
        color: root.color
        font.pixelSize: Theme.fontSizeL
        font.features: Theme.tabularFigures
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
