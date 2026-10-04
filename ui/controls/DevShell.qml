pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import MyBench.Framework

// The window of the dev executables (mybench-<id>): the app's frame reduced to one instrument —
// a toolbar (instrument, running count, device button, inspector toggle), the pane, the
// InspectorDock and a status line — so dev screenshots match the app (docs/UI.md §7).
// `--tile` shows the Overview tile instead, `--show-help` opens Help in the dock, `--theme
// light|dark` picks the theme. Space runs/stops, Shift+Space is Single, F1 Help, Ctrl+I the dock.
ApplicationWindow {
    id: window

    readonly property string instrumentId: AppInfo.instrument
    readonly property var info: InstrumentRegistry.info(instrumentId)
    readonly property InstrumentController controller: InstrumentRegistry.controller(instrumentId)
    readonly property bool tileMode: AppInfo.flag("tile")
    property InstrumentPanel pane: null
    property bool inspectorShown: true

    width: 1280
    height: 800
    minimumWidth: Theme.minWindowWidth
    minimumHeight: Theme.minWindowHeight
    visible: true
    title: (info.title ?? instrumentId) + " — " + AppInfo.name
    color: Theme.window

    Settings {
        id: prefs
        category: "appearance"
        property string themeMode: "dark"
    }
    // Screenshots without --theme are dark (the default), whatever a stored preference says.
    ThemeSync { mode: AppInfo.theme.length > 0 ? AppInfo.theme : AppInfo.screenshot ? "dark" : prefs.themeMode }

    Component.onCompleted: {
        if (controller !== null && !tileMode) {
            PanelRegistry.whenReady(instrumentId, panel => window.pane = panel as InstrumentPanel)
            panelLoader.setSource(info.panelUrl, { controller: controller })
        }
        if (AppInfo.flag("show-help"))
            dock.showHelp(info.helpId ?? "app")
    }

    Connections {
        target: Help
        function onShowRequested(id: string) {
            window.inspectorShown = true
            dock.showHelp(id)
        }
    }

    // A text field or an open popup keeps Space for itself.
    readonly property bool spaceAllowed: {
        const item = activeFocusItem
        return controller !== null && !(item instanceof TextInput) && !(item instanceof TextEdit)
    }
    Shortcut {
        sequences: ["Space"]
        enabled: window.spaceAllowed
        onActivated: window.controller.toggle()
    }
    Shortcut {
        sequences: ["Shift+Space"]
        enabled: window.spaceAllowed && window.controller.supportsSingle && window.controller.singleEnabled
        onActivated: window.controller.runOnce()
    }
    Shortcut {
        sequences: Theme.macShortcuts ? [StandardKey.HelpContents, "F1"] : [StandardKey.HelpContents]
        onActivated: dock.showHelp(window.info.helpId ?? "app")
    }
    Shortcut {
        sequences: ["Ctrl+I"]
        onActivated: window.inspectorShown = !window.inspectorShown
    }

    header: Rectangle {
        implicitHeight: Theme.toolbarHeight
        color: Theme.window

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingL
            anchors.rightMargin: Theme.spacingS
            spacing: Theme.spacingS

            Text {
                text: AppInfo.name
                color: Theme.text
                font.pixelSize: Theme.fontSizeHeading
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("%1 · development build").arg(window.info.title ?? window.instrumentId)
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
            }
            Button {
                visible: InstrumentRegistry.anyStoppable
                flat: true
                text: InstrumentRegistry.runningCount > 0
                      ? qsTr("%n running · Stop All", "", InstrumentRegistry.runningCount) : qsTr("Stop All")
                onClicked: InstrumentRegistry.stopAll()
            }
            DeviceButton {
                Layout.maximumWidth: 280
            }
            ToolButton {
                objectName: "inspectorToggle"
                visible: !window.tileMode
                icon.source: Theme.icon("sidebar-right")
                checkable: true
                checked: window.inspectorShown
                onClicked: window.inspectorShown = !window.inspectorShown
                Accessible.name: window.inspectorShown ? qsTr("Hide Inspector") : qsTr("Show Inspector")
                ToolTip.visible: hovered
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: Accessible.name + "   " + Theme.keys("Ctrl+I")
            }
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.separator
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Text {
                anchors.centerIn: parent
                visible: window.controller === null
                text: qsTr("Unknown instrument “%1”. Registered: %2").arg(window.instrumentId)
                      .arg(InstrumentRegistry.ids.join(", "))
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }

            Loader {
                id: panelLoader
                anchors.fill: parent
            }

            // --tile: the Overview tile at its two sizes.
            Rectangle {
                anchors.fill: parent
                visible: window.controller !== null && window.tileMode
                color: Theme.base
                Row {
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
        }

        InspectorDock {
            id: dock
            // Hidden for a panel without inspector unless its help is open (as in the app).
            visible: window.inspectorShown && !window.tileMode
                     && (window.pane === null || window.pane.hasInspector || dock.page === "help")
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.inspectorWidth
            panel: window.pane
            paneShown: window.controller !== null
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.statusBarHeight
        color: Theme.window

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.separator
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingM
            anchors.rightMargin: Theme.spacingM
            anchors.topMargin: 1
            spacing: Theme.spacingM
            ResourceChips {
                textForm: true
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            Text {
                visible: window.controller !== null && window.controller.running && window.controller.throughput > 0
                text: Units.format(window.controller ? window.controller.throughput : 0, "S/s", 3)
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeSmall
                font.features: Theme.tabularFigures
            }
        }
    }

    ToastArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingL
    }
}
