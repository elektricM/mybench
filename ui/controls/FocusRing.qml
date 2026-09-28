import QtQuick

// Keyboard-focus outline around its parent. Put it inside a control's background:
//     background: Rectangle { radius: 6; FocusRing { shown: control.visualFocus; radius: 9 } }
Rectangle {
    property bool shown: false

    anchors.fill: parent
    anchors.margins: -3
    radius: Theme.radiusM + 3
    color: "transparent"
    border.width: Theme.focusWidth
    border.color: Theme.focusRing
    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
}
