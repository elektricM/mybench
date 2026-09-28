import QtQuick
import QtQuick.Effects

// Indicator lamp with a soft glow when lit, and an optional label.
//     Led { on: controller.timedOut; color: Theme.warning; text: qsTr("Timeout") }
Item {
    id: root

    property bool on: false
    property color color: Theme.success
    property real size: 10
    property string text
    property color textColor: on ? Theme.text : Theme.textMuted
    property int fontSize: Theme.fontSizeM
    property bool glow: true
    property bool labelFirst: false

    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(size, label.visible ? label.implicitHeight : 0)
    Accessible.role: Accessible.Indicator
    Accessible.name: text
    Accessible.checked: on

    Row {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7
        layoutDirection: root.labelFirst ? Qt.RightToLeft : Qt.LeftToRight

        Item {
            width: root.size
            height: root.size
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                id: lamp
                anchors.fill: parent
                radius: width / 2
                color: root.on ? root.color : Theme.alpha(root.color, Theme.dark ? 0.14 : 0.16)
                border.width: 1
                border.color: root.on ? Qt.lighter(root.color, 1.25) : Theme.alpha(root.color, 0.42)
                Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
                // specular highlight
                Rectangle {
                    visible: root.size >= 9
                    x: parent.width * 0.26
                    y: parent.height * 0.18
                    width: parent.width * 0.34
                    height: parent.height * 0.26
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, root.on ? 0.45 : 0.10)
                }
            }
            MultiEffect {
                source: lamp
                anchors.fill: lamp
                visible: root.glow && root.on
                shadowEnabled: true
                shadowColor: root.color
                shadowBlur: 1.0
                shadowOpacity: Theme.dark ? 0.9 : 0.55
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
                shadowScale: 1.25
                blurMax: 12
            }
        }

        Text {
            id: label
            visible: root.text.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.textColor
            font.pixelSize: root.fontSize
        }
    }
}
