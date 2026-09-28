import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: header
    required property GalleryTheme theme
    property string title
    property string subtitle
    spacing: 3
    Text {
        text: header.title
        color: header.theme.text
        font.pixelSize: 20
        font.weight: Font.DemiBold
    }
    Text {
        Layout.fillWidth: true
        text: header.subtitle
        color: header.theme.muted
        font.pixelSize: 13
        elide: Text.ElideRight
    }
}
