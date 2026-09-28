pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Layouts
import MyBench.Framework

// Frame of every instrument panel (docs/UI.md, app.md 3.2): the display area (default
// children) with the header actions (Export ▾ data/image/PDF, Help, ⋯ Reset to defaults, Focus,
// settings toggle), banners for pause/errors/reconnection, the Inspector (docked and resizable
// when the panel is wide, an overlay drawer when it is narrower than `narrowWidth`) and the
// RunBar. Shell shortcuts reach it through ShellActions (Ctrl+I, Ctrl+L, Ctrl+P, focus mode).
//     InstrumentPanel {
//         controller: root.controller
//         displayTitle: "CH 0 · CH 1"
//         displayInfo: Units.format(root.controller.sampleRate, "S/s")
//         BenchPlot { anchors.fill: parent }                  // display content
//         inspector: [
//             Section { title: qsTr("Channel 0 Settings"); accentColor: Theme.ch0 },
//             Section { title: qsTr("Timebase") }
//         ]
//         runBar.showTimeout: true
//         runBarControls: [ ActionButton { text: qsTr("Autoscale") } ]
//         runBarActions: [ ExportButton { controller: root.controller }, HelpButton { topic: "scope" } ]
//     }
Rectangle {
    id: root

    property InstrumentController controller: null
    property string displayTitle
    property string displayInfo
    // Wide panels: the inspector is docked; false = focus mode (display only).
    property bool inspectorVisible: true
    // Narrow panels: the inspector is an overlay, opened with the header button.
    property bool inspectorOverlayOpen: false
    property int narrowWidth: 820
    property int inspectorWidth: Theme.inspectorWidth
    property bool showErrorBanner: true
    property bool showExport: true             // Export ▾ menu in the display header
    // [{ name, style: TraceStyle, color }]: adds "Trace style…" to the ⋯ menu (and to the right-click
    // menu of the panel's BenchPlots) opening a TraceStyleDialog for these traces.
    property var traceStyles: []
    readonly property string instrumentId: controller ? controller.instrumentId : ""
    readonly property bool focusMode: instrumentId.length > 0 && ShellActions.focusInstrument === instrumentId
    readonly property bool narrow: width < narrowWidth
    readonly property bool docked: inspectorVisible && !narrow && !focusMode
    default property alias display: displayArea.content
    property alias displayTools: displayArea.tools
    property alias inspector: inspectorItem.sections
    property alias runBar: runBar
    property alias runBarActions: runBar.actions
    property alias runBarControls: runBar.extraControls
    property alias displayArea: displayArea
    property alias inspectorItem: inspectorItem

    color: Theme.surface
    onNarrowChanged: inspectorOverlayOpen = false
    // Inspector sections remember their collapsed state per instrument (session view state).
    function keySections() {
        if (instrumentId.length === 0)
            return
        for (const section of inspectorItem.sections) {
            if (section instanceof Section && section.stateKey.length === 0 && section.title.length > 0)
                section.stateKey = instrumentId + "/" + section.title
        }
    }
    onInstrumentIdChanged: keySections()
    Component.onCompleted: keySections()

    function openTraceStyleDialog(index = 0) {
        traceStyleLoader.active = true
        const dialog = traceStyleLoader.item as TraceStyleDialog
        if (dialog)
            dialog.openFor(index)
    }
    function toggleInspector() {
        if (narrow)
            inspectorOverlayOpen = !inspectorOverlayOpen
        else
            inspectorVisible = !inspectorVisible
    }
    function exportData() {
        if (!controller || !controller.hasData) {
            Notifications.notify(Notifications.Info, qsTr("Nothing to export yet"), qsTr("Run the instrument first."))
            return
        }
        dataDialog.currentFolder = DataExport.lastFolderFor("csv")
        dataDialog.selectedFile = DataExport.suggestedFileUrl(instrumentId, "csv")
        dataDialog.open()
    }
    // Export image… / Export PDF… / Print… (app.md 10.2): the Print options first (PrintDialog:
    // ink saver, user/computer/time stamp, settings table, orientation, printer, copies), then
    // the file (image, PDF) or the printer.
    function exportImage(pdf: bool) {
        openPrintDialog(pdf ? "pdf" : "image")
    }
    function printPanel() {
        openPrintDialog(DataExport.printingAvailable ? "printer" : "pdf")
    }
    function openPrintDialog(destination: string) {
        printDialog.destination = destination
        printDialog.open()
    }
    function continuePrint() {
        const options = printDialog.options()
        if (options.destination === "printer") {
            writeImage("", options)
            return
        }
        const pdf = options.destination === "pdf"
        imageDialog.pdf = pdf
        imageDialog.exportOptions = options
        imageDialog.currentFolder = DataExport.lastFolderFor(pdf ? "pdf" : "png")
        imageDialog.selectedFile = DataExport.suggestedFileUrl(instrumentId, pdf ? "pdf" : "png")
        imageDialog.open()
    }
    // Grabs the display (or the whole panel, options.target) at 2x and writes it. `options`:
    // PrintDialog.options(), or a bool for the old signature (true = PDF, default options).
    function writeImage(file: url, options: var) {
        const o = typeof options === "boolean" ? { destination: options ? "pdf" : "image" } : options
        const title = controller ? controller.title : ""
        const metadata = controller ? controller.exportMetadata() : {}
        const source = o.target === "panel" ? root : displayArea
        source.grabToImage(result => {
            const temporary = DataExport.temporaryFilePath("png")
            if (!result.saveToFile(temporary)) {
                Notifications.notify(Notifications.Error, qsTr("Image not saved"), temporary)
                return
            }
            const path = DataExport.localPath(file)
            if (o.destination === "printer") {
                if (DataExport.print(temporary, title, metadata, o))
                    Notifications.notify(Notifications.Success, qsTr("Sent to the printer"), o.printer ?? "")
                else
                    Notifications.notify(Notifications.Error, qsTr("Not printed"), DataExport.lastError)
            } else if (o.destination === "pdf") {
                if (DataExport.writePdf(file, temporary, title, metadata, o))
                    Notifications.notify(Notifications.Success, qsTr("PDF exported"), path)
                else
                    Notifications.notify(Notifications.Error, qsTr("PDF not saved"), DataExport.lastError)
            } else {
                if (DataExport.writeImage(file, temporary, title, metadata, o))
                    Notifications.notify(Notifications.Success, qsTr("Image exported"), path)
                else
                    Notifications.notify(Notifications.Error, qsTr("Image not saved"), DataExport.lastError)
            }
        }, Qt.size(source.width * 2, source.height * 2))
    }

    Connections {
        target: ShellActions
        function onInspectorToggleRequested(id: string) { if (id === root.instrumentId) root.toggleInspector() }
        function onExportDataRequested(id: string) { if (id === root.instrumentId) root.exportData() }
        function onExportImageRequested(id: string) { if (id === root.instrumentId) root.exportImage(false) }
        function onExportPdfRequested(id: string) { if (id === root.instrumentId) root.exportImage(true) }
        function onPrintRequested(id: string) { if (id === root.instrumentId) root.printPanel() }
    }

    Loader {
        id: traceStyleLoader
        active: false
        sourceComponent: TraceStyleDialog { entries: root.traceStyles }
    }
    FileDialog {
        id: dataDialog
        title: qsTr("Export %1 data").arg(root.controller ? root.controller.title : "")
        fileMode: FileDialog.SaveFile
        nameFilters: DataExport.nameFiltersFor(root.instrumentId)
        onAccepted: root.controller.exportData(selectedFile)
    }
    PrintDialog {
        id: printDialog
        instrumentTitle: root.controller ? root.controller.title : ""
        onAccepted: root.continuePrint()
    }
    FileDialog {
        id: imageDialog
        property bool pdf: false
        property var exportOptions: ({})
        title: pdf ? qsTr("Export PDF") : qsTr("Export image")
        fileMode: FileDialog.SaveFile
        nameFilters: pdf ? [qsTr("PDF (*.pdf)")] : [qsTr("PNG image (*.png)")]
        defaultSuffix: pdf ? "pdf" : "png"
        onAccepted: root.writeImage(selectedFile, exportOptions)
    }

    Keys.onEscapePressed: event => {
        event.accepted = inspectorOverlayOpen
        inspectorOverlayOpen = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Display column (banners + display area).
            ColumnLayout {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: parent.width - (root.docked ? inspectorHost.width : 0) - 2 * Theme.spacingM
                anchors.margins: Theme.spacingM
                spacing: Theme.spacingS

                Banner {
                    Layout.fillWidth: true
                    visible: root.controller !== null && root.controller.paused
                    tone: "warning"
                    text: root.controller ? root.controller.pauseReason : ""
                    ActionButton {
                        text: qsTr("Resume")
                        iconName: "play"
                        compact: true
                        tone: "subtle"
                        color: Theme.warning
                        onClicked: root.controller.resume()
                    }
                }

                Banner {
                    Layout.fillWidth: true
                    visible: root.controller !== null && root.controller.interruptedByDisconnect && !root.controller.running
                             && DeviceHub.connected
                    tone: "success"
                    text: qsTr("Device reconnected")
                    ActionButton {
                        text: qsTr("Resume")
                        iconName: "play"
                        compact: true
                        tone: "subtle"
                        color: Theme.success
                        onClicked: root.controller.run()
                    }
                }

                // A run refused because of the user's input (InvalidInput): a warning, not an error.
                Banner {
                    Layout.fillWidth: true
                    visible: root.showErrorBanner && root.controller !== null && !root.controller.hasError
                             && root.controller.inputWarning.length > 0 && !root.controller.running
                    tone: "warning"
                    text: root.controller ? root.controller.inputWarning : ""
                    closable: true
                    onClosed: root.controller.clearInputWarning()
                }

                Banner {
                    Layout.fillWidth: true
                    visible: root.showErrorBanner && root.controller !== null && root.controller.hasError
                             && !root.controller.running
                    tone: "danger"
                    text: root.controller ? root.controller.errorText : ""
                    closable: true
                    onClosed: root.controller.clearError()
                    ActionButton {
                        text: qsTr("Details")
                        compact: true
                        tone: "subtle"
                        color: Theme.accent
                        toolTip: qsTr("Show the event log with the technical details")
                        onClicked: ShellActions.eventLogRequested()
                    }
                }

                DisplayArea {
                    id: displayArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    alwaysShowHeader: true
                    title: root.displayTitle
                    info: root.displayInfo
                    trailingTools: [
                        IconButton {
                            visible: root.showExport
                            iconName: "export"
                            size: 26
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("Export")
                            onClicked: exportMenu.popup()
                            Menu {
                                id: exportMenu
                                MenuItem {
                                    text: qsTr("Export data…")
                                    icon.source: Theme.icon("download")
                                    enabled: root.controller !== null && root.controller.hasData
                                    onTriggered: root.exportData()
                                }
                                MenuItem { text: qsTr("Export image…"); icon.source: Theme.icon("image"); onTriggered: root.exportImage(false) }
                                MenuItem { text: qsTr("Export PDF…"); icon.source: Theme.icon("file"); onTriggered: root.exportImage(true) }
                                MenuItem {
                                    visible: DataExport.printingAvailable
                                    height: visible ? implicitHeight : 0
                                    text: qsTr("Print…")
                                    icon.source: Theme.icon("printer")
                                    onTriggered: root.printPanel()
                                }
                            }
                        },
                        HelpButton {
                            size: 26
                            iconSize: Theme.iconSizeS
                            topic: InstrumentRegistry.info(root.instrumentId).helpId ?? "app"
                        },
                        IconButton {
                            iconName: "more-horizontal"
                            size: 26
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("More")
                            onClicked: moreMenu.popup()
                            Menu {
                                id: moreMenu
                                MenuItem {
                                    text: qsTr("Reset to defaults")
                                    icon.source: Theme.icon("reset")
                                    enabled: root.controller !== null
                                    onTriggered: root.controller.resetSettings()
                                }
                                MenuItem {
                                    text: qsTr("Trace style…")
                                    icon.source: Theme.icon("edit")
                                    visible: root.traceStyles.length > 0
                                    height: visible ? implicitHeight : 0
                                    onTriggered: root.openTraceStyleDialog()
                                }
                                MenuItem {
                                    text: root.focusMode ? qsTr("Exit focus mode") : qsTr("Focus mode")
                                    icon.source: Theme.icon("maximize")
                                    onTriggered: ShellActions.toggleFocus(root.instrumentId)
                                }
                            }
                        },
                        IconButton {
                            iconName: "maximize"
                            size: 26
                            iconSize: Theme.iconSizeS
                            checked: root.focusMode
                            toolTip: root.focusMode ? qsTr("Exit focus mode") : qsTr("Focus mode: display only")
                            shortcutText: Theme.keys("Ctrl+Shift+F")
                            onClicked: ShellActions.toggleFocus(root.instrumentId)
                        },
                        IconButton {
                            readonly property bool shown: root.narrow ? root.inspectorOverlayOpen : root.inspectorVisible
                            visible: !root.focusMode
                            iconName: shown ? "sidebar-right" : "sliders"
                            size: 26
                            iconSize: Theme.iconSizeS
                            checked: !root.narrow && !root.inspectorVisible
                            toolTip: shown ? qsTr("Hide settings") : qsTr("Show settings")
                            shortcutText: Theme.keys("Ctrl+I")
                            onClicked: root.toggleInspector()
                        }
                    ]
                }
            }

            // Click-away scrim of the overlay inspector.
            MouseArea {
                anchors.fill: parent
                visible: root.narrow && root.inspectorOverlayOpen && !root.focusMode
                onClicked: root.inspectorOverlayOpen = false
                Rectangle {
                    anchors.fill: parent
                    color: Theme.overlay
                    opacity: 0.5
                }
            }

            // Inspector host: docked on the right, or an overlay drawer.
            Item {
                id: inspectorHost
                readonly property bool overlay: root.narrow
                width: overlay ? Math.min(root.inspectorWidth, body.width - 48) : root.inspectorWidth
                height: parent.height
                x: parent.width - (visible ? width : 0)
                visible: root.docked || (root.narrow && root.inspectorOverlayOpen && !root.focusMode)

                Rectangle {
                    visible: !inspectorHost.overlay
                    width: 1
                    height: parent.height
                    color: Theme.divider
                }
                Inspector {
                    id: inspectorItem
                    anchors.fill: parent
                    anchors.leftMargin: inspectorHost.overlay ? 0 : 1
                    layer.enabled: inspectorHost.overlay
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: Theme.shadow
                        shadowBlur: 1.0
                        shadowHorizontalOffset: -4
                        shadowOpacity: Theme.dark ? 0.9 : 0.4
                        blurMax: 32
                    }
                }

                // Resize handle of the docked inspector.
                MouseArea {
                    visible: !inspectorHost.overlay
                    x: -3
                    width: 7
                    height: parent.height
                    cursorShape: Qt.SplitHCursor
                    hoverEnabled: true
                    property real startX: 0
                    property real startWidth: 0
                    onPressed: mouse => {
                        startX = mapToItem(body, mouse.x, 0).x
                        startWidth = root.inspectorWidth
                    }
                    onPositionChanged: mouse => {
                        if (!pressed)
                            return
                        const dx = mapToItem(body, mouse.x, 0).x - startX
                        root.inspectorWidth = Math.max(260, Math.min(520, startWidth - dx, body.width - 360))
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.pressed || parent.containsMouse ? 3 : 0
                        height: parent.height
                        color: parent.pressed ? Theme.accent : Theme.alpha(Theme.accent, 0.6)
                        Behavior on width { NumberAnimation { duration: Theme.durationFast } }
                    }
                }
            }
        }

        RunBar {
            id: runBar
            Layout.fillWidth: true
            controller: root.controller
        }
    }
}
