import QtQuick

// v1 compatibility (use StatusIndicator): a steady state dot (`pulse` is ignored: no pulsing).
//     StatusDot { color: Theme.positive }
Rectangle {
    property bool pulse: false
    property real size: 8

    implicitWidth: size
    implicitHeight: size
    radius: width / 2
    color: Theme.textTertiary
}
