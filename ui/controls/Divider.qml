import QtQuick

// 1 px hairline (Theme.separator); vertical when `vertical` (use inside Row/RowLayout).
Rectangle {
    property bool vertical: false
    property real length: 0     // 0: fill (set width/height yourself or use Layout.fill*)

    implicitWidth: vertical ? Theme.borderWidth : length
    implicitHeight: vertical ? length : Theme.borderWidth
    color: Theme.separator
}
