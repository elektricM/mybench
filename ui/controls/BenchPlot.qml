pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import MyBench.Framework
import MyBench.Plot

// MyBench.Plot's Plot with the shell theme: dark/light color scheme from Theme and the display
// surface as background, so it blends into a DisplayArea. Use it instead of Plot in panels and
// tiles; every Plot property, element and method is available (see ui/plot/README.md).
//     BenchPlot { anchors.fill: parent; PlotTrace { buffer: controller.ch0; color: Theme.ch0 } }
//     BenchPlot { bare: true; ... }     // tiles: transparent, no frame/labels/interaction
//
// Plot tools (app.md 8.3), for interactive non-bare plots: a PlotToolbar at the top right of the
// plot area while the pointer is over the plot (`tools`), and a PlotContextMenu on right click
// (`contextMenu`). Export image/data go to ShellActions with `instrumentId` (by default the id of
// the enclosing InstrumentPanel, so the panel's own export runs), else to the plot's PNG/CSV
// export. "Trace style…" opens a TraceStyleDialog for `traceStyles` (by default the enclosing
// panel's), "Cursors" appears when `cursorsAvailable` (toggles `cursorsEnabled`), "Copy data" when
// `copyDataAvailable` (→ copyDataRequested()); `exportDataHandled: true` leaves Export data… to the
// host (exportDataRequested() only).
//     BenchPlot {
//         cursorsAvailable: true
//         cursorsEnabled: controller.cursorsOn
//         onCursorsToggled: on => controller.cursorsOn = on
//         traceStyles: [{ name: "CH 0", style: controller.ch0Style, color: Theme.ch0 }]
//     }
Plot {
    id: plot

    // Minimal look for tiles and previews: transparent, no frame, no labels, no interaction,
    // no margins (set it, then only add traces).
    property bool bare: false
    // Overlay toolbar (hover) and right-click menu.
    property bool tools: !bare
    // Toolbar shown without hover (screenshots, documentation; `--show-plot-tools` on the command line).
    property bool toolsAlwaysVisible: AppInfo.flag("show-plot-tools")
    property bool contextMenu: true
    // Export routing: ShellActions.export{Data,Image}Requested(id). "" = the enclosing panel's id.
    property string instrumentId: ""
    // true: Export data… only emits exportDataRequested() (the host writes its own data).
    property bool exportDataHandled: false
    // "Copy data" entry of the context menu (the host handles copyDataRequested()).
    property bool copyDataAvailable: false
    // Cursors on/off entry of the context menu.
    property bool cursorsAvailable: false
    property bool cursorsEnabled: false
    // [{ name, style: TraceStyle, color }] for "Trace style…"; [] = the enclosing panel's list.
    property var traceStyles: []
    property bool traceStyleAvailable: effectiveTraceStyles.length > 0

    readonly property InstrumentPanel panel: internal.panel
    readonly property string effectiveInstrumentId: instrumentId.length > 0 ? instrumentId
                                                  : panel ? panel.instrumentId : ""
    readonly property var effectiveTraceStyles: traceStyles.length > 0 ? traceStyles
                                              : panel && panel.traceStyles ? panel.traceStyles : []

    signal cursorsToggled(bool on)
    signal traceStyleRequested()
    signal exportImageRequested()
    signal exportDataRequested()
    signal copyDataRequested()

    colorScheme: Theme.dark ? Plot.Dark : Plot.Light
    backgroundColor: bare ? "transparent" : Theme.display
    plotAreaColor: bare ? "transparent" : undefined
    frameVisible: !bare
    interactive: !bare
    xLabelsVisible: !bare
    yLabelsVisible: !bare
    leftMargin: bare ? 0 : -1
    rightMargin: bare ? 0 : -1
    topMargin: bare ? 2 : -1
    bottomMargin: bare ? 2 : -1

    function openContextMenu(point: point) {
        menuLoader.active = true
        const menu = menuLoader.item as PlotContextMenu
        if (menu)
            menu.popup(plot, point)
    }
    function openTraceStyles() {
        plot.traceStyleRequested()
        if (plot.traceStyles.length > 0) {
            styleLoader.active = true
            const dialog = styleLoader.item as TraceStyleDialog
            if (dialog)
                dialog.openFor(0)
        } else if (plot.panel && plot.effectiveTraceStyles.length > 0) {
            plot.panel.openTraceStyleDialog()
        }
    }
    function exportImage() {
        plot.exportImageRequested()
        if (plot.effectiveInstrumentId.length > 0) {
            ShellActions.exportImageRequested(plot.effectiveInstrumentId)
        } else {
            fileLoader.csv = false
            fileLoader.active = true
            (fileLoader.item as FileDialog).open()
        }
    }
    function exportData() {
        plot.exportDataRequested()
        if (plot.exportDataHandled)
            return
        if (plot.effectiveInstrumentId.length > 0) {
            ShellActions.exportDataRequested(plot.effectiveInstrumentId)
        } else {
            fileLoader.csv = true
            fileLoader.active = true
            (fileLoader.item as FileDialog).open()
        }
    }

    onContextMenuRequested: point => {
        if (plot.contextMenu && !plot.bare)
            plot.openContextMenu(point)
    }
    onImageCopied: ok => Notifications.notify(ok ? Notifications.Success : Notifications.Error,
                                              ok ? qsTr("Image copied") : qsTr("Image not copied"))
    onImageSaved: (path, ok) => {
        if (!internal.savingImage)
            return
        internal.savingImage = false
        Notifications.notify(ok ? Notifications.Success : Notifications.Error,
                             ok ? qsTr("Image exported") : qsTr("Image not saved"), path)
    }
    onParentChanged: internal.findPanel()
    Component.onCompleted: internal.findPanel()

    QtObject {
        id: internal
        property InstrumentPanel panel: null
        property bool toolsWanted: false
        property bool savingImage: false   // our own Export image (toast on imageSaved)
        function findPanel() {
            for (let p = plot.parent; p; p = p.parent) {
                const found = p as InstrumentPanel
                if (found) {
                    panel = found
                    return
                }
            }
            panel = null
        }
    }

    HoverHandler {
        id: hover
        enabled: plot.tools && plot.interactive
        onHoveredChanged: if (hovered) internal.toolsWanted = true
    }

    Loader {
        id: toolbarLoader
        active: plot.tools && plot.interactive && (internal.toolsWanted || plot.toolsAlwaysVisible)
        x: plot.plotArea.x + plot.plotArea.width - width - 6
        y: plot.plotArea.y + 6
        z: 100
        visible: plot.plotArea.width >= width + 60 && plot.plotArea.height >= height + 40
        sourceComponent: PlotToolbar {
            plot: plot
            shown: hover.hovered || plot.toolsAlwaysVisible
        }
    }

    Loader {
        id: menuLoader
        active: false
        sourceComponent: PlotContextMenu {
            plot: plot
            cursorsAvailable: plot.cursorsAvailable
            cursorsEnabled: plot.cursorsEnabled
            traceStyleAvailable: plot.traceStyleAvailable
            onCursorsToggled: on => {
                plot.cursorsEnabled = on
                plot.cursorsToggled(on)
            }
            onTraceStyleRequested: plot.openTraceStyles()
            onExportImageRequested: plot.exportImage()
            onExportDataRequested: plot.exportData()
            copyDataAvailable: plot.copyDataAvailable
            onCopyDataRequested: plot.copyDataRequested()
        }
    }

    Loader {
        id: styleLoader
        active: false
        sourceComponent: TraceStyleDialog {
            entries: plot.traceStyles
        }
    }

    // Fallback export (no instrument): the plot's own PNG / CSV of the visible range.
    Loader {
        id: fileLoader
        property bool csv: false
        active: false
        sourceComponent: FileDialog {
            title: fileLoader.csv ? qsTr("Export plot data") : qsTr("Export plot image")
            fileMode: FileDialog.SaveFile
            nameFilters: fileLoader.csv ? [qsTr("CSV (*.csv)")] : [qsTr("PNG image (*.png)")]
            defaultSuffix: fileLoader.csv ? "csv" : "png"
            currentFolder: DataExport.lastFolder
            onAccepted: {
                if (!fileLoader.csv) {
                    internal.savingImage = plot.saveImage(selectedFile)
                } else if (plot.saveCsv(selectedFile)) {
                    Notifications.notify(Notifications.Success, qsTr("Data exported"), DataExport.localPath(selectedFile))
                } else {
                    Notifications.notify(Notifications.Error, qsTr("Data not saved"), DataExport.localPath(selectedFile))
                }
            }
        }
    }
}
