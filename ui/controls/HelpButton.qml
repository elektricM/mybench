import QtQuick
import MyBench.Framework

// Opens the shell's help drawer on `topic` (docs/help/<topic>.md).
//     HelpButton { topic: "scope" }
IconButton {
    property string topic: "app"

    iconName: "help-circle"
    toolTip: qsTr("Help")
    shortcutText: "F1"
    onClicked: Help.show(topic)
}
