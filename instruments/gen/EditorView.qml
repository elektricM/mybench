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

    Shortcut { sequences: [StandardKey.Undo]; enabled: root.visible; onActivated: root.editor.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: root.visible; onActivated: root.editor.redo() }
    Shortcut { sequences: [StandardKey.Copy]; enabled: root.visible; onActivated: root.editor.copy() }
    Shortcut { sequences: [StandardKey.Cut]; enabled: root.visible; onActivated: root.editor.cut() }
    Shortcut { sequences: [StandardKey.Paste]; enabled: root.visible; onActivated: root.editor.paste() }
    Shortcut { sequences: [StandardKey.Delete]; enabled: root.visible && segmentList.activeFocus; onActivated: root.editor.deleteSelection() }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        // --- Toolbar (File / Edit menus of ELVISmx) -----------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            ActionButton {
                text: qsTr("File")
                iconName: "file"
                compact: true
                tone: "ghost"
                onClicked: fileMenu.popup()
                Menu {
                    id: fileMenu
                    MenuItem { text: qsTr("New"); icon.source: Theme.icon("file-plus"); onTriggered: root.guarded(() => root.editor.newDocument()) }
                    MenuItem { text: qsTr("Open…"); icon.source: Theme.icon("folder-open"); onTriggered: root.guarded(() => openDialog.open()) }
                    MenuItem { text: qsTr("Save"); icon.source: Theme.icon("save"); onTriggered: { if (!root.editor.save()) saveDialog.open() } }
                    MenuItem { text: qsTr("Save As…"); onTriggered: saveDialog.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Import from File…"); icon.source: Theme.icon("upload"); onTriggered: { importDialog.mode = 0; importFileDialog.open() } }
                    MenuItem { text: qsTr("Export…"); icon.source: Theme.icon("download"); onTriggered: exportDialog.open() }
                }
            }
            IconButton { iconName: "file-plus"; toolTip: qsTr("New"); onClicked: root.guarded(() => root.editor.newDocument()) }
            IconButton { iconName: "folder-open"; toolTip: qsTr("Open…"); onClicked: root.guarded(() => openDialog.open()) }
            IconButton { iconName: "save"; toolTip: qsTr("Save"); onClicked: { if (!root.editor.save()) saveDialog.open() } }
            ToolSeparator {}
            IconButton { iconName: "chevron-left"; enabled: root.editor.canUndo; toolTip: qsTr("Undo %1").arg(root.editor.undoText); shortcutText: "⌘Z"; onClicked: root.editor.undo() }
            IconButton { iconName: "chevron-right"; enabled: root.editor.canRedo; toolTip: qsTr("Redo %1").arg(root.editor.redoText); shortcutText: "⇧⌘Z"; onClicked: root.editor.redo() }
            ToolSeparator {}
            IconButton { iconName: "copy"; toolTip: qsTr("Copy"); onClicked: root.editor.copy() }
            IconButton { iconName: "layers"; enabled: root.editor.canPaste; toolTip: qsTr("Paste"); onClicked: root.editor.paste() }
            IconButton { iconName: "trash"; toolTip: qsTr("Clear (delete the selection)"); onClicked: root.editor.selectionKind === 3 ? clearAllDialog.open() : root.editor.deleteSelection() }
            ActionButton {
                text: qsTr("Edit")
                compact: true
                tone: "ghost"
                onClicked: editMenu.popup()
                Menu {
                    id: editMenu
                    MenuItem { text: qsTr("Undo %1").arg(root.editor.undoText); enabled: root.editor.canUndo; onTriggered: root.editor.undo() }
                    MenuItem { text: qsTr("Redo %1").arg(root.editor.redoText); enabled: root.editor.canRedo; onTriggered: root.editor.redo() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Cut"); onTriggered: root.editor.cut() }
                    MenuItem { text: qsTr("Copy"); onTriggered: root.editor.copy() }
                    MenuItem { text: qsTr("Paste"); enabled: root.editor.canPaste; onTriggered: root.editor.paste() }
                    MenuItem { text: qsTr("Clear"); onTriggered: root.editor.selectionKind === 3 ? clearAllDialog.open() : root.editor.deleteSelection() }
                    MenuItem { text: qsTr("Select All"); onTriggered: root.editor.selectAll() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Import from File…"); onTriggered: { importDialog.mode = 0; importFileDialog.open() } }
                    MenuItem { text: qsTr("Scale Selection…"); onTriggered: scaleDialog.open() }
                    MenuItem { text: qsTr("Info…"); onTriggered: infoDialog.open() }
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
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 160

            BenchPlot {
                id: plot
                anchors.fill: parent
                hoverReadout: !root.editor.drawMode
                dragMode: root.editor.drawMode ? Plot.NoDrag : Plot.Pan
                xAxis {
                    min: root.editor.viewStart
                    max: root.editor.viewEnd
                    unit: root.frequencyView ? root.editor.unitName : "s"
                    title: root.frequencyView ? qsTr("Frequency") : qsTr("Time")
                    unitInLabels: true
                }
                yAxis {
                    min: root.editor.viewMin
                    max: root.editor.viewMax
                    unit: root.frequencyView ? "dBVrms" : "V"
                    title: root.frequencyView ? qsTr("Power") : qsTr("Amplitude")
                }
                onClicked: (x, y) => root.editor.selectAt(x)

                PlotRegion {
                    visible: !root.frequencyView && root.editor.view === 0 && root.editor.segmentCount > 1
                             && root.editor.selectionKind !== 3
                    from: root.editor.highlightStart
                    to: root.editor.highlightEnd
                    color: Theme.alpha(Theme.cursor1, Theme.dark ? 0.10 : 0.16)
                    edgesVisible: true
                }
                PlotTrace {
                    id: trace
                    buffer: root.editor.preview
                    name: root.editor.documentName
                    unit: root.frequencyView ? "dBV" : "V"
                    color: Theme.accent
                    width: root.editor.lineWidth
                    lineVisible: root.editor.plotStyle !== 2
                    pointsVisible: root.editor.plotStyle !== 0 || root.editor.drawMode
                    step: root.editor.stepPlot
                }
                PlotCursor {
                    visible: root.editor.showCursors && !root.editor.drawMode
                    label: "C1"
                    color: Theme.cursor1
                    trace: trace
                    snap: true
                    position: root.editor.viewStart + 0.25 * (root.editor.viewEnd - root.editor.viewStart)
                }
                PlotCursor {
                    visible: root.editor.showCursors && !root.editor.drawMode
                    label: "C2"
                    color: Theme.cursor2
                    trace: trace
                    snap: true
                    position: root.editor.viewStart + 0.75 * (root.editor.viewEnd - root.editor.viewStart)
                }
            }

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
                onWheel: wheel => wheel.accepted = false
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
                    if (pressed && root.editor.drawTool === 0) {
                        const v = valueAt(mouse.x, mouse.y)
                        root.editor.drawTo(v.x, v.y)
                    }
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
                ActionButton { Layout.fillWidth: true; text: qsTr("New Segment"); iconName: "plus"; compact: true; onClicked: root.editor.newSegment() }
                ActionButton { Layout.fillWidth: true; text: qsTr("New Component"); iconName: "plus"; compact: true
                               enabled: root.editor.selectionKind !== 3; onClicked: root.editor.newComponent() }
                RowLayout {
                    spacing: 2
                    IconButton { iconName: "chevron-up"; size: Theme.controlHeightS; toolTip: qsTr("Move up (Alt+↑)"); onClicked: root.editor.moveSelection(-1) }
                    IconButton { iconName: "chevron-down"; size: Theme.controlHeightS; toolTip: qsTr("Move down (Alt+↓)"); onClicked: root.editor.moveSelection(1) }
                    IconButton { iconName: "trash"; size: Theme.controlHeightS; toolTip: qsTr("Clear"); onClicked: root.editor.selectionKind === 3 ? clearAllDialog.open() : root.editor.deleteSelection() }
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
        nameFilters: [qsTr("Waveforms (*.mbw *.wav *.csv *.txt *.bin *.wdt)"), qsTr("MYBENCH waveform (*.mbw)"), qsTr("All files (*)")]
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
        nameFilters: [qsTr("CSV (*.csv)"), qsTr("WAV audio, 32-bit float (*.wav)"), qsTr("Binary 16-bit, ±5 V (*.bin)")]
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
        title: qsTr("Waveform Info")
        anchors.centerIn: Overlay.overlay
        modal: true
        width: 440
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: infoText.text = root.editor.info
        onAccepted: root.editor.info = infoText.text
        TextArea {
            id: infoText
            anchors.fill: parent
            implicitHeight: 140
            placeholderText: qsTr("Description of this waveform")
            wrapMode: TextEdit.Wrap
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
