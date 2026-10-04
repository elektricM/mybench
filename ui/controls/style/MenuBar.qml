import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// In-window menu bar (Windows, Linux; on macOS the Controls MenuBar is the native global menu and
// this is not drawn). A row of MenuBarItems on the window color with a hairline below.
//     menuBar: MenuBar { Menu { title: qsTr("&File"); Action { text: qsTr("&Open…") } } }
T.MenuBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: Theme.spacingXs
    rightPadding: Theme.spacingXs
    topPadding: 2
    bottomPadding: 2 + Theme.borderWidth
    spacing: 0
    font.pixelSize: Theme.fontSizeBody

    delegate: MenuBarItem {}

    contentItem: Row {
        spacing: control.spacing
        Repeater {
            model: control.contentModel
        }
    }

    background: Rectangle {
        implicitHeight: Theme.controlHeight + 4
        color: Theme.window
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.separator
        }
    }
}
