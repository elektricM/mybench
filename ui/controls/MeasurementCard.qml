pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework

// One measurement under the display (PicoScope style): swatch and title ("CH 0 · RMS"), the
// value large, and its statistics (min, max, mean, σ) beside it when `stats` has any. A × on
// hover emits removeRequested(); `menu` (a Menu) opens on right-click and from ⋯.
//     MeasurementCard { title: "CH 0 · RMS"; color: Theme.signalCh0; value: "707.1 mV"
//                       stats: ({ min: "706.4 mV", max: "708.0 mV", mean: "707.2 mV", stdDev: "0.31 mV" })
//                       onRemoveRequested: model.remove(index) }
// Numbers instead of strings: set `numericValue` and `unit` (stats entries may be numbers too); the
// card formats them with Units.format to `significantDigits` (σ to `stdDevDigits`).
//     MeasurementCard { title: "CH 0 · Frequency"; numericValue: m.value; unit: "Hz"
//                       stats: ({ min: m.min, max: m.max, mean: m.mean, stdDev: m.sd }) }
Item {
    id: card

    property string title
    property color color: Theme.accent
    property string value
    property var stats: ({})
    property real numericValue: NaN              // NaN: show `value`
    property string unit
    property int significantDigits: 4
    property int stdDevDigits: Math.max(1, significantDigits - 1)
    // What the card shows: the formatted number, else `value`.
    readonly property string valueText: !isNaN(numericValue) ? Units.format(numericValue, unit, significantDigits) : value
    property Menu menu: null
    property bool removable: true
    property bool selected: false

    readonly property bool hasStats: stats !== null && stats !== undefined
                                     && ["min", "max", "mean", "stdDev"].some(k => String(stats[k] ?? "").length > 0)
    readonly property bool hovered: hover.hovered

    signal removeRequested()

    // A statistic as shown: numbers formatted like the value, strings as given.
    function statText(key: string): string {
        const v = stats?.[key]
        if (v === undefined || v === null || v === "")
            return "—"
        if (typeof v === "number")
            return Units.format(v, unit, key === "stdDev" ? stdDevDigits : significantDigits)
        return String(v)
    }

    implicitWidth: Math.max(180, row.implicitWidth + 15 + 10 + 2)
    implicitHeight: Math.max(64, column.implicitHeight + 2 * 7 + 2)
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.cardFill
        border.width: 1
        border.color: card.selected ? Theme.accent : card.hovered ? Theme.controlBorder : Theme.cardBorder
        // The source's color down the left edge (which trace a value belongs to, at a glance).
        Rectangle {
            width: 3
            radius: 1.5
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; leftMargin: 4; topMargin: 8; bottomMargin: 8 }
            color: card.color
        }
    }
    Accessible.role: Accessible.StaticText
    Accessible.name: title + ": " + valueText

    HoverHandler { id: hover }
    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: card.menu !== null
        onTapped: (point, button) => card.menu.popup(card, point.position.x, point.position.y)
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.leftMargin: 15          // clear of the 3 px source bar at 4 px
        anchors.rightMargin: 6
        anchors.topMargin: 6
        anchors.bottomMargin: 7
        spacing: 1

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Rectangle {
                implicitWidth: 8
                implicitHeight: 8
                radius: 2
                color: card.color
            }
            Text {
                Layout.fillWidth: true
                text: card.title
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideRight
            }
            IconButton {
                objectName: "menuButton"
                visible: card.menu !== null
                opacity: card.hovered || card.menu?.visible ? 1 : 0
                iconName: "more-horizontal"
                size: 18
                iconSize: 14
                toolTip: qsTr("Options")
                onClicked: card.menu.popup(this, 0, height)
            }
            IconButton {
                objectName: "removeButton"
                visible: card.removable
                opacity: card.hovered || activeFocus ? 1 : 0
                iconName: "close"
                size: 18
                iconSize: 12
                toolTip: qsTr("Remove measurement")
                Accessible.name: qsTr("Remove %1").arg(card.title)
                onClicked: card.removeRequested()
            }
        }

        RowLayout {
            id: row
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Text {
                id: valueText
                Layout.alignment: Qt.AlignVCenter
                text: card.valueText.length > 0 ? card.valueText : "—"
                color: Theme.text
                font.pixelSize: Theme.fontSizeReadoutM
                font.weight: Font.DemiBold
                font.features: Theme.tabularFigures
                wrapMode: Text.NoWrap
            }
            Item { Layout.fillWidth: true; visible: !card.hasStats }

            GridLayout {
                visible: card.hasStats
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                columns: 4
                columnSpacing: 4
                rowSpacing: 0
                Repeater {
                    model: [ { key: "min", name: qsTr("min") }, { key: "max", name: qsTr("max") },
                             { key: "mean", name: qsTr("mean") }, { key: "stdDev", name: "σ" } ]
                    delegate: Item {
                        id: statCell
                        required property var modelData
                        required property int index
                        Layout.row: index % 2
                        Layout.column: index < 2 ? 0 : 2
                        Layout.columnSpan: 2
                        implicitWidth: statRow.implicitWidth + (index < 2 ? 8 : 0)
                        implicitHeight: statRow.implicitHeight
                        Row {
                            id: statRow
                            spacing: 4
                            Text {
                                text: statCell.modelData.name
                                color: Theme.textTertiary
                                font.pixelSize: Theme.fontSizeCaption
                            }
                            Text {
                                text: card.statText(statCell.modelData.key)
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                                font.features: Theme.tabularFigures
                            }
                        }
                    }
                }
            }
        }
    }
}
