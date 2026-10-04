import QtQuick
import QtQuick.Layouts

// Centered empty-state message (a display before the first run, an empty list): a plain icon,
// a short title, an explanation and optional action children. No decorative icon box.
//     EmptyState { iconName: "play"; title: qsTr("Press Run or Space to start") }
ColumnLayout {
    id: root

    property string iconName
    property string title
    property string text
    property color textColor: Theme.textSecondary
    default property alias actions: actionsRow.data

    spacing: Theme.spacingXs

    Icon {
        visible: root.iconName.length > 0
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: Theme.spacingXs
        name: root.iconName
        size: Theme.iconSizeL
        color: Theme.textTertiary
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.title
        color: root.textColor
        font.pixelSize: Theme.fontSizeBody
        font.weight: Font.Medium
    }
    Text {
        visible: root.text.length > 0
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 360
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }
    Row {
        id: actionsRow
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.spacingS
        spacing: Theme.spacingS
    }
}
