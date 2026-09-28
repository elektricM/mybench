import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.ToolBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: 6

    background: Rectangle {
        implicitHeight: Theme.topBarHeight
        color: Theme.chrome
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.divider
        }
    }
}
