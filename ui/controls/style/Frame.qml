import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// A hairline-bordered area (lists and previews inside dialogs; never a card in a pane).
T.Frame {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    padding: Theme.spacingM

    background: Rectangle {
        radius: Theme.radiusControl
        color: "transparent"
        border.width: Theme.borderWidth
        border.color: Theme.separator
    }
}
