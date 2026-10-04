import QtQuick

// Keycap hint ("⌘K", "Space"). Use "Ctrl+" in `keys`; it shows as ⌘ on macOS (Theme.keys()).
//     KeyHint { keys: "Ctrl+K" }
Rectangle {
    id: root

    property string keys
    readonly property string label: Theme.keys(keys)

    implicitWidth: Math.max(implicitHeight, text.implicitWidth + 2 * Theme.spacingXs + 2)
    implicitHeight: Theme.fontSizeCaption + 7
    radius: 3
    color: "transparent"
    border.width: Theme.borderWidth
    border.color: Theme.controlBorder

    Text {
        id: text
        anchors.centerIn: parent
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeCaption
    }
}
