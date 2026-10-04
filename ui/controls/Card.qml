import QtQuick
import QtQuick.Layouts

// v1 compatibility (no cards in panes): a flat titled group — heading, optional subtitle and tools,
// then the content in a column (give children Layout.fillWidth: true).
//     Card { title: qsTr("Device"); ColumnLayout { ... } }
Item {
    id: root

    property string title
    property string subtitle
    property string iconName
    property int padding: 0
    property bool hoverable: false      // v1, ignored
    readonly property bool hovered: false
    property color color: "transparent" // v1, ignored
    property real radius: 0             // v1, ignored
    default property alias content: body.data
    property alias headerTools: tools.data

    implicitWidth: Math.max(titleRow.visible ? titleRow.implicitWidth : 0, body.implicitWidth) + 2 * padding
    implicitHeight: (titleRow.visible ? titleRow.implicitHeight + Theme.spacingS : 0) + body.implicitHeight + 2 * padding

    RowLayout {
        id: titleRow
        visible: root.title.length > 0
        x: root.padding
        y: root.padding
        width: root.width - 2 * root.padding
        spacing: Theme.spacingS
        Icon {
            visible: root.iconName.length > 0
            name: root.iconName
            color: Theme.textSecondary
        }
        Column {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeHeading
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
        }
        Row {
            id: tools
            spacing: Theme.spacingXs
        }
    }

    ColumnLayout {
        id: body
        x: root.padding
        y: root.padding + (titleRow.visible ? titleRow.implicitHeight + Theme.spacingS : 0)
        width: root.width - 2 * root.padding
        spacing: Theme.spacingS
    }
}
