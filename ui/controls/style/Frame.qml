import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.Frame {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: 12

    background: Rectangle {
        radius: Theme.radiusL
        color: "transparent"
        border.width: 1
        border.color: Theme.border
    }
}
