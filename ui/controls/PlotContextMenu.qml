pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Plot

// The context menu of a plot (docs/UI.md §6 "Plots"; native outside screenshot/test mode):
// Autoscale, Reset View, Undo Zoom, the autoscale locks, Show Cursors, Time Window ▸ and Clear
// (trends), Trace Style…, Copy Image, Copy Data, Export Image…, Export Data…. BenchPlot opens it on
// Plot.contextMenuRequested; the export, style, window and clear entries are signals.
//     PlotContextMenu { id: menu; plot: thePlot; onExportDataRequested: ... }
Menu {
    id: root

    property Plot plot: null
    property bool cursorsAvailable: false
    property bool cursorsEnabled: false
    property bool traceStyleAvailable: false
    property bool exportAvailable: true
    property bool copyDataAvailable: false
    property var timeWindows: []        // [{ text, seconds }]
    property int timeWindowIndex: -1
    property bool clearAvailable: false
    // Zoom and autoscale entries only for interactive plots (not for fixed previews).
    readonly property bool zoomable: plot !== null && plot.interactive

    signal cursorsToggled(bool on)
    signal traceStyleRequested()
    signal exportImageRequested()
    signal exportDataRequested()
    signal copyDataRequested()
    signal timeWindowActivated(int index)
    signal clearRequested()

    // Hidden entries take no room in the in-window rendering.
    component OptionalItem: MenuItem {
        height: visible ? implicitHeight : 0
    }
    component OptionalSeparator: MenuSeparator {
        height: visible ? implicitHeight : 0
    }

    OptionalItem {
        visible: root.zoomable
        text: qsTr("Autoscale")
        onTriggered: root.plot.autoscale()
    }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Reset View")
        onTriggered: root.plot.resetView()
    }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Undo Zoom")
        enabled: root.plot !== null && root.plot.canUndoZoom
        onTriggered: root.plot.undoZoom()
    }
    OptionalSeparator { visible: root.zoomable }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Follow X Data")
        checkable: true
        checked: root.plot !== null && root.plot.xAutoscaleLock
        onTriggered: root.plot.xAutoscaleLock = checked
    }
    OptionalItem {
        visible: root.zoomable
        text: qsTr("Follow Y Data")
        checkable: true
        checked: root.plot !== null && root.plot.yAutoscaleLock
        onTriggered: root.plot.yAutoscaleLock = checked
    }
    OptionalItem {
        visible: root.cursorsAvailable
        text: qsTr("Show Cursors")
        checkable: true
        checked: root.cursorsEnabled
        onTriggered: root.cursorsToggled(checked)
    }
    Menu {
        id: windowMenu
        title: qsTr("Time Window")
        Instantiator {
            model: root.timeWindows
            delegate: MenuItem {
                required property var modelData
                required property int index
                text: modelData.text
                checkable: true
                checked: index === root.timeWindowIndex
                onTriggered: root.timeWindowActivated(index)
            }
            onObjectAdded: (index, object) => windowMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => windowMenu.removeItem(object as MenuItem)
        }
    }
    OptionalItem {
        id: clearItem
        visible: root.clearAvailable
        text: qsTr("Clear")
        onTriggered: root.clearRequested()
    }
    OptionalSeparator { visible: root.zoomable || root.cursorsAvailable || root.clearAvailable }
    OptionalItem {
        visible: root.traceStyleAvailable
        text: qsTr("Trace Style…")
        onTriggered: root.traceStyleRequested()
    }
    MenuItem {
        text: qsTr("Copy Image")
        enabled: root.plot !== null
        onTriggered: root.plot.copyImage()
    }
    OptionalItem {
        visible: root.copyDataAvailable
        text: qsTr("Copy Data")
        onTriggered: root.copyDataRequested()
    }
    OptionalItem {
        visible: root.exportAvailable
        text: qsTr("Export Image…")
        onTriggered: root.exportImageRequested()
    }
    OptionalItem {
        visible: root.exportAvailable
        text: qsTr("Export Data…")
        onTriggered: root.exportDataRequested()
    }

    // The Time Window submenu exists only for trend plots (taken out of the menu otherwise; kept
    // alive by this property).
    readonly property Menu timeWindowMenu: windowMenu
    function indexOf(item: QtObject): int {
        for (let i = 0; i < root.count; ++i) {
            if (root.itemAt(i) === item || root.menuAt(i) === item)
                return i
        }
        return -1
    }
    function syncWindowMenu() {
        const index = indexOf(windowMenu)
        if (root.timeWindows.length === 0 && index >= 0)
            root.takeMenu(index)
        else if (root.timeWindows.length > 0 && index < 0)
            root.insertMenu(Math.max(0, indexOf(clearItem)), windowMenu)
    }
    onTimeWindowsChanged: syncWindowMenu()
    Component.onCompleted: syncWindowMenu()
}
