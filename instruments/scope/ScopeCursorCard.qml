pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// The readout of a cursor pair under the display, in the look of a MeasurementCard: the two
// cursor colors as swatches and as a two-tone edge, the main difference large (Δt or ΔV), and up
// to four labelled values beside it (1/Δt, ΔV, C1, C2 …).
//     ScopeCursorCard { names: ["C1", "C2"]; title: qsTr("Cursors"); value: "1.25 ms"
//                       items: [{ name: "1/Δt", text: "800 Hz" }, { name: "ΔV", text: "1.2 V" }] }
Item {
    id: card

    property string title
    property var names: ["C1", "C2"]
    property color color1: Theme.cursor1
    property color color2: Theme.cursor2
    property string value
    property var items: []          // [{ name, text, color? }], two rows of two

    implicitWidth: Math.max(180, row.implicitWidth + 2 * 10 + 2)
    implicitHeight: Math.max(64, column.implicitHeight + 2 * 7 + 2)
    Accessible.role: Accessible.StaticText
    Accessible.name: title + ": " + value

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPopup
        color: Theme.cardFill
        border.width: 1
        border.color: Theme.cardBorder
        // The pair's colors down the left edge: C1 above, C2 below.
        Column {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; leftMargin: 4; topMargin: 8; bottomMargin: 8 }
            width: 3
            Rectangle { width: 3; height: parent.height / 2; radius: 1.5; color: card.color1 }
            Rectangle { width: 3; height: parent.height / 2; radius: 1.5; color: card.color2 }
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.leftMargin: 15   // as MeasurementCard: the colored bar never touches the value
        anchors.rightMargin: 8
        anchors.topMargin: 6
        anchors.bottomMargin: 7
        spacing: 1

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: card.names
                delegate: Row {
                    id: nameChip
                    required property string modelData
                    required property int index
                    spacing: 3
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 2
                        color: nameChip.index === 0 ? card.color1 : card.color2
                    }
                    Text {
                        text: nameChip.modelData
                        color: nameChip.index === 0 ? card.color1 : card.color2
                        font.pixelSize: Theme.fontSizeCaption
                        font.weight: Font.DemiBold
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                leftPadding: 2
                text: card.title
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                elide: Text.ElideRight
            }
        }

        RowLayout {
            id: row
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            Text {
                objectName: "cursorCardValue"
                Layout.alignment: Qt.AlignVCenter
                text: card.value.length > 0 ? card.value : "—"
                color: Theme.text
                font.pixelSize: Theme.fontSizeReadoutM
                font.weight: Font.DemiBold
                font.features: Theme.tabularFigures
            }
            Item { Layout.fillWidth: true }
            GridLayout {
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                columns: 2
                columnSpacing: 12
                rowSpacing: 0
                Repeater {
                    model: card.items
                    delegate: Row {
                        id: cell
                        required property var modelData
                        spacing: 4
                        Text {
                            text: cell.modelData.name
                            color: cell.modelData.color ?? Theme.textTertiary
                            font.pixelSize: Theme.fontSizeCaption
                        }
                        Text {
                            text: cell.modelData.text.length > 0 ? cell.modelData.text : "—"
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
