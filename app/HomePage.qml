pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Home: every instrument as a live tile, the device card, recent sessions and shortcuts.
Rectangle {
    id: home

    required property WorkspaceState workspace

    signal newSessionRequested()
    signal openSessionRequested()
    signal openRecentRequested(url file)
    signal deviceInfoRequested()

    color: Theme.surface

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: page.implicitHeight + 2 * Theme.spacingXl
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: page
            x: Math.max(Theme.spacingXl, (flick.width - width) / 2)
            y: Theme.spacingXl
            width: Math.min(flick.width - 2 * Theme.spacingXl, 1480)
            spacing: Theme.spacingXl

            // Header
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingM
                ColumnLayout {
                    spacing: 4
                    Text {
                        text: qsTr("Workbench")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeHeadline
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: {
                            const running = InstrumentRegistry.runningCount
                            const device = DeviceHub.currentId.length > 0 ? DeviceHub.name : qsTr("No device")
                            return device + "  ·  " + qsTr("%n instrument(s)", "", InstrumentRegistry.count)
                                   + (running > 0 ? "  ·  " + qsTr("%n running", "", running) : "")
                        }
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: qsTr("New session")
                    iconName: "file-plus"
                    tone: "ghost"
                    onClicked: home.newSessionRequested()
                }
                ActionButton {
                    text: qsTr("Open session…")
                    iconName: "folder-open"
                    onClicked: home.openSessionRequested()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingXl

                // Instrument tiles
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingM
                    Text {
                        text: qsTr("INSTRUMENTS")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1.2
                    }
                    GridLayout {
                        id: tiles
                        Layout.fillWidth: true
                        columns: Math.max(1, Math.floor((width + columnSpacing) / (300 + columnSpacing)))
                        columnSpacing: Theme.spacingL
                        rowSpacing: Theme.spacingL
                        Repeater {
                            model: InstrumentRegistry
                            delegate: TileFrame {
                                required instrumentId
                                Layout.fillWidth: true
                                Layout.preferredHeight: 196
                                onOpenRequested: home.workspace.open(instrumentId)
                            }
                        }
                    }
                }

                // Side column
                ColumnLayout {
                    Layout.preferredWidth: 340
                    Layout.maximumWidth: 340
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingL

                    DeviceCard {
                        Layout.fillWidth: true
                        onDeviceInfoRequested: home.deviceInfoRequested()
                    }

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("Recent sessions")
                        iconName: "clock"
                        Repeater {
                            model: SessionManager.recentSessions.slice(0, 5)
                            delegate: ItemDelegate {
                                id: recentItem
                                required property var modelData
                                Layout.fillWidth: true
                                enabled: recentItem.modelData.exists
                                onClicked: home.openRecentRequested(recentItem.modelData.file)
                                contentItem: ColumnLayout {
                                    spacing: 1
                                    Text {
                                        Layout.fillWidth: true
                                        text: recentItem.modelData.name
                                        color: Theme.text
                                        font.pixelSize: Theme.fontSizeBody
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: recentItem.modelData.path
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSizeS
                                        elide: Text.ElideMiddle
                                    }
                                }
                            }
                        }
                        Text {
                            visible: SessionManager.recentSessions.length === 0
                            Layout.fillWidth: true
                            text: qsTr("Save the workspace and every instrument's settings as a .mybench session to find it here.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeM
                            wrapMode: Text.WordWrap
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        title: qsTr("Shortcuts")
                        iconName: "zap"
                        Repeater {
                            model: [
                                { keys: "Ctrl+K", text: qsTr("Command palette") },
                                { keys: "Space", text: qsTr("Run / stop the focused instrument") },
                                { keys: "Ctrl+1", text: qsTr("Open instrument 1…9") },
                                { keys: "Ctrl+.", text: qsTr("Stop all") },
                                { keys: "F1", text: qsTr("Help") }
                            ]
                            delegate: RowLayout {
                                id: shortcutRow
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: shortcutRow.modelData.text
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeM
                                }
                                KeyHint { keys: shortcutRow.modelData.keys }
                            }
                        }
                    }
                }
            }
        }
    }
}
