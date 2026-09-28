import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    padding: 4
    verticalPadding: 4

    contentItem: Rectangle {
        implicitWidth: 180
        implicitHeight: 1
        color: Theme.divider
    }
}
