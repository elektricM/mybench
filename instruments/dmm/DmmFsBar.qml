pragma ComponentBehavior: Bound
import QtQuick
import MyBench.Controls

// Percent-of-full-scale gauge under the Multimeter readout: 50 segments, a longer tick every
// 10 %, scale labels 0 / 50 / 100 and the "% FS" caption. `value` is the fraction 0..1
// (negative: empty, before the first reading); `overload` paints it in the danger color.
Item {
    id: root

    property real value: -1
    property bool overload: false
    property color color: Theme.ch1
    property bool showScale: true

    readonly property int segmentCount: 50
    readonly property real fill: Math.max(0, Math.min(1, value))

    implicitWidth: 480
    implicitHeight: showScale ? 30 : 14
    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Percent Full-Scale")

    Row {
        id: segments
        anchors.left: parent.left
        anchors.right: caption.left
        anchors.rightMargin: Theme.spacingS
        height: 14
        spacing: 2
        readonly property real segmentWidth: (width - (root.segmentCount - 1) * spacing) / root.segmentCount
        Repeater {
            model: root.segmentCount
            delegate: Rectangle {
                required property int index
                readonly property bool major: (index + 1) % 5 === 0
                readonly property bool lit: root.value >= 0 && (index + 0.5) / root.segmentCount <= root.fill
                y: major ? 0 : 3
                width: segments.segmentWidth
                height: major ? 14 : 8
                radius: 1
                color: lit ? (root.overload ? Theme.danger : root.color)
                           : Theme.dark ? "#1C2027" : "#E1E5EA"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
        }
    }
    Text {
        id: caption
        anchors.right: parent.right
        anchors.verticalCenter: segments.verticalCenter
        text: "% FS"
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.DemiBold
    }
    Repeater {
        model: root.showScale ? [0, 50, 100] : []
        delegate: Text {
            required property int modelData
            x: segments.x + modelData / 100 * segments.width - (modelData === 0 ? 0 : modelData === 100 ? width : width / 2)
            y: segments.height + 3
            text: modelData
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeXs
            font.features: Theme.tabularFigures
        }
    }
}
