import QtQuick

// 1 px separator line; vertical when `vertical` (use inside Row/RowLayout).
Rectangle {
    property bool vertical: false
    property real length: 0     // 0: fill (set width/height yourself or use Layout.fill*)

    implicitWidth: vertical ? 1 : length
    implicitHeight: vertical ? length : 1
    color: Theme.divider
}
