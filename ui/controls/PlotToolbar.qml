pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import MyBench.Plot

// Overlay tool palette of a plot (app.md 8.3, the ELVISmx graph palette): pointer / zoom (menu:
// box, X only, Y only, zoom in/out, undo, reset) / pan tools, and X / Y autoscale (click = once,
// lock = continuous). BenchPlot shows it at the top right of the plot area while hovered.
//     PlotToolbar { plot: thePlot; shown: hover.hovered }
Rectangle {
    id: root

    property Plot plot: null
    // Hover state of the host (the toolbar fades in); it also stays while its menu is open or it
    // is hovered itself.
    property bool shown: false
    readonly property bool active: shown || hover.hovered || zoomMenu.visible

    implicitWidth: row.implicitWidth + 2 * 3
    implicitHeight: row.implicitHeight + 2 * 3
    radius: Theme.radiusM
    color: Theme.alpha(Theme.surfaceRaised, 0.94)
    border.width: 1
    border.color: Theme.border
    opacity: active ? 1 : 0
    visible: opacity > 0 && plot !== null
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

    HoverHandler { id: hover }

    component PaletteButton: IconButton {
        size: 22
        iconSize: Theme.iconSizeS
    }

    // Autoscale once (axis letter) + lock toggle, drawn as one segmented pair.
    component AxisScale: Row {
        id: axis
        required property string letter
        required property bool locked
        signal once()
        signal lockToggled(bool on)
        spacing: 0
        T.AbstractButton {
            id: onceButton
            implicitWidth: onceRow.implicitWidth + 10
            implicitHeight: 22
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            Accessible.name: qsTr("Autoscale %1").arg(axis.letter)
            onClicked: axis.once()
            background: Rectangle {
                radius: Theme.radiusS
                color: onceButton.down ? Theme.surfacePressed : onceButton.hovered ? Theme.surfaceHover : "transparent"
                FocusRing { shown: onceButton.visualFocus; radius: Theme.radiusS + 3 }
            }
            contentItem: Item {
                Row {
                    id: onceRow
                    anchors.centerIn: parent
                    spacing: 2
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "autoscale"
                        size: 12
                        color: axis.locked ? Theme.accent : onceButton.hovered ? Theme.text : Theme.textSecondary
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: axis.letter
                        color: axis.locked ? Theme.accent : onceButton.hovered ? Theme.text : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeS
                        font.weight: Font.DemiBold
                    }
                }
            }
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("Autoscale %1 once").arg(axis.letter)
        }
        PaletteButton {
            iconName: axis.locked ? "lock" : "unlock"
            iconSize: 12
            width: 18
            checked: axis.locked
            toolTip: axis.locked ? qsTr("%1 autoscale locked: click to unlock").arg(axis.letter)
                                 : qsTr("Lock %1 autoscale (follow the data)").arg(axis.letter)
            onClicked: axis.lockToggled(!axis.locked)
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 1

        PaletteButton {
            iconName: "pointer"
            checked: root.plot !== null && root.plot.dragMode === Plot.NoDrag
            toolTip: qsTr("Pointer: drag cursors and markers only")
            onClicked: root.plot.dragMode = Plot.NoDrag
        }
        PaletteButton {
            id: zoomButton
            readonly property bool zoomTool: root.plot !== null && root.plot.dragMode === Plot.Zoom
            iconName: !zoomTool || root.plot.zoomAxes === Plot.ZoomXY ? "zoom-box"
                    : root.plot.zoomAxes === Plot.ZoomX ? "zoom-x" : "zoom-y"
            checked: zoomTool
            toolTip: qsTr("Zoom")
            shortcutText: Theme.macShortcuts ? qsTr("⌘-drag") : qsTr("Ctrl+drag")
            onClicked: zoomMenu.popup(zoomButton, 0, zoomButton.height + 4)

            Menu {
                id: zoomMenu
                function setZoomTool(axes: int) {
                    root.plot.zoomAxes = axes
                    root.plot.dragMode = Plot.Zoom
                }
                MenuItem {
                    text: qsTr("Zoom box")
                    icon.source: Theme.icon("zoom-box")
                    onTriggered: zoomMenu.setZoomTool(Plot.ZoomXY)
                }
                MenuItem {
                    text: qsTr("Zoom X only")
                    icon.source: Theme.icon("zoom-x")
                    onTriggered: zoomMenu.setZoomTool(Plot.ZoomX)
                }
                MenuItem {
                    text: qsTr("Zoom Y only")
                    icon.source: Theme.icon("zoom-y")
                    onTriggered: zoomMenu.setZoomTool(Plot.ZoomY)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Zoom in")
                    icon.source: Theme.icon("zoom-in")
                    onTriggered: root.plot.zoomAt(Qt.point(root.plot.plotArea.x + root.plot.plotArea.width / 2,
                                                           root.plot.plotArea.y + root.plot.plotArea.height / 2), 1.5, 1.5)
                }
                MenuItem {
                    text: qsTr("Zoom out")
                    icon.source: Theme.icon("zoom-out")
                    onTriggered: root.plot.zoomAt(Qt.point(root.plot.plotArea.x + root.plot.plotArea.width / 2,
                                                           root.plot.plotArea.y + root.plot.plotArea.height / 2), 1 / 1.5, 1 / 1.5)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Undo zoom")
                    icon.source: Theme.icon("undo")
                    enabled: root.plot !== null && root.plot.canUndoZoom
                    onTriggered: root.plot.undoZoom()
                }
                MenuItem {
                    text: qsTr("Reset zoom")
                    icon.source: Theme.icon("reset")
                    onTriggered: root.plot.resetView()
                }
            }
        }
        PaletteButton {
            iconName: "hand"
            checked: root.plot !== null && root.plot.dragMode === Plot.Pan
            toolTip: qsTr("Pan")
            onClicked: root.plot.dragMode = Plot.Pan
        }

        Rectangle {
            width: 1
            height: 14
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.divider
        }
        Item { width: 2; height: 1 }

        AxisScale {
            letter: "X"
            locked: root.plot !== null && root.plot.xAutoscaleLock
            onOnce: root.plot.autoscaleX()
            onLockToggled: on => root.plot.xAutoscaleLock = on
        }
        AxisScale {
            letter: "Y"
            locked: root.plot !== null && root.plot.yAutoscaleLock
            onOnce: root.plot.autoscaleY()
            onLockToggled: on => root.plot.yAutoscaleLock = on
        }
    }
}
