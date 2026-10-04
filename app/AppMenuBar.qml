pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MyBench.Framework
import MyBench.Controls

// The menu bar (docs/UI.md §9): the global menu on macOS, in the window elsewhere (hidden with
// Ctrl+M). Every entry is an AppActions Action; the lists (recent files, instruments, the
// focused instrument's commands, devices, tools, resources) are filled live. Native menus
// ignore `visible`: entries that come and go are inserted and removed, platform-specific ones
// are removed once.
MenuBar {
    id: bar

    required property AppActions actions
    readonly property bool mac: AppInfo.platform === "macos"
    readonly property string revealText: mac ? qsTr("Show in Finder") : AppInfo.platform === "windows"
                                               ? qsTr("Show in Explorer") : qsTr("Show in Folder")

    // Items made by an Instantiator, kept in model order right before `before` (null: at the end
    // of `menu`). `leading` (optional) is a separator inserted before them while there are any.
    component MenuList: Instantiator {
        id: list
        required property Menu menu
        property QtObject before: null
        property MenuSeparator leading: null
        property var entries: []

        function indexIn(object: QtObject): int {
            for (let i = 0; i < menu.count; ++i)
                if (menu.itemAt(i) === object || menu.menuAt(i) === object)
                    return i
            return -1
        }
        function endIndex(): int {
            return before ? indexIn(before) : menu.count
        }
        onObjectAdded: (index, object) => {
            if (leading && entries.length === 0)
                menu.insertItem(endIndex(), leading)
            const position = endIndex() - entries.length + Math.min(index, entries.length)
            if (object instanceof Menu)
                menu.insertMenu(position, object as Menu)
            else
                menu.insertItem(position, object as Item)
            entries.splice(Math.min(index, entries.length), 0, object)
        }
        onObjectRemoved: (index, object) => {
            const at = indexIn(object)
            if (at >= 0) {
                if (object instanceof Menu)
                    menu.takeMenu(at)
                else
                    menu.takeItem(at)
            }
            const k = entries.indexOf(object)
            if (k >= 0)
                entries.splice(k, 1)
            if (leading && entries.length === 0 && indexIn(leading) >= 0)
                menu.takeItem(indexIn(leading))
        }
    }
    // A file of Open Recent: Open, Show in Finder, Pin/Unpin.
    component FileMenu: Menu {
        id: file
        required property var modelData
        readonly property bool pinned: Launcher.pinnedFiles.length >= 0 && Launcher.isPinned(modelData.path)
        title: modelData.name
        enabled: modelData.exists
        MenuItem { text: qsTr("Open"); onTriggered: Launcher.openFile(file.modelData.path) }
        MenuItem { text: bar.revealText; onTriggered: Launcher.showInFolder(file.modelData.path) }
        MenuItem {
            text: file.pinned ? qsTr("Unpin") : qsTr("Pin")
            onTriggered: file.pinned ? Launcher.unpinFile(file.modelData.path) : Launcher.pinFile(file.modelData.path)
        }
    }

    Menu {
        id: fileMenu
        title: qsTr("&File")
        MenuItem { action: bar.actions.newSession }
        MenuItem { action: bar.actions.openSession }
        Menu {
            id: recentMenu
            title: qsTr("Open Recent")
            MenuList {
                menu: recentMenu
                before: recentFilesSeparator
                model: SessionManager.recentSessions
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    enabled: modelData.exists
                    onTriggered: bar.actions.dialogs.openRecent(modelData.file)
                }
            }
            MenuSeparator { id: recentFilesSeparator }
            Menu {
                id: exportsMenu
                title: qsTr("Exported Files")
                MenuList {
                    menu: exportsMenu
                    before: clearExports
                    leading: null
                    model: Launcher.recentExports
                    delegate: FileMenu {}
                }
                MenuItem {
                    id: clearExports
                    text: qsTr("Clear Exported Files")
                    enabled: Launcher.recentExports.length > 0
                    onTriggered: Launcher.clearRecentExports()
                }
            }
            Menu {
                id: pinnedMenu
                title: qsTr("Pinned Files")
                MenuList {
                    menu: pinnedMenu
                    before: pinFile
                    model: Launcher.pinnedFiles
                    delegate: FileMenu {}
                }
                MenuItem {
                    id: pinFile
                    text: qsTr("Pin a File…")
                    onTriggered: pinDialog.open()
                }
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Clear Recent Sessions")
                enabled: SessionManager.recentSessions.length > 0
                onTriggered: SessionManager.clearRecentSessions()
            }
        }
        MenuSeparator {}
        MenuItem { action: bar.actions.saveSession }
        MenuItem { action: bar.actions.saveSessionAs }
        MenuItem { action: bar.actions.revertToSaved }
        MenuSeparator {}
        MenuItem { action: bar.actions.exportData }
        MenuItem { action: bar.actions.exportImage }
        MenuItem { action: bar.actions.printAction }
        MenuSeparator {}
        MenuItem { action: bar.actions.closeWindow }
        MenuSeparator {}
        MenuItem { action: bar.actions.preferencesAction }
        MenuItem { id: quitItem; action: bar.actions.quit }
    }

    Menu {
        title: qsTr("&Edit")
        MenuItem { action: bar.actions.cut }
        MenuItem { action: bar.actions.copy }
        MenuItem { action: bar.actions.paste }
        MenuSeparator {}
        MenuItem { action: bar.actions.selectAll }
    }

    Menu {
        id: viewMenu
        title: qsTr("&View")
        MenuItem { action: bar.actions.toggleSidebar }
        MenuItem { action: bar.actions.toggleInspector }
        MenuItem { action: bar.actions.toggleMeasurements }
        MenuItem { action: bar.actions.toggleStatusBar }
        MenuItem { id: menuBarItem; action: bar.actions.toggleMenuBar }
        MenuSeparator {}
        MenuItem { action: bar.actions.toggleSplit }
        MenuSeparator {}
        Menu {
            title: qsTr("Appearance")
            MenuItem { action: bar.actions.appearanceSystem }
            MenuItem { action: bar.actions.appearanceLight }
            MenuItem { action: bar.actions.appearanceDark }
        }
        MenuItem { action: bar.actions.alwaysDarkDisplays }
        MenuSeparator {}
        MenuItem { action: bar.actions.commandPalette }
        MenuSeparator {}
        MenuItem { action: bar.actions.fullScreenAction }
    }

    Menu {
        id: instrumentMenu
        title: qsTr("&Instrument")
        MenuItem { action: bar.actions.showOverview }
        MenuSeparator {}
        MenuList {
            menu: instrumentMenu
            before: panesEnd
            model: bar.actions.paneActions
            delegate: MenuItem {
                required property Action modelData
                action: modelData
            }
        }
        MenuSeparator { id: panesEnd }
        MenuItem { action: bar.actions.nextInstrument }
        MenuItem { action: bar.actions.previousInstrument }
        MenuSeparator {}
        MenuItem { action: bar.actions.runStop }
        MenuItem { action: bar.actions.single }
        MenuItem { action: bar.actions.stopAll }
        MenuSeparator {}
        MenuItem { action: bar.actions.releaseOutputs }
        MenuItem { action: bar.actions.resetSettings }
        MenuItem { id: resetAllItem; action: bar.actions.resetAllInstruments }
        // The focused instrument's commands, after a separator while there are any.
        MenuList {
            menu: instrumentMenu
            leading: MenuSeparator {}
            model: bar.actions.commandActions
            delegate: MenuItem {
                required property Action modelData
                action: modelData
            }
        }
    }

    // Devices and device commands (MyBench.Controls DeviceMenu); switching and reset ask through
    // the toolbar's device button.
    DeviceMenu {
        title: qsTr("&Device")
        onSelectRequested: deviceId => {
            if (bar.actions.deviceButton)
                bar.actions.deviceButton.requestSelect(deviceId)
        }
        onResetRequested: bar.actions.resetDevice.trigger()
        onDeviceInfoRequested: bar.actions.deviceInfo.trigger()
        onSimulatorSettingsRequested: bar.actions.simulatorSettings.trigger()
    }

    Menu {
        id: toolsMenu
        title: qsTr("&Tools")
        MenuList {
            menu: toolsMenu
            before: editToolsItem
            leading: null
            model: Launcher.tools
            delegate: MenuItem {
                required property var modelData
                required property int index
                text: modelData.name
                enabled: modelData.exists !== false
                onTriggered: Launcher.launchTool(index)
            }
        }
        MenuItem { id: editToolsItem; action: bar.actions.editTools }
    }

    // macOS only (removed elsewhere).
    Menu {
        id: windowMenu
        title: qsTr("&Window")
        MenuItem { action: bar.actions.minimize }
        MenuItem { action: bar.actions.zoom }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Bring All to Front")
            onTriggered: {
                const window = bar.Window.window
                if (window)
                    window.raise()
            }
        }
    }

    Menu {
        id: helpMenu
        title: qsTr("&Help")
        MenuItem { action: bar.actions.help }
        MenuItem { action: bar.actions.instrumentHelp }
        MenuItem { action: bar.actions.keyboardShortcuts }
        MenuList {
            menu: helpMenu
            before: eventLogSeparator
            leading: MenuSeparator {}
            model: Launcher.resources
            delegate: MenuItem {
                required property var modelData
                text: modelData.title
                onTriggered: Launcher.openUrl(modelData.url)
            }
        }
        MenuSeparator { id: eventLogSeparator }
        MenuItem { action: bar.actions.eventLog }
        MenuSeparator {}
        MenuItem { action: bar.actions.about }
    }

    FileDialog {
        id: pinDialog
        title: qsTr("Pin a File")
        fileMode: FileDialog.OpenFiles
        onAccepted: {
            for (const file of selectedFiles)
                Launcher.pinFile(file)
        }
    }

    Component.onCompleted: {
        if (mac) {
            fileMenu.removeItem(quitItem)        // the application menu has Quit
            viewMenu.removeItem(menuBarItem)     // the global menu bar cannot be hidden
        } else {
            removeMenu(windowMenu)
        }
    }
}
