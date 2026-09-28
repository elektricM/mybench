pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Display of the Waveform Editor tab (ELVISmx WaveformEditor.exe): File/Edit toolbar, the
// waveform plot (selected segment highlighted, cursors, sketch drawing) and the segment list with
// the structure buttons. The parameters of the selection are in the inspector.
Item {
    id: root

    required property GenController controller
    required property WaveformEditor editor
    readonly property bool frequencyView: editor.xAxis === 1
    property var pendingAction: null

    function guarded(action) {
        if (editor.modified) {
            pendingAction = action
            unsavedDialog.open()
        } else {
            action()
        }
    }
    function saveThen(action) {
        if (editor.save())
            action()
        else {
            pendingAction = action
            saveDialog.open()
        }
    }
    function save() {
        if (!editor.save())
            saveDialog.open()
    }
    function clearSelection() {
        if (editor.selectionKind === 3)
            clearAllDialog.open()
        else
            editor.deleteSelection()
    }
    // File > Close: back to an empty document and the Generator tab (asks to save first).
    function closeDocument() {
        guarded(() => {
            editor.newDocument()
            controller.activeTab = 0
        })
    }

    // Keyboard shortcuts of the ELVISmx editor (File / Edit / Help menus). They apply while the
    // editor (this view or its inspector cards) has the focus: the ShortcutOverride is accepted
    // so the shell's session shortcuts (Ctrl+N/O/S/W/I) do not fire, and a text field keeps its
    // own editing keys (it accepts the override first).
    function shortcutAction(event: KeyEvent): var {
        if (!root.visible)
            return null
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        const alt = (event.modifiers & Qt.AltModifier) !== 0
        if (alt)
            return null
        if (!ctrl) {
            if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && segmentList.activeFocus)
                return () => root.clearSelection()
            return null
        }
        switch (event.key) {
        case Qt.Key_N: return () => root.guarded(() => root.editor.newDocument())
        case Qt.Key_O: return () => root.guarded(() => openDialog.open())
        case Qt.Key_W: return () => root.closeDocument()
        case Qt.Key_S: return shift ? () => saveDialog.open() : () => root.save()
        case Qt.Key_I: return () => infoDialog.open()
        case Qt.Key_A: return () => root.editor.selectAll()
        case Qt.Key_H: return () => Help.show("gen")
        case Qt.Key_Z: return shift ? () => root.editor.redo() : () => root.editor.undo()
        case Qt.Key_Y: return () => root.editor.redo()
        case Qt.Key_X: return () => root.editor.cut()
        case Qt.Key_C: return () => root.editor.copy()
        case Qt.Key_V: return () => root.editor.paste()
        default: return null
        }
    }
    // Screenshot scenes (GenPanel, dev executable only).
    function showScene(scene: string) {
        if (scene === "settings") {
            settingsDialog.open()
        } else if (scene === "draw") {
            editor.newDocument()
            editor.segmentDuration = 1
            editor.newComponent()
            editor.componentType = 2
            editor.sketchSamples = 50
            editor.drawMode = true
            editor.drawTool = 1
            editor.addVertex(0.3, 0.8)
            editor.addVertex(0.7, -0.5)
        } else if (scene === "unsaved") {
            editor.newSegment()
            controller.channel1.mode = GenChannel.Arbitrary
            controller.loadLibraryWaveform(1, 2)
            controller.editChannel(1, false)
        } else if (scene === "tracestyle") {
            openTraceStyle()
        } else if (scene === "info") {
            infoDialog.open()
        } else if (scene === "menu") {
            fileMenu.popup(fileButton, 0, fileButton.height)
        } else if (scene.startsWith("import:")) {
            importDialog.mode = 0
            importDialog.openFor("file://" + scene.slice(7))
        }
    }
    function openTraceStyle() {
        plot.openTraceStyles()
    }
    function claimsShortcut(event: KeyEvent): bool {
        return shortcutAction(event) !== null
    }
    function handleShortcut(event: KeyEvent) {
        const action = shortcutAction(event)
        if (action) {
            action()
            event.accepted = true
        }
    }

    Keys.onShortcutOverride: event => event.accepted = root.claimsShortcut(event)
    Keys.onPressed: event => root.handleShortcut(event)
    // Showing the editor tab gives it the keyboard focus (its shortcuts work at once).
    onVisibleChanged: if (visible) segmentList.forceActiveFocus()

    Connections {
        target: root.controller
        // Edit on a channel card while this editor holds another, modified document.
        function onEditChannelNeedsConfirmation(channel: int) {
            root.guarded(() => root.controller.editChannel(channel, true))
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        // --- Toolbar (File / Edit menus of ELVISmx) -----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            ActionButton {
                id: fileButton
                text: qsTr("File")
                iconName: "file"
                compact: true
                tone: "ghost"
                onClicked: fileMenu.popup()
                Menu {
                    id: fileMenu
                    width: 260
                    EditorMenuItem { text: qsTr("New"); iconName: "file-plus"; keys: "Ctrl+N"; onTriggered: root.guarded(() => root.editor.newDocument()) }
                    EditorMenuItem { text: qsTr("Open…"); iconName: "folder-open"; keys: "Ctrl+O"; onTriggered: root.guarded(() => openDialog.open()) }
                    EditorMenuItem { text: qsTr("Close"); iconName: "close"; keys: "Ctrl+W"; onTriggered: root.closeDocument() }
                    MenuSeparator {}
                    EditorMenuItem { text: qsTr("Save"); iconName: "save"; keys: "Ctrl+S"; onTriggered: root.save() }
                    EditorMenuItem { text: qsTr("Save As…"); keys: "Ctrl+Shift+S"; onTriggered: saveDialog.open() }
                    MenuSeparator {}
                    EditorMenuItem { text: qsTr("Import from File…"); iconName: "upload"; onTriggered: { importDialog.mode = 0; importFileDialog.open() } }
                    EditorMenuItem { text: qsTr("Export…"); iconName: "download"; onTriggered: exportDialog.open() }
                }
            }
            IconButton { iconName: "file-plus"; toolTip: qsTr("New"); shortcutText: KeyText.of("Ctrl+N"); onClicked: root.guarded(() => root.editor.newDocument()) }
            IconButton { iconName: "folder-open"; toolTip: qsTr("Open…"); shortcutText: KeyText.of("Ctrl+O"); onClicked: root.guarded(() => openDialog.open()) }
            IconButton { iconName: "save"; toolTip: qsTr("Save"); shortcutText: KeyText.of("Ctrl+S"); onClicked: root.save() }
            ToolSeparator {}
            IconButton { iconName: "chevron-left"; enabled: root.editor.canUndo; toolTip: qsTr("Undo %1").arg(root.editor.undoText); shortcutText: KeyText.of("Ctrl+Z"); onClicked: root.editor.undo() }
            IconButton { iconName: "chevron-right"; enabled: root.editor.canRedo; toolTip: qsTr("Redo %1").arg(root.editor.redoText); shortcutText: KeyText.of("Ctrl+Shift+Z"); onClicked: root.editor.redo() }
            ToolSeparator {}
            IconButton { iconName: "copy"; toolTip: qsTr("Copy"); shortcutText: KeyText.of("Ctrl+C"); onClicked: root.editor.copy() }
            IconButton { iconName: "layers"; enabled: root.editor.canPaste; toolTip: qsTr("Paste"); shortcutText: KeyText.of("Ctrl+V"); onClicked: root.editor.paste() }
            IconButton { iconName: "trash"; toolTip: qsTr("Clear (delete the selection)"); shortcutText: qsTr("Del"); onClicked: root.clearSelection() }
            ActionButton {
                text: qsTr("Edit")
                compact: true
                tone: "ghost"
                onClicked: editMenu.popup()
                Menu {
                    id: editMenu
                    width: 260
                    EditorMenuItem { text: qsTr("Undo %1").arg(root.editor.undoText); keys: "Ctrl+Z"; enabled: root.editor.canUndo; onTriggered: root.editor.undo() }
                    EditorMenuItem { text: qsTr("Redo %1").arg(root.editor.redoText); keys: "Ctrl+Shift+Z"; enabled: root.editor.canRedo; onTriggered: root.editor.redo() }
                    MenuSeparator {}
                    EditorMenuItem { text: qsTr("Cut"); keys: "Ctrl+X"; onTriggered: root.editor.cut() }
                    EditorMenuItem { text: qsTr("Copy"); keys: "Ctrl+C"; onTriggered: root.editor.copy() }
                    EditorMenuItem { text: qsTr("Paste"); keys: "Ctrl+V"; enabled: root.editor.canPaste; onTriggered: root.editor.paste() }
                    EditorMenuItem { text: qsTr("Clear"); keys: "Del"; onTriggered: root.clearSelection() }
                    EditorMenuItem { text: qsTr("Select All"); keys: "Ctrl+A"; onTriggered: root.editor.selectAll() }
                    MenuSeparator {}
                    EditorMenuItem { text: qsTr("Import from File…"); onTriggered: { importDialog.mode = 0; importFileDialog.open() } }
                    EditorMenuItem { text: qsTr("Scale Selection…"); onTriggered: scaleDialog.open() }
                    EditorMenuItem { text: qsTr("Settings…"); iconName: "settings"; onTriggered: settingsDialog.open() }
                    EditorMenuItem { text: qsTr("Info…"); iconName: "info"; keys: "Ctrl+I"; onTriggered: infoDialog.open() }
                }
            }
            ActionButton {
                text: qsTr("Help")
                compact: true
                tone: "ghost"
                onClicked: helpMenu.popup()
                Menu {
                    id: helpMenu
                    width: 260
                    EditorMenuItem { text: qsTr("Waveform Editor Help"); iconName: "help"; keys: "Ctrl+H"; onTriggered: Help.show("gen") }
                    EditorMenuItem { text: qsTr("About Waveform Editor…"); onTriggered: aboutDialog.open() }
                }
            }
            Item { Layout.fillWidth: true }
            BusyIndicator {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                running: root.editor.rendering
                visible: busyDelay.show
                Timer {
                    id: busyDelay
                    property bool show: false
                    interval: 200
                    running: root.editor.rendering
                    onTriggered: show = true
                    onRunningChanged: if (!running) show = false
                }
            }
            Text {
                visible: root.width > 620
                text: qsTr("Use in")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
            }
            ActionButton {
                text: root.controller.channel0.name
                compact: true
                tone: "subtle"
                color: Theme.ch0
                checkable: false
                toolTip: qsTr("Play this waveform on %1 (Arbitrary mode)").arg(root.controller.channel0.name)
                onClicked: root.editor.useInChannel(0)
            }
            ActionButton {
                text: root.controller.channel1.name
                compact: true
                tone: "subtle"
                color: Theme.ch1
                toolTip: qsTr("Play this waveform on %1 (Arbitrary mode)").arg(root.controller.channel1.name)
                onClicked: root.editor.useInChannel(1)
            }
        }

        // --- Plot -------------------------------------------------------------------------------------
        Item {
            id: plotBox
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160

            // Animate Graphs (Settings): the selection highlight glides to the new segment.
            property real highlightStart: root.editor.highlightStart
            property real highlightEnd: root.editor.highlightEnd
            Behavior on highlightStart { enabled: root.editor.animateGraphs; NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            Behavior on highlightEnd { enabled: root.editor.animateGraphs; NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

            // Axes: the plot's autoscale locks (on by default, as ELVISmx) fit the view to the
            // rendered data after every edit; a user zoom or pan turns the lock off and is then kept
            // across edits, until Autoscale or the lock (plot toolbar / right-click menu) is used
            // again. In Draw mode the Y range is frozen at its extent when Draw was pressed.
            property bool yLockBeforeDraw: true
            property bool drawing: false
            Connections {
                target: root.editor
                function onDrawChanged() {
                    if (root.editor.drawMode === plotBox.drawing)
                        return
                    plotBox.drawing = root.editor.drawMode
                    if (root.editor.drawMode) {
                        plotBox.yLockBeforeDraw = plot.yAutoscaleLock
                        plot.yAutoscaleLock = false
                        plot.yAxis.min = root.editor.viewMin
                        plot.yAxis.max = root.editor.viewMax
                    } else {
                        plot.yAutoscaleLock = plotBox.yLockBeforeDraw
                    }
                }
            }

            BenchPlot {
                id: plot
                anchors.fill: parent
                hoverReadout: !root.editor.drawMode
                dragMode: root.editor.drawMode ? Plot.NoDrag : Plot.Pan
                cursorsAvailable: !root.editor.drawMode
                copyDataAvailable: true
                onCopyDataRequested: root.editor.copyData()
                cursorsEnabled: root.editor.showCursors
                onCursorsToggled: on => root.editor.showCursors = on
                // Trace style: the panel's list (GenPanel). Export data from the plot: the rendered
                // editor waveform (GenController::exportTable exports it while this tab is shown).
                xAutoscaleLock: true
                yAutoscaleLock: true
                xAxis {
                    unit: root.frequencyView ? root.editor.unitName : "s"
                    title: root.frequencyView ? qsTr("Frequency") : qsTr("Time")
                    unitInLabels: true
                }
                yAxis {
                    unit: root.frequencyView ? "dBVrms" : "V"
                    title: root.frequencyView ? qsTr("Power") : qsTr("Amplitude")
                    // A flat waveform gets a ±1 V window around its value.
                    minimumSpan: root.editor.flat ? 2 : 0
                }
                onClicked: (x, y) => {
                    root.forceActiveFocus()
                    root.editor.selectAt(x)
                }

                PlotRegion {
                    visible: !root.frequencyView && root.editor.view === 0 && root.editor.segmentCount > 1
                             && root.editor.selectionKind !== 3
                    from: plotBox.highlightStart
                    to: plotBox.highlightEnd
                    color: Theme.alpha(root.editor.selectionColor.a > 0 ? root.editor.selectionColor : Theme.cursor1,
                                       root.editor.selectionColor.a > 0 ? 0.35 : Theme.dark ? 0.10 : 0.16)
                    edgesVisible: true
                }
                PlotTrace {
                    id: trace
                    visible: root.editor.plotVisible
                    buffer: root.editor.preview
                    name: root.editor.documentName
                    unit: root.frequencyView ? "dBV" : "V"
                    color: Theme.accent
                    userStyle: root.editor.traceStyle
                    pointsVisible: root.editor.drawMode || root.editor.plotStyle !== 0
                }
                PlotCursor {
                    id: cursor1
                    visible: root.editor.showCursors && !root.editor.drawMode
                    label: "C1"
                    color: root.editor.cursorColor.a > 0 ? root.editor.cursorColor : Theme.cursor1
                    trace: trace
                    snap: true
                    position: root.editor.viewStart + 0.25 * (root.editor.viewEnd - root.editor.viewStart)
                }
                PlotCursor {
                    id: cursor2
                    visible: root.editor.showCursors && !root.editor.drawMode
                    label: "C2"
                    color: root.editor.cursorColor.a > 0 ? root.editor.cursorColor : Theme.cursor2
                    trace: trace
                    snap: true
                    position: root.editor.viewStart + 0.75 * (root.editor.viewEnd - root.editor.viewStart)
                }
            }
            // Settings colors that replace the theme's (transparent = keep the theme's).
            Binding { target: plot; property: "gridColor"; value: root.editor.gridColor; when: root.editor.gridColor.a > 0 }
            Binding { target: plot; property: "plotAreaColor"; value: root.editor.backgroundColor; when: root.editor.backgroundColor.a > 0 }

            // Freehand drawing / adding line vertices.
            MouseArea {
                id: drawArea
                x: plot.plotArea.x
                y: plot.plotArea.y
                width: plot.plotArea.width
                height: plot.plotArea.height
                visible: root.editor.drawMode
                cursorShape: Qt.CrossCursor
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                property point pointer: Qt.point(NaN, NaN)
                onWheel: wheel => wheel.accepted = false
                onExited: pointer = Qt.point(NaN, NaN)
                function valueAt(mx: real, my: real): point {
                    const p = mapToItem(plot, mx, my)
                    return Qt.point(plot.pixelToX(p.x), plot.pixelToY(p.y))
                }
                onPressed: mouse => {
                    const v = valueAt(mouse.x, mouse.y)
                    if (root.editor.drawTool === 0)
                        root.editor.drawBegin(v.x, v.y)
                }
                onPositionChanged: mouse => {
                    const v = valueAt(mouse.x, mouse.y)
                    pointer = v
                    if (pressed && root.editor.drawTool === 0)
                        root.editor.drawTo(v.x, v.y)
                }
                onReleased: root.editor.drawEnd()
                onClicked: mouse => {
                    if (root.editor.drawTool === 1) {
                        const v = valueAt(mouse.x, mouse.y)
                        root.editor.addVertex(v.x, v.y)
                    }
                }
            }

            // Line-mode vertices: drag to move, right-click to delete.
            Repeater {
                model: root.editor.drawMode && root.editor.drawTool === 1 ? root.editor.vertices : []
                delegate: Rectangle {
                    id: handle
                    required property var modelData
                    required property int index
                    readonly property real px: {
                        plot.xAxis.min; plot.xAxis.max; plot.width   // dependencies
                        return plot.xToPixel(modelData.x)
                    }
                    readonly property real py: {
                        plot.yAxis.min; plot.yAxis.max; plot.height
                        return plot.yToPixel(modelData.y)
                    }
                    x: px - width / 2
                    y: py - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: handleArea.pressed ? Theme.accent : Theme.display
                    border.width: 2
                    border.color: Theme.accent
                    MouseArea {
                        id: handleArea
                        anchors.fill: parent
                        anchors.margins: -4
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: Qt.SizeAllCursor
                        onPressed: mouse => {
                            if (mouse.button === Qt.LeftButton)
                                root.editor.vertexDragBegin()
                        }
                        onPositionChanged: mouse => {
                            if (!pressed)
                                return
                            const p = mapToItem(plot, mouse.x, mouse.y)
                            root.editor.moveVertex(handle.index, plot.pixelToX(p.x), plot.pixelToY(p.y))
                        }
                        onReleased: mouse => {
                            if (mouse.button === Qt.LeftButton)
                                root.editor.vertexDragEnd()
                        }
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton)
                                root.editor.removeVertex(handle.index)
                        }
                    }
                }
            }

            // Draw-mode palette.
            Rectangle {
                visible: root.editor.drawMode
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: plot.plotArea.y + 6
                width: drawRow.implicitWidth + 12
                height: drawRow.implicitHeight + 8
                radius: Theme.radiusM
                color: Theme.surfaceRaised
                border.width: 1
                border.color: Theme.border
                RowLayout {
                    id: drawRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingS
                    Text {
                        // ELVISmx "Mouse Position" indicator.
                        Layout.preferredWidth: 190
                        text: isNaN(drawArea.pointer.x) ? qsTr("x: —   y: —")
                              : qsTr("x: %1   y: %2").arg(Units.format(drawArea.pointer.x, "s", 4)).arg(Units.format(drawArea.pointer.y, "V", 4))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeS
                        font.features: Theme.tabularFigures
                        ToolTip.visible: positionHover.hovered
                        ToolTip.text: qsTr("Mouse position")
                        HoverHandler { id: positionHover }
                    }
                    SegmentedControl {
                        compact: true
                        model: [ { text: qsTr("Freehand"), icon: "edit", toolTip: qsTr("Hold the mouse button and draw") },
                                 { text: qsTr("Line"), icon: "trend", toolTip: qsTr("Click to add points; drag a point to move it; right-click deletes it") } ]
                        currentIndex: root.editor.drawTool
                        onActivated: index => root.editor.drawTool = index
                    }
                    ActionButton {
                        text: qsTr("Done")
                        compact: true
                        tone: "accent"
                        onClicked: root.editor.drawMode = false
                    }
                }
            }

            Rectangle {
                visible: root.editor.aliasWarning
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 8
                anchors.topMargin: plot.plotArea.y + 6
                width: aliasRow.implicitWidth + 12
                height: 22
                radius: 11
                color: Theme.alpha(Theme.warning, 0.15)
                Row {
                    id: aliasRow
                    anchors.centerIn: parent
                    spacing: 4
                    Icon { name: "warning"; color: Theme.warning; size: Theme.iconSizeS }
                    Text { text: qsTr("Aliasing: a frequency is above half the sample rate"); color: Theme.warning; font.pixelSize: Theme.fontSizeS }
                }
            }
        }

        // --- Segment list and structure --------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(150, root.height * 0.3)
            spacing: Theme.spacingS

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 4
                RowLayout {
                    spacing: 2
                    // Draw mode grays the structure controls (ELVISmx) until Draw is released.
                    enabled: !root.editor.drawMode
                    IconButton {
                        iconName: "chevron-left"
                        size: Theme.controlHeightS
                        enabled: root.editor.segmentIndex > 0
                        toolTip: qsTr("Previous segment")
                        onClicked: root.editor.selectSegment(root.editor.segmentIndex - 1)
                    }
                    SiField {
                        Layout.preferredWidth: 64
                        engineering: false
                        decimals: 0
                        stepSize: 1
                        showArrows: false
                        from: 1
                        to: Math.max(1, root.editor.segmentCount)
                        toolTip: qsTr("Index of the selected segment (1 to %1)").arg(root.editor.segmentCount)
                        value: root.editor.segmentIndex + 1
                        onValueModified: v => root.editor.selectSegment(Math.round(v) - 1)
                    }
                    IconButton {
                        iconName: "chevron-right"
                        size: Theme.controlHeightS
                        enabled: root.editor.segmentIndex < root.editor.segmentCount - 1
                        toolTip: qsTr("Next segment")
                        onClicked: root.editor.selectSegment(root.editor.segmentIndex + 1)
                    }
                    Text {
                        text: qsTr("Index")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        // Two-cursor readout (Settings > Show Cursors).
                        visible: cursor1.visible
                        readonly property real dx: cursor2.position - cursor1.position
                        text: root.frequencyView
                              ? qsTr("ΔX %1   ΔY %2").arg(Units.format(dx, root.editor.unitName, 4)).arg(Units.format(cursor2.value - cursor1.value, "dB", 4))
                              : qsTr("ΔX %1   ΔY %2   1/ΔX %3").arg(Units.format(dx, "s", 4)).arg(Units.format(cursor2.value - cursor1.value, "V", 4))
                                    .arg(Math.abs(dx) > 0 ? Units.format(1 / Math.abs(dx), "Hz", 4) : "—")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeS
                        font.features: Theme.tabularFigures
                    }
                    Text {
                        text: root.editor.statusText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                        font.features: Theme.tabularFigures
                        elide: Text.ElideLeft
                        Layout.maximumWidth: 360
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusM
                    color: Theme.surfaceSunken
                    border.width: 1
                    border.color: segmentList.activeFocus ? Theme.accent : Theme.border
                    enabled: !root.editor.drawMode
                    opacity: enabled ? 1 : 0.55

                    ListView {
                        id: segmentList
                        anchors.fill: parent
                        anchors.margins: 2
                        clip: true
                        focus: true
                        model: root.editor.rows
                        currentIndex: root.editor.currentRow
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        Keys.onUpPressed: event => {
                            if (event.modifiers & Qt.AltModifier) root.editor.moveSelection(-1)
                            else root.editor.currentRow = Math.max(0, root.editor.currentRow - 1)
                        }
                        Keys.onDownPressed: event => {
                            if (event.modifiers & Qt.AltModifier) root.editor.moveSelection(1)
                            else root.editor.currentRow = Math.min(count - 1, root.editor.currentRow + 1)
                        }
                        property int dragFrom: -1
                        delegate: Rectangle {
                            id: rowItem
                            required property int index
                            required property string text
                            required property string detail
                            required property int kind
                            required property bool warning
                            required property string iconName
                            readonly property bool selected: index === root.editor.currentRow || root.editor.selectionKind === 3
                            width: ListView.view.width
                            height: 24
                            radius: Theme.radiusS
                            color: selected ? Theme.accentMuted : rowMouse.containsMouse ? Theme.surfaceHover : "transparent"
                            border.width: segmentList.dragFrom >= 0 && rowMouse.containsMouse && segmentList.dragFrom !== index ? 1 : 0
                            border.color: Theme.accent
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: rowItem.kind === 0 ? 6 : 26
                                anchors.rightMargin: 6
                                spacing: 6
                                Icon {
                                    name: rowItem.iconName
                                    size: Theme.iconSizeS
                                    color: rowItem.kind === 0 ? Theme.textSecondary : Theme.accent
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: rowItem.text
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeM
                                    font.weight: rowItem.kind === 0 ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Icon {
                                    visible: rowItem.warning
                                    name: "warning"
                                    size: Theme.iconSizeS
                                    color: Theme.warning
                                }
                                Text {
                                    text: rowItem.detail
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeS
                                    font.features: Theme.tabularFigures
                                }
                            }
                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onPressed: {
                                    segmentList.forceActiveFocus()
                                    root.editor.currentRow = rowItem.index
                                    segmentList.dragFrom = rowItem.index
                                }
                                onReleased: mouse => {
                                    const p = mapToItem(segmentList.contentItem, mouse.x, mouse.y)
                                    const to = segmentList.indexAt(p.x, p.y)
                                    if (segmentList.dragFrom >= 0 && to >= 0 && to !== segmentList.dragFrom)
                                        root.editor.moveRow(segmentList.dragFrom, to)
                                    segmentList.dragFrom = -1
                                }
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Theme.controlHeightS + 4
                Layout.preferredWidth: 150
                Layout.fillWidth: false
                spacing: 4
                enabled: !root.editor.drawMode
                ActionButton { Layout.fillWidth: true; text: qsTr("New Segment"); iconName: "plus"; compact: true
                               toolTip: qsTr("Insert an empty 1 s segment after the current one"); onClicked: root.editor.newSegment() }
                ActionButton { Layout.fillWidth: true; text: qsTr("New Component"); iconName: "plus"; compact: true
                               toolTip: qsTr("Insert a component (the last library function chosen) after the selection")
                               enabled: root.editor.selectionKind !== 3; onClicked: root.editor.newComponent() }
                RowLayout {
                    spacing: 2
                    IconButton { iconName: "chevron-up"; size: Theme.controlHeightS; toolTip: qsTr("Move up (Alt+↑)"); onClicked: root.editor.moveSelection(-1) }
                    IconButton { iconName: "chevron-down"; size: Theme.controlHeightS; toolTip: qsTr("Move down (Alt+↓)"); onClicked: root.editor.moveSelection(1) }
                    IconButton { iconName: "trash"; size: Theme.controlHeightS; toolTip: qsTr("Clear"); onClicked: root.clearSelection() }
                    IconButton { iconName: "grid"; size: Theme.controlHeightS; toolTip: qsTr("Select All"); onClicked: root.editor.selectAll() }
                }
            }
        }

        Text {
            visible: root.editor.warnings.length > 0
            Layout.fillWidth: true
            text: root.editor.warnings.join("\n")
            color: Theme.warning
            font.pixelSize: Theme.fontSizeS
            wrapMode: Text.WordWrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
    }

    // --- Dialogs ---------------------------------------------------------------------------------------
    FileDialog {
        id: openDialog
        title: qsTr("Open waveform")
        nameFilters: [qsTr("Waveforms (*.mbw *.wfm *.wav *.csv *.txt *.bin *.wdt)"), qsTr("MYBENCH waveform (*.mbw)"),
                      qsTr("NI Waveform Editor document (*.wfm)"), qsTr("All files (*)")]
        onAccepted: root.editor.openFile(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save waveform")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("MYBENCH waveform (*.mbw)")]
        defaultSuffix: "mbw"
        onAccepted: {
            if (root.editor.saveAs(selectedFile) && root.pendingAction) {
                const action = root.pendingAction
                root.pendingAction = null
                action()
            }
        }
        onRejected: root.pendingAction = null
    }
    FileDialog {
        id: exportDialog
        title: qsTr("Export rendered waveform")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)"), qsTr("WAV audio, 32-bit float (*.wav)"), qsTr("Binary 16-bit, ±5 V (*.bin)"),
                      qsTr("LabVIEW waveform, MYBENCH layout (*.wdt)")]
        defaultSuffix: "csv"
        onAccepted: root.editor.exportFile(selectedFile)
    }
    FileDialog {
        id: importFileDialog
        title: qsTr("Import from file")
        nameFilters: [qsTr("Waveforms (*.mbw *.wav *.csv *.txt *.bin *.wdt)"), qsTr("Text data (*.csv *.txt)"),
                      qsTr("WAV audio (*.wav)"), qsTr("Binary 16-bit (*.bin)"), qsTr("All files (*)")]
        onAccepted: importDialog.openFor(selectedFile)
    }

    ImportDialog {
        id: importDialog
        editor: root.editor
    }
    ScaleDialog {
        id: scaleDialog
        editor: root.editor
    }
    Dialog {
        id: infoDialog
        title: qsTr("Information")
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 460
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: infoText.text = root.editor.info
        onAccepted: root.editor.info = infoText.text
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingS
            Field {
                Layout.fillWidth: true
                label: qsTr("File")
                TextField {
                    Layout.fillWidth: true
                    readOnly: true
                    text: root.editor.filePath
                    placeholderText: qsTr("(not saved yet)")
                    selectByMouse: true
                }
            }
            Field {
                Layout.fillWidth: true
                label: qsTr("Description")
                TextArea {
                    id: infoText
                    Layout.fillWidth: true
                    implicitHeight: 140
                    placeholderText: qsTr("Description of this waveform")
                    wrapMode: TextEdit.Wrap
                }
            }
        }
    }
    EditorSettingsDialog {
        id: settingsDialog
        editor: root.editor
    }
    Dialog {
        id: aboutDialog
        title: qsTr("About Waveform Editor")
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 400
        standardButtons: Dialog.Ok
        Text {
            width: parent.width
            text: qsTr("Waveform Editor of the %1 Waveform Generator, version %2.\n\nBuilds arbitrary waveforms from segments of library functions, expressions and freehand sketches, and plays them on AO 0 / AO 1 or the audio outputs.")
                  .arg(AppInfo.name).arg(AppInfo.version)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
    }
    ConfirmDialog {
        id: clearAllDialog
        title: qsTr("Clear the whole waveform?")
        text: qsTr("Every segment is removed; the document keeps one empty 10 s segment. You can undo it.")
        acceptText: qsTr("Clear")
        destructive: true
        onAccepted: root.editor.deleteSelection()
    }
    Dialog {
        id: unsavedDialog
        title: qsTr("Save changes to %1?").arg(root.editor.documentName)
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 420
        Text {
            width: parent.width
            text: qsTr("The waveform has unsaved changes.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: Theme.spacingS
            Item { Layout.fillWidth: true }
            ActionButton {
                Layout.bottomMargin: Theme.spacingL
                text: qsTr("Cancel")
                onClicked: { root.pendingAction = null; unsavedDialog.close() }
            }
            ActionButton {
                Layout.bottomMargin: Theme.spacingL
                text: qsTr("Discard")
                tone: "danger"
                onClicked: {
                    unsavedDialog.close()
                    const action = root.pendingAction
                    root.pendingAction = null
                    if (action) action()
                }
            }
            ActionButton {
                Layout.bottomMargin: Theme.spacingL
                Layout.rightMargin: Theme.spacingL
                text: qsTr("Save")
                tone: "accent"
                onClicked: {
                    unsavedDialog.close()
                    const action = root.pendingAction
                    root.pendingAction = null
                    root.saveThen(action)
                }
            }
        }
    }
}
