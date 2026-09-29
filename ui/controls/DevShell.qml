pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import MyBench.Framework

// Minimal shell of the dev executables (mybench-<id>): one instrument panel with the device
// selector, resource chips, help drawer and toasts. `--tile` shows the dashboard tiles instead,
// `--show-help` opens the help drawer, `--theme light|dark` picks the theme.
ApplicationWindow {
    id: window

    readonly property string instrumentId: AppInfo.instrument
    readonly property var info: InstrumentRegistry.info(instrumentId)
    readonly property InstrumentController controller: InstrumentRegistry.controller(instrumentId)
    readonly property bool tileMode: AppInfo.flag("tile")
    property bool helpOpen: AppInfo.flag("show-help")
    property bool themeFromCommandLine: AppInfo.theme.length > 0

    width: 1280
    height: 800
    minimumWidth: 880
    minimumHeight: 540
    visible: true
    title: (info.title ?? instrumentId) + " — " + AppInfo.name
    color: Theme.window

    Settings {
        id: prefs
        category: "appearance"
        property string themeMode: "system"
    }
    ThemeSync { mode: window.themeFromCommandLine ? AppInfo.theme : prefs.themeMode }

    function toggleTheme() {
        prefs.themeMode = Theme.dark ? "light" : "dark"
        themeFromCommandLine = false
    }

    Connections {
        target: Help
        function onShowRequested(id: string) {
            helpView.show(id)
            window.helpOpen = true
        }
    }

    Shortcut {
        sequences: ["Space"]
        enabled: window.controller !== null
        onActivated: window.controller.toggle()
    }
    Shortcut {
        sequences: Theme.macShortcuts ? [StandardKey.HelpContents, "F1"] : [StandardKey.HelpContents]
        onActivated: window.helpOpen = !window.helpOpen
    }

    header: Rectangle {
        implicitHeight: Theme.topBarHeight
        color: Theme.chrome

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingL
            anchors.rightMargin: Theme.spacingM
            spacing: Theme.spacingM

            Icon {
                name: "app-logo"
                size: 22
                color: Theme.accent
            }
            Text {
                text: AppInfo.name
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.Bold
                font.letterSpacing: 0.4
            }
            Text {
                text: "/"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeL
            }
            Icon {
                name: window.info.icon ?? ""
                color: Theme.textSecondary
            }
            Text {
                text: window.info.title ?? window.instrumentId
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
            }
            Badge {
                text: qsTr("Dev")
                color: Theme.textSecondary
            }

            Item { Layout.fillWidth: true }

            ResourceChips {}
            DeviceSelector {
                Layout.preferredWidth: 250
            }
            Divider {
                vertical: true
                length: 22
            }
            IconButton {
                iconName: Theme.dark ? "sun" : "moon"
                toolTip: Theme.dark ? qsTr("Light theme") : qsTr("Dark theme")
                onClicked: window.toggleTheme()
            }
            IconButton {
                iconName: "help-circle"
                checked: window.helpOpen
                toolTip: qsTr("Help")
                shortcutText: "F1"
                onClicked: window.helpOpen = !window.helpOpen
            }
        }
        Divider {
            anchors.bottom: parent.bottom
            width: parent.width
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            EmptyState {
                anchors.centerIn: parent
                visible: window.controller === null
                iconName: "warning"
                title: qsTr("Unknown instrument “%1”").arg(window.instrumentId)
                text: qsTr("Registered: %1").arg(InstrumentRegistry.ids.join(", "))
            }

            Loader {
                id: panelLoader
                anchors.fill: parent
                active: window.controller !== null && !window.tileMode
                Component.onCompleted: {
                    if (active)
                        setSource(window.info.panelUrl, { controller: window.controller })
                }
            }

            // --tile: the dashboard tile at its two sizes (Home and pinned column).
            Row {
                visible: window.controller !== null && window.tileMode
                anchors.centerIn: parent
                spacing: Theme.spacingXl
                TileFrame {
                    width: 320
                    height: 200
                    instrumentId: window.instrumentId
                }
                TileFrame {
                    width: 280
                    height: 156
                    compact: true
                    instrumentId: window.instrumentId
                }
            }
        }

        HelpView {
            id: helpView
            visible: window.helpOpen
            Layout.fillHeight: true
            Layout.preferredWidth: 400
            topic: window.info.helpId ?? "app"
            onCloseRequested: window.helpOpen = false
        }
    }

    ToastArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingL
    }
}
