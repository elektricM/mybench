pragma ComponentBehavior: Bound
import QtQuick

// Placeholder for a display that is not implemented yet (instrument skeletons): a faint
// graticule with a centered caption. Replace it with the real Plot/Readout.
Item {
    id: root

    property string text
    property string detail
    property int divisionsX: 10
    property int divisionsY: 8
    property color gridColor: Theme.displayGrid

    Repeater {
        model: root.divisionsX + 1
        delegate: Rectangle {
            required property int index
            x: Math.round(index * (root.width - 1) / root.divisionsX)
            width: 1
            height: root.height
            color: root.gridColor
            opacity: index === root.divisionsX / 2 ? 1 : 0.6
        }
    }
    Repeater {
        model: root.divisionsY + 1
        delegate: Rectangle {
            required property int index
            y: Math.round(index * (root.height - 1) / root.divisionsY)
            height: 1
            width: root.width
            color: root.gridColor
            opacity: index === root.divisionsY / 2 ? 1 : 0.6
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 6
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.text
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeXl
            font.weight: Font.Medium
        }
        Text {
            visible: root.detail.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.detail
            color: Theme.textDisabled
            font.pixelSize: Theme.fontSizeM
        }
    }
}
