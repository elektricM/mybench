import QtQuick

// v1 compatibility (deleted in the cleanup phase; state is shown with StatusIndicator, channels with
// ChannelBadge): a short label in a quiet hairline box, sentence case.
//     Badge { text: "×3"; color: Theme.textSecondary }
Item {
    id: root

    property string text
    property color color: Theme.textSecondary
    property bool solid: false          // v1, ignored (no filled badges)
    property bool caps: false           // v1, ignored (no capitals)
    property int fontSize: Theme.fontSizeCaption
    property string iconName

    implicitWidth: row.implicitWidth + 2 * Theme.spacingXs + 2
    implicitHeight: Math.max(16, label.implicitHeight + 2)

    Rectangle {
        anchors.fill: parent
        radius: 3
        color: "transparent"
        border.width: Theme.borderWidth
        border.color: Theme.separator
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingXs
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
            text: root.text
            color: root.color
            font.pixelSize: root.fontSize
        }
    }
}
