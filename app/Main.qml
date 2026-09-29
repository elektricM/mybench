pragma ComponentBehavior: Bound
import QtQml
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import MyBench.Framework
import MyBench.Controls

// The MYBENCH window (docs/UI.md "Application shell").
ApplicationWindow {
    id: window

    readonly property bool mac: AppInfo.platform === "macos"
    // Unified title bar (content under a transparent macOS title bar): opt-in, --unified-titlebar.
    readonly property bool unifiedTitleBar: mac && AppInfo.flag("unified-titlebar")
    property bool themeFromCommandLine: AppInfo.theme.length > 0
    readonly property bool focusMode: ShellActions.focusInstrument.length > 0
    readonly property InstrumentController focusedController: focusMode ? InstrumentRegistry.controller(ShellActions.focusInstrument)
                                                              : workspace.focused.length > 0 && !workspace.home
                                                                ? InstrumentRegistry.controller(workspace.focused) : null
    property bool quitConfirmed: false

    // Default: 1440 x 900, at most 90 % of a smaller screen (app.md 2.1); then the stored size.
    width: Math.min(1440, Math.round(Screen.desktopAvailableWidth * 0.9))
    height: Math.min(900, Math.round(Screen.desktopAvailableHeight * 0.9))
    minimumWidth: 1024
    minimumHeight: 640
    visible: true
    title: sessionGuard.windowTitle
    flags: unifiedTitleBar ? (Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint) : Qt.Window
    color: Theme.window

    // --- State ------------------------------------------------------------------------------
    WorkspaceState {
        id: workspace
        onChanged: Qt.callLater(window.saveLayout)
        onHelpTopicChanged: helpView.show(helpTopic)
    }
    function saveLayout() {
        SessionManager.workspace = workspace.toMap()
    }
    HelpFollower { workspace: workspace }
    // Pause toasts only for panels that are not on screen (app.md 9.1).
    Binding {
        target: ResourceArbiter
        property: "visibleInstruments"
        value: window.focusMode ? [ShellActions.focusInstrument]
             : workspace.currentTab ? [workspace.currentTab.primary, workspace.currentTab.secondary].filter(s => s.length > 0) : []
    }
    // Details on an error toast / banner opens the event log.
    Connections {
        target: ShellActions
        function onEventLogRequested() { logPopup.open() }
    }
    // Help drawer width (app.md 2.5: 420 px, 320-720, remembered).
    Settings {
        id: helpPrefs
        category: "help"
        property int width: 420
    }

    Settings {
        id: prefs
        category: "appearance"
        property string themeMode: "system"     // system | light | dark (Preferences)
    }
    Settings {
        id: behaviour
        category: "preferences"
        property bool confirmQuit: true
    }
    // Window geometry (a UI preference, not part of sessions).
    Settings {
        id: windowPrefs
        category: "window"
        property alias x: window.x
        property alias y: window.y
        property alias width: window.width
        property alias height: window.height
        property bool maximized: false
    }
    onVisibilityChanged: {
        if (visibility === Window.Maximized || visibility === Window.Windowed)
            windowPrefs.maximized = visibility === Window.Maximized
    }

    // Save-first guard for Open / Open recent / Quit (app.md 11.2) and the window title.
    SessionGuard {
        id: sessionGuard
        onSaveFirstRequested: saveFirstDialog.open()
    }
    ShellKeys { id: shellKeys }
    function openSession() {
        sessionGuard.run(() => openDialog.open())
    }
    function openRecent(file: url) {
        sessionGuard.run(() => SessionManager.load(file))
    }

    // Quit: save a modified named session first, then confirm while instruments run.
    onClosing: close => {
        if (quitConfirmed || AppInfo.screenshot)
            return
        const mustConfirm = behaviour.confirmQuit && InstrumentRegistry.anyRunning
        const documents = InstrumentRegistry.unsavedDocuments()
        if (!sessionGuard.needsPrompt && !mustConfirm && documents.length === 0)
            return
        close.accepted = false
        if (documents.length > 0 && !unsavedDocumentsConfirm.accepted) {
            unsavedDocumentsConfirm.documents = documents
            unsavedDocumentsConfirm.open()
            return
        }
        sessionGuard.run(() => {
            if (behaviour.confirmQuit && InstrumentRegistry.anyRunning) {
                quitConfirm.open()
            } else {
                window.quitConfirmed = true
                Qt.callLater(window.close)
            }
        })
    }
    ThemeSync { mode: window.themeFromCommandLine ? AppInfo.theme : prefs.themeMode }
    function setThemeMode(mode: string) {
        prefs.themeMode = mode
        themeFromCommandLine = false
    }

    Connections {
        target: SessionManager
        function onWorkspaceRestored(map: var) {
            workspace.fromMap(map)
        }
    }
    Connections {
        target: Help
        function onShowRequested(id: string) {
            workspace.showHelp(id)
        }
    }
    // An instrument opens another panel (ShellActions.openInstrument / InstrumentRegistry).
    Connections {
        target: InstrumentRegistry
        function onOpenRequested(id: string, beside: bool) {
            if (beside && !workspace.home && !workspace.isVisible(id))
                workspace.openBeside(id)
            else
                workspace.open(id)
        }
    }

    Component.onCompleted: {
        workspace.fromMap(SessionManager.workspace)
        // --panel is repeatable: every panel gets a tab, the last one is focused.
        for (const id of AppInfo.panels)
            workspace.open(id)
        if (AppInfo.value("split").length > 0)
            workspace.openBeside(AppInfo.value("split"))
        if (AppInfo.value("page") === "home")
            workspace.showHome()
        if (AppInfo.flag("dashboard"))
            workspace.dashboardPinned = true
        if (AppInfo.flag("show-help"))
            workspace.showHelp(window.focusedController ? (InstrumentRegistry.info(workspace.focused).helpId ?? "app") : "app")
        if (AppInfo.flag("preferences"))
            preferences.open()
        if (AppInfo.flag("palette"))
            palette.open()
        if (AppInfo.flag("log"))
            logPopup.open()
        // --print-dialog: the focused panel's Print options (screenshots, reviews).
        if (AppInfo.flag("print-dialog") && window.focusedController)
            Qt.callLater(() => ShellActions.printRequested(window.focusedController.instrumentId))
        if (windowPrefs.maximized && !AppInfo.screenshot)
            window.showMaximized()
        saveLayout()
    }

    // --- Layout -----------------------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        NavRail {
            visible: !window.focusMode
            Layout.fillHeight: true
            Layout.preferredWidth: window.unifiedTitleBar ? 76 : Theme.navRailWidth
            workspace: workspace
            showLogo: !window.unifiedTitleBar
            onMoveWindowRequested: window.startSystemMove()
            onPreferencesRequested: preferences.open()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TopBar {
                visible: !window.focusMode
                Layout.fillWidth: true
                workspace: workspace
                onPaletteRequested: palette.open()
                onMoveWindowRequested: window.startSystemMove()
                onNewSessionRequested: window.newSession()
                onOpenSessionRequested: window.openSession()
                onOpenRecentRequested: file => window.openRecent(file)
                onSaveSessionRequested: window.saveSession()
                onSaveSessionAsRequested: window.saveSessionAs()
                onDeviceInfoRequested: deviceInfo.open()
                onRevertAllRequested: revertConfirm.open()
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    TabStrip {
                        visible: !window.focusMode
                        Layout.fillWidth: true
                        workspace: workspace
                    }
                    WorkspaceView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        workspace: workspace
                        onNewSessionRequested: window.newSession()
                        onOpenSessionRequested: window.openSession()
                        onOpenRecentRequested: file => window.openRecent(file)
                        onDeviceInfoRequested: deviceInfo.open()
                    }
                }

                HelpView {
                    id: helpView
                    visible: workspace.helpOpen && !window.focusMode
                    Layout.fillHeight: true
                    Layout.preferredWidth: helpPrefs.width
                    topic: workspace.helpTopic
                    onCloseRequested: workspace.toggleHelp()

                    // Resize handle on the left edge.
                    MouseArea {
                        objectName: "helpResizeHandle"
                        x: -3
                        width: 7
                        height: parent.height
                        z: 10
                        cursorShape: Qt.SplitHCursor
                        hoverEnabled: true
                        property real startX: 0
                        property int startWidth: 0
                        onPressed: mouse => {
                            startX = mapToItem(null, mouse.x, 0).x
                            startWidth = helpPrefs.width
                        }
                        onPositionChanged: mouse => {
                            if (pressed)
                                helpPrefs.width = Math.max(320, Math.min(720, startWidth - (mapToItem(null, mouse.x, 0).x - startX)))
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.pressed || parent.containsMouse ? 3 : 0
                            height: parent.height
                            color: parent.pressed ? Theme.accent : Theme.alpha(Theme.accent, 0.6)
                        }
                    }
                }

                DashboardColumn {
                    id: dashboardColumn
                    visible: workspace.dashboardPinned && !window.focusMode
                    Layout.fillHeight: true
                    Layout.preferredWidth: implicitWidth
                    workspace: workspace
                }
            }

            StatusBar {
                id: statusBar
                visible: !window.focusMode
                Layout.fillWidth: true
                workspace: workspace
                onLogRequested: logPopup.opened ? logPopup.close() : logPopup.open()
            }
        }
    }

    ToastArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacingL + (dashboardColumn.visible ? dashboardColumn.width : 0)
        anchors.bottomMargin: Theme.statusBarHeight + Theme.spacingL
    }

    // Focus mode bar (app.md 6.5): the panel's display fills the window.
    Rectangle {
        visible: window.focusMode
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 72
        width: focusRow.implicitWidth + 16
        height: 40
        radius: 20
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.borderStrong
        z: 900
        RowLayout {
            id: focusRow
            anchors.centerIn: parent
            spacing: Theme.spacingS
            Icon {
                Layout.leftMargin: 6
                name: InstrumentRegistry.info(ShellActions.focusInstrument).icon ?? ""
                color: Theme.textSecondary
            }
            Text {
                text: InstrumentRegistry.info(ShellActions.focusInstrument).title ?? ""
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
            }
            ActionButton {
                compact: true
                tone: window.focusedController && window.focusedController.running ? "danger" : "success"
                iconName: window.focusedController && window.focusedController.running ? "stop-filled" : "play"
                text: window.focusedController && window.focusedController.running ? qsTr("Stop") : qsTr("Run")
                onClicked: window.focusedController.toggle()
            }
            ActionButton {
                compact: true
                tone: "ghost"
                text: qsTr("Exit")
                shortcutText: "Esc"
                toolTip: qsTr("Exit focus mode")
                onClicked: ShellActions.focusInstrument = ""
            }
        }
    }

    CommandPalette {
        id: palette
        commands: window.buildCommands
    }
    PreferencesDialog {
        id: preferences
        themeMode: prefs.themeMode
        onThemeModeRequested: mode => window.setThemeMode(mode)
    }
    DeviceInfoDialog { id: deviceInfo }
    AboutDialog { id: about }
    ConfirmDialog {
        id: quitConfirm
        title: qsTr("Quit %1?").arg(AppInfo.name)
        text: qsTr("%n instrument(s) still running. Quitting stops them and puts the outputs in their safe state.", "",
                   InstrumentRegistry.runningCount)
        acceptText: qsTr("Quit")
        destructive: true
        onAccepted: {
            window.quitConfirmed = true
            window.close()
        }
    }
    // Panels editing a document that is not saved (InstrumentController.unsavedDocument).
    ConfirmDialog {
        id: unsavedDocumentsConfirm
        property var documents: []
        property bool accepted: false
        title: qsTr("Quit without saving?")
        text: qsTr("These documents have unsaved changes:\n%1").arg(documents.map(d => "• " + d).join("\n"))
        acceptText: qsTr("Quit without saving")
        destructive: true
        onAccepted: {
            accepted = true
            Qt.callLater(window.close)
        }
    }
    ConfirmDialog {
        id: revertConfirm
        title: qsTr("Revert all instruments to defaults?")
        text: qsTr("Every instrument's settings go back to the documented defaults. The layout is kept.")
        acceptText: qsTr("Revert")
        destructive: true
        onAccepted: SessionManager.revertAllToDefaults()
    }

    EventLogPopup {
        id: logPopup
        x: window.width - width - Theme.spacingM
        y: window.height - height - Theme.statusBarHeight - Theme.spacingS
    }

    // --- Sessions ---------------------------------------------------------------------------
    function newSession() {
        if (SessionManager.modified)
            newSessionDialog.open()
        else
            SessionManager.newSession()
    }
    function saveSession() {
        if (!SessionManager.saveCurrent())
            saveSessionAs()
    }
    function saveSessionAs() {
        saveDialog.selectedFile = SessionManager.suggestedFileUrl()
        saveDialog.open()
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open session")
        fileMode: FileDialog.OpenFile
        nameFilters: SessionManager.nameFilters
        onAccepted: SessionManager.load(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save session")
        fileMode: FileDialog.SaveFile
        defaultSuffix: SessionManager.fileExtension
        nameFilters: SessionManager.nameFilters
        onAccepted: SessionManager.save(selectedFile)
    }
    Dialog {
        id: saveFirstDialog
        anchors.centerIn: Overlay.overlay
        width: 440
        modal: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("Save changes to “%1”?").arg(SessionManager.currentName)
        onRejected: sessionGuard.cancel()
        Text {
            width: parent.width
            text: qsTr("The session has unsaved changes. They are lost if you don't save them.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: Theme.spacingS
            ActionButton {
                Layout.leftMargin: Theme.spacingL
                Layout.bottomMargin: Theme.spacingL
                text: qsTr("Don't save")
                tone: "ghost"
                onClicked: {
                    saveFirstDialog.close()
                    sessionGuard.discard()
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                Layout.bottomMargin: Theme.spacingL
                text: qsTr("Cancel")
                onClicked: saveFirstDialog.reject()
            }
            ActionButton {
                Layout.bottomMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                text: qsTr("Save")
                tone: "accent"
                onClicked: {
                    saveFirstDialog.close()
                    sessionGuard.save()
                }
            }
        }
    }
    Dialog {
        id: newSessionDialog
        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        title: qsTr("Start a new session?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        onAccepted: SessionManager.newSession()
        Text {
            width: parent.width
            text: qsTr("Every instrument goes back to its default settings and the workspace layout is reset. Unsaved changes of the current session are lost.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
    }

    // --- Keyboard -----------------------------------------------------------------------------
    // Ctrl+\ : unsplit, or put the next tab (else the first other instrument) beside this one.
    function toggleSplit() {
        if (workspace.home)
            return
        if (workspace.split) {
            workspace.unsplit(workspace.currentIndex)
            return
        }
        const next = workspace.tabs[workspace.currentIndex + 1] ?? workspace.tabs[workspace.currentIndex - 1]
        if (next) {
            workspace.openBeside(next.primary)
            return
        }
        const other = InstrumentRegistry.ids.find(id => id !== workspace.currentTab.primary)
        if (other)
            workspace.openBeside(other)
    }

    function openByIndex(index: int) {
        const ids = InstrumentRegistry.ids
        if (index >= 0 && index < ids.length)
            workspace.open(ids[index])
    }

    Shortcut { sequences: ["Ctrl+K"]; onActivated: palette.opened ? palette.close() : palette.open() }
    Shortcut {
        sequences: ["Space"]
        // Not while a focused button/toggle/combo/slider uses Space itself (app.md 6.2).
        enabled: window.focusedController !== null && !palette.opened && shellKeys.spaceAllowed(window.activeFocusItem)
        onActivated: window.focusedController.toggle()
    }
    Shortcut { sequences: ["F5"]; enabled: window.focusedController !== null; onActivated: window.focusedController.run() }
    Shortcut { sequences: ["Shift+F5"]; enabled: window.focusedController !== null; onActivated: window.focusedController.stop() }
    Shortcut { sequences: ["Ctrl+."]; onActivated: InstrumentRegistry.stopAll() }
    Shortcut { sequences: ["Ctrl+0"]; onActivated: workspace.showHome() }
    Instantiator {
        model: 9
        delegate: Shortcut {
            required property int index
            sequence: "Ctrl+" + (index + 1)
            onActivated: window.openByIndex(index)
        }
    }
    Shortcut { sequences: [StandardKey.Close]; enabled: !workspace.home; onActivated: workspace.closeTab(workspace.currentIndex) }
    Shortcut { sequences: ["Ctrl+Tab"]; onActivated: workspace.select(workspace.currentIndex + 1 < workspace.tabs.length ? workspace.currentIndex + 1 : -1) }
    Shortcut { sequences: ["Ctrl+Shift+Tab"]; onActivated: workspace.select(workspace.currentIndex > -1 ? workspace.currentIndex - 1 : workspace.tabs.length - 1) }
    Shortcut { sequences: Theme.macShortcuts ? [StandardKey.HelpContents, "F1"] : [StandardKey.HelpContents]; onActivated: workspace.toggleHelp() }
    Shortcut { sequences: ["Ctrl+Shift+D"]; onActivated: workspace.toggleDashboard() }
    Shortcut {
        sequences: ["Shift+Space"]
        enabled: window.focusedController !== null && window.focusedController.singleEnabled && !palette.opened
        onActivated: window.focusedController.runOnce()
    }
    Shortcut { sequences: ["Ctrl+\\"]; onActivated: window.toggleSplit() }
    Shortcut { sequences: ["Ctrl+I"]; enabled: window.focusedController !== null; onActivated: ShellActions.inspectorToggleRequested(window.focusedController.instrumentId) }
    Shortcut { sequences: ["Ctrl+L"]; enabled: window.focusedController !== null; onActivated: ShellActions.exportDataRequested(window.focusedController.instrumentId) }
    Shortcut { sequences: [StandardKey.Print]; enabled: window.focusedController !== null; onActivated: ShellActions.printRequested(window.focusedController.instrumentId) }
    Shortcut {
        sequences: ["Ctrl+Shift+F"]
        enabled: window.focusedController !== null
        onActivated: ShellActions.toggleFocus(window.focusedController.instrumentId)
    }
    Shortcut { sequences: ["Esc"]; enabled: window.focusMode; onActivated: ShellActions.focusInstrument = "" }
    Shortcut { sequences: ["F11"]; onActivated: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut { sequences: [StandardKey.New]; onActivated: window.newSession() }
    Shortcut { sequences: [StandardKey.Quit]; onActivated: window.close() }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: preferences.open() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: window.saveSession() }
    Shortcut { sequences: ["Ctrl+Shift+S"]; onActivated: window.saveSessionAs() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: window.openSession() }

    // --- Commands (palette) -------------------------------------------------------------------
    function buildCommands(): var {
        const list = []
        const current = workspace.currentTab
        list.push({ title: qsTr("Go to Home"), icon: "home", keys: "Ctrl+0", keywords: "dashboard tiles",
                    run: () => workspace.showHome() })
        const ids = InstrumentRegistry.ids
        for (let i = 0; i < ids.length; ++i) {
            const info = InstrumentRegistry.info(ids[i])
            const id = info.instrumentId
            const c = InstrumentRegistry.controller(id)
            list.push({ title: qsTr("Open %1").arg(info.title), subtitle: info.description, icon: info.icon,
                        keys: i < 9 ? "Ctrl+" + (i + 1) : "", keywords: info.shortTitle,
                        run: () => workspace.open(id) })
            if (current && current.primary !== id && current.secondary !== id) {
                const other = InstrumentRegistry.info(current.primary)
                list.push({ title: qsTr("Open %1 beside %2").arg(info.shortTitle).arg(other.shortTitle),
                            subtitle: qsTr("Side by side"), icon: "split", keywords: "split",
                            run: () => workspace.openBeside(id) })
            }
            if (c && c.running)
                list.push({ title: qsTr("Stop %1").arg(info.title), icon: "stop-filled", run: () => c.stop() })
            else if (c)
                list.push({ title: qsTr("Run %1").arg(info.title), icon: "play", run: () => c.run() })
            if (c)
                list.push({ title: qsTr("Reset %1 settings to defaults").arg(info.title), icon: "reset",
                            run: () => c.resetSettings() })
            // Instrument commands (InstrumentController.addCommand): "Oscilloscope: Autoscale".
            for (const command of (c ? c.commands : [])) {
                if (!command.enabled)
                    continue
                const commandId = command.id
                list.push({ title: qsTr("%1: %2").arg(info.title).arg(command.title), icon: command.icon || info.icon,
                            keys: command.keys, keywords: info.shortTitle + " " + command.keywords,
                            argumentLabel: command.argumentLabel,
                            run: argument => {
                                // The panel is created (synchronously) on first open; call
                                // later so its QML handlers (commandTriggered) are connected.
                                workspace.open(id)
                                Qt.callLater(() => c.invokeCommand(commandId, argument))
                            } })
            }
        }
        list.push({ title: qsTr("Stop all instruments"), icon: "stop-filled", keys: "Ctrl+.", run: () => InstrumentRegistry.stopAll() })
        for (const mode of [{ id: "system", text: qsTr("Theme: follow the system") },
                            { id: "light", text: qsTr("Theme: light") },
                            { id: "dark", text: qsTr("Theme: dark") }])
            list.push({ title: mode.text, icon: mode.id === "light" ? "sun" : mode.id === "dark" ? "moon" : "settings",
                        keywords: "theme appearance color scheme", run: () => window.setThemeMode(mode.id) })
        list.push({ title: workspace.dashboardPinned ? qsTr("Unpin dashboard") : qsTr("Pin dashboard"),
                    icon: "sidebar-right", keys: "Ctrl+Shift+D", run: () => workspace.toggleDashboard() })
        list.push({ title: workspace.helpOpen ? qsTr("Hide help") : qsTr("Show help"), icon: "help-circle", keys: "F1",
                    run: () => workspace.toggleHelp() })
        list.push({ title: qsTr("Show event log"), icon: "list", run: () => logPopup.open() })
        const focusedId = window.focusedController ? window.focusedController.instrumentId : ""
        if (focusedId.length > 0) {
            const t = window.focusedController.title
            list.push({ title: qsTr("%1: Export data…").arg(t), icon: "download", keys: "Ctrl+L", keywords: "log csv",
                        run: () => ShellActions.exportDataRequested(focusedId) })
            list.push({ title: qsTr("%1: Export image…").arg(t), icon: "image", keywords: "png screenshot",
                        run: () => ShellActions.exportImageRequested(focusedId) })
            list.push({ title: qsTr("%1: Export PDF…").arg(t), icon: "file", keywords: "print",
                        run: () => ShellActions.exportPdfRequested(focusedId) })
            list.push({ title: qsTr("%1: Print…").arg(t), icon: "printer", keys: "Ctrl+P", keywords: "ink saver paper",
                        run: () => ShellActions.printRequested(focusedId) })
            if (window.focusedController.supportsSingle && window.focusedController.singleEnabled)
                list.push({ title: qsTr("%1: Single").arg(t), icon: "step", keys: "Shift+Space", run: () => window.focusedController.runOnce() })
            list.push({ title: window.focusMode ? qsTr("Exit focus mode") : qsTr("%1: Focus mode").arg(t), icon: "maximize",
                        keys: "Ctrl+Shift+F", keywords: "toggle view display only",
                        run: () => ShellActions.toggleFocus(focusedId) })
            list.push({ title: qsTr("%1: Show / hide settings").arg(t), icon: "sliders", keys: "Ctrl+I", keywords: "inspector",
                        run: () => ShellActions.inspectorToggleRequested(focusedId) })
        }
        list.push({ title: workspace.split ? qsTr("Unsplit") : qsTr("Split view"), icon: "split", keys: "Ctrl+\\",
                    run: () => window.toggleSplit() })
        list.push({ title: qsTr("Revert all instruments to defaults…"), icon: "reset", run: () => revertConfirm.open() })
        list.push({ title: qsTr("Preferences…"), icon: "settings", keys: "Ctrl+,", run: () => preferences.open() })
        list.push({ title: qsTr("Device info…"), icon: "info", run: () => deviceInfo.open() })
        list.push({ title: qsTr("About %1").arg(AppInfo.name), icon: "app-logo", run: () => about.open() })
        for (const topic of Help.topics)
            list.push({ title: qsTr("Help: %1").arg(topic.title), subtitle: qsTr("Help page"), icon: "book", keywords: "manual",
                        run: () => workspace.showHelp(topic.id) })
        list.push({ title: qsTr("New session"), icon: "file-plus", run: () => window.newSession() })
        list.push({ title: qsTr("Open session…"), icon: "folder-open", keys: "Ctrl+O", run: () => window.openSession() })
        list.push({ title: qsTr("Save session"), icon: "save", keys: "Ctrl+S", run: () => window.saveSession() })
        list.push({ title: qsTr("Save session as…"), icon: "save", keys: "Ctrl+Shift+S", run: () => window.saveSessionAs() })
        list.push({ title: qsTr("Reset device"), subtitle: qsTr("Outputs to 0 V, digital lines to inputs"), icon: "reset",
                    run: () => DeviceHub.reset() })
        list.push({ title: qsTr("Rescan devices"), icon: "refresh", keywords: "usb hot-plug", run: () => DeviceHub.rescan() })
        if (DeviceHub.simulatorAvailable)
            list.push({ title: DeviceHub.simulatorPlugged ? qsTr("Simulator: unplug") : qsTr("Simulator: plug in"),
                        icon: "unlink", keywords: "hot-plug test",
                        run: () => DeviceHub.simulatorPlugged = !DeviceHub.simulatorPlugged })
        return list
    }
}
