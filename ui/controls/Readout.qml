pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Framework

// A measured value (docs/UI.md §11 "Values", §13.8): a sentence-case label, the number with tabular
// figures and a fixed number of significant digits, the unit, an optional caption and %FS bar.
// "OL" when over range, a "Hold" note, dimmed while stale (after Stop). The tooltip shows the full
// precision; the context menu copies the value.
//     Readout { size: "xl"; label: qsTr("DC voltage"); value: ctl.reading; unit: "V"; digits: 5 }
//     Readout { size: "m"; swatchColor: Theme.signalCh0; label: "RMS"; valueText: "0.707 V" }
Item {
    id: root

    property real value: NaN
    property string unit
    property int digits: 4
    property string label
    property string valueText          // overrides the formatted value ("12.3 mV": the last word is the unit)
    property string caption            // under the value, e.g. "Range 20 V · Auto"
    property bool overload: false
    property bool hold: false
    property bool stale: false         // dimmed: last value of a stopped instrument
    property string size: "l"          // "s" | "m" | "l" | "xl"
    property color swatchColor: "transparent"   // channel swatch before the label
    property color color: Theme.text   // value color (text; never a channel color)
    property int fontSize: size === "xl" ? Theme.fontSizeReadoutXL
                         : size === "l" ? Theme.fontSizeReadoutL
                         : size === "m" ? Theme.fontSizeReadoutM : Theme.fontSizeHeading
    property int alignment: Qt.AlignHCenter
    property bool showBar: false
    property real barValue: 0          // -1..1 (fraction of full scale; negative fills left of 0)
    property bool barBipolar: false
    property string barLabel: "% FS"

    // "12.34 mV" -> number "12.34", unit "mV"
    readonly property string formatted: overload ? "OL"
                                       : valueText.length > 0 ? valueText
                                       : Units.format(value, unit, digits, false)
    readonly property int split: formatted.lastIndexOf(" ")
    readonly property string numberPart: overload ? "OL" : split > 0 ? formatted.slice(0, split) : formatted
    readonly property string unitPart: overload ? unit : split > 0 ? formatted.slice(split + 1) : ""
    readonly property string fullText: overload ? qsTr("Over range")
                                     : valueText.length > 0 || !isFinite(value) ? formatted
                                     : Units.format(value, unit, 10, true)
    readonly property bool large: fontSize >= Theme.fontSizeReadoutL
    readonly property bool hasSwatch: swatchColor.a > 0

    implicitWidth: Math.max(column.implicitWidth, 64)
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.StaticText
    Accessible.name: (label.length > 0 ? label + ": " : "") + fullText

    function copyValue() {
        clipboard.text = root.overload ? "OL" : root.valueText.length > 0 || !isFinite(root.value)
                                       ? root.formatted : String(root.value)
        clipboard.selectAll()
        clipboard.copy()
    }
    TextEdit {
        id: clipboard
        visible: false
    }

    Column {
        id: column
        width: root.width
        spacing: root.large ? Theme.spacingXs : 2

        Row {
            visible: root.label.length > 0 || root.hold
            spacing: Theme.spacingXs + 2
            anchors.horizontalCenter: root.alignment === Qt.AlignHCenter ? parent.horizontalCenter : undefined
            anchors.right: root.alignment === Qt.AlignRight ? parent.right : undefined
            Rectangle {
                visible: root.hasSwatch
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: 2
                color: root.swatchColor
            }
            Text {
                text: root.label
                color: Theme.textSecondary
                font.pixelSize: root.large ? Theme.fontSizeBody : Theme.fontSizeSmall
            }
            Text {
                visible: root.hold
                text: qsTr("Hold")
                color: Theme.caution
                font.pixelSize: root.large ? Theme.fontSizeBody : Theme.fontSizeSmall
                font.weight: Font.Medium
            }
        }

        Row {
            id: valueRow
            spacing: Math.round(root.fontSize * (root.large ? 0.18 : 0.25))
            anchors.horizontalCenter: root.alignment === Qt.AlignHCenter ? parent.horizontalCenter : undefined
            anchors.right: root.alignment === Qt.AlignRight ? parent.right : undefined
            opacity: root.stale ? 0.45 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
            Text {
                id: number
                text: root.numberPart
                color: root.overload ? Theme.negative : root.color
                font.pixelSize: root.fontSize
                font.weight: root.size === "xl" || root.fontSize >= Theme.fontSizeReadoutXL ? Font.Light : Font.Normal
                font.features: Theme.tabularFigures
            }
            Text {
                visible: root.unitPart.length > 0
                anchors.baseline: number.baseline
                text: root.unitPart
                color: root.overload ? Theme.negative : Theme.textSecondary
                font.pixelSize: root.large ? Math.max(Theme.fontSizeSmall, Math.round(root.fontSize * (root.fontSize >= Theme.fontSizeReadoutXL ? 0.4 : 0.5))) : root.fontSize
            }
        }

        Text {
            visible: root.caption.length > 0
            anchors.horizontalCenter: root.alignment === Qt.AlignHCenter ? parent.horizontalCenter : undefined
            anchors.right: root.alignment === Qt.AlignRight ? parent.right : undefined
            text: root.caption
            color: Theme.textTertiary
            font.pixelSize: root.large ? Theme.fontSizeSmall : Theme.fontSizeCaption
            font.features: Theme.tabularFigures
        }

        // Fraction of full scale: a thin bar with quarter ticks.
        Item {
            visible: root.showBar
            width: parent.width
            height: 16

            Item {
                id: track
                anchors.left: parent.left
                anchors.right: barCaption.left
                anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                height: 4
                readonly property real fill: Math.max(-1, Math.min(1, root.barValue))
                readonly property real zero: root.barBipolar ? width / 2 : 0
                readonly property real end: root.barBipolar ? zero + fill * width / 2 : Math.abs(fill) * width
                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: Theme.alpha(Theme.text, 0.12)
                }
                Rectangle {
                    x: Math.min(track.zero, track.end)
                    width: Math.abs(track.end - track.zero)
                    height: parent.height
                    radius: 2
                    color: root.overload ? Theme.negative : Theme.accent
                }
                Repeater {
                    model: 5
                    delegate: Rectangle {
                        required property int index
                        x: Math.round(index * (track.width - 1) / 4)
                        y: track.height + 2
                        width: 1
                        height: 3
                        color: Theme.textTertiary
                    }
                }
            }
            Text {
                id: barCaption
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.barLabel
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeCaption
            }
        }
    }

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && !root.overload && root.fullText !== root.formatted
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: root.fullText

    ContextMenu.menu: Menu {
        MenuItem {
            text: qsTr("Copy Value")
            onTriggered: root.copyValue()
        }
    }
}
