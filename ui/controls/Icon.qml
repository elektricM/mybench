import QtQuick
import QtQuick.Controls.impl as Impl

// A monochrome icon from assets/icons, tinted with `color` (the artwork's alpha is kept).
//     Icon { name: "play"; color: Theme.run }
Item {
    id: root

    property string name
    property real size: Theme.iconSize
    property color color: Theme.textSecondary

    implicitWidth: size
    implicitHeight: size

    Impl.IconImage {
        anchors.fill: parent
        source: Theme.icon(root.name)
        sourceSize: Qt.size(root.size, root.size)
        fillMode: Image.PreserveAspectFit
        color: root.color
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    }
}
