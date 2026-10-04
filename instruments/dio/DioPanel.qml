pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Digital I/O panel (docs/instruments/dio.md), round-2 layout (docs/UI.md "Round 2"): the ELVISmx
// Digital Reader and Digital Writer in one display. Pane header: Run, radix (Hex | Bin | Dec);
// Release Outputs is the pane header's own. Display: Inputs / Outputs values, eight line tiles
// (DIO 7 ... DIO 0: LED, direction, owner, the manual-pattern switch of OUT lines) and the
// timeline (window selector in its toolbar). Right column: Lines (direction per line), Reader
// (sample interval), Writer (pattern picture buttons and its settings), the Labels tool.
Item {
    id: root

    required property DioController controller

    readonly property var presetNames: [qsTr("0-3"), qsTr("4-7"), qsTr("0-7"), qsTr("None")]
    readonly property bool hasInputs: root.controller.inputMask !== 0
    readonly property bool hasOutputs: root.controller.outputMask !== 0
    readonly property bool narrow: width < 900
    property string tool: ""
    // One radix for the panel (controller state): Inputs, Outputs, pattern number, cursor values.
    readonly property var radixChoices: [DioController.Hex, DioController.Binary, DioController.Decimal]
    function setRadix(r: int) {
        root.controller.inputRadix = r
        root.controller.outputRadix = r
    }
    // Pattern picture buttons (32 x 18 glyphs), in DioController.WriterPattern order.
    readonly property var patternGlyphs: [
        "M4 15V5h6v10 M14 15V5h6v10 M7 9l3-4 3 4",
        "M2 16h5v-3h5v-3h5v-3h5v-3h5v-3h3",
        "M2 14h5V4h5v10h5V4h5v10h5V4h3",
        "M2 15h5V4h4v11h16 M2 15h28 M14 2l4 2-4 2"
    ]
    readonly property var patternShortNames: [qsTr("Manual"), qsTr("Ramp"), qsTr("Alternate"), qsTr("Walking 1s")]

    function laneColor(line: int): color {
        return (root.controller.outputMask >> line) & 1 ? Theme.accent
             : (root.controller.inputMask >> line) & 1 ? Theme.text : Theme.textMuted
    }
    // Keys 0-7 flip the manual bit of an OUT line (panel focused).
    focus: true
    Keys.onPressed: event => {
        if (event.modifiers === Qt.NoModifier && event.key >= Qt.Key_0 && event.key <= Qt.Key_7) {
            root.controller.toggleLine(event.key - Qt.Key_0)
            event.accepted = true
        }
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        // No display title or info line: the line groups are in the Lines card.
        showRunControls: false
        runBar.showTimeout: false
        inspector: []

        headerContent: [
            RowLayout {
                spacing: Theme.spacingM
                RunButton {
                    objectName: "dioRunButton"
                    running: root.controller.running
                    stateText: root.controller.running ? qsTr("Running") : ""
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    shortcutText: "⌘R"
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "dioSingleButton"
                    text: qsTr("Single")
                    Layout.preferredHeight: 52
                    enabled: root.controller.canRun
                             || (root.controller.state === InstrumentController.Running
                                 && root.controller.runMode === InstrumentController.Continuous)
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Read the lines, write the value or play the pattern once, then stop")
                    onClicked: root.controller.runOnce()
                }
                ColumnLayout {
                    spacing: 3
                    Text {
                        visible: !root.narrow
                        text: qsTr("Radix")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    SegmentedControl {
                        objectName: "dioRadix"
                        compact: root.narrow
                        model: [{ text: qsTr("Hex"), toolTip: qsTr("Hexadecimal") },
                                { text: qsTr("Bin"), toolTip: qsTr("Binary") },
                                { text: qsTr("Dec"), toolTip: qsTr("Decimal") }]
                        currentIndex: root.radixChoices.indexOf(root.controller.outputRadix)
                        onActivated: index => root.setRadix(root.radixChoices[index])
                    }
                }
            }
        ]

        Item {
        anchors.fill: parent

        // --- Display -----------------------------------------------------------------------
        ColumnLayout {
            id: display
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
            clip: true   // a very narrow pane cuts the display instead of running under the column
            spacing: Theme.spacingM

            // Numeric values (ELVISmx "Numeric Value", one per function).
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.leftMargin: Theme.spacingS
                Layout.rightMargin: Theme.spacingS
                spacing: Theme.spacingXl

                Repeater {
                    model: [
                        { title: qsTr("Inputs"), shown: root.hasInputs, value: root.controller.inputValue,
                          radix: root.controller.inputRadix, width: root.controller.inputWidth, color: Theme.text, output: false },
                        { title: qsTr("Outputs"), shown: root.hasOutputs, value: root.controller.writtenValue,
                          radix: root.controller.outputRadix, width: root.controller.outputWidth, color: Theme.accent, output: true }
                    ]
                    delegate: ColumnLayout {
                        id: valueBlock
                        required property var modelData
                        visible: modelData.shown
                        spacing: 2
                        RowLayout {
                            spacing: Theme.spacingS
                            Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: valueBlock.modelData.color }
                            Text {
                                text: valueBlock.modelData.title.toUpperCase()
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: Font.DemiBold
                                font.letterSpacing: 1
                            }
                        }
                        RowLayout {
                            spacing: Theme.spacingS
                            Text {
                                text: {
                                    void valueBlock.modelData.value   // refresh on every new value
                                    void valueBlock.modelData.radix
                                    void root.controller.hasReading
                                    return root.controller.numericValueText(valueBlock.modelData.output)
                                }
                                color: valueBlock.modelData.color
                                font.pixelSize: 34
                                font.weight: Font.Light
                                font.family: valueBlock.modelData.radix === DioController.Binary ? Theme.monoFamily : Theme.fontFamily
                                font.features: Theme.tabularFigures
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // Line cards, MSB left (DIO 7 ... DIO 0) as in ELVISmx.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                Layout.preferredHeight: Math.min(200, Math.max(120, display.height * 0.3))
                spacing: Theme.spacingS
                Repeater {
                    model: 8
                    delegate: DioLineCard {
                        required property int index
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        controller: root.controller
                        line: 7 - index
                    }
                }
            }

            // Timeline.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 160

                BenchPlot {
                    id: timelinePlot
                    anchors.fill: parent
                    placeholderText: qsTr("Press Run to record the line states")
                    hoverReadout: false
                    xAxis {
                        unit: "s"
                        unitInLabels: true
                        follow: false   // see root.updateFollow()
                    }
                    yAxis { min: 0; max: 8; divisions: 8; minorTicks: 0; inverted: true; labelsVisible: false; interactive: false; minorGridVisible: false }
                    PlotLogicTrace {
                        buffer: root.controller.timeline
                        lanes: [7, 6, 5, 4, 3, 2, 1, 0]
                        laneNames: [7, 6, 5, 4, 3, 2, 1, 0].map(l => root.controller.lineName(l))
                        laneColors: [7, 6, 5, 4, 3, 2, 1, 0].map(l => root.laneColor(l))
                        fillOpacity: 0.16
                    }
                    PlotCursor {
                        id: cursor1
                        visible: cursorsButton.checked
                        label: "C1"
                        color: Theme.cursor1
                        readoutVisible: false
                    }
                    PlotCursor {
                        id: cursor2
                        visible: cursorsButton.checked
                        label: "C2"
                        color: Theme.cursor2
                        readoutVisible: false
                    }
                    Component.onCompleted: root.resetTimelineView()
                    ToolTip.visible: timelineHover.hovered && timelineHover.point.position.y < 26
                    ToolTip.delay: 900
                    ToolTip.text: qsTr("Software-timed sampling: pulses shorter than the sample interval can be missed, edges have several ms of jitter. The last 10 minutes are kept.")
                    HoverHandler { id: timelineHover }
                }
                Row {
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: 6
                    anchors.rightMargin: 10
                    spacing: Theme.spacingXs
                    SegmentedControl {
                        compact: true
                        model: ["5 s", "10 s", "30 s", "60 s"]
                        currentIndex: root.controller.timelineWindow
                        onActivated: index => root.controller.timelineWindow = index
                    }
                    IconButton {
                        id: cursorsButton
                        iconName: "crosshair"
                        checkable: true
                        size: Theme.controlHeightS
                        iconSize: Theme.iconSizeS
                        toolTip: qsTr("Cursors (time, Δt and line values)")
                        onToggled: if (checked) root.placeCursors()
                    }
                    IconButton {
                        iconName: root.controller.timelinePaused ? "play" : "pause"
                        size: Theme.controlHeightS
                        iconSize: Theme.iconSizeS
                        checked: root.controller.timelinePaused
                        toolTip: root.controller.timelinePaused ? qsTr("Follow the live lines") : qsTr("Pause the view (recording continues)")
                        onClicked: {
                            root.controller.timelinePaused = !root.controller.timelinePaused
                            if (root.controller.timelinePaused) {
                                timelinePlot.xAxis.follow = false
                            } else {
                                root.followArmed = true
                                root.resetTimelineView()
                            }
                        }
                    }
                    IconButton {
                        iconName: "trash"
                        size: Theme.controlHeightS
                        iconSize: Theme.iconSizeS
                        toolTip: qsTr("Clear the timeline")
                        onClicked: {
                            root.controller.clearTimeline()
                            root.followArmed = true
                            root.resetTimelineView()
                        }
                    }
                }
                // Line values at the cursors.
                Rectangle {
                    visible: cursorsButton.checked
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: timelinePlot.plotArea.x + 6
                    anchors.bottomMargin: parent.height - timelinePlot.plotArea.y - timelinePlot.plotArea.height + 6
                    width: cursorText.implicitWidth + cursorRadixButton.width + 24
                    height: Math.max(cursorText.implicitHeight, cursorRadixButton.height) + 10
                    radius: Theme.radiusS
                    color: Theme.alpha(Theme.surfaceRaised, 0.92)
                    border.width: 1
                    border.color: Theme.border
                    // Radix of the line values (Digital Waveform Viewer: Decimal / Hex / Octal / Binary / SI): the
                    // panel radix (controller state, saved), also reachable here for Octal and SI.
                    DioRadixButton {
                        id: cursorRadixButton
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        radix: root.controller.outputRadix
                        onRadixSelected: r => root.setRadix(r)
                    }
                    Text {
                        id: cursorText
                        anchors.left: cursorRadixButton.right
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: {
                            void root.controller.timelineEnd   // refresh while recording
                            return root.cursorLine("C1", cursor1.position) + "\n" + root.cursorLine("C2", cursor2.position)
                                   + "\n" + qsTr("Δt %1").arg(Units.format(cursor2.position - cursor1.position, "s"))
                        }
                        color: Theme.text
                        font.family: Theme.monoFamily
                        font.pixelSize: Theme.fontSizeS
                    }
                }
            }
        }

        // --- Right column ------------------------------------------------------------------
        Rectangle {
            id: column
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: root.narrow ? 220 : 266
            color: Theme.window
            Rectangle { width: 1; height: parent.height; color: Theme.separator }

            Flickable {
                id: columnFlick
                anchors { fill: parent; leftMargin: 1 }
                contentHeight: columnLayout.implicitHeight + 2 * Theme.spacingS
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                ColumnLayout {
                    id: columnLayout
                    x: Theme.spacingS
                    y: Theme.spacingS
                    width: columnFlick.width - 2 * Theme.spacingS
                    spacing: Theme.spacingS

                    // Lines: the direction of every line.
                    ChannelCard {
                        objectName: "dioLinesCard"
                        Layout.fillWidth: true
                        title: qsTr("Lines")
                        color: Theme.accent
                        checkable: false
                        compact: root.narrow
                        // ELVISmx "Lines to Read" / "Lines to Write" groups, as quick sets.
                        Repeater {
                            model: [{ text: qsTr("Read"), write: false }, { text: qsTr("Write"), write: true }]
                            delegate: RowLayout {
                                id: presetRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Text {
                                    Layout.preferredWidth: 40
                                    text: presetRow.modelData.text
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                }
                                SegmentedControl {
                                    objectName: presetRow.modelData.write ? "dioWritePreset" : "dioReadPreset"
                                    Layout.fillWidth: true
                                    compact: true
                                    model: root.presetNames
                                    currentIndex: presetRow.modelData.write ? root.controller.writePreset : root.controller.readPreset
                                    onActivated: index => {
                                        if (presetRow.modelData.write)
                                            root.controller.setWritePreset(index)
                                        else
                                            root.controller.setReadPreset(index)
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: 8
                            delegate: RowLayout {
                                id: directionRow
                                required property int index
                                readonly property int line: 7 - index
                                readonly property var owner: root.controller.lineOwners[line] ?? ({})
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Text {
                                    Layout.preferredWidth: 40
                                    text: "DIO " + directionRow.line
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeS
                                    font.weight: Font.DemiBold
                                }
                                SegmentedControl {
                                    objectName: "dioDirection" + directionRow.line
                                    Layout.fillWidth: true
                                    compact: true
                                    model: [{ text: qsTr("In") }, { text: qsTr("Out") }, { text: "–", toolTip: qsTr("Unused: free for other instruments") }]
                                    currentIndex: (root.controller.outputMask >> directionRow.line) & 1 ? 1
                                                : (root.controller.inputMask >> directionRow.line) & 1 ? 0 : 2
                                    onActivated: i => root.controller.setLineDirection(directionRow.line, i)
                                }
                                Badge {
                                    visible: (directionRow.owner.owner ?? "").length > 0
                                    text: directionRow.owner.owner ?? ""
                                    caps: false
                                    color: directionRow.owner.active ? Theme.warning : Theme.textSecondary
                                    HoverHandler { id: ownerHover }
                                    ToolTip.visible: ownerHover.hovered
                                    ToolTip.text: directionRow.owner.text ?? ""
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.controller.conflictMask !== 0
                            text: qsTr("Running Digital I/O on lines another instrument uses pauses it; set them to “–” to share the connector.")
                            color: Theme.warning
                            font.pixelSize: Theme.fontSizeCaption
                            wrapMode: Text.WordWrap
                        }
                    }

                    // Reader: the polling period (inputs and timeline).
                    ChannelCard {
                        objectName: "dioReaderCard"
                        Layout.fillWidth: true
                        title: qsTr("Reader")
                        color: Theme.text
                        checkable: false
                        compact: root.narrow
                        KnobField {
                            objectName: "dioSampleInterval"
                            Layout.fillWidth: true
                            label: qsTr("Sample interval")
                            unit: "s"
                            digits: 3
                            knobSize: 34
                            compact: root.narrow
                            from: 0.01
                            to: 1
                            logarithmic: true
                            defaultValue: 0.1
                            value: root.controller.sampleInterval / 1000
                            toolTip: qsTr("Inputs and timeline; software timed")
                            onValueModified: v => root.controller.sampleInterval = Math.round(v * 1000)
                        }
                    }

                    // Writer: pattern and its settings.
                    ChannelCard {
                        objectName: "dioWriterCard"
                        visible: root.hasOutputs
                        Layout.fillWidth: true
                        title: qsTr("Writer")
                        color: Theme.accent
                        checkable: false
                        compact: root.narrow
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 4
                            rowSpacing: 4
                            Repeater {
                                model: 4
                                PictureButton {
                                    required property int index
                                    objectName: "dioPattern" + index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    text: root.patternShortNames[index]
                                    glyph: root.patternGlyphs[index]
                                    checkable: false     // checked follows the controller
                                    checked: root.controller.pattern === index
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.controller.patternNames[index] ?? ""
                                    onClicked: root.controller.pattern = index
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.controller.pattern === DioController.Manual
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Pattern number (0 … %1)").arg(root.controller.formatValue(root.controller.outputMax, root.controller.outputRadix, 0))
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                            }
                            DioNumberField {
                                objectName: "dioManualValue"
                                Layout.fillWidth: true
                                value: root.controller.manualValue
                                maximum: root.controller.outputMax
                                radix: root.controller.outputRadix
                                bits: root.controller.outputWidth
                                onValueModified: v => root.controller.manualValue = v
                                // ELVISmx: a typed value that is malformed or too large is not accepted.
                                onEntered: text => {
                                    const v = root.controller.parseEntry(text, root.controller.outputRadix)
                                    if (v >= 0)
                                        root.controller.manualValue = v
                                }
                                onRadixSelected: r => root.setRadix(r)
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingXs
                                Repeater {
                                    model: [
                                        { text: qsTr("Toggle"), action: DioController.Toggle, tip: qsTr("Invert every line") },
                                        { text: qsTr("Rotate"), action: DioController.Rotate, tip: qsTr("Rotate one bit in the direction (the bit leaving re-enters on the other side)") },
                                        { text: qsTr("Shift"), action: DioController.Shift, tip: qsTr("Shift one bit in the direction (a 0 enters)") }
                                    ]
                                    delegate: ActionButton {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        compact: true
                                        text: modelData.text
                                        toolTip: modelData.tip
                                        onClicked: root.controller.applyAction(modelData.action)
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            visible: root.controller.pattern === DioController.Manual || root.controller.pattern === DioController.WalkingOnes
                            Layout.fillWidth: true
                            spacing: 3
                            Text {
                                text: qsTr("Direction")
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeCaption
                            }
                            SegmentedControl {
                                Layout.fillWidth: true
                                model: [qsTr("Left"), qsTr("Right")]
                                currentIndex: root.controller.shiftDirection
                                onActivated: index => root.controller.shiftDirection = index
                            }
                        }
                        KnobField {
                            objectName: "dioStepInterval"
                            visible: root.controller.pattern !== DioController.Manual
                            Layout.fillWidth: true
                            label: qsTr("Step interval")
                            unit: "s"
                            digits: 3
                            knobSize: 34
                            compact: root.narrow
                            from: 0.01
                            to: 10
                            logarithmic: true
                            defaultValue: 0.5
                            value: root.controller.stepInterval / 1000
                            toolTip: qsTr("Time between two values of the pattern")
                            onValueModified: v => root.controller.stepInterval = Math.round(v * 1000)
                        }
                    }

                    ToolPalette {
                        objectName: "dioTools"
                        Layout.fillWidth: true
                        columns: 3
                        model: [{ id: "labels", text: qsTr("Labels"), icon: "edit" }]
                        currentId: root.tool
                        onActivated: id => root.tool = root.tool === id ? "" : id
                    }
                }
            }
        }

        // Floating tool panels over the right part of the display.
        ToolPanel {
            objectName: "dioLabelsPanel"
            visible: root.tool === "labels"
            anchors { right: column.left; top: parent.top; margins: Theme.spacingS }
            width: 280
            z: 10
            title: qsTr("Line Labels")
            onCloseRequested: root.tool = ""
            Repeater {
                model: 8
                delegate: RowLayout {
                    id: labelRow
                    required property int index
                    readonly property int line: 7 - index
                    readonly property var owner: root.controller.lineOwners[line] ?? ({})
                    Layout.fillWidth: true
                    Text {
                        Layout.preferredWidth: 44
                        text: "DIO " + labelRow.line
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeS
                    }
                    TextField {
                        Layout.fillWidth: true
                        placeholderText: (labelRow.owner.owner ?? "").length > 0 ? labelRow.owner.owner : qsTr("label")
                        text: root.controller.lineLabels[labelRow.line] ?? ""
                        font.pixelSize: Theme.fontSizeS
                        onEditingFinished: root.controller.setLineLabel(labelRow.line, text)
                    }
                }
            }
        }
        }
    }

    function resetTimelineView() {
        // Newest sample near the right edge; a fresh recording starts at the left edge.
        const span = root.controller.timelineSeconds
        const end = Math.max(root.controller.timelineEnd + span * 0.03, span)
        timelinePlot.xAxis.setRange(end - span, end)
        timelinePlot.xAxis.follow = false
        root.updateFollow()
    }
    // Scroll with the newest sample once the recording fills the window (a fresh recording starts
    // at 0 s). Set imperatively: panning or zooming by the user turns following off until Pause/
    // play, a window change or a new run.
    property bool followArmed: true
    function updateFollow() {
        if (root.followArmed && !root.controller.timelinePaused
                && root.controller.timelineEnd > timelinePlot.xAxis.max - root.controller.timelineSeconds * 0.03) {
            timelinePlot.xAxis.follow = true
            root.followArmed = false
        }
    }
    function placeCursors() {
        const lo = timelinePlot.xAxis.min, hi = timelinePlot.xAxis.max
        cursor1.position = lo + (hi - lo) * 0.3
        cursor2.position = lo + (hi - lo) * 0.6
    }
    function cursorLine(name: string, x: real): string {
        let bits = ""
        for (let l = 7; l >= 0; --l) {
            bits += root.controller.timeline.levelAt(l, x) ? "1" : "0"
            if (l === 4)
                bits += " "
        }
        return name + "  " + Units.format(x, "s") + "   7…0 " + bits + "   "
               + root.controller.valuesAtTime(x, root.controller.outputRadix)
    }
    Connections {
        target: root.controller
        function onTimelineWindowChanged() { root.followArmed = true; root.resetTimelineView() }
        function onStarted() { if (!root.controller.timelinePaused) { root.followArmed = true; root.resetTimelineView() } }
        function onTimelineAdvanced() { root.updateFollow() }
    }
}
