pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Pinnable right column of compact live tiles (DMM reading, DIO LEDs, generator state...)
// visible next to any panel.
Rectangle {
    id: column

    required property WorkspaceState workspace

    implicitWidth: 300
    color: Theme.surface

    Divider {
        vertical: true
        anchors.left: parent.left
        height: parent.height
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: 44
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingS
                Icon { name: "dashboard"; color: Theme.textSecondary }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Dashboard")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                }
                IconButton {
                    iconName: "pin-off"
                    toolTip: qsTr("Unpin dashboard")
                    onClicked: column.workspace.toggleDashboard()
                }
            }
            Divider { anchors.bottom: parent.bottom; width: parent.width }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: tiles.implicitHeight + 2 * Theme.spacingM
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: tiles
                x: Theme.spacingM
                y: Theme.spacingM
                width: flick.width - 2 * Theme.spacingM
                spacing: Theme.spacingM
                Repeater {
                    model: InstrumentRegistry
                    delegate: TileFrame {
                        required instrumentId
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        compact: true
                        onOpenRequested: column.workspace.open(instrumentId)
                    }
                }
            }
        }
    }
}
