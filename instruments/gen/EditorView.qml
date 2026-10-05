pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Waveform Editor view (ELVISmx WaveformEditor.exe) in the instrument-panel pattern: a top bar
// (Back to the Generator, Undo / Redo, Zoom to fit, New / Open / Save, ⋯ with every File and Edit
// action); the waveform display on the left (selected segment highlighted, cursors, sketch
// drawing, the segment list below it); on the right the Document and Segment cards and a tool
// palette (Draw, Functions, Import, Export, Display, Scale, Info) whose tools open a floating panel.
Item {
    id: root

    required property GenController controller
    required property WaveformEditor editor
    readonly property bool frequencyView: editor.xAxis === 1
    // Narrow pane: knob fields without knobs, palette icons only.
    readonly property bool compact: width < Theme.instrumentCompactWidth
    property var pendingAction: null
    // Open tool panel ("" = none).
    property string tool: ""

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
    // Close: back to an empty document and the Generator view (asks to save first).
    function closeDocument() {
        guarded(() => {
            editor.newDocument()
            controller.activeTab = 0
        })
    }
    function openImport() {
        importDialog.mode = 0
        importFileDialog.open()
    }
    // Zoom to fit: both axes follow the rendered waveform again.
    function zoomToFit() {
        plot.xAutoscaleLock = true
        plot.yAutoscaleLock = true
        plot.autoscale()
    }
    // Makes sure a component is selected (a new one after the selection when a segment or the
    // whole waveform is selected).
    function ensureComponent() {
        if (editor.selectionKind === 2)
            return
        if (editor.selectionKind !== 1)
            editor.selectSegment(Math.max(0, editor.segmentIndex))
        editor.newComponent()
    }
    // Draw tool: draws one cycle of a sketch component (the selected one, or a new one).
    function toggleDraw() {
        if (editor.drawMode) {
            editor.drawMode = false
            return
        }
        if (!(editor.selectionKind === 2 && editor.componentType === 2)) {
            ensureComponent()
            editor.componentType = 2
        }
        editor.drawMode = true
    }
    // Functions tool: the selected component becomes this library function (type 0), a formula
    // (type 1) or a sketch (type 2).
    function applyFunction(type: int, index: int) {
        ensureComponent()
        if (type === 0)
            editor.libraryFunction = index
        editor.componentType = type
    }
    function activateTool(id: string) {
        if (id === "draw") {
            tool = ""
            toggleDraw()
        } else {
            tool = tool === id ? "" : id
        }
    }

    // Keyboard shortcuts of the ELVISmx editor (File / Edit menus). They apply while the editor
    // (this view or its cards) has the focus: the ShortcutOverride is accepted so the shell's
    // session shortcuts (Ctrl+N/O/S/W/I) do not fire, and a text field keeps its own editing keys
    // (it accepts the override first).
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
        case Qt.Key_I: return () => root.tool = "info"
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
            tool = "info"
        } else if (scene === "menu") {
            moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)
        } else if (scene === "component") {
            controller.activeTab = 1
            editor.newComponent()
            editor.libraryFunction = 1
            editor.componentType = 0
        } else if (scene.startsWith("editor-tool-")) {
            controller.activeTab = 1
            activateTool(scene.substring(12))
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
    // Showing the editor gives it the keyboard focus (its shortcuts work at once).
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
        spacing: 0

        // --- Top bar ------------------------------------------------------------------------------------
        RowLayout {
            id: topBar
            objectName: "editorTopBar"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingS
            Layout.rightMargin: Theme.spacingS
            Layout.topMargin: Theme.spacingXs
            spacing: 2
            Button {
                objectName: "backToGenerator"
                text: qsTr("Generator")
                icon.source: Theme.icon("chevron-left")
                flat: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to the generator")
                onClicked: root.controller.activeTab = 0
            }
            ToolSeparator {}
            IconButton {
                objectName: "editorUndo"
                iconName: "undo"
                enabled: root.editor.canUndo
                toolTip: root.editor.undoText.length > 0 ? qsTr("Undo %1").arg(root.editor.undoText) : qsTr("Undo")
                shortcutText: KeyText.of("Ctrl+Z")
                onClicked: root.editor.undo()
            }
            IconButton {
                id: redoButton
                objectName: "editorRedo"
                iconName: "undo"
                // The undo arrow, mirrored.
                transform: Scale { origin.x: redoButton.width / 2; xScale: -1 }
                enabled: root.editor.canRedo
                toolTip: root.editor.redoText.length > 0 ? qsTr("Redo %1").arg(root.editor.redoText) : qsTr("Redo")
                shortcutText: KeyText.of("Ctrl+Shift+Z")
                onClicked: root.editor.redo()
            }
            IconButton {
                objectName: "editorZoomFit"
                iconName: "zoom-fit"
                toolTip: qsTr("Zoom to fit")
                onClicked: root.zoomToFit()
            }
            ToolSeparator {}
            IconButton { objectName: "editorNew"; iconName: "file-plus"; toolTip: qsTr("New"); shortcutText: KeyText.of("Ctrl+N"); onClicked: root.guarded(() => root.editor.newDocument()) }
            IconButton { objectName: "editorOpen"; iconName: "folder-open"; toolTip: qsTr("Open…"); shortcutText: KeyText.of("Ctrl+O"); onClicked: root.guarded(() => openDialog.open()) }
            IconButton { objectName: "editorSave"; iconName: "save"; toolTip: qsTr("Save"); shortcutText: KeyText.of("Ctrl+S"); onClicked: root.save() }
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
            IconButton {
                id: moreButton
                objectName: "editorMore"
                iconName: "more-horizontal"
                toolTip: qsTr("More")
                onClicked: moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)
                // Every File and Edit action of the editor (the keys work while the editor has the focus).
                Menu {
                    id: moreMenu
                    MenuItem { text: qsTr("New"); onTriggered: root.guarded(() => root.editor.newDocument()) }
                    MenuItem { text: qsTr("Open…"); onTriggered: root.guarded(() => openDialog.open()) }
                    MenuItem { text: qsTr("Close"); onTriggered: root.closeDocument() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Save"); onTriggered: root.save() }
                    MenuItem { text: qsTr("Save As…"); onTriggered: saveDialog.open() }
                    MenuItem { text: qsTr("Import from File…"); onTriggered: root.openImport() }
                    MenuItem { text: qsTr("Export…"); onTriggered: exportDialog.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Undo"); enabled: root.editor.canUndo; onTriggered: root.editor.undo() }
                    MenuItem { text: qsTr("Redo"); enabled: root.editor.canRedo; onTriggered: root.editor.redo() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Cut"); onTriggered: root.editor.cut() }
                    MenuItem { text: qsTr("Copy"); onTriggered: root.editor.copy() }
                    MenuItem { text: qsTr("Paste"); enabled: root.editor.canPaste; onTriggered: root.editor.paste() }
                    MenuItem { text: qsTr("Clear"); onTriggered: root.clearSelection() }
                    MenuItem { text: qsTr("Select All"); onTriggered: root.editor.selectAll() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Scale Selection…"); onTriggered: root.tool = "scale" }
                    MenuItem { text: qsTr("Info…"); onTriggered: root.tool = "info" }
                    MenuItem { text: qsTr("Settings…"); onTriggered: settingsDialog.open() }
                    MenuItem { text: qsTr("Help"); onTriggered: Help.show("gen") }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // --- Display: waveform plot and segment list -----------------------------------------------
            ColumnLayout {
                id: displayColumn
                objectName: "editorDisplay"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.spacingS
                spacing: Theme.spacingS

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
                        objectName: "editorPlot"
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
                    // The display stays the largest part: the list only takes a strip under the plot.
                Layout.fillHeight: false
                Layout.preferredHeight: Math.max(110, displayColumn.height * 0.22)
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

            // --- Right column (InstrumentColumn): Document and Segment cards, palette pinned below -----
            InstrumentColumn {
                id: controlsColumn
                objectName: "editorControls"
                Layout.fillHeight: true
                compact: root.compact
                EditorDocumentCard {
                    Layout.fillWidth: true
                    controller: root.controller
                    editor: root.editor
                    compact: root.compact
                }
                EditorSegmentCard {
                    Layout.fillWidth: true
                    editor: root.editor
                    compact: root.compact
                }
                tools: ToolPalette {
                    objectName: "editorTools"
                    columns: 4
                    cellHeight: root.compact ? 36 : 52
                    currentId: root.editor.drawMode ? "draw" : root.tool
                    model: [
                        { id: "draw", text: root.compact ? "" : qsTr("Draw"), toolTip: root.compact ? qsTr("Draw") : "", icon: "edit" },
                        { id: "functions", text: root.compact ? "" : qsTr("Functions"), toolTip: root.compact ? qsTr("Functions") : "", icon: "wave-sine" },
                        { id: "import", text: root.compact ? "" : qsTr("Import"), toolTip: root.compact ? qsTr("Import") : "", icon: "upload" },
                        { id: "export", text: root.compact ? "" : qsTr("Export"), toolTip: root.compact ? qsTr("Export") : "", icon: "export" },
                        { id: "display", text: root.compact ? "" : qsTr("Display"), toolTip: root.compact ? qsTr("Display") : "", icon: "eye" },
                        { id: "scale", text: root.compact ? "" : qsTr("Scale"), toolTip: root.compact ? qsTr("Scale") : "", icon: "ruler" },
                        { id: "info", text: root.compact ? "" : qsTr("Info"), toolTip: root.compact ? qsTr("Info") : "", icon: "info" }
                    ]
                    onActivated: id => root.activateTool(id)
                }
            }
        }
    }

    // --- Floating tool panel over the display, top right (left of the controls column) -----------------
    ToolPanel {
        id: toolPanel
        objectName: "editorToolPanel"
        visible: root.tool.length > 0
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: topBar.height + Theme.spacingS
        anchors.rightMargin: controlsColumn.width + Theme.spacingS
        width: Math.min(Theme.toolPanelWidth, Math.max(220, displayColumn.width - 2 * Theme.spacingS))
        maximumHeight: parent.height - topBar.height - 2 * Theme.spacingS
        title: root.tool === "functions" ? qsTr("Functions")
               : root.tool === "import" ? qsTr("Import")
               : root.tool === "export" ? qsTr("Export")
               : root.tool === "display" ? qsTr("Display")
               : root.tool === "scale" ? qsTr("Scale Selection")
               : root.tool === "info" ? qsTr("Information") : ""
        onCloseRequested: root.tool = ""

        // Functions: the 21 library functions, a formula or a sketch for the selected component.
        GridLayout {
            objectName: "editorFunctionsTool"
            visible: root.tool === "functions"
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingXs
            rowSpacing: Theme.spacingXs
            Repeater {
                model: root.editor.functionNames
                delegate: Button {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: modelData
                    checkable: false
                    highlighted: root.editor.selectionKind === 2 && root.editor.componentType === 0 && root.editor.libraryFunction === index
                    onClicked: root.applyFunction(0, index)
                }
            }
            Button {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("Formula f(x)")
                highlighted: root.editor.selectionKind === 2 && root.editor.componentType === 1
                onClicked: root.applyFunction(1, -1)
            }
            Button {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("Sketch")
                highlighted: root.editor.selectionKind === 2 && root.editor.componentType === 2
                onClicked: root.applyFunction(2, -1)
            }
        }
        // Import: a file as new segments (with its column / format options), or into the sketch.
        ColumnLayout {
            visible: root.tool === "import"
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            Button {
                objectName: "editorImportFile"
                Layout.fillWidth: true
                text: qsTr("Import from File…")
                icon.source: Theme.icon("upload")
                onClicked: root.openImport()
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Into the Sketch…")
                enabled: root.editor.selectionKind === 2 && root.editor.componentType === 2 && !root.editor.drawMode
                onClicked: sketchImport.open()
            }
        }
        // Export: the rendered waveform as a file, or its data to the clipboard.
        ColumnLayout {
            visible: root.tool === "export"
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            Button {
                objectName: "editorExportFile"
                Layout.fillWidth: true
                text: qsTr("Export Waveform…")
                icon.source: Theme.icon("export")
                onClicked: exportDialog.open()
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Save As…")
                icon.source: Theme.icon("save")
                onClicked: saveDialog.open()
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Copy Data")
                icon.source: Theme.icon("copy")
                onClicked: root.editor.copyData()
            }
        }
        ColumnLayout {
            visible: root.tool === "display"
            Layout.fillWidth: true
            spacing: Theme.spacingS
            EditorDisplayTool {
                Layout.fillWidth: true
                editor: root.editor
                onTraceStyleRequested: root.openTraceStyle()
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Settings…")
                icon.source: Theme.icon("settings")
                onClicked: settingsDialog.open()
            }
        }
        EditorScaleTool {
            visible: root.tool === "scale"
            Layout.fillWidth: true
            editor: root.editor
        }
        // Information: the file and the description stored with the document.
        ColumnLayout {
            visible: root.tool === "info"
            Layout.fillWidth: true
            spacing: Theme.spacingS
            onVisibleChanged: if (visible) infoText.text = root.editor.info
            TextField {
                Layout.fillWidth: true
                readOnly: true
                text: root.editor.filePath
                placeholderText: qsTr("Not saved")
                selectByMouse: true
            }
            TextArea {
                id: infoText
                objectName: "editorInfoText"
                Layout.fillWidth: true
                implicitHeight: 120
                placeholderText: qsTr("Description")
                wrapMode: TextEdit.Wrap
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Apply")
                highlighted: true
                enabled: infoText.text !== root.editor.info
                onClicked: root.editor.info = infoText.text
            }
        }
    }
    SketchImport { id: sketchImport; editor: root.editor }

    // --- Dialogs (native file dialogs, confirmations, import options, settings) ---------------------------------------------------------------------------------------
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
    EditorSettingsDialog {
        id: settingsDialog
        editor: root.editor
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
