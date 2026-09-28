import QtQuick
import QtQuick.Controls
import MyBench.Plot

// Right-click menu of a plot (app.md 8.3): Autoscale X/Y, autoscale locks, Undo/Reset zoom,
// Cursors on/off (when the host has cursors), Trace style…, Copy image, Export image…/data….
// BenchPlot opens it on Plot.contextMenuRequested; the export and style entries are signals.
//     PlotContextMenu { id: menu; plot: thePlot; onExportDataRequested: ... }
//     Connections { target: thePlot; function onContextMenuRequested(p) { menu.popup(thePlot, p) } }
Menu {
    id: root

    property Plot plot: null
    property bool cursorsAvailable: false
    property bool cursorsEnabled: false
    property bool traceStyleAvailable: false
    property bool exportAvailable: true
    property bool copyDataAvailable: false
    // Zoom and autoscale entries only for interactive plots (not for fixed previews).
    readonly property bool zoomable: plot !== null && plot.interactive

    signal cursorsToggled(bool on)
    signal traceStyleRequested()
    signal exportImageRequested()
    signal exportDataRequested()
    signal copyDataRequested()

    // Hidden entries take no room.
    component OptionalItem: MenuItem {
        height: visible ? implicitHeight : 0
    }
    component OptionalSeparator: MenuSeparator {
        height: visible ? implicitHeight : 0
    }

    OptionalItem {
        visible: root.zoomable
        text: qsTr("Autoscale X")
        icon.source: Theme.icon("zoom-x")
        enabled: root.plot !== null
        onTriggered: root.plot.autoscaleX()
    }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Autoscale Y")
        icon.source: Theme.icon("zoom-y")
        enabled: root.plot !== null
        onTriggered: root.plot.autoscaleY()
    }
    OptionalItem {
        visible: root.zoomable
        readonly property bool locked: root.plot !== null && root.plot.xAutoscaleLock
        text: locked ? qsTr("Unlock X autoscale") : qsTr("Lock X autoscale")
        icon.source: Theme.icon(locked ? "unlock" : "lock")
        onTriggered: root.plot.xAutoscaleLock = !locked
    }
    OptionalItem {
        visible: root.zoomable
        readonly property bool locked: root.plot !== null && root.plot.yAutoscaleLock
        text: locked ? qsTr("Unlock Y autoscale") : qsTr("Lock Y autoscale")
        icon.source: Theme.icon(locked ? "unlock" : "lock")
        onTriggered: root.plot.yAutoscaleLock = !locked
    }
    OptionalSeparator { visible: root.zoomable }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Undo zoom")
        icon.source: Theme.icon("undo")
        enabled: root.plot !== null && root.plot.canUndoZoom
        onTriggered: root.plot.undoZoom()
    }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Reset zoom")
        icon.source: Theme.icon("reset")
        enabled: root.plot !== null
        onTriggered: root.plot.resetView()
    }
    OptionalSeparator { visible: root.zoomable && (root.cursorsAvailable || root.traceStyleAvailable) }
    OptionalItem {
        visible: root.cursorsAvailable
        text: root.cursorsEnabled ? qsTr("Hide cursors") : qsTr("Show cursors")
        icon.source: Theme.icon("crosshair")
        onTriggered: root.cursorsToggled(!root.cursorsEnabled)
    }
    OptionalItem {
        visible: root.traceStyleAvailable
        text: qsTr("Trace style…")
        icon.source: Theme.icon("edit")
        onTriggered: root.traceStyleRequested()
    }
    OptionalSeparator { visible: root.zoomable || root.cursorsAvailable || root.traceStyleAvailable }
    MenuItem {
        text: qsTr("Copy image")
        icon.source: Theme.icon("copy")
        enabled: root.plot !== null
        onTriggered: root.plot.copyImage()
    }
    OptionalItem {
        visible: root.copyDataAvailable
        text: qsTr("Copy data")
        icon.source: Theme.icon("copy")
        onTriggered: root.copyDataRequested()
    }
    OptionalItem {
        visible: root.exportAvailable
        text: qsTr("Export image…")
        icon.source: Theme.icon("image")
        onTriggered: root.exportImageRequested()
    }
    OptionalItem {
        visible: root.exportAvailable
        text: qsTr("Export data…")
        icon.source: Theme.icon("download")
        onTriggered: root.exportDataRequested()
    }
}
