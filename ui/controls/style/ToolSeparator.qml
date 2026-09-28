import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.ToolSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: vertical ? 6 : 2
    verticalPadding: vertical ? 2 : 6

    contentItem: Rectangle {
        implicitWidth: control.vertical ? 1 : 20
        implicitHeight: control.vertical ? 20 : 1
        color: Theme.divider
    }
}
