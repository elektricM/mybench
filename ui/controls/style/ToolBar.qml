import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Window toolbar: the window color, Theme.toolbarHeight, a hairline below.
T.ToolBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: 0
    leftPadding: Theme.spacingS
    rightPadding: Theme.spacingS

    background: Rectangle {
        implicitHeight: Theme.toolbarHeight
        color: Theme.window
        Rectangle {
            anchors.bottom: control.position === T.ToolBar.Footer ? undefined : parent.bottom
            anchors.top: control.position === T.ToolBar.Footer ? parent.top : undefined
            width: parent.width
            height: Theme.borderWidth
            color: Theme.separator
        }
    }
}
