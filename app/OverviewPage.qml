pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Overview, the first sidebar entry (docs/UI.md §7): the device, the recent sessions and every
// instrument as a uniform tile (pinned ones first). Fits one screen at 1280×800.
Rectangle {
    id: page

    required property WorkspaceState workspace
    required property AppActions actions

    readonly property bool wideTop: width >= 900
    // Pinned instruments first ("Pin to Top"), then the sidebar order.
    readonly property var tileIds: {
        const ids = InstrumentRegistry.ids
        const pinned = Launcher.favorites.filter(id => ids.indexOf(id) >= 0)
        return pinned.concat(ids.filter(id => pinned.indexOf(id) < 0))
    }

    function shortDate(value: var): string {
        if (!value)
            return ""
        const date = new Date(value)
        if (isNaN(date.getTime()))
            return ""
        const now = new Date()
        if (date.toDateString() === now.toDateString())
            return Qt.formatTime(date, Qt.locale().timeFormat(Locale.ShortFormat))
        return date.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }

    color: Theme.base

    component Heading: Text {
        color: Theme.text
        font.pixelSize: Theme.fontSizeHeading
        font.weight: Font.DemiBold
    }
    component Secondary: Text {
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        elide: Text.ElideRight
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * Theme.spacingXl
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            x: Theme.spacingXl
            y: Theme.spacingXl
            width: flick.width - 2 * Theme.spacingXl
            spacing: Theme.spacingXl

            // --- Device and recent sessions --------------------------------------------------
            GridLayout {
                Layout.fillWidth: true
                columns: page.wideTop ? 2 : 1
                columnSpacing: Theme.spacingXl * 2
                rowSpacing: Theme.spacingXl

                // Device: state, identity, actions; a first-run hint without a device.
                ColumnLayout {
                    objectName: "overviewDevice"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingXs

                    Heading { text: qsTr("Device") }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingXs
                        spacing: Theme.spacingS
                        Icon {
                            name: DeviceHub.simulated ? "cpu" : "usb"
                            size: Theme.iconSizeL
                            color: Theme.textSecondary
                        }
                        Text {
                            text: DeviceHub.currentId.length > 0 ? DeviceHub.name : qsTr("No device")
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Font.Medium
                        }
                        Rectangle {
                            Layout.leftMargin: Theme.spacingXs
                            implicitWidth: Theme.spacingS - 1
                            implicitHeight: implicitWidth
                            radius: width / 2
                            color: DeviceHub.connectionState === DeviceHub.Connected ? Theme.positive
                                 : DeviceHub.connectionState === DeviceHub.Resetting ? Theme.caution : Theme.negative
                        }
                        Secondary {
                            Layout.fillWidth: true
                            text: DeviceHub.connectionState === DeviceHub.Connected ? qsTr("Connected")
                                : DeviceHub.connectionState === DeviceHub.Resetting ? qsTr("Resetting…")
                                : DeviceHub.connectionState === DeviceHub.Disconnected ? qsTr("Disconnected — waiting for the device")
                                : qsTr("Not connected")
                        }
                    }
                    Secondary {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.iconSizeL + Theme.spacingS
                        visible: DeviceHub.currentId.length > 0
                        text: [DeviceHub.productType,
                               DeviceHub.serialNumber.length > 0 ? qsTr("Serial %1").arg(DeviceHub.serialNumber) : "",
                               DeviceHub.simulated ? qsTr("Simulated") : DeviceHub.backend]
                              .filter(s => s.length > 0).join(" · ")
                    }
                    // First run: no myDAQ connected.
                    Secondary {
                        objectName: "firstRunHint"
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.iconSizeL + Theme.spacingS
                        visible: !DeviceHub.connected && !DeviceHub.simulated
                        text: qsTr("Connect a myDAQ by USB")
                    }
                    // The simulated device's wiring (why AI 0 / AI 1 show what AO 0 generates).
                    SimWiring {
                        Layout.leftMargin: Theme.iconSizeL + Theme.spacingS
                        Layout.topMargin: Theme.spacingXs
                        Layout.maximumWidth: 320
                        Layout.fillWidth: true
                        visible: DeviceHub.simulated
                        background: Theme.base
                    }
                    Flow {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingS
                        spacing: Theme.spacingS
                        Button {
                            visible: !DeviceHub.connected && DeviceHub.simulatorAvailable && !DeviceHub.simulated
                            text: qsTr("Use Simulated Device")
                            onClicked: page.actions.simulatedDevice.trigger()
                        }
                        Button { action: page.actions.deviceInfo }
                        Button { action: page.actions.resetDevice }
                        Button {
                            objectName: "overviewResetAll"
                            text: qsTr("Reset All…")
                            icon.source: Theme.icon("reset")
                            onClicked: page.actions.resetAllInstruments.trigger()
                            ToolTip.visible: hovered
                            ToolTip.delay: Theme.toolTipDelay
                            ToolTip.text: qsTr("Reset All Instruments to Defaults…")
                        }
                        Button {
                            visible: DeviceHub.simulated
                            action: page.actions.simulatorSettings
                        }
                    }
                }

                // Recent sessions (up to five).
                ColumnLayout {
                    objectName: "overviewSessions"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: Theme.spacingXs
                        Heading { Layout.fillWidth: true; text: qsTr("Recent Sessions") }
                        Button { flat: true; action: page.actions.openSession }
                    }
                    Secondary {
                        id: noSessions
                        visible: SessionManager.recentSessions.length === 0
                        Layout.fillWidth: true
                        text: qsTr("No saved sessions")
                        HoverHandler { id: noSessionsHover }
                        ToolTip.visible: noSessionsHover.hovered
                        ToolTip.delay: Theme.toolTipDelay
                        ToolTip.text: qsTr("A session keeps the layout and every instrument's settings.")
                    }
                    Repeater {
                        model: SessionManager.recentSessions.slice(0, 5)
                        delegate: ItemDelegate {
                            id: sessionRow
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: -Theme.spacingS
                            Layout.rightMargin: -Theme.spacingS
                            enabled: modelData.exists
                            onClicked: page.actions.dialogs.openRecent(modelData.file)
                            ToolTip.visible: hovered
                            ToolTip.delay: Theme.toolTipDelay
                            ToolTip.text: modelData.path
                            contentItem: RowLayout {
                                spacing: Theme.spacingS
                                Icon { name: "layers"; color: Theme.textSecondary }
                                Text {
                                    text: sessionRow.modelData.name
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeBody
                                }
                                Secondary {
                                    Layout.fillWidth: true
                                    text: sessionRow.modelData.path
                                    elide: Text.ElideMiddle
                                }
                                Secondary {
                                    text: page.shortDate(sessionRow.modelData.modified)
                                    font.features: Theme.tabularFigures
                                }
                            }
                        }
                    }
                }
            }

            // --- Instruments -------------------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingM
                Heading { text: qsTr("Instruments") }
                GridLayout {
                    id: grid
                    objectName: "overviewTiles"
                    readonly property int minimumTileWidth: 236
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor((width + columnSpacing) / (minimumTileWidth + columnSpacing)))
                    columnSpacing: Theme.spacingM
                    rowSpacing: Theme.spacingM
                    Repeater {
                        model: page.tileIds
                        delegate: OverviewTileFrame {
                            id: tile
                            required property string modelData
                            readonly property var info: InstrumentRegistry.info(modelData)
                            readonly property InstrumentController controller: InstrumentRegistry.controller(modelData)
                            objectName: "overviewTile-" + modelData
                            paneId: modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: grid.minimumTileWidth
                            Layout.preferredHeight: 132
                            title: info.title ?? modelData
                            iconName: info.icon ?? ""
                            keyValue: controller ? controller.keyValue : ""
                            status: !controller ? "" : controller.paused ? "paused"
                                  : controller.state === InstrumentController.Starting ? "starting"
                                  : controller.running ? "running" : controller.hasError ? "error" : ""
                            running: controller !== null && controller.running
                            canToggle: controller !== null && (controller.running || controller.canRun)
                            pinned: Launcher.favorites.indexOf(modelData) >= 0
                            preview: (info.tileUrl ?? "").toString().length > 0 ? tilePreview : null
                            menu: tileMenu
                            onClicked: page.workspace.show(modelData)
                            onRunToggled: controller.toggle()

                            Component {
                                id: tilePreview
                                Loader {
                                    asynchronous: true
                                    Component.onCompleted: setSource(tile.info.tileUrl, { controller: tile.controller })
                                }
                            }
                            Menu {
                                id: tileMenu
                                MenuItem { text: qsTr("Show"); onTriggered: page.workspace.show(tile.modelData) }
                                MenuItem { text: qsTr("Show Beside"); onTriggered: page.workspace.showBeside(tile.modelData) }
                                MenuSeparator {}
                                MenuItem {
                                    text: tile.running ? qsTr("Stop") : qsTr("Run")
                                    enabled: tile.canToggle
                                    onTriggered: tile.controller.toggle()
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: tile.pinned ? qsTr("Unpin") : qsTr("Pin to Top")
                                    onTriggered: Launcher.toggleFavorite(tile.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
