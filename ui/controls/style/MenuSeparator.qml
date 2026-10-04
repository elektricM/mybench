import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.MenuSeparator {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    horizontalPadding: Theme.spacingS
    verticalPadding: Theme.spacingXs

    contentItem: Rectangle {
        implicitWidth: 160
        implicitHeight: Theme.borderWidth
        color: Theme.separator
    }
}
