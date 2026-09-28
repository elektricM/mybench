pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// Mutually exclusive choice shown as joined segments with a sliding highlight.
// `model`: strings, or objects { text, icon, toolTip, enabled }. Controlled component: bind
// currentIndex and handle activated(index).
//     SegmentedControl {
//         model: [{ icon: "wave-sine-line", toolTip: "Sine" }, { icon: "wave-square-line", toolTip: "Square" }]
//         currentIndex: ctl.waveform
//         onActivated: index => ctl.waveform = index
//     }
T.Control {
    id: control

    component SegmentButton: T.AbstractButton {
        property real naturalWidth: 0
    }

    property var model: []
    property int currentIndex: 0
    property color accentColor: Theme.accent   // selected text/icon color
    property bool compact: false
    readonly property int count: segmentRepeater.count
    // Sum of the segments' natural widths (the control stretches them equally when wider).
    readonly property real naturalWidth: {
        let total = 0
        for (let i = 0; i < segmentRepeater.count; ++i) {
            const segment = segmentRepeater.itemAt(i) as SegmentButton
            if (segment)
                total += segment.naturalWidth
        }
        return total
    }

    signal activated(int index)

    function entry(index: int): var {
        const e = model[index]
        return (typeof e === "string" || typeof e === "number") ? { text: String(e) } : (e ?? {})
    }

    function select(index: int) {
        if (index < 0 || index >= count || index === currentIndex)
            return
        if (entry(index).enabled === false)
            return
        activated(index)
    }

    implicitHeight: compact ? Theme.controlHeightS : Theme.controlHeight
    implicitWidth: naturalWidth + 4
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    font.pixelSize: compact ? Theme.fontSizeM : Theme.fontSizeBody

    Keys.onLeftPressed: select(currentIndex - 1)
    Keys.onRightPressed: select(currentIndex + 1)

    background: Rectangle {
        radius: Theme.radiusM
        color: Theme.surfaceSunken
        border.width: 1
        border.color: Theme.border
        FocusRing { shown: control.visualFocus; radius: Theme.radiusM + 3 }
    }

    contentItem: Item {
        id: track

        Rectangle {
            id: highlight
            readonly property Item target: segmentRepeater.count > 0 && control.currentIndex >= 0
                                           ? segmentRepeater.itemAt(Math.min(control.currentIndex, segmentRepeater.count - 1)) : null
            visible: target !== null
            x: target ? target.x + 2 : 0
            y: 2
            width: target ? target.width : 0
            height: track.height - 4
            radius: Theme.radiusS
            color: Theme.dark ? "#2A2E38" : "#FFFFFF"
            border.width: 1
            border.color: Theme.dark ? "#383D49" : Theme.border
            Behavior on x { enabled: highlight.width > 0; NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
            Behavior on width { enabled: highlight.width > 0; NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        }

        Row {
            id: segmentsRow
            x: 2
            height: track.height
            Repeater {
                id: segmentRepeater
                model: control.model
                delegate: SegmentButton {
                    id: segment
                    required property int index
                    readonly property var info: control.entry(index)
                    readonly property bool selected: index === control.currentIndex
                    naturalWidth: Math.max(control.compact ? 26 : 30,
                        (iconItem.visible ? iconItem.width : 0) + (label.visible ? label.implicitWidth : 0)
                        + (iconItem.visible && label.visible ? 6 : 0) + (control.compact ? 14 : 20))

                    width: control.availableWidth > control.naturalWidth + 4.5 && control.count > 0
                           ? (track.width - 4) / control.count : naturalWidth
                    height: segmentsRow.height
                    enabled: control.enabled && info.enabled !== false
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: control.select(index)
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: info.text ?? info.toolTip ?? ""
                    Accessible.checked: selected

                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: 6
                            Icon {
                                id: iconItem
                                visible: (segment.info.icon ?? "").length > 0
                                anchors.verticalCenter: parent.verticalCenter
                                name: segment.info.icon ?? ""
                                size: control.compact ? Theme.iconSizeS : Theme.iconSize
                                color: !segment.enabled ? Theme.textDisabled
                                     : segment.selected ? control.accentColor
                                     : segment.hovered ? Theme.text : Theme.textSecondary
                            }
                            Text {
                                id: label
                                visible: (segment.info.text ?? "").length > 0
                                anchors.verticalCenter: parent.verticalCenter
                                text: segment.info.text ?? ""
                                font.pixelSize: control.font.pixelSize
                                font.weight: segment.selected ? Font.DemiBold : Font.Normal
                                color: !segment.enabled ? Theme.textDisabled
                                     : segment.selected ? Theme.text
                                     : segment.hovered ? Theme.text : Theme.textSecondary
                            }
                        }
                    }

                    ToolTip.visible: hovered && (info.toolTip ?? "").length > 0
                    ToolTip.delay: 500
                    ToolTip.text: info.toolTip ?? ""
                }
            }
        }
    }
}
