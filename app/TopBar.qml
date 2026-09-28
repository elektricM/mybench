pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// Top bar: device selector and status, resource chips, command palette, Stop all, session
// menu, dashboard pin, theme, help. Doubles as the window's title bar on macOS.
Rectangle {
    id: bar

    required property WorkspaceState workspace

    signal paletteRequested()
    signal moveWindowRequested()
    signal themeToggleRequested()
    signal newSessionRequested()
    signal openSessionRequested()
    signal openRecentRequested(url file)
    signal saveSessionRequested()
    signal saveSessionAsRequested()
    signal deviceInfoRequested()
    signal revertAllRequested()

    implicitHeight: Theme.topBarHeight
    color: Theme.chrome

    DragHandler {
        target: null
        onActiveChanged: if (active) bar.moveWindowRequested()
    }
    TapHandler {
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: {
            const w = bar.Window.window
            if (w)
                w.visibility = w.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
        }
    }

    Divider {
        anchors.bottom: parent.bottom
        width: parent.width
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingM
        spacing: Theme.spacingS + 2

        DeviceSelector {
            Layout.preferredWidth: 236
            onDeviceInfoRequested: bar.deviceInfoRequested()
        }
        ResourceChips {
            Layout.leftMargin: Theme.spacingXs
            onInstrumentClicked: id => bar.workspace.open(id)
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Command palette entry.
            T.AbstractButton {
                id: paletteButton
                anchors.centerIn: parent
                width: Math.min(parent.width - 16, 340)
                height: 30
                visible: parent.width > 180
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                onClicked: bar.paletteRequested()
                Accessible.name: qsTr("Command palette")
                background: Rectangle {
                    radius: Theme.radiusM
                    color: paletteButton.hovered ? Theme.surfaceHover : Theme.surfaceSunken
                    border.width: 1
                    border.color: paletteButton.hovered ? Theme.borderStrong : Theme.border
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    FocusRing { shown: paletteButton.visualFocus; radius: Theme.radiusM + 3 }
                }
                contentItem: Item {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8
                        Icon {
                            name: "search"
                            size: Theme.iconSizeS
                            color: Theme.textMuted
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Search or run a command…")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeBody
                            elide: Text.ElideRight
                        }
                        KeyHint { keys: "Ctrl+K" }
                    }
                }
            }
        }

        ActionButton {
            text: qsTr("Stop all")
            iconName: "stop-filled"
            compact: true
            tone: InstrumentRegistry.anyStoppable ? "subtle" : "ghost"
            color: Theme.danger
            enabled: InstrumentRegistry.anyStoppable
            toolTip: qsTr("Stop every running instrument")
            shortcutText: Theme.keys("Ctrl+.")
            onClicked: InstrumentRegistry.stopAll()
        }

        // Session menu.
        ActionButton {
            id: sessionButton
            tone: "ghost"
            compact: true
            iconName: "layers"
            text: SessionManager.currentName + (SessionManager.modified ? "  •" : "")
            toolTip: SessionManager.currentFile.toString().length > 0 ? SessionManager.currentFile.toString() : qsTr("Session")
            onClicked: sessionMenu.popup(sessionButton, 0, sessionButton.height + 4)

            Menu {
                id: sessionMenu
                MenuItem { text: qsTr("New Session"); icon.source: Theme.icon("file-plus"); onTriggered: bar.newSessionRequested() }
                MenuItem { text: qsTr("Open Session…"); icon.source: Theme.icon("folder-open"); onTriggered: bar.openSessionRequested() }
                Menu {
                    title: qsTr("Open Recent")
                    enabled: SessionManager.recentSessions.length > 0
                    Repeater {
                        model: SessionManager.recentSessions
                        delegate: MenuItem {
                            required property var modelData
                            text: modelData.name
                            enabled: modelData.exists
                            onTriggered: bar.openRecentRequested(modelData.file)
                        }
                    }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Clear Recent"); onTriggered: SessionManager.clearRecentSessions() }
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Save Session"); icon.source: Theme.icon("save"); onTriggered: bar.saveSessionRequested() }
                MenuItem { text: qsTr("Save Session As…"); onTriggered: bar.saveSessionAsRequested() }
                MenuSeparator {}
                MenuItem { text: qsTr("Revert All to Defaults…"); icon.source: Theme.icon("reset"); onTriggered: bar.revertAllRequested() }
            }
        }

        Divider {
            vertical: true
            length: 22
        }

        IconButton {
            iconName: "sidebar-right"
            checkable: true
            checked: bar.workspace.dashboardPinned
            toolTip: bar.workspace.dashboardPinned ? qsTr("Unpin dashboard") : qsTr("Pin dashboard")
            shortcutText: Theme.keys("Ctrl+Shift+D")
            onClicked: bar.workspace.toggleDashboard()
        }
        IconButton {
            iconName: Theme.dark ? "sun" : "moon"
            toolTip: Theme.dark ? qsTr("Light theme") : qsTr("Dark theme")
            shortcutText: Theme.keys("Ctrl+Shift+L")
            onClicked: bar.themeToggleRequested()
        }
        IconButton {
            iconName: "help-circle"
            checked: bar.workspace.helpOpen
            toolTip: qsTr("Help")
            shortcutText: "F1"
            onClicked: bar.workspace.toggleHelp()
        }
    }
}
