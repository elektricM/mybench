// Rounded panel hosting a plot.
import QtQuick

Rectangle {
    id: card
    required property GalleryTheme theme
    default property alias content: inner.data
    property real padding: 10
    radius: 10
    color: theme.card
    border.color: theme.border
    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: card.padding
    }
}
