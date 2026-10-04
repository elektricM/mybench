import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.ToolSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: vertical ? Theme.spacingXs : 2
    verticalPadding: vertical ? 2 : Theme.spacingXs

    contentItem: Rectangle {
        implicitWidth: control.vertical ? Theme.borderWidth : 16
        implicitHeight: control.vertical ? 16 : Theme.borderWidth
        color: Theme.separator
    }
}
