pragma ComponentBehavior: Bound
import QtQml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import MyBench.Framework
import MyBench.Controls

// The MYBENCH window (docs/UI.md §7–§11): menu bar, toolbar, sidebar, Overview or one/two panes,
// inspector dock, status bar. The layout adapts to the window width (§8): the sidebar is
// expanded, compact or a drawer; the inspector is docked or an overlay sheet; a split compacts the
// sidebar or is suspended when the window is too narrow.
ApplicationWindow {
    id: window

    readonly property bool mac: AppInfo.platform === "macos"
    // macOS: the toolbar is the title bar (default; --no-unified-titlebar opts out).
    readonly property bool unifiedTitleBar: mac && !AppInfo.offscreen && !AppInfo.flag("no-unified-titlebar")
    property real titleButtonsRight: 0
    readonly property string layoutClass: layout.layoutClass
    readonly property InstrumentPanel focusedPanel: PanelRegistry.revision >= 0 && !workspace.overview
                                                    ? PanelRegistry.panel(workspace.focused) as InstrumentPanel : null

    function toggleSidebar() {
        if (layout.sidebarDrawer) {
            drawer.opened ? drawer.close() : drawer.open()
            return
        }
        const shown = layout.sidebarVisible
        layout.sidebarOverride = ""
        workspace.setSidebarShown(!shown)
    }
    // Saves the layout (autosave and session) when it differs from the last saved one; the startup
    // state itself does not mark the session edited.
    property string savedLayout: ""
    function saveLayout() {
        const map = workspace.toMap()
        const text = JSON.stringify(map)
        if (text !== savedLayout) {
            savedLayout = text
            SessionManager.workspace = map
        }
    }
    // Preferences ▸ General ▸ Reset Window Layout: default window size (centred, not maximized),
    // equal split, default sidebar/inspector visibility; the shown instruments stay.
    function resetWindowLayout() {
        if (visibility === Window.Maximized || visibility === Window.FullScreen)
            showNormal()
        width = Math.min(1440, Math.round(Screen.desktopAvailableWidth * 0.9))
        height = Math.min(900, Math.round(Screen.desktopAvailableHeight * 0.9))
        x = Screen.virtualX + Math.round((Screen.desktopAvailableWidth - width) / 2)
        y = Screen.virtualY + Math.round((Screen.desktopAvailableHeight - height) / 2)
        windowGeometry.maximized = false
        workspace.resetLayout()
    }
    function toggleZoom() {
        visibility === Window.Maximized ? showNormal() : showMaximized()
    }
    function updateWindowChrome() {
        if (!unifiedTitleBar)
            return
        WindowChrome.setTitleHidden(window, true)
        // The traffic lights centred on the toolbar (AppKit puts them in its own title bar
        // height and re-lays them out on resize and activation, hence the calls below).
        WindowChrome.centerButtons(window, toolbar.height)
        const buttons = WindowChrome.buttonsRect(window)
        titleButtonsRight = buttons.width > 0 ? buttons.x + buttons.width : 0
    }
    onWidthChanged: if (unifiedTitleBar) Qt.callLater(updateWindowChrome)
    onHeightChanged: if (unifiedTitleBar) Qt.callLater(updateWindowChrome)
    onActiveChanged: if (unifiedTitleBar) Qt.callLater(updateWindowChrome)

    // Default 1440 × 900, at most 90 % of a smaller screen; then the stored geometry.
    width: Math.min(1440, Math.round(Screen.desktopAvailableWidth * 0.9))
    height: Math.min(900, Math.round(Screen.desktopAvailableHeight * 0.9))
    minimumWidth: Theme.minWindowWidth
    minimumHeight: Theme.minWindowHeight
    visible: true
    title: dialogs.windowTitle
    flags: unifiedTitleBar ? (Qt.Window | Qt.ExpandedClientAreaHint | Qt.NoTitleBarBackgroundHint) : Qt.Window
    color: Theme.window

    onVisibilityChanged: {
        if (window.visibility === Window.Maximized || window.visibility === Window.Windowed)
            windowGeometry.maximized = window.visibility === Window.Maximized
        Qt.callLater(updateWindowChrome)
    }
    onClosing: close => close.accepted = dialogs.mayClose()


    // --- State -------------------------------------------------------------------------------------
    AppPreferences { id: prefs }
    ShellLayout {
        id: layout
        workspace: workspace
        width: window.width
        preferredInspectorWidth: prefs.view.inspectorWidth
        focusedPanel: window.focusedPanel
        onSidebarDrawerChanged: {
            if (!sidebarDrawer)
                drawer.close()
        }
    }
    ThemeSync {
        mode: prefs.themeMode
        alwaysDarkDisplays: prefs.alwaysDarkDisplays
    }
    WorkspaceState {
        id: workspace
        layoutClass: window.layoutClass
        onChanged: Qt.callLater(window.saveLayout)
        onSidebarCollapsedChanged: layout.sidebarOverride = ""
        onInspectorVisibleChanged: layout.inspectorOverride = ""
    }
    // Panels not on screen are the ones that pause for another (docs/UI.md §10).
    Binding {
        target: ResourceArbiter
        property: "visibleInstruments"
        value: workspaceView.visiblePanes.map(id => workspace.instrumentOf(id))
    }
    Settings {
        id: windowGeometry
        category: "window"
        property alias x: window.x
        property alias y: window.y
        property alias width: window.width
        property alias height: window.height
        property bool maximized: false
    }
    ShellDialogs {
        id: dialogs
        preferences: prefs
        workspace: workspace
        onQuitAccepted: window.close()
    }
    AppActions {
        id: actions
        workspace: workspace
        dialogs: dialogs
        preferences: prefs
        deviceButton: toolbar.deviceButton
        sidebarShown: layout.sidebarVisible || drawer.opened
        inspectorShown: layout.inspectorWanted
        inspectorAvailable: layout.inspectorHasContent
        fullScreen: window.visibility === Window.FullScreen
        paletteOpen: palette.opened
        onPaletteRequested: palette.opened ? palette.close() : palette.open()
        onEventLogRequested: logPopup.opened ? logPopup.close() : logPopup.open()
        onSidebarToggleRequested: window.toggleSidebar()
        onFullScreenToggleRequested: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
        onCloseWindowRequested: window.close()
        onMinimizeRequested: window.showMinimized()
        onZoomRequested: window.toggleZoom()
        onArgumentRequested: command => palette.openFor(command)
    }

    // Framework requests: a restored layout, a help page, another panel (openInstrument), the event
    // log (error Details), About.
    Connections { target: SessionManager; function onWorkspaceRestored(map: var) { workspace.fromMap(map) } }
    Connections { target: Help; function onShowRequested(id: string) { workspace.showHelp(id) } }
    Connections {
        target: InstrumentRegistry
        function onOpenRequested(id: string, beside: bool) { beside ? workspace.showBeside(id) : workspace.show(id) }
    }
    Connections { target: ShellActions; function onEventLogRequested() { logPopup.open() } }
    Connections { target: Launcher; function onAboutRequested() { dialogs.showAbout() } }
    Connections { target: prefs; function onWindowLayoutResetRequested() { window.resetWindowLayout() } }

    Component.onCompleted: {
        workspace.fromMap(SessionManager.workspace)
        for (const id of AppInfo.panels)             // --panel is repeatable; the last one shows
            workspace.show(id)
        if (AppInfo.value("split").length > 0)
            workspace.showBeside(AppInfo.value("split"))
        if (["overview", "home"].indexOf(AppInfo.value("page")) >= 0)
            workspace.showOverview()
        if (["full", "compact", "hidden"].indexOf(AppInfo.value("sidebar")) >= 0)
            layout.sidebarOverride = AppInfo.value("sidebar")
        if (["shown", "hidden"].indexOf(AppInfo.value("inspector")) >= 0)
            layout.inspectorOverride = AppInfo.value("inspector")
        if (AppInfo.flag("show-help"))
            workspace.showHelp(workspace.helpTopicOf(workspace.overview ? "" : workspace.focused))
        // The small windows once the main window is shown (offscreen: popups in it).
        Qt.callLater(() => {
            if (AppInfo.flag("preferences") || AppInfo.value("preferences").length > 0)
                dialogs.showPreferences(AppInfo.value("preferences"))      // --preferences[=page]
            if (AppInfo.flag("about"))
                dialogs.showAbout()
            if (AppInfo.flag("device-info"))
                dialogs.showDeviceInfo()
            if (AppInfo.flag("simulator-settings"))
                dialogs.showSimulatorSettings()
        })
        if (AppInfo.flag("palette"))
            palette.open()
        if (AppInfo.flag("log"))
            logPopup.open()
        if (AppInfo.flag("print-dialog") && !workspace.overview)
            PanelRegistry.whenReady(workspace.focused, panel => panel.printPanel())
        if (windowGeometry.maximized && !AppInfo.screenshot)
            window.showMaximized()
        savedLayout = JSON.stringify(workspace.toMap())
        // The unified title bar is the default (ThemeSync still reads the old opt-in flag).
        Theme.unifiedTitleBar = unifiedTitleBar
        Qt.callLater(updateWindowChrome)
    }

    // --- Chrome --------------------------------------------------------------------------------------
    menuBar: AppMenuBar {
        actions: actions
        // macOS: the global menu (never drawn in the window, also not offscreen).
        visible: window.mac ? !AppInfo.offscreen : prefs.view.menuBarShown
    }
    header: AppToolBar {
        id: toolbar
        actions: actions
        dialogs: dialogs
        unified: window.unifiedTitleBar
        leadingInset: window.unifiedTitleBar ? Math.max(SafeArea.margins.left, window.titleButtonsRight + Theme.spacingS) : 0
        onMoveRequested: window.startSystemMove()
        onZoomRequested: window.toggleZoom()
    }
    footer: StatusBar {
        visible: prefs.view.statusBarShown
        onLogRequested: actions.eventLog.trigger()
    }

    // --- Content: sidebar | panes | inspector ------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0
        Item {
            id: sidebarHost
            Layout.fillHeight: true
            Layout.preferredWidth: layout.sidebarDrawer ? 0 : layout.sidebarWidth
            visible: Layout.preferredWidth > 0
        }
        Workspace {
            id: workspaceView
            Layout.fillWidth: true
            Layout.fillHeight: true
            workspace: workspace
            actions: actions
            splitSuspended: layout.splitSuspended
        }
        Item {
            id: dockHost
            Layout.fillHeight: true
            Layout.preferredWidth: layout.dockWidth
            visible: layout.inspectorDocked
            // Drag the left edge to resize (Theme.inspectorMinWidth … inspectorMaxWidth, saved).
            EdgeResizeHandle {
                objectName: "inspectorResizeHandle"
                currentWidth: layout.inspectorWidth
                onResized: width => prefs.setInspectorWidth(width)
            }
        }
    }

    // One sidebar and one inspector dock, moved between their docked and overlay hosts.
    Sidebar {
        id: sidebar
        parent: layout.sidebarDrawer ? drawer.contentItem : sidebarHost
        anchors.fill: parent
        workspace: workspace
        dialogs: dialogs
        compact: layout.sidebarCompact && !layout.sidebarDrawer
        onEntryActivated: drawer.close()
    }
    Drawer {
        id: drawer
        y: toolbar.height + (window.menuBar && window.menuBar.visible ? window.menuBar.height : 0)
        width: Theme.sidebarWidth
        height: window.contentItem.height
        edge: Qt.LeftEdge
        interactive: layout.sidebarDrawer
        padding: 0
    }
    Item {
        id: sheetHost
        objectName: "inspectorSheet"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(layout.inspectorWidth, parent.width - Theme.sidebarCompactWidth)
        visible: layout.inspectorSheet
        z: 50
        layer.enabled: visible
        layer.effect: PopupShadow {}
    }
    InspectorDock {
        id: dock
        parent: layout.inspectorSheet ? sheetHost : dockHost
        anchors.fill: parent
        panel: window.focusedPanel
        paneShown: !workspace.overview && workspace.focused.length > 0
    }
    Binding { target: dock; property: "page"; value: workspace.dockPage }
    Binding { target: dock; property: "helpTopic"; value: workspace.helpTopic }
    Connections {
        target: dock
        function onPageActivated(page: string) {
            page === "help" ? workspace.showHelp(workspace.helpTopicOf(workspace.focused)) : workspace.showSettings()
        }
    }

    ToastArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacingL + layout.dockWidth
        anchors.bottomMargin: Theme.spacingL
    }
    CommandPalette {
        id: palette
        commands: actions.paletteCommands
    }
    EventLogPopup {
        id: logPopup
        x: window.contentItem.width - width - Theme.spacingS
        y: window.contentItem.height - height - Theme.spacingS
    }
}
