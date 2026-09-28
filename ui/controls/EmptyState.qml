import QtQuick
import QtQuick.Layouts

// Centered icon + title + explanation (+ optional action children).
//     EmptyState { iconName: "usb"; title: qsTr("No device"); text: qsTr("Connect a myDAQ.") }
ColumnLayout {
    id: root

    property string iconName
    property string title
    property string text
    default property alias actions: actionsRow.data

    spacing: Theme.spacingS

    Rectangle {
        visible: root.iconName.length > 0
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: Theme.spacingXs
        implicitWidth: 44
        implicitHeight: 44
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.border
        Icon {
            anchors.centerIn: parent
            name: root.iconName
            size: Theme.iconSizeL
            color: Theme.textSecondary
        }
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.title
        color: Theme.text
        font.pixelSize: Theme.fontSizeL
        font.weight: Font.DemiBold
    }
    Text {
        visible: root.text.length > 0
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 360
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeM
        wrapMode: Text.WordWrap
    }
    Row {
        id: actionsRow
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: Theme.spacingS
        spacing: Theme.spacingS
    }
}
