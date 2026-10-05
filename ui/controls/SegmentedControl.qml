pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// One choice out of 2–4 short options (docs/UI.md §6): joined segments sized to their content on a
// recessed track (radius 6, like the macOS segmented control); the current segment is a lighter,
// raised chip with a soft shadow, colors from the palette (Theme). Wider than needed, it stays at
// its natural width (left-aligned); narrower, the segments shrink and elide.
// `model`: strings, or objects { text, icon, toolTip, enabled }. Controlled component: bind
// currentIndex and apply activated(index).
//     SegmentedControl { model: [qsTr("Immediate"), qsTr("Edge")]; currentIndex: ctl.mode
//                        onActivated: index => ctl.mode = index }
T.Control {
    id: control

    property var model: []
    property int currentIndex: 0
    property color accentColor: Theme.text      // v1: color of the selected text/icon
    property bool compact: false                // Theme.controlHeightS
    readonly property int count: segmentRepeater.count
    // Sum of the segments' natural widths.
    readonly property real naturalWidth: {
        let total = 0
        for (let i = 0; i < segmentRepeater.count; ++i) {
            const segment = segmentRepeater.itemAt(i) as Segment
            if (segment)
                total += segment.naturalWidth
        }
        return total
    }
    readonly property real trackWidth: Math.min(availableWidth, naturalWidth + 2 * inset)
    readonly property int inset: 2
    readonly property int radius: 6

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

    component Segment: T.AbstractButton {
        property real naturalWidth: 0
    }

    implicitWidth: naturalWidth + 2 * inset
    implicitHeight: compact ? Theme.controlHeightS : Theme.controlHeight
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    font.pixelSize: compact ? Theme.fontSizeSmall : Theme.fontSizeBody
    Accessible.role: Accessible.PageTabList

    Keys.onLeftPressed: select(currentIndex - 1)
    Keys.onRightPressed: select(currentIndex + 1)

    background: Item {
        Rectangle {
            width: control.trackWidth
            height: parent.height
            radius: control.radius
            color: Theme.alpha(Theme.text, Theme.dark ? 0.09 : 0.065)
            opacity: control.enabled ? 1 : 0.6
            FocusRing { shown: control.visualFocus }
        }
    }

    contentItem: Item {
        id: track

        // Soft shadow under the current segment (raised look).
        Rectangle {
            visible: chip.visible
            x: chip.x
            y: chip.y + 1
            width: chip.width
            height: chip.height
            radius: chip.radius
            color: Theme.alpha("#000000", Theme.dark ? 0.30 : 0.10)
            opacity: chip.opacity
        }
        // The current segment.
        Rectangle {
            id: chip
            objectName: "segmentChip"
            readonly property Item target: segmentRepeater.count > 0 && control.currentIndex >= 0
                                           ? segmentRepeater.itemAt(Math.min(control.currentIndex, segmentRepeater.count - 1)) : null
            visible: target !== null
            x: target ? segments.x + target.x : 0
            y: control.inset
            width: target ? target.width : 0
            height: track.height - 2 * control.inset
            radius: control.radius - 1
            color: Theme.dark ? Theme.mix(Theme.controlFill, "#FFFFFF", 0.26) : Theme.mix(Theme.controlFill, "#FFFFFF", 0.6)
            border.width: Theme.borderWidth
            border.color: Theme.dark ? Theme.alpha("#FFFFFF", 0.10) : Theme.alpha(Theme.text, 0.10)
            opacity: control.enabled ? 1 : 0.6
            Behavior on x {
                enabled: Theme.animations && chip.width > 0
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
            Behavior on width {
                enabled: Theme.animations && chip.width > 0
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }

        Row {
            id: segments
            x: control.inset
            height: track.height
            readonly property real shrink: control.naturalWidth > 0 ? Math.min(1, (control.trackWidth - 2 * control.inset) / control.naturalWidth) : 1

            Repeater {
                id: segmentRepeater
                model: control.model
                delegate: Segment {
                    id: segment
                    required property int index
                    readonly property var info: control.entry(index)
                    readonly property bool selected: index === control.currentIndex
                    readonly property bool hasIcon: (info.icon ?? "").length > 0
                    readonly property bool hasText: (info.text ?? "").length > 0
                    naturalWidth: Math.ceil((hasIcon ? Theme.iconSize : 0) + (hasText ? measure.advanceWidth : 0)
                                            + (hasIcon && hasText ? Theme.spacingXs + 2 : 0)
                                            + (control.compact ? 2 * Theme.spacingS : 2 * Theme.spacingM))
                    width: Math.floor(naturalWidth * segments.shrink)
                    height: segments.height
                    enabled: control.enabled && info.enabled !== false
                    hoverEnabled: true
                    focusPolicy: Qt.NoFocus
                    onClicked: control.select(index)
                    Accessible.role: Accessible.PageTab
                    Accessible.name: info.text ?? info.toolTip ?? ""
                    Accessible.checked: selected

                    // Width from the Medium weight, so selecting never resizes a segment.
                    TextMetrics {
                        id: measure
                        font.pixelSize: control.font.pixelSize
                        font.weight: Font.Medium
                        text: segment.info.text ?? ""
                    }
                    // Hairline between two unselected segments.
                    Rectangle {
                        visible: segment.index > 0 && !segment.selected && segment.index - 1 !== control.currentIndex
                        x: 0
                        y: Math.round(segment.height * 0.28)
                        width: Theme.borderWidth
                        height: segment.height - 2 * y
                        color: Theme.separator
                    }

                    contentItem: Item {
                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.spacingXs + 2
                            Icon {
                                visible: segment.hasIcon
                                anchors.verticalCenter: parent.verticalCenter
                                name: segment.info.icon ?? ""
                                size: control.compact ? Theme.iconSizeS : Theme.iconSize
                                color: !segment.enabled ? Theme.textDisabled
                                     : segment.selected ? control.accentColor
                                     : segment.hovered ? Theme.text : Theme.textSecondary
                            }
                            Text {
                                id: label
                                visible: segment.hasText
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, segment.width - (segment.hasIcon ? Theme.iconSize + 6 : 0) - 8)
                                text: segment.info.text ?? ""
                                font.pixelSize: control.font.pixelSize
                                font.weight: segment.selected ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                color: !segment.enabled ? Theme.textDisabled
                                     : segment.selected ? control.accentColor
                                     : segment.hovered ? Theme.text : Theme.textSecondary
                            }
                        }
                    }

                    ToolTip.visible: hovered && (info.toolTip ?? "").length > 0
                    ToolTip.delay: Theme.toolTipDelay
                    ToolTip.text: info.toolTip ?? ""
                }
            }
        }
    }
}
