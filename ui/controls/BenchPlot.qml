pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import MyBench.Framework
import MyBench.Plot

// MyBench.Plot's Plot in the theme (docs/UI.md §2 "Display tokens", §6 "Plots"): every color role
// (display surface, grid, frame, axis text, titles, series colors, in-plot panels) and the font
// (caption) come from Theme, so plots follow light/dark and "Always Use Dark Displays". Use it
// instead of Plot in panels and tiles; every Plot property, element and method is available.
//     BenchPlot { anchors.fill: parent; PlotTrace { buffer: controller.ch0; color: Theme.signalCh0 } }
//     BenchPlot { bare: true; ... }     // tiles: transparent, no frame/labels/interaction
//
// Interactive plots get a PlotToolbar at the top right of the plot area while the pointer is over
// the plot (`tools`; `toolbarItems` adds items at its left, e.g. a time-window selector) and a
// context menu on right click (`contextMenu`). Export routes to ShellActions with `instrumentId`
// (default: the enclosing InstrumentPanel's), else to the plot's own PNG/CSV export. "Trace
// Style…" opens a TraceStyleDialog for `traceStyles` (default: the panel's), "Show Cursors" appears
// with `cursorsAvailable`, "Copy Data" with `copyDataAvailable`, "Time Window ▸" with `timeWindows`,
// "Clear" with `clearAvailable`.
Plot {
    id: plot

    // Minimal look for tiles and previews: transparent, no frame, no labels, no interaction.
    property bool bare: false
    property bool tools: !bare
    // Toolbar shown without hover (screenshots, documentation; `--show-plot-tools`).
    property bool toolsAlwaysVisible: AppInfo.flag("show-plot-tools")
    property bool contextMenu: true
    property list<Item> toolbarItems
    property string instrumentId: ""
    property bool exportDataHandled: false
    property bool copyDataAvailable: false
    property bool cursorsAvailable: false
    property bool cursorsEnabled: false
    property var traceStyles: []
    property bool traceStyleAvailable: effectiveTraceStyles.length > 0
    // Trend plots (TrendView): [{ text, seconds }], the current index, and Clear.
    property var timeWindows: []
    property int timeWindowIndex: -1
    property bool clearAvailable: false

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
    signal timeWindowActivated(int index)
    signal clearRequested()

    colorScheme: Theme.displayDark ? Plot.Dark : Plot.Light
    backgroundColor: bare ? "transparent" : Theme.display
    plotAreaColor: bare ? "transparent" : Theme.display
    gridColor: Theme.plotGridMajor
    minorGridColor: Theme.plotGridMinor
    frameColor: Theme.plotFrame
    textColor: Theme.displayTextSecondary
    titleColor: Theme.displayText
    accentColor: Theme.accent
    panelColor: Theme.displayDark ? Theme.mix(Theme.display, "#FFFFFF", 0.07) : Theme.base
    seriesColors: Theme.seriesColors
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeCaption
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
        active: plot.tools && plot.interactive && (internal.toolsWanted || plot.toolsAlwaysVisible || plot.toolbarItems.length > 0)
        x: plot.plotArea.x + plot.plotArea.width - width - Theme.spacingXs
        y: plot.plotArea.y + Theme.spacingXs
        z: 100
        visible: plot.plotArea.width >= width + 40 && plot.plotArea.height >= height + 40
        sourceComponent: PlotToolbar {
            plot: plot
            shown: hover.hovered || plot.toolsAlwaysVisible
            extras: plot.toolbarItems
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
            copyDataAvailable: plot.copyDataAvailable
            timeWindows: plot.timeWindows
            timeWindowIndex: plot.timeWindowIndex
            clearAvailable: plot.clearAvailable
            onCursorsToggled: on => {
                plot.cursorsEnabled = on
                plot.cursorsToggled(on)
            }
            onTraceStyleRequested: plot.openTraceStyles()
            onExportImageRequested: plot.exportImage()
            onExportDataRequested: plot.exportData()
            onCopyDataRequested: plot.copyDataRequested()
            onTimeWindowActivated: index => plot.timeWindowActivated(index)
            onClearRequested: plot.clearRequested()
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
            title: fileLoader.csv ? qsTr("Export Plot Data") : qsTr("Export Plot Image")
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
