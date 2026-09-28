import QtQuick
import QtQuick.Layouts

// Plain raised container with an optional title row (Home cards, dashboard groups). Children
// are stacked in a column (give them Layout.fillWidth: true).
//     Card { title: qsTr("Device"); ColumnLayout { ... } }
Rectangle {
    id: root

    property string title
    property string subtitle
    property string iconName
    property int padding: Theme.spacingL
    property bool hoverable: false
    readonly property bool hovered: hover.hovered
    default property alias content: body.data
    property alias headerTools: tools.data

    implicitWidth: Math.max(200, body.implicitWidth + 2 * padding)
    implicitHeight: (titleRow.visible ? titleRow.implicitHeight + Theme.spacingM : 0) + body.implicitHeight + 2 * padding
    radius: Theme.radiusXl
    color: Theme.surfaceRaised
    border.width: 1
    border.color: hoverable && hovered ? Theme.borderStrong : Theme.border
    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

    HoverHandler { id: hover; enabled: root.hoverable }

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
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
            }
            Text {
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
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
        y: root.padding + (titleRow.visible ? titleRow.implicitHeight + Theme.spacingM : 0)
        width: root.width - 2 * root.padding
        spacing: Theme.spacingS
    }
}
