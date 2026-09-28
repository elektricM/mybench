// Offscreen self-test with a Window root: an ApplicationWindow with header and footer.
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    width: 480
    height: 300
    visible: true
    color: Application.styleHints.colorScheme === Qt.Light ? "#f4f5f7" : "#16181c"
    title: "window root"

    header: ToolBar {
        Label { anchors.centerIn: parent; text: "header — colorScheme " + Application.styleHints.colorScheme }
    }
    footer: Label { text: "footer"; padding: 6 }

    Rectangle {
        anchors.centerIn: parent
        width: 120; height: 60; radius: 6
        color: "#00b8d4"
        Label { anchors.centerIn: parent; text: "content" }
    }
}
