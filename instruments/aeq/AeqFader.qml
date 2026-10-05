pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Vertical console fader of the Audio Equalizer: typeable value, a track with a center (0 dB)
// mark and a handle, caption. Controlled: bind `value`, handle moved(value). Double-click resets.
// The value field takes a number in the shown unit (`inputScale` converts, e.g. 100 for percent).
//     AeqFader { label: "Bass"; from: -12; to: 12; value: ctl.bass; onMoved: v => ctl.bass = v }
Item {
    id: root

    property string label
    property real from: -12
    property real to: 12
    property real value: 0
    property real stepSize: 1
    property real resetValue: 0
    property color accent: Theme.accent
    property string valueText: (value > 0 ? "+" : value < 0 ? "−" : "") + Math.abs(value).toFixed(Math.round(Math.abs(value) * 10) % 10 === 0 ? 0 : 1) + " dB"
    property bool centered: true      // fill from the middle (gains) or from the bottom (volume)
    property real inputScale: 1       // typed number / inputScale = value

    function commitText(text: string) {
        const v = parseFloat(text.replace("−", "-").replace(",", "."))
        if (isFinite(v))
            root.moved(Math.max(root.from, Math.min(root.to, v / root.inputScale)))
    }

    signal moved(real value)

    implicitWidth: 48
    implicitHeight: 180

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingXs

        TextInput {
            id: valueInput
            objectName: "valueInput"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.max(root.width, 44)
            horizontalAlignment: TextInput.AlignHCenter
            text: root.valueText
            color: root.enabled ? Theme.text : Theme.textMuted
            selectionColor: Theme.selection
            selectedTextColor: Theme.selectionText
            selectByMouse: true
            enabled: root.enabled
            font.pixelSize: Theme.fontSizeM
            font.features: Theme.tabularFigures
            onEditingFinished: {
                root.commitText(text)
                text = Qt.binding(() => root.valueText)
            }
            Rectangle {   // edit underline while typing
                anchors { left: parent.left; right: parent.right; top: parent.bottom }
                height: 1
                visible: valueInput.activeFocus
                color: root.accent
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Track.
            Rectangle {
                id: track
                anchors.horizontalCenter: parent.horizontalCenter
                y: 8
                width: 6
                height: parent.height - 16
                radius: 3
                color: Theme.surfaceSunken
                readonly property real position: (root.value - root.from) / (root.to - root.from)
                readonly property real zero: root.centered ? (0 - root.from) / (root.to - root.from) : 0
                Rectangle {
                    width: parent.width
                    radius: 3
                    y: parent.height * (1 - Math.max(track.position, track.zero))
                    height: parent.height * Math.abs(track.position - track.zero)
                    color: root.enabled ? root.accent : Theme.textDisabled
                    opacity: 0.85
                }
            }
            // Scale ticks.
            Repeater {
                model: 5
                delegate: Rectangle {
                    required property int index
                    x: track.x + track.width + 4
                    y: track.y + track.height * index / 4
                    width: index === 2 && root.centered ? 9 : 5
                    height: 1
                    color: Theme.textMuted
                    opacity: 0.6
                }
            }
            // Handle.
            Rectangle {
                id: handle
                anchors.horizontalCenter: parent.horizontalCenter
                y: track.y + track.height * (1 - track.position) - height / 2
                width: 26
                height: 14
                radius: 4
                color: Theme.knobFace
                border.color: dragArea.pressed ? root.accent : Theme.borderStrong
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width - 10
                    height: 2
                    radius: 1
                    color: root.accent
                }
            }
            MouseArea {
                id: dragArea
                anchors.fill: parent
                enabled: root.enabled
                preventStealing: true
                function valueAt(y: real): real {
                    const t = 1 - Math.max(0, Math.min(1, (y - track.y) / track.height))
                    const raw = root.from + t * (root.to - root.from)
                    return Math.round(raw / root.stepSize) * root.stepSize
                }
                onPressed: mouse => root.moved(valueAt(mouse.y))
                onPositionChanged: mouse => { if (pressed) root.moved(valueAt(mouse.y)) }
                onDoubleClicked: root.moved(root.resetValue)
                onWheel: wheel => root.moved(Math.max(root.from, Math.min(root.to, root.value + (wheel.angleDelta.y > 0 ? 1 : -1) * root.stepSize)))
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
            font.weight: Font.DemiBold
        }
    }

    Accessible.role: Accessible.Slider
    Accessible.name: root.label
    activeFocusOnTab: true
    Keys.onUpPressed: root.moved(Math.min(root.to, root.value + root.stepSize))
    Keys.onDownPressed: root.moved(Math.max(root.from, root.value - root.stepSize))
    ToolTip.visible: hover.hovered
    ToolTip.delay: 600
    ToolTip.text: root.label
    HoverHandler { id: hover }
}
