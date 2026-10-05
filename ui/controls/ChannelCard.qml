pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A channel's always-visible controls (PicoScope style): header with a color swatch, title
// ("CH 0"), subtitle ("AI 0") and an on/off switch; body = the items declared inside (column,
// 10 px gaps: KnobFields, segmented controls...), then a line of fact chips ("DC", "±10 V range").
// Unchecked, the body stays (dimmed, still usable) so a channel can be set up before it is switched
// on; `collapseWhenOff: true` folds it to its header instead. Controlled: bind `checked`, apply
// toggled().
//     ChannelCard { title: "CH 0"; subtitle: "AI 0"; color: Theme.signalCh0
//                   checked: ch.enabled; onToggled: on => ch.enabled = on
//                   facts: ["DC", "±10 V range"]
//                   KnobField { label: qsTr("Scale"); ... } }
Item {
    id: card

    property string title
    property string subtitle
    property color color: Theme.accent           // the channel color: swatch and left edge
    property bool checked: true
    property bool checkable: true                // false: no switch (e.g. References)
    property bool collapseWhenOff: false         // true: unchecked = header only
    property var facts: []
    property bool compact: false                 // tighter paddings
    property alias headerExtras: headerExtrasRow.data   // small items left of the switch
    default property alias content: body.data

    readonly property real padding: compact ? Theme.spacingS : Theme.spacingS + 2
    readonly property bool active: checked || !checkable      // on (header wash, full-strength body)
    readonly property bool expanded: active || !collapseWhenOff

    signal toggled(bool checked)

    implicitWidth: Math.max(220, column.implicitWidth)
    implicitHeight: column.implicitHeight + 2
    readonly property real radius: Theme.radiusPopup
    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: Theme.cardFill
        border.width: 1
        border.color: Theme.cardBorder
    }
    Accessible.role: Accessible.Grouping
    Accessible.name: subtitle.length > 0 ? title + " " + subtitle : title

    ColumnLayout {
        id: column
        x: 1
        y: 1
        width: card.width - 2
        spacing: 0

        // Header.
        Item {
            id: header
            Layout.fillWidth: true
            implicitHeight: Math.max(Theme.controlHeight + 4, headerRow.implicitHeight + 2 * card.padding - 4)
            implicitWidth: headerRow.implicitWidth + 2 * card.padding

            // Header accent: a faint wash of the channel color.
            Rectangle {
                anchors.fill: parent
                topLeftRadius: card.radius - 1
                topRightRadius: card.radius - 1
                bottomLeftRadius: card.expanded ? 0 : card.radius - 1
                bottomRightRadius: card.expanded ? 0 : card.radius - 1
                color: card.active ? Theme.alpha(card.color, Theme.dark ? 0.18 : 0.12) : "transparent"
            }
            // The channel's color along the top edge: the card reads as that channel at a glance.
            Rectangle {
                anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: card.radius - 2; rightMargin: card.radius - 2 }
                height: 2
                color: card.color
                opacity: card.active ? 1 : 0.35
            }
            TapHandler {
                enabled: card.checkable
                onTapped: card.toggled(!card.checked)
            }

            RowLayout {
                id: headerRow
                anchors.fill: parent
                anchors.leftMargin: card.padding
                anchors.rightMargin: card.padding - 2
                spacing: Theme.spacingS

                Rectangle {
                    implicitWidth: 10
                    implicitHeight: 10
                    radius: 3
                    color: card.active ? card.color : Theme.alpha(card.color, 0.4)
                }
                Text {
                    text: card.title
                    color: card.active ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: card.subtitle.length > 0
                    text: card.subtitle
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
                Item {
                    visible: card.subtitle.length === 0
                    Layout.fillWidth: true
                }
                RowLayout {
                    id: headerExtrasRow
                    spacing: Theme.spacingXs
                    visible: children.length > 0
                }
                Switch {
                    id: toggle
                    objectName: "channelSwitch"
                    visible: card.checkable
                    checked: card.checked
                    padding: 0
                    Accessible.name: qsTr("%1 on").arg(card.title)
                    onToggled: {
                        const on = checked
                        checked = Qt.binding(() => card.checked)
                        card.toggled(on)
                    }
                }
            }
        }

        Rectangle {
            visible: card.expanded && (body.children.length > 0 || card.facts.length > 0)
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.separator
        }

        ColumnLayout {
            objectName: "channelBody"
            visible: card.expanded && (body.children.length > 0 || card.facts.length > 0)
            Layout.fillWidth: true
            Layout.margins: card.padding
            spacing: 10
            opacity: card.active ? 1 : 0.55

            ColumnLayout {
                id: body
                Layout.fillWidth: true
                visible: children.length > 0
                spacing: 10
            }

            Flow {
                visible: card.facts.length > 0
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: card.facts
                    delegate: Rectangle {
                        required property var modelData
                        implicitWidth: chipText.implicitWidth + 12
                        implicitHeight: chipText.implicitHeight + 2
                        radius: 4
                        color: Theme.hoverFill
                        border.width: 1
                        border.color: Theme.separator
                        Text {
                            id: chipText
                            anchors.centerIn: parent
                            text: String(parent.modelData)
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
