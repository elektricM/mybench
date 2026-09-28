pragma ComponentBehavior: Bound
import QtQuick
import MyBench.Framework

// Big numeric display: label, value with SI prefix, unit, optional % of full-scale bar graph,
// overload ("OL") and hold states.
//     Readout { label: qsTr("DC Voltage"); value: ctl.reading; unit: "V"; digits: 4
//               overload: ctl.overload; showBar: true; barValue: ctl.fractionOfRange }
Item {
    id: root

    property real value: NaN
    property string unit
    property int digits: 4
    property string label
    property string valueText          // overrides the formatted value
    property string caption            // under the value, e.g. "Range 20 V · Auto"
    property bool overload: false
    property bool hold: false
    property bool stale: false         // dim the value (e.g. idle, last reading)
    property color color: Theme.text
    property int fontSize: Theme.fontSizeDisplay
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

    implicitWidth: Math.max(column.implicitWidth, 160)
    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: root.width
        spacing: 4

        Row {
            visible: root.label.length > 0 || root.hold
            spacing: Theme.spacingS
            anchors.horizontalCenter: root.alignment === Qt.AlignHCenter ? parent.horizontalCenter : undefined
            Text {
                text: root.label.toUpperCase()
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
                font.weight: Font.DemiBold
                font.letterSpacing: 1.0
            }
            Badge {
                visible: root.hold
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Hold")
                color: Theme.warning
            }
        }

        Row {
            spacing: Math.round(root.fontSize * 0.14)
            anchors.horizontalCenter: root.alignment === Qt.AlignHCenter ? parent.horizontalCenter : undefined
            Text {
                id: number
                text: root.numberPart
                color: root.overload ? Theme.danger : root.color
                opacity: root.stale ? 0.55 : 1
                font.pixelSize: root.fontSize
                font.weight: Font.Light
                font.features: Theme.tabularFigures
                Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
            }
            Text {
                visible: root.unitPart.length > 0
                anchors.baseline: number.baseline
                text: root.unitPart
                color: root.overload ? Theme.alpha(Theme.danger, 0.8) : Theme.textSecondary
                font.pixelSize: Math.round(root.fontSize * 0.42)
                font.weight: Font.Normal
            }
        }

        Text {
            visible: root.caption.length > 0
            anchors.horizontalCenter: root.alignment === Qt.AlignHCenter ? parent.horizontalCenter : undefined
            text: root.caption
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
            font.features: Theme.tabularFigures
        }

        // Bar graph (% of full scale), 40 segments.
        Item {
            visible: root.showBar
            width: parent.width
            height: 22

            Row {
                id: segments
                anchors.left: parent.left
                anchors.right: barCaption.left
                anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                readonly property int n: 40
                readonly property real segmentWidth: (width - (n - 1) * spacing) / n
                readonly property real fill: Math.max(-1, Math.min(1, root.barValue))
                Repeater {
                    model: segments.n
                    delegate: Rectangle {
                        required property int index
                        readonly property real center: root.barBipolar ? (index + 0.5) / segments.n * 2 - 1 : (index + 0.5) / segments.n
                        readonly property bool lit: root.barBipolar
                            ? (segments.fill >= 0 ? center >= 0 && center <= segments.fill : center <= 0 && center >= segments.fill)
                            : center <= Math.abs(segments.fill)
                        width: segments.segmentWidth
                        height: index % 5 === 0 ? 12 : 8
                        y: (12 - height) / 2
                        radius: 1
                        color: lit ? (root.overload ? Theme.danger : root.color === Theme.text ? Theme.accent : root.color)
                                   : (Theme.dark ? "#1E2128" : "#E3E6EB")
                    }
                }
            }
            Text {
                id: barCaption
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.barLabel
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
