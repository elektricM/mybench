pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Pinnable right column of compact live tiles chosen by the user (app.md 2.4): `+` adds an
// instrument, the × of a tile removes it, the grip above a tile drags it to another place. The
// list is WorkspaceState.dashboardTiles (default DMM + DIO; stored with the layout and sessions).
Rectangle {
    id: column

    required property WorkspaceState workspace
    readonly property int tileHeight: 150
    readonly property var available: InstrumentRegistry.ids.filter(id => column.workspace.dashboardTiles.indexOf(id) < 0)

    implicitWidth: 260
    color: Theme.surface

    Divider {
        vertical: true
        anchors.left: parent.left
        height: parent.height
    }

    Menu {
        id: addMenu
        Repeater {
            model: column.available
            delegate: MenuItem {
                required property string modelData
                text: InstrumentRegistry.info(modelData).title ?? modelData
                icon.source: Theme.icon(InstrumentRegistry.info(modelData).icon ?? "")
                onTriggered: column.workspace.addDashboardTile(modelData)
            }
        }
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
                spacing: Theme.spacingXs
                Icon { name: "dashboard"; color: Theme.textSecondary }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingXs
                    text: qsTr("Dashboard")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.DemiBold
                }
                IconButton {
                    id: addButton
                    iconName: "plus"
                    toolTip: column.available.length > 0 ? qsTr("Add a tile") : qsTr("Every instrument is on the dashboard")
                    enabled: column.available.length > 0
                    onClicked: addMenu.popup(addButton, 0, addButton.height)
                }
                IconButton {
                    iconName: "pin-off"
                    toolTip: qsTr("Unpin dashboard")
                    shortcutText: Theme.keys("Ctrl+Shift+D")
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
            interactive: !tiles.dragging
            clip: true
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: flick }

            ColumnLayout {
                id: tiles
                property bool dragging: false
                x: Theme.spacingM
                y: Theme.spacingM
                width: flick.width - 2 * Theme.spacingM
                spacing: Theme.spacingM

                Repeater {
                    model: column.workspace.dashboardTiles
                    delegate: Item {
                        id: slot
                        required property string modelData
                        required property int index
                        property real dragY: 0
                        readonly property int target: Math.max(0, Math.min(column.workspace.dashboardTiles.length - 1,
                                                       index + Math.round(dragY / (column.tileHeight + tiles.spacing))))

                        Layout.fillWidth: true
                        Layout.preferredHeight: column.tileHeight
                        z: gripDrag.active ? 10 : 0

                        // Drop position while dragging.
                        Rectangle {
                            visible: gripDrag.active && slot.target !== slot.index
                            x: 0
                            width: parent.width
                            height: 2
                            radius: 1
                            color: Theme.accent
                            y: (slot.target - slot.index) * (column.tileHeight + tiles.spacing)
                               + (slot.target > slot.index ? column.tileHeight + tiles.spacing / 2 : -tiles.spacing / 2) - 1
                        }

                        TileFrame {
                            id: tile
                            width: slot.width
                            height: slot.height
                            y: gripDrag.active ? slot.dragY : 0
                            opacity: gripDrag.active ? 0.92 : 1
                            instrumentId: slot.modelData
                            compact: true
                            showFavorite: false
                            closable: true
                            onOpenRequested: column.workspace.open(instrumentId)
                            onCloseRequested: column.workspace.removeDashboardTile(instrumentId)
                        }

                        // Drag grip (top center of the tile).
                        Item {
                            id: grip
                            anchors.horizontalCenter: tile.horizontalCenter
                            y: tile.y
                            width: 48
                            height: 12
                            Accessible.name: qsTr("Drag to reorder")
                            Rectangle {
                                anchors.centerIn: parent
                                width: 24
                                height: 4
                                radius: 2
                                visible: tile.hovered || gripHover.hovered || gripDrag.active
                                color: gripHover.hovered || gripDrag.active ? Theme.textSecondary : Theme.borderStrong
                            }
                            HoverHandler {
                                id: gripHover
                                cursorShape: gripDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            }
                            DragHandler {
                                id: gripDrag
                                target: null
                                xAxis.enabled: false
                                onActiveTranslationChanged: if (active) slot.dragY = activeTranslation.y
                                onActiveChanged: {
                                    tiles.dragging = active
                                    if (!active) {
                                        column.workspace.moveDashboardTile(slot.index, slot.target)
                                        slot.dragY = 0
                                    }
                                }
                            }
                            ToolTip.visible: gripHover.hovered && !gripDrag.active
                            ToolTip.delay: 700
                            ToolTip.text: qsTr("Drag to reorder")
                        }
                    }
                }

                // No tiles
                ColumnLayout {
                    visible: column.workspace.dashboardTiles.length === 0
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spacingXl
                    spacing: Theme.spacingM
                    Icon {
                        Layout.alignment: Qt.AlignHCenter
                        name: "dashboard"
                        size: Theme.iconSizeL
                        color: Theme.textMuted
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No tiles")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("Keep live readings of any instrument next to the panel you work in.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeM
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        id: emptyAdd
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Add a tile")
                        iconName: "plus"
                        onClicked: addMenu.popup(emptyAdd, 0, emptyAdd.height)
                    }
                }
            }
        }
    }
}
