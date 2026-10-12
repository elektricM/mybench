pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Framework
import MyBench.Controls

// Every command of the shell, once (docs/UI.md §9). The menu bar, the context menus, the toolbar
// and pane buttons, the command palette and the keyboard all use these Actions; nothing else
// defines a shortcut. Things only the window can do are requested through signals.
//
// Space and Shift+Space (Run/Stop and Single of the focused pane) are window Shortcuts gated by
// ShellKeys.spaceAllowed(), never menu keys: a native menu would take them from text fields.
Item {
    id: actions

    required property WorkspaceState workspace
    required property ShellDialogs dialogs
    required property AppPreferences preferences
    // The toolbar's device button: device switching and reset ask through it.
    property DeviceButton deviceButton: null
    // Window state, bound by the shell.
    property bool sidebarShown: workspace.sidebarShown
    property bool inspectorShown: workspace.inspectorShown
    // The focused pane has something to show in the inspector (settings or help). Data Logger,
    // Counter/Timer and Waveform Generator keep their controls beside the display: no inspector.
    property bool inspectorAvailable: true
    property bool fullScreen: false
    property bool paletteOpen: false
    property bool menuBarShown: preferences.view.menuBarShown

    readonly property bool mac: AppInfo.platform === "macos"
    readonly property string focusedPane: workspace.overview ? "" : workspace.focused
    readonly property InstrumentController focusedController: focusedPane.length > 0
        ? InstrumentRegistry.controller(workspace.instrumentOf(focusedPane)) : null
    readonly property string focusedTitle: focusedPane.length === 0 ? ""
        : focusedPane.indexOf("/") >= 0 ? (InstrumentRegistry.viewInfo(focusedPane).title ?? "")
        : (InstrumentRegistry.info(focusedPane).title ?? "")
    // The loaded panel of the focused pane (null while it loads).
    readonly property InstrumentPanel focusedPanel: PanelRegistry.revision >= 0 && focusedPane.length > 0
                                                    ? PanelRegistry.panel(focusedPane) as InstrumentPanel : null

    signal paletteRequested()
    signal eventLogRequested()
    signal sidebarToggleRequested()
    signal fullScreenToggleRequested()
    signal closeWindowRequested()
    signal minimizeRequested()
    signal zoomRequested()
    // A command that needs a value first (palette argument mode).
    signal argumentRequested(var command)

    // An Action with its shortcut spec (`keys`: a string or a StandardKey), palette metadata and
    // the text the palette shows for it.
    component ShellAction: Action {
        id: shellAction
        property string iconName                 // assets/icons name
        property var keys: undefined
        property string keywords
        property string paletteText: text
        property bool inPalette: true
        // The shortcut as portable text ("Ctrl+S", "Meta+Ctrl+S") for key hints.
        readonly property string keysText: shellAction.keys === undefined ? "" : KeyText.portable(shellAction.keys)
        shortcut: keys === undefined ? "" : keys
        icon.source: iconName.length > 0 ? Theme.icon(iconName) : ""
    }

    // --- Runs a command on a pane, once its panel is loaded (PanelRegistry.whenReady) --------------
    function onPanel(paneId: string, fn: var) {
        if (paneId.length === 0)
            return
        workspace.show(paneId)
        PanelRegistry.whenReady(paneId, fn)
    }
    function callPanel(paneId: string, method: string) {
        onPanel(paneId, panel => {
            if (typeof panel[method] === "function")
                panel[method]()
        })
    }
    // Runs instrument command `commandId` of `instrumentId` once its panel is loaded: commands
    // handled in QML (commandTriggered) are only connected then. Runs exactly once.
    function invokeCommand(instrumentId: string, commandId: string, argument: var) {
        const controller = InstrumentRegistry.controller(instrumentId)
        if (!controller)
            return
        onPanel(instrumentId, () => controller.invokeCommand(commandId, argument))
    }
    function paneTitle(paneId: string): string {
        return paneId.indexOf("/") >= 0 ? (InstrumentRegistry.viewInfo(paneId).title ?? paneId)
                                        : (InstrumentRegistry.info(paneId).title ?? paneId)
    }
    function paneIcon(paneId: string): string {
        return paneId.indexOf("/") >= 0 ? (InstrumentRegistry.viewInfo(paneId).icon ?? "")
                                        : (InstrumentRegistry.info(paneId).icon ?? "")
    }
    function textEdit(): var {
        const item = Window.activeFocusItem
        return item && canCall(item, "selectAll") && canCall(item, "paste") ? item : null
    }
    // Duck-typed calls (text fields, panels with copy()); names resolved at run time.
    function canCall(object: var, name: string): bool {
        return object !== null && object !== undefined && typeof object[name] === "function"
    }
    function callIf(object: var, name: string): bool {
        if (!canCall(object, name))
            return false
        object[name]()
        return true
    }

    // --- File ----------------------------------------------------------------------------------------
    property ShellAction newSession: ShellAction {
        text: qsTr("New Session"); iconName: "file-plus"; keys: StandardKey.New
        onTriggered: actions.dialogs.newSession()
    }
    property ShellAction openSession: ShellAction {
        text: qsTr("Open…"); paletteText: qsTr("Open Session…"); iconName: "folder-open"; keys: StandardKey.Open
        onTriggered: actions.dialogs.openSession()
    }
    property ShellAction saveSession: ShellAction {
        text: qsTr("Save"); paletteText: qsTr("Save Session"); iconName: "save"; keys: StandardKey.Save
        onTriggered: actions.dialogs.saveSession()
    }
    property ShellAction saveSessionAs: ShellAction {
        text: qsTr("Save As…"); paletteText: qsTr("Save Session As…"); keys: "Ctrl+Shift+S"
        onTriggered: actions.dialogs.saveSessionAs()
    }
    property ShellAction revertToSaved: ShellAction {
        text: qsTr("Revert to Saved"); iconName: "undo"
        enabled: actions.dialogs.sessionGuard.needsPrompt
        onTriggered: actions.dialogs.revertToSaved()
    }
    property ShellAction exportData: ShellAction {
        text: qsTr("Export Data…"); iconName: "download"; keys: "Ctrl+E"; keywords: "csv save log"
        enabled: actions.focusedController !== null
        onTriggered: actions.callPanel(actions.focusedPane, "exportData")
    }
    property ShellAction exportImage: ShellAction {
        text: qsTr("Export Image…"); iconName: "image"; keys: "Ctrl+Shift+E"; keywords: "png pdf screenshot"
        enabled: actions.focusedController !== null
        onTriggered: actions.onPanel(actions.focusedPane, panel => {
            if (typeof panel.exportImage === "function")
                panel.exportImage(false)
        })
    }
    property ShellAction printAction: ShellAction {
        text: qsTr("Print…"); iconName: "printer"; keys: StandardKey.Print; keywords: "paper pdf"
        enabled: actions.focusedController !== null
        onTriggered: actions.callPanel(actions.focusedPane, "printPanel")
    }
    property ShellAction closeWindow: ShellAction {
        text: qsTr("Close Window"); keys: StandardKey.Close; inPalette: false
        onTriggered: actions.closeWindowRequested()
    }
    property ShellAction preferencesAction: ShellAction {
        text: qsTr("Preferences…"); iconName: "settings"; keys: "Ctrl+,"; keywords: "settings options"
        onTriggered: actions.dialogs.showPreferences("")
    }
    property ShellAction quit: ShellAction {
        // macOS: the application menu's Quit owns ⌘Q.
        text: qsTr("Quit %1").arg(AppInfo.name); keys: actions.mac ? undefined : StandardKey.Quit; inPalette: false
        onTriggered: actions.closeWindowRequested()
    }

    // --- Edit (the focused text field, else the focused pane) ------------------------------------
    property ShellAction cut: ShellAction {
        text: qsTr("Cut"); keys: StandardKey.Cut; inPalette: false
        onTriggered: actions.callIf(actions.textEdit(), "cut")
    }
    property ShellAction copy: ShellAction {
        text: qsTr("Copy"); iconName: "copy"; keys: StandardKey.Copy; inPalette: false
        onTriggered: {
            if (!actions.callIf(actions.textEdit(), "copy"))
                actions.callIf(actions.focusedPanel, "copy")
        }
    }
    property ShellAction paste: ShellAction {
        text: qsTr("Paste"); keys: StandardKey.Paste; inPalette: false
        onTriggered: actions.callIf(actions.textEdit(), "paste")
    }
    property ShellAction selectAll: ShellAction {
        text: qsTr("Select All"); keys: StandardKey.SelectAll; inPalette: false
        onTriggered: actions.callIf(actions.textEdit(), "selectAll")
    }

    // --- View ----------------------------------------------------------------------------------------
    property ShellAction toggleSidebar: ShellAction {
        text: qsTr("Show Sidebar"); paletteText: checked ? qsTr("Hide Sidebar") : qsTr("Show Sidebar")
        iconName: "sidebar-right"; keys: actions.mac ? "Meta+Ctrl+S" : "Ctrl+Alt+S"
        checkable: true; checked: actions.sidebarShown
        onTriggered: actions.sidebarToggleRequested()
    }
    property ShellAction toggleInspector: ShellAction {
        text: qsTr("Show Inspector"); paletteText: checked ? qsTr("Hide Inspector") : qsTr("Show Inspector")
        iconName: "sidebar-right"; keys: actions.mac ? "Alt+Ctrl+I" : "Ctrl+I"; keywords: "settings panel"
        checkable: true; checked: actions.inspectorShown && actions.inspectorAvailable
        enabled: actions.inspectorAvailable
        onTriggered: actions.workspace.setInspectorShown(!actions.inspectorShown)
    }
    property ShellAction toggleMeasurements: ShellAction {
        readonly property bool available: actions.focusedPanel !== null
        text: qsTr("Show Measurements"); paletteText: checked ? qsTr("Hide Measurements") : qsTr("Show Measurements")
        checkable: true; enabled: available
        checked: available && actions.focusedPanel.measurementsVisible === true
        onTriggered: actions.focusedPanel.setMeasurementsVisible(!actions.focusedPanel.measurementsVisible)
    }
    property ShellAction toggleStatusBar: ShellAction {
        text: qsTr("Show Status Bar"); paletteText: checked ? qsTr("Hide Status Bar") : qsTr("Show Status Bar")
        checkable: true; checked: actions.preferences.view.statusBarShown
        onTriggered: actions.preferences.view.statusBarShown = !actions.preferences.view.statusBarShown
    }
    property ShellAction toggleMenuBar: ShellAction {
        text: qsTr("Show Menu Bar"); keys: actions.mac ? undefined : "Ctrl+M"
        checkable: true; checked: actions.menuBarShown; enabled: !actions.mac; inPalette: !actions.mac
        onTriggered: actions.preferences.view.menuBarShown = !actions.preferences.view.menuBarShown
    }
    property ShellAction toggleSplit: ShellAction {
        text: actions.workspace.split ? qsTr("Close Split") : qsTr("Show Beside")
        iconName: "split"; keys: "Ctrl+\\"; keywords: "split side by side two panes"
        enabled: !actions.workspace.overview
        onTriggered: actions.workspace.toggleSplit()
    }
    property ActionGroup appearanceGroup: ActionGroup { exclusive: true }
    property ShellAction appearanceSystem: ShellAction {
        text: qsTr("System"); paletteText: qsTr("Appearance: System"); keywords: "theme color scheme"
        checkable: true; checked: actions.preferences.themeMode === "system"
        ActionGroup.group: actions.appearanceGroup
        onTriggered: actions.preferences.setThemeMode("system")
    }
    property ShellAction appearanceLight: ShellAction {
        text: qsTr("Light"); paletteText: qsTr("Appearance: Light"); iconName: "sun"; keywords: "theme color scheme"
        checkable: true; checked: actions.preferences.themeMode === "light"
        ActionGroup.group: actions.appearanceGroup
        onTriggered: actions.preferences.setThemeMode("light")
    }
    property ShellAction appearanceDark: ShellAction {
        text: qsTr("Dark"); paletteText: qsTr("Appearance: Dark"); iconName: "moon"; keywords: "theme color scheme"
        checkable: true; checked: actions.preferences.themeMode === "dark"
        ActionGroup.group: actions.appearanceGroup
        onTriggered: actions.preferences.setThemeMode("dark")
    }
    property ShellAction alwaysDarkDisplays: ShellAction {
        text: qsTr("Always Use Dark Displays"); keywords: "plot readout theme"
        checkable: true; checked: actions.preferences.alwaysDarkDisplays
        onTriggered: actions.preferences.setAlwaysDarkDisplays(!actions.preferences.alwaysDarkDisplays)
    }
    property ShellAction fullScreenAction: ShellAction {
        text: actions.fullScreen ? qsTr("Exit Full Screen") : qsTr("Enter Full Screen")
        iconName: "maximize"; keys: StandardKey.FullScreen
        onTriggered: actions.fullScreenToggleRequested()
    }
    property ShellAction commandPalette: ShellAction {
        text: qsTr("Command Palette…"); iconName: "search"; keys: "Ctrl+K"; inPalette: false
        onTriggered: actions.paletteRequested()
    }

    // --- Instrument ----------------------------------------------------------------------------------
    property ShellAction showOverview: ShellAction {
        text: qsTr("Overview"); paletteText: qsTr("Show Overview"); iconName: "grid"; keys: "Ctrl+0"; keywords: "home device"
        checkable: true; checked: actions.workspace.overview
        onTriggered: actions.workspace.showOverview()
    }
    // One Action per pane (instruments ⌘1–⌘9, then the views), in sidebar order.
    property list<ShellAction> paneActions
    property Instantiator paneActionFactory: Instantiator {
        model: actions.workspace.paneOrder()
        delegate: ShellAction {
            required property string modelData
            readonly property string paneId: modelData
            readonly property int number: InstrumentRegistry.ids.indexOf(paneId)
            text: actions.paneTitle(paneId)
            paletteText: qsTr("Show %1").arg(text)
            iconName: actions.paneIcon(paneId)
            keys: number >= 0 && number < 9 ? "Ctrl+" + (number + 1) : undefined
            checkable: true
            checked: actions.focusedPane === paneId
            onTriggered: actions.workspace.show(paneId)
        }
        onObjectAdded: (index, object) => actions.paneActions.push(object as ShellAction)
        onObjectRemoved: (index, object) => {
            const list = []
            for (const a of actions.paneActions)
                if (a !== object)
                    list.push(a)
            actions.paneActions = list
        }
    }
    property ShellAction nextInstrument: ShellAction {
        text: qsTr("Next Instrument"); keys: StandardKey.NextChild
        onTriggered: actions.workspace.cycle(1)
    }
    property ShellAction previousInstrument: ShellAction {
        text: qsTr("Previous Instrument"); keys: StandardKey.PreviousChild
        onTriggered: actions.workspace.cycle(-1)
    }
    property ShellAction runStop: ShellAction {
        readonly property bool running: actions.focusedController !== null && actions.focusedController.running
        text: running ? qsTr("Stop") : qsTr("Run")
        paletteText: actions.focusedTitle.length > 0 ? (running ? qsTr("Stop %1") : qsTr("Run %1")).arg(actions.focusedTitle) : text
        iconName: running ? "stop-filled" : "play"; keys: "Ctrl+R"; inPalette: false
        enabled: actions.focusedController !== null && (running || actions.focusedController.canRun)
        onTriggered: actions.focusedController.toggle()
    }
    property ShellAction single: ShellAction {
        text: qsTr("Single"); iconName: "step"; keys: "Ctrl+Shift+R"; inPalette: false
        enabled: actions.focusedController !== null && actions.focusedController.supportsSingle
                 && actions.focusedController.singleEnabled
        onTriggered: actions.focusedController.runOnce()
    }
    property ShellAction stopAll: ShellAction {
        text: qsTr("Stop All"); iconName: "stop-filled"; keys: "Ctrl+."; keywords: "every instrument"
        enabled: InstrumentRegistry.anyStoppable
        onTriggered: InstrumentRegistry.stopAll()
    }
    property ShellAction resetSettings: ShellAction {
        text: qsTr("Reset Settings…"); iconName: "reset"; inPalette: false
        enabled: actions.focusedController !== null
        onTriggered: actions.dialogs.resetSettings(actions.focusedPane)
    }
    property ShellAction releaseOutputs: ShellAction {
        text: qsTr("Release Outputs"); keywords: "safe state 0 V"
        enabled: actions.focusedController !== null && !actions.focusedController.running && actions.focusedController.holdsOutputs
        onTriggered: actions.focusedController.returnToSafeState()
    }
    property ShellAction resetAllInstruments: ShellAction {
        text: qsTr("Reset All Instruments to Defaults…"); iconName: "reset"; keywords: "defaults revert"
        onTriggered: actions.dialogs.resetAllInstruments()
    }
    // The focused instrument's commands (InstrumentController.commands).
    property list<ShellAction> commandActions
    property Instantiator commandActionFactory: Instantiator {
        model: actions.focusedController ? actions.focusedController.commands : []
        delegate: ShellAction {
            required property var modelData
            text: (modelData.title ?? "") + ((modelData.argumentLabel ?? "").length > 0 ? "…" : "")
            iconName: modelData.icon ?? ""
            enabled: modelData.enabled ?? true
            inPalette: false
            onTriggered: {
                const id = actions.focusedController.instrumentId
                const commandId = modelData.id
                if ((modelData.argumentLabel ?? "").length > 0)
                    actions.argumentRequested(actions.commandEntry(id, modelData))
                else
                    actions.invokeCommand(id, commandId, undefined)
            }
        }
        onObjectAdded: (index, object) => actions.commandActions.push(object as ShellAction)
        onObjectRemoved: (index, object) => {
            const list = []
            for (const a of actions.commandActions)
                if (a !== object)
                    list.push(a)
            actions.commandActions = list
        }
    }

    // --- Device --------------------------------------------------------------------------------------
    property ShellAction simulatedDevice: ShellAction {
        text: qsTr("Simulated Device"); iconName: "cpu"; keywords: "simulator"
        checkable: true; checked: DeviceHub.simulated; enabled: DeviceHub.simulatorAvailable
        onTriggered: {
            if (actions.deviceButton)
                actions.deviceButton.requestSelect("sim:0")
        }
    }
    property ShellAction rescan: ShellAction {
        text: qsTr("Rescan"); paletteText: qsTr("Rescan Devices"); iconName: "refresh"; keywords: "usb hot-plug"
        onTriggered: DeviceHub.rescan()
    }
    property ShellAction resetDevice: ShellAction {
        text: qsTr("Reset Device…"); iconName: "reset"; keywords: "outputs 0 V"
        enabled: DeviceHub.connected && !DeviceHub.resetting && !InstrumentRegistry.anyRunning
        onTriggered: {
            if (actions.deviceButton)
                actions.deviceButton.requestReset()
        }
    }
    property ShellAction deviceInfo: ShellAction {
        text: qsTr("Device Info…"); iconName: "info"; keywords: "serial product"
        onTriggered: actions.dialogs.showDeviceInfo()
    }
    property ShellAction simulatorSettings: ShellAction {
        text: qsTr("Simulator Settings…"); iconName: "settings"; keywords: "test signal noise"
        enabled: DeviceHub.simulatorAvailable
        onTriggered: actions.dialogs.showSimulatorSettings()
    }
    property ShellAction simulatorPlugged: ShellAction {
        text: qsTr("Simulator Plugged In"); keywords: "hot-plug unplug"
        checkable: true; checked: DeviceHub.simulatorPlugged
        enabled: DeviceHub.simulatorAvailable && DeviceHub.simulated
        onTriggered: DeviceHub.simulatorPlugged = !DeviceHub.simulatorPlugged
    }

    // --- Tools, Window, Help -------------------------------------------------------------------------
    property ShellAction editTools: ShellAction {
        text: qsTr("Edit Tools…"); keywords: "launcher programs"
        onTriggered: actions.dialogs.showPreferences("tools")
    }
    property ShellAction minimize: ShellAction {
        text: qsTr("Minimize"); keys: actions.mac ? "Ctrl+M" : undefined; inPalette: false
        onTriggered: actions.minimizeRequested()
    }
    property ShellAction zoom: ShellAction {
        text: qsTr("Zoom"); inPalette: false
        onTriggered: actions.zoomRequested()
    }
    property ShellAction help: ShellAction {
        text: qsTr("%1 Help").arg(AppInfo.name); iconName: "help-circle"; keys: StandardKey.HelpContents; keywords: "manual getting started"
        onTriggered: actions.workspace.showHelp("app")
    }
    property ShellAction instrumentHelp: ShellAction {
        text: actions.focusedTitle.length > 0 ? qsTr("%1 Help").arg(actions.focusedTitle) : qsTr("Instrument Help")
        iconName: "book"; enabled: actions.focusedPane.length > 0
        onTriggered: actions.workspace.showHelp(actions.workspace.helpTopicOf(actions.focusedPane))
    }
    property ShellAction keyboardShortcuts: ShellAction {
        text: qsTr("Keyboard Shortcuts"); keywords: "keys"
        onTriggered: actions.workspace.showHelp(Help.has("shortcuts") ? "shortcuts" : "app")
    }
    property ShellAction eventLog: ShellAction {
        text: qsTr("Event Log"); iconName: "list"; keywords: "messages errors warnings"
        onTriggered: actions.eventLogRequested()
    }
    property ShellAction about: ShellAction {
        text: qsTr("About %1").arg(AppInfo.name); iconName: "info"; keywords: "version license"
        onTriggered: actions.dialogs.showAbout()
    }

    // --- Window shortcuts that are not menu keys ---------------------------------------------------
    readonly property bool spaceAllowed: focusedController !== null && !paletteOpen
                                         && shellKeys.spaceAllowed(Window.activeFocusItem)
    ShellKeys { id: shellKeys }
    property list<Shortcut> windowShortcuts: [
        Shortcut {
            sequences: ["Space"]
            enabled: actions.spaceAllowed
            onActivated: actions.focusedController.toggle()
        },
        Shortcut {
            sequences: ["Shift+Space"]
            enabled: actions.spaceAllowed && actions.single.enabled
            onActivated: actions.focusedController.runOnce()
        },
        // F1 also opens the help where the standard help key is another one (macOS: ⌘?).
        Shortcut {
            sequences: ["F1"]
            enabled: KeyText.all(StandardKey.HelpContents).indexOf("F1") < 0
            onActivated: actions.help.trigger()
        }
    ]

    // Every Action with a shortcut (tests: no duplicates).
    readonly property var staticActions: [newSession, openSession, saveSession, saveSessionAs, revertToSaved, exportData,
        exportImage, printAction, closeWindow, preferencesAction, quit, cut, copy, paste, selectAll, toggleSidebar,
        toggleInspector, toggleMeasurements, toggleStatusBar, toggleMenuBar, toggleSplit, appearanceSystem,
        appearanceLight, appearanceDark, alwaysDarkDisplays, fullScreenAction, commandPalette, showOverview,
        nextInstrument, previousInstrument, runStop, single, stopAll, resetSettings, releaseOutputs,
        resetAllInstruments, simulatedDevice, rescan, resetDevice, deviceInfo, simulatorSettings, simulatorPlugged,
        editTools, minimize, zoom, help, instrumentHelp, keyboardShortcuts, eventLog, about]
    function allActions(): var {
        return staticActions.concat(Array.from(paneActions), Array.from(commandActions))
    }

    // Shortcut text ("Ctrl+S", portable) of an action, for key hints.
    function keysText(action: var): string {
        return action && action.keysText !== undefined ? action.keysText : ""
    }

    // --- Command palette entries ([{ title, subtitle, icon, keys, keywords, argumentLabel, run }]) ---
    function entry(action: var): var {
        return { title: action.paletteText, icon: action.iconName, keys: keysText(action), keywords: action.keywords,
                 run: () => action.trigger() }
    }
    function commandEntry(instrumentId: string, command: var): var {
        const info = InstrumentRegistry.info(instrumentId)
        return { title: qsTr("%1: %2").arg(info.title).arg(command.title), icon: command.icon || info.icon,
                 keys: command.keys ?? "", keywords: info.shortTitle + " " + (command.keywords ?? ""),
                 argumentLabel: command.argumentLabel ?? "",
                 run: argument => actions.invokeCommand(instrumentId, command.id, argument) }
    }
    function paletteCommands(): var {
        const list = []
        const add = action => {
            if (action.inPalette && action.enabled)
                list.push(entry(action))
        }
        add(showOverview)
        for (const action of paneActions)
            list.push(entry(action))
        for (const id of InstrumentRegistry.ids) {
            const c = InstrumentRegistry.controller(id)
            if (!c)
                continue
            const title = InstrumentRegistry.info(id).title
            if (c.running)
                list.push({ title: qsTr("Stop %1").arg(title), icon: "stop-filled", run: () => c.stop() })
            else if (c.canRun)
                list.push({ title: qsTr("Run %1").arg(title), icon: "play", run: () => c.run() })
            if (!c.running && c.supportsSingle && c.singleEnabled && c.canRun)
                list.push({ title: qsTr("Single %1").arg(title), icon: "step", run: () => c.runOnce() })
            list.push({ title: qsTr("Reset %1 Settings…").arg(title), icon: "reset",
                        run: () => actions.dialogs.resetSettings(id) })
            for (const command of c.commands)
                if (command.enabled ?? true)
                    list.push(commandEntry(id, command))
        }
        for (const action of [stopAll, exportData, exportImage, printAction, releaseOutputs, toggleSplit, toggleSidebar,
                              toggleInspector, toggleMeasurements, toggleStatusBar, toggleMenuBar, appearanceSystem,
                              appearanceLight, appearanceDark, alwaysDarkDisplays, fullScreenAction, newSession,
                              openSession, saveSession, saveSessionAs, revertToSaved, resetAllInstruments,
                              simulatedDevice, simulatorPlugged, rescan, resetDevice, deviceInfo, simulatorSettings,
                              preferencesAction, editTools, help, instrumentHelp, keyboardShortcuts, eventLog, about])
            add(action)
        for (const action of paneActions) {
            const paneId = action.paneId
            if (!workspace.overview && !workspace.isShown(paneId))
                list.push({ title: qsTr("Show %1 Beside").arg(action.text), icon: "split", keywords: "split side by side",
                            run: () => actions.workspace.showBeside(paneId) })
        }
        for (const session of SessionManager.recentSessions)
            if (session.exists)
                list.push({ title: qsTr("Open Recent: %1").arg(session.name), subtitle: session.path ?? "", icon: "layers",
                            keywords: "session", run: () => actions.dialogs.openRecent(session.file) })
        for (const topic of Help.topics)
            list.push({ title: qsTr("Help: %1").arg(topic.title), icon: "book", keywords: "manual",
                        run: () => actions.workspace.showHelp(topic.id) })
        return list
    }
}
