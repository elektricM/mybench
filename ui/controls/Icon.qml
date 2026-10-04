import QtQuick
import QtQuick.Controls.impl as Impl

// The one standalone icon wrapper: a monochrome icon of assets/icons (24 grid, 1.75 stroke)
// tinted with `color`. Controls use their public `icon.source` / `icon.color` instead.
//     Icon { name: "play"; color: Theme.textSecondary }
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
    }
}
