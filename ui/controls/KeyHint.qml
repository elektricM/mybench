import QtQuick
import MyBench.Framework

// Keycap hint ("⌘K", "Space"). Use "Ctrl+" in `keys`; it shows as ⌘ on macOS.
//     KeyHint { keys: "Ctrl+K" }
Rectangle {
    id: root

    property string keys
    readonly property string label: AppInfo.platform === "macos"
                                    ? keys.replace("Ctrl+", "⌘").replace("Shift+", "⇧").replace("Alt+", "⌥")
                                    : keys

    implicitWidth: text.implicitWidth + 10
    implicitHeight: 18
    radius: Theme.radiusXs + 1
    color: Theme.surfaceSunken
    border.width: 1
    border.color: Theme.borderStrong

    Text {
        id: text
        anchors.centerIn: parent
        text: root.label
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeS
        font.weight: Font.Medium
    }
}
