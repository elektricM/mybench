import QtQuick

// Keyboard-focus ring (docs/UI.md §4): 2 px accent, 1 px outside the control, shown only for
// keyboard focus (`visualFocus`). Put it inside a control's background:
//     background: Rectangle { radius: Theme.radiusControl; FocusRing { shown: control.visualFocus } }
Rectangle {
    property bool shown: false
    property color ringColor: Theme.focusRing

    anchors.fill: parent
    anchors.margins: -(Theme.focusWidth + 1)
    radius: Theme.radiusControl + Theme.focusWidth + 1
    color: "transparent"
    border.width: Theme.focusWidth
    border.color: ringColor
    visible: shown
}
