import QtQuick

// Small state dot; `pulse` animates a ring around it (running, waiting for trigger).
//     StatusDot { color: Theme.success; pulse: controller.running }
Item {
    id: root

    property color color: Theme.textMuted
    property bool pulse: false
    property real size: 8

    implicitWidth: size
    implicitHeight: size

    Rectangle {
        id: ring
        anchors.centerIn: parent
        width: root.size
        height: root.size
        radius: width / 2
        color: "transparent"
        border.width: 1.5
        border.color: root.color
        opacity: 0
        visible: root.pulse && Theme.animations

        SequentialAnimation on scale {
            running: ring.visible
            loops: Animation.Infinite
            NumberAnimation { from: 1; to: 2.6; duration: 1400; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 200 }
        }
        SequentialAnimation on opacity {
            running: ring.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0.7; to: 0; duration: 1400; easing.type: Easing.OutCubic }
            PauseAnimation { duration: 200 }
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.size
        height: root.size
        radius: width / 2
        color: root.color
        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
    }
}
