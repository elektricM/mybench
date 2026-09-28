import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.ToolTip {
    id: control

    x: parent ? (parent.width - implicitWidth) / 2 : 0
    y: -implicitHeight - 6
    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    margins: 8
    topPadding: 5
    bottomPadding: 5
    leftPadding: 9
    rightPadding: 9
    font.pixelSize: Theme.fontSizeM
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast } }

    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        color: Theme.dark ? Theme.text : "#F4F5F7"
    }

    background: Rectangle {
        radius: Theme.radiusS
        color: Theme.dark ? "#2A2D35" : "#22252C"
        border.width: Theme.dark ? 1 : 0
        border.color: "#383C46"
    }
}
