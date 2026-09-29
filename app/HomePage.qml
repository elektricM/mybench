pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Home (app.md 2.7, 18.1): the launcher. Favorites strip, every instrument as a live tile, the
// Tools / Files / Resources cards, the device card, recent sessions and shortcuts. The persistent
// launcher state (favorites, tools, pinned files, recent exports) lives in Launcher.
Rectangle {
    id: home

    required property WorkspaceState workspace

    signal newSessionRequested()
    signal openSessionRequested()
    signal openRecentRequested(url file)
    signal deviceInfoRequested()

    readonly property var favoriteIds: Launcher.favorites.filter(id => InstrumentRegistry.contains(id))

    function fileIcon(suffix: string): string {
        if (["png", "jpg", "jpeg", "svg", "bmp", "gif"].indexOf(suffix) >= 0)
            return "image"
        if (["csv", "tsv", "txt", "lvm", "dat"].indexOf(suffix) >= 0)
            return "list"
        if (suffix === "mybench")
            return "layers"
        return "file"
    }
    function toolIcon(kind: string): string {
        return kind === "url" ? "link" : kind === "program" ? "zap" : kind === "folder" ? "folder-open" : "file"
    }
    function shortDate(value: var): string {
        if (!value)
            return ""
        const date = new Date(value)
        const now = new Date()
        if (date.toDateString() === now.toDateString())
            return Qt.formatTime(date, "HH:mm")
        return date.toLocaleDateString(Qt.locale(), "d MMM" + (date.getFullYear() !== now.getFullYear() ? " yyyy" : ""))
    }

    color: Theme.surface

    component SectionLabel: Text {
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        font.weight: Font.DemiBold
        font.letterSpacing: 1.2
    }
    component Hint: Text {
        Layout.fillWidth: true
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeM
        wrapMode: Text.WordWrap
        lineHeight: 1.1
    }
    // Rows of a card (no gap between rows; hover highlight slightly wider than the text column).
    component RowList: ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: -Theme.spacingS
        Layout.rightMargin: -Theme.spacingS
        spacing: 0
    }
    component SmallLabel: Text {
        Layout.topMargin: Theme.spacingXs
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.DemiBold
        font.letterSpacing: 1.0
    }

    // A starred instrument: icon, title, run state; click opens, hover shows Run/Stop.
    component FavoriteChip: Rectangle {
        id: chip
        required property string instrumentId
        readonly property var info: InstrumentRegistry.info(instrumentId)
        readonly property InstrumentController controller: InstrumentRegistry.controller(instrumentId)
        readonly property bool running: controller !== null && controller.running
        signal openRequested()

        implicitWidth: chipRow.implicitWidth + 2 * Theme.spacingS + 4
        implicitHeight: 44
        radius: Theme.radiusL
        color: chipHover.hovered ? Theme.surfaceHover : Theme.surfaceRaised
        border.width: 1
        border.color: chipHover.hovered ? Theme.borderStrong : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        Accessible.role: Accessible.Button
        Accessible.name: info.title ?? instrumentId

        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: chip.openRequested() }

        RowLayout {
            id: chipRow
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spacingS
            spacing: Theme.spacingS
            Rectangle {
                implicitWidth: 28
                implicitHeight: 28
                radius: Theme.radiusM
                color: Theme.surfaceSunken
                border.width: 1
                border.color: Theme.border
                Icon {
                    anchors.centerIn: parent
                    name: chip.info.icon ?? ""
                    color: chip.running ? Theme.success : Theme.text
                }
            }
            Text {
                text: chip.info.title ?? chip.instrumentId
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
            }
            StatusDot {
                visible: chip.running || (chip.controller !== null && (chip.controller.paused || chip.controller.hasError))
                color: chip.running ? Theme.success : chip.controller && chip.controller.paused ? Theme.warning : Theme.danger
                pulse: chip.running
                size: 7
            }
            IconButton {
                size: 26
                iconSize: Theme.iconSizeS
                iconName: chip.running ? "stop-filled" : "play"
                iconColor: !enabled ? Theme.textDisabled : chip.running ? Theme.stop
                           : chipHover.hovered ? Theme.run : Theme.textMuted
                enabled: chip.controller !== null && (chip.running || chip.controller.canRun)
                toolTip: chip.running ? qsTr("Stop") : qsTr("Run")
                onClicked: chip.controller.toggle()
            }
        }
    }

    ToolDialog { id: toolDialog }
    FileDialog {
        id: pinDialog
        title: qsTr("Pin a file to Home")
        fileMode: FileDialog.OpenFiles
        currentFolder: DataExport.lastFolder
        onAccepted: {
            for (const file of selectedFiles)
                Launcher.pinFile(file)
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: page.implicitHeight + 2 * Theme.spacingXl
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: flick }

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
                    text: qsTr("Getting started")
                    iconName: "book"
                    tone: "ghost"
                    toolTip: qsTr("Open the getting-started help page")
                    onClicked: home.workspace.showHelp("app")
                }
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
                id: columns
                Layout.fillWidth: true
                spacing: Theme.spacingXl

                // Main column: favorites, instrument tiles, tools / files / resources
                ColumnLayout {
                    id: mainColumn
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingM

                    SectionLabel { text: qsTr("FAVORITES") }
                    Flow {
                        Layout.fillWidth: true
                        visible: home.favoriteIds.length > 0
                        spacing: Theme.spacingS
                        Repeater {
                            model: home.favoriteIds
                            delegate: FavoriteChip {
                                required property string modelData
                                instrumentId: modelData
                                onOpenRequested: home.workspace.open(instrumentId)
                            }
                        }
                    }
                    Rectangle {
                        visible: home.favoriteIds.length === 0
                        Layout.fillWidth: true
                        implicitHeight: 44
                        radius: Theme.radiusL
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM
                            anchors.rightMargin: Theme.spacingM
                            spacing: Theme.spacingS
                            Icon { name: "star"; color: Theme.textMuted }
                            Hint {
                                text: qsTr("Star an instrument (the ☆ on its tile) to keep it here, one click away.")
                                wrapMode: Text.NoWrap
                                elide: Text.ElideRight
                            }
                        }
                    }

                    SectionLabel {
                        Layout.topMargin: Theme.spacingS
                        text: qsTr("INSTRUMENTS")
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

                    SectionLabel {
                        Layout.topMargin: Theme.spacingS
                        text: qsTr("TOOLS AND FILES")
                    }
                    GridLayout {
                        id: library
                        Layout.fillWidth: true
                        columns: width >= 560 ? 2 : 1
                        columnSpacing: Theme.spacingL
                        rowSpacing: Theme.spacingL

                        // --- Tools: user shortcuts to programs, documents, folders, web pages
                        Card {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 1
                            title: qsTr("Tools")
                            iconName: "grid"
                            headerTools: IconButton {
                                size: 24
                                iconSize: Theme.iconSizeS
                                iconName: "plus"
                                toolTip: qsTr("Add a tool…")
                                onClicked: toolDialog.add()
                            }
                            RowList {
                                visible: Launcher.tools.length > 0
                                Repeater {
                                    model: Launcher.tools
                                    delegate: LauncherRow {
                                        id: toolRow
                                        required property var modelData
                                        required property int index
                                        iconName: home.toolIcon(modelData.kind)
                                        title: modelData.name
                                        subtitle: modelData.target
                                        subtitleElide: Text.ElideMiddle
                                        available: modelData.exists
                                        toolTip: modelData.target
                                        onClicked: Launcher.launchTool(index)
                                        IconButton {
                                            size: 24
                                            iconSize: Theme.iconSizeS
                                            iconName: "more-horizontal"
                                            toolTip: qsTr("More")
                                            onClicked: toolMenu.popup()
                                            Menu {
                                                id: toolMenu
                                                MenuItem { text: qsTr("Edit…"); icon.source: Theme.icon("edit"); onTriggered: toolDialog.edit(toolRow.index) }
                                                MenuItem {
                                                    text: qsTr("Move up")
                                                    icon.source: Theme.icon("chevron-up")
                                                    enabled: toolRow.index > 0
                                                    onTriggered: Launcher.moveTool(toolRow.index, toolRow.index - 1)
                                                }
                                                MenuItem {
                                                    text: qsTr("Move down")
                                                    icon.source: Theme.icon("chevron-down")
                                                    enabled: toolRow.index < Launcher.tools.length - 1
                                                    onTriggered: Launcher.moveTool(toolRow.index, toolRow.index + 1)
                                                }
                                                MenuSeparator {}
                                                MenuItem { text: qsTr("Remove"); icon.source: Theme.icon("trash"); onTriggered: Launcher.removeTool(toolRow.index) }
                                            }
                                        }
                                    }
                                }
                            }
                            Hint {
                                visible: Launcher.tools.length === 0
                                text: qsTr("Add the programs, documents, folders and web pages you use next to the myDAQ (a simulator, a lab sheet, a course page).")
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.spacingXs
                                spacing: Theme.spacingS
                                ActionButton {
                                    compact: true
                                    text: qsTr("Add tool…")
                                    iconName: "plus"
                                    onClicked: toolDialog.add()
                                }
                                ActionButton {
                                    compact: true
                                    tone: "ghost"
                                    text: qsTr("Find more online")
                                    iconName: "external"
                                    toolTip: Launcher.moreToolsUrl
                                    onClicked: Launcher.openUrl(Launcher.moreToolsUrl)
                                }
                            }
                        }

                        // --- Files: pinned files and the last exports
                        Card {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 1
                            title: qsTr("Files")
                            iconName: "file"
                            headerTools: IconButton {
                                size: 24
                                iconSize: Theme.iconSizeS
                                iconName: "pin"
                                toolTip: qsTr("Pin a file…")
                                onClicked: pinDialog.open()
                            }

                            SmallLabel {
                                visible: Launcher.pinnedFiles.length > 0
                                text: qsTr("PINNED")
                            }
                            RowList {
                                visible: Launcher.pinnedFiles.length > 0
                                Repeater {
                                    model: Launcher.pinnedFiles
                                    delegate: LauncherRow {
                                        id: pinnedRow
                                        required property var modelData
                                        iconName: home.fileIcon(modelData.suffix)
                                        title: modelData.name
                                        subtitle: modelData.folder
                                        subtitleElide: Text.ElideMiddle
                                        available: modelData.exists
                                        toolTip: modelData.path
                                        onClicked: Launcher.openFile(modelData.path)
                                        IconButton {
                                            size: 24
                                            iconSize: Theme.iconSizeS
                                            iconName: "folder-open"
                                            toolTip: qsTr("Show in folder")
                                            onClicked: Launcher.showInFolder(pinnedRow.modelData.path)
                                        }
                                        IconButton {
                                            size: 24
                                            iconSize: Theme.iconSizeS
                                            iconName: "pin-off"
                                            toolTip: qsTr("Unpin")
                                            onClicked: Launcher.unpinFile(pinnedRow.modelData.path)
                                        }
                                    }
                                }
                            }

                            SmallLabel {
                                visible: Launcher.recentExports.length > 0
                                text: qsTr("RECENT EXPORTS")
                            }
                            RowList {
                                visible: Launcher.recentExports.length > 0
                                Repeater {
                                    model: Launcher.recentExports
                                    delegate: LauncherRow {
                                        id: exportRow
                                        required property var modelData
                                        iconName: home.fileIcon(modelData.suffix)
                                        title: modelData.name
                                        subtitle: modelData.folder
                                        subtitleElide: Text.ElideMiddle
                                        trailing: home.shortDate(modelData.modified)
                                        available: modelData.exists
                                        toolTip: modelData.path
                                        onClicked: Launcher.openFile(modelData.path)
                                        IconButton {
                                            size: 24
                                            iconSize: Theme.iconSizeS
                                            iconName: "folder-open"
                                            toolTip: qsTr("Show in folder")
                                            onClicked: Launcher.showInFolder(exportRow.modelData.path)
                                        }
                                        IconButton {
                                            size: 24
                                            iconSize: Theme.iconSizeS
                                            iconName: "pin"
                                            toolTip: qsTr("Pin")
                                            visible: Launcher.pinnedFiles.length >= 0 && !Launcher.isPinned(exportRow.modelData.path)   // re-evaluated on pin
                                            onClicked: Launcher.pinFile(exportRow.modelData.path)
                                        }
                                        IconButton {
                                            size: 24
                                            iconSize: Theme.iconSizeS
                                            iconName: "close"
                                            toolTip: qsTr("Remove from the list")
                                            onClicked: Launcher.removeRecentExport(exportRow.modelData.path)
                                        }
                                    }
                                }
                            }
                            Hint {
                                visible: Launcher.pinnedFiles.length === 0 && Launcher.recentExports.length === 0
                                text: qsTr("Data, images and PDFs you export appear here. Pin the files you open often (lab sheets, datasheets, reference data).")
                            }
                            ActionButton {
                                Layout.topMargin: Theme.spacingXs
                                compact: true
                                text: qsTr("Pin file…")
                                iconName: "pin"
                                onClicked: pinDialog.open()
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
                        headerTools: IconButton {
                            visible: SessionManager.recentSessions.length > 0
                            size: 24
                            iconSize: Theme.iconSizeS
                            iconName: "folder-open"
                            toolTip: qsTr("Open session…")
                            onClicked: home.openSessionRequested()
                        }
                        RowList {
                            visible: SessionManager.recentSessions.length > 0
                            Repeater {
                                model: SessionManager.recentSessions
                                delegate: LauncherRow {
                                    id: recentItem
                                    required property var modelData
                                    iconName: "layers"
                                    title: modelData.name
                                    subtitle: modelData.path
                                    subtitleElide: Text.ElideMiddle
                                    trailing: home.shortDate(modelData.modified)
                                    available: modelData.exists
                                    toolTip: modelData.exists ? modelData.path : qsTr("%1 (not found)").arg(modelData.path)
                                    onClicked: {
                                        if (modelData.exists)
                                            home.openRecentRequested(modelData.file)
                                    }
                                    IconButton {
                                        size: 24
                                        iconSize: Theme.iconSizeS
                                        iconName: "close"
                                        toolTip: qsTr("Remove from the list")
                                        onClicked: SessionManager.removeRecentSession(recentItem.modelData.file)
                                    }
                                }
                            }
                        }
                        Hint {
                            visible: SessionManager.recentSessions.length === 0
                            text: qsTr("Save the workspace and every instrument's settings as a .mybench session to find it here.")
                        }
                    }

                    // --- Resources: getting started, help pages, documentation links, About
                    Card {
                        Layout.fillWidth: true
                        title: qsTr("Resources")
                        iconName: "book"

                        RowList {
                            LauncherRow {
                                iconName: "book"
                                iconColor: Theme.accent
                                title: qsTr("Getting started with %1").arg(AppInfo.name)
                                subtitle: qsTr("The window, sessions and shortcuts")
                                onClicked: home.workspace.showHelp("app")
                            }
                        }
                        SmallLabel { text: qsTr("INSTRUMENT HELP") }
                        Flow {
                            Layout.fillWidth: true
                            Layout.bottomMargin: Theme.spacingXs
                            spacing: 6
                            Repeater {
                                model: InstrumentRegistry.ids.filter(id => Help.has(InstrumentRegistry.info(id).helpId ?? id))
                                delegate: ActionButton {
                                    required property string modelData
                                    readonly property var info: InstrumentRegistry.info(modelData)
                                    compact: true
                                    text: info.shortTitle ?? modelData
                                    toolTip: qsTr("Help: %1").arg(info.title ?? modelData)
                                    onClicked: home.workspace.showHelp(info.helpId ?? modelData)
                                }
                            }
                        }
                        SmallLabel { text: qsTr("DOCUMENTATION") }
                        RowList {
                            Repeater {
                                model: Launcher.resources
                                delegate: LauncherRow {
                                    required property var modelData
                                    iconName: modelData.icon
                                    title: modelData.title
                                    subtitle: modelData.subtitle
                                    toolTip: modelData.url
                                    onClicked: Launcher.openUrl(modelData.url)
                                    Icon {
                                        name: "external"
                                        size: Theme.iconSizeS
                                        color: Theme.textMuted
                                    }
                                }
                            }
                            LauncherRow {
                                iconName: "info"
                                title: qsTr("About %1").arg(AppInfo.name)
                                subtitle: qsTr("Version %1 · license · credits").arg(AppInfo.version)
                                onClicked: Launcher.showAbout()
                            }
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
                                { keys: "Ctrl+Shift+D", text: qsTr("Dashboard column") },
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
