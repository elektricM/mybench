pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Digital I/O panel (docs/instruments/dio.md): the ELVISmx Digital Reader and Digital Writer in
// one display — Inputs / Outputs numeric values with their radix, eight line cards (DIO 7 ...
// DIO 0, LED, direction, manual switch, owner) and the timeline of the line states; inspector
// cards Line Directions, Digital Reader, Digital Writer and Timeline.
Item {
    id: root

    required property DioController controller

    readonly property var presetNames: [qsTr("0 - 3"), qsTr("4 - 7"), qsTr("0 - 7"), qsTr("None")]
    readonly property bool hasInputs: root.controller.inputMask !== 0
    readonly property bool hasOutputs: root.controller.outputMask !== 0

    function laneColor(line: int): color {
        return (root.controller.outputMask >> line) & 1 ? Theme.math
             : (root.controller.inputMask >> line) & 1 ? Theme.ch1 : Theme.textMuted
    }
    function valueText(value: int, radix: int, width: int): string {
        return value < 0 ? "—" : root.controller.formatValue(value, radix, width)
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
        displayTitle: qsTr("Line States")
        displayInfo: [root.hasInputs ? qsTr("Read %1").arg(root.controller.readLinesLabel) : "",
                      root.hasOutputs ? qsTr("Write %1").arg(root.controller.writeLinesLabel) : ""].filter(s => s.length > 0).join(" · ")

        // --- Display -----------------------------------------------------------------------
        ColumnLayout {
            id: display
            anchors.fill: parent
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
                        { title: qsTr("Inputs"), shown: root.hasInputs, value: root.controller.hasReading ? root.controller.inputValue : -1,
                          radix: root.controller.inputRadix, width: root.controller.inputWidth, color: Theme.ch1, output: false },
                        { title: qsTr("Outputs"), shown: root.hasOutputs, value: root.controller.writtenValue,
                          radix: root.controller.outputRadix, width: root.controller.outputWidth, color: Theme.math, output: true }
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
                            Text {
                                text: valueBlock.modelData.output ? root.controller.writeLinesLabel : root.controller.readLinesLabel
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                        RowLayout {
                            spacing: Theme.spacingS
                            DioRadixButton {
                                Layout.alignment: Qt.AlignVCenter
                                radix: valueBlock.modelData.radix
                                onRadixSelected: r => {
                                    if (valueBlock.modelData.output)
                                        root.controller.outputRadix = r
                                    else
                                        root.controller.inputRadix = r
                                }
                            }
                            Text {
                                text: root.valueText(valueBlock.modelData.value, valueBlock.modelData.radix, valueBlock.modelData.width)
                                color: valueBlock.modelData.value < 0 ? Theme.textMuted : valueBlock.modelData.color
                                font.pixelSize: 34
                                font.weight: Font.Light
                                font.family: valueBlock.modelData.radix === DioController.Binary ? Theme.monoFamily : Theme.fontFamily
                                font.features: Theme.tabularFigures
                            }
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                // Writer state.
                ColumnLayout {
                    visible: root.hasOutputs
                    spacing: 2
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("PATTERN")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: root.controller.patternNames[root.controller.pattern]
                              + (root.controller.pattern !== DioController.Manual && root.controller.running && root.controller.sequenceStep >= 0
                                 ? qsTr(" · every %1 ms").arg(root.controller.stepInterval) : "")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                    }
                }
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
                    ToolTip.text: qsTr("Software-timed sampling: pulses shorter than the sample interval can be missed, edges have several ms of jitter.")
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
                    width: cursorText.implicitWidth + 16
                    height: cursorText.implicitHeight + 10
                    radius: Theme.radiusS
                    color: Theme.alpha(Theme.surfaceRaised, 0.92)
                    border.width: 1
                    border.color: Theme.border
                    Text {
                        id: cursorText
                        anchors.centerIn: parent
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

        // --- Inspector ---------------------------------------------------------------------
        inspector: [
            Section {
                id: directionsSection
                title: qsTr("Line Directions")
                summary: directionsSection.collapsed ? root.controller.readLinesLabel + " in · " + root.controller.writeLinesLabel + " out" : ""
                Repeater {
                    model: 8
                    delegate: RowLayout {
                        id: directionRow
                        required property int index
                        readonly property var owner: root.controller.lineOwners[index] ?? ({})
                        Layout.fillWidth: true
                        spacing: Theme.spacingS
                        Text {
                            Layout.preferredWidth: 40
                            text: "DIO " + directionRow.index
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeM
                            font.weight: Font.DemiBold
                        }
                        SegmentedControl {
                            Layout.preferredWidth: 118
                            compact: true
                            model: [qsTr("In"), qsTr("Out"), "–"]
                            accentColor: currentIndex === 0 ? Theme.ch1 : currentIndex === 1 ? Theme.math : Theme.textSecondary
                            currentIndex: (root.controller.outputMask >> directionRow.index) & 1 ? 1
                                        : (root.controller.inputMask >> directionRow.index) & 1 ? 0 : 2
                            onActivated: i => root.controller.setLineDirection(directionRow.index, i)
                        }
                        TextField {
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.controlHeightS
                            placeholderText: (directionRow.owner.owner ?? "").length > 0 ? directionRow.owner.owner : qsTr("label")
                            text: root.controller.lineLabels[directionRow.index] ?? ""
                            font.pixelSize: Theme.fontSizeS
                            onEditingFinished: root.controller.setLineLabel(directionRow.index, text)
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
                // Lines of other instruments.
                Rectangle {
                    Layout.fillWidth: true
                    visible: root.controller.foreignMask !== 0
                    implicitHeight: ownersText.implicitHeight + 16
                    radius: Theme.radiusM
                    color: Theme.tint(root.controller.conflictMask !== 0 ? Theme.warning : Theme.accent)
                    Text {
                        id: ownersText
                        anchors.fill: parent
                        anchors.margins: 8
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeS
                        text: {
                            const lines = []
                            for (let l = 0; l < 8; ++l) {
                                const o = root.controller.lineOwners[l]
                                if (o && o.text.length > 0)
                                    lines.push(o.text)
                            }
                            return lines.join("\n") + (root.controller.conflictMask !== 0
                                   ? "\n" + qsTr("Running Digital I/O on these lines pauses that instrument; set them to “–” to share the connector.")
                                   : "")
                        }
                    }
                }
            },
            Section {
                id: readerSection
                title: qsTr("Digital Reader")
                accentColor: Theme.ch1
                summary: readerSection.collapsed ? root.controller.readLinesLabel : ""
                Text {
                    text: qsTr("Configuration Settings")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                }
                FieldRow {
                    Field {
                        label: qsTr("Lines to Read")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.presetNames
                            currentIndex: Math.min(root.controller.readPreset, 3)
                            displayText: root.controller.readPreset === 4 ? qsTr("Custom") : currentText
                            onActivated: index => root.controller.setReadPreset(index)
                        }
                    }
                    Field {
                        label: qsTr("Sample interval")
                        hint: qsTr("Polling period of the inputs and of the timeline (10 ms ... 1 s)")
                        SiField {
                            Layout.fillWidth: true
                            unit: "s"
                            from: 0.01
                            to: 1
                            digits: 3
                            value: root.controller.sampleInterval / 1000
                            onValueModified: v => root.controller.sampleInterval = Math.round(v * 1000)
                        }
                    }
                }
            },
            Section {
                id: writerSection
                title: qsTr("Digital Writer")
                accentColor: Theme.math
                summary: writerSection.collapsed ? root.controller.writeLinesLabel + " · " + root.controller.patternNames[root.controller.pattern] : ""
                Text {
                    text: qsTr("Configuration Settings")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                }
                FieldRow {
                    Field {
                        label: qsTr("Lines to Write")
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.presetNames
                            currentIndex: Math.min(root.controller.writePreset, 3)
                            displayText: root.controller.writePreset === 4 ? qsTr("Custom") : currentText
                            onActivated: index => root.controller.setWritePreset(index)
                        }
                    }
                    Field {
                        label: qsTr("Pattern")
                        ComboBox {
                            Layout.fillWidth: true
                            enabled: root.hasOutputs
                            model: root.controller.patternNames
                            currentIndex: root.controller.pattern
                            onActivated: index => root.controller.pattern = index
                        }
                    }
                }
                Field {
                    label: qsTr("Pattern Number")
                    trailing: root.hasOutputs ? qsTr("0 … %1").arg(root.controller.formatValue(root.controller.outputMax, root.controller.outputRadix, 0)) : ""
                    disabled: root.controller.pattern !== DioController.Manual || !root.hasOutputs
                    DioNumberField {
                        Layout.fillWidth: true
                        enabled: root.controller.pattern === DioController.Manual && root.hasOutputs
                        value: root.controller.manualValue
                        maximum: root.controller.outputMax
                        radix: root.controller.outputRadix
                        bits: root.controller.outputWidth
                        onValueModified: v => root.controller.manualValue = v
                        onRadixSelected: r => root.controller.outputRadix = r
                    }
                }
                // Manual Pattern switches (ELVISmx layout: lines 7 ... 0, HI up / LO down).
                Field {
                    label: qsTr("Manual Pattern")
                    disabled: root.controller.pattern !== DioController.Manual
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Repeater {
                            model: 8
                            delegate: ColumnLayout {
                                id: switchColumn
                                required property int index
                                readonly property int line: 7 - index
                                readonly property bool isOut: (root.controller.outputMask >> line) & 1
                                readonly property int bit: {
                                    let b = 0
                                    for (let l = 0; l < line; ++l)
                                        if ((root.controller.outputMask >> l) & 1)
                                            ++b
                                    return b
                                }
                                Layout.fillWidth: true
                                Layout.preferredWidth: 30
                                spacing: 2
                                DioSwitch {
                                    Layout.alignment: Qt.AlignHCenter
                                    enabled: switchColumn.isOut && root.controller.pattern === DioController.Manual
                                    opacity: switchColumn.isOut ? 1 : 0.35
                                    checked: switchColumn.isOut && ((root.controller.manualValue >> switchColumn.bit) & 1)
                                    onToggled: root.controller.toggleLine(switchColumn.line)
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: switchColumn.line
                                    color: switchColumn.isOut ? Theme.textSecondary : Theme.textDisabled
                                    font.pixelSize: Theme.fontSizeXs
                                }
                            }
                        }
                        ColumnLayout {
                            spacing: 8
                            Text { text: qsTr("HI"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeXs }
                            Text { text: qsTr("LO"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeXs }
                        }
                    }
                }
                Field {
                    label: qsTr("Action")
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Repeater {
                            model: [
                                { text: qsTr("Toggle"), action: DioController.Toggle, tip: qsTr("Invert every line") },
                                { text: qsTr("Rotate"), action: DioController.Rotate, tip: qsTr("Rotate one bit in the Direction (the bit leaving re-enters on the other side)") },
                                { text: qsTr("Shift"), action: DioController.Shift, tip: qsTr("Shift one bit in the Direction (a 0 enters)") }
                            ]
                            delegate: ActionButton {
                                required property var modelData
                                Layout.fillWidth: true
                                compact: true
                                text: modelData.text
                                toolTip: modelData.tip
                                enabled: root.controller.pattern === DioController.Manual && root.hasOutputs
                                onClicked: root.controller.applyAction(modelData.action)
                            }
                        }
                    }
                }
                FieldRow {
                    Field {
                        label: qsTr("Direction")
                        disabled: root.controller.pattern !== DioController.Manual && root.controller.pattern !== DioController.WalkingOnes
                        SegmentedControl {
                            Layout.fillWidth: true
                            enabled: root.controller.pattern === DioController.Manual || root.controller.pattern === DioController.WalkingOnes
                            model: [qsTr("Left"), qsTr("Right")]
                            currentIndex: root.controller.shiftDirection
                            onActivated: index => root.controller.shiftDirection = index
                        }
                    }
                    Field {
                        label: qsTr("Step interval")
                        hint: qsTr("Time between two values of a pattern (10 ms ... 10 s)")
                        disabled: root.controller.pattern === DioController.Manual
                        SiField {
                            Layout.fillWidth: true
                            enabled: root.controller.pattern !== DioController.Manual
                            unit: "s"
                            from: 0.01
                            to: 10
                            digits: 3
                            value: root.controller.stepInterval / 1000
                            onValueModified: v => root.controller.stepInterval = Math.round(v * 1000)
                        }
                    }
                }
                // Outputs keep driving after Stop: say so, and offer to release them.
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.controller.drivenMask !== 0 && !root.controller.running
                    Icon { name: "info"; size: Theme.iconSizeS; color: Theme.math }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%1 still driven").arg(ResourceArbiter.dioLinesLabel(root.controller.drivenMask))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeS
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        text: qsTr("Release")
                        compact: true
                        tone: "subtle"
                        color: Theme.math
                        toolTip: qsTr("Make the driven lines inputs again (high impedance)")
                        onClicked: root.controller.releaseOutputs()
                    }
                }
            },
            Section {
                id: timelineSection
                title: qsTr("Timeline")
                collapsed: true
                summary: timelineSection.collapsed ? qsTr("%1 s window").arg(root.controller.timelineSeconds) : ""
                FieldRow {
                    Field {
                        label: qsTr("Window")
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["5 s", "10 s", "30 s", "60 s"]
                            currentIndex: root.controller.timelineWindow
                            onActivated: index => root.controller.timelineWindow = index
                        }
                    }
                    Field {
                        label: qsTr("Sample interval")
                        SiField {
                            Layout.fillWidth: true
                            unit: "s"
                            from: 0.01
                            to: 1
                            digits: 3
                            value: root.controller.sampleInterval / 1000
                            onValueModified: v => root.controller.sampleInterval = Math.round(v * 1000)
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("All eight lines are sampled by software (not a hardware-timed logic analyzer): pulses shorter than the sample interval can be missed. The last 10 minutes are kept; Export data saves them.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            }
        ]
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
        return name + "  " + Units.format(x, "s") + "   7…0 " + bits
    }
    Connections {
        target: root.controller
        function onTimelineWindowChanged() { root.followArmed = true; root.resetTimelineView() }
        function onStarted() { if (!root.controller.timelinePaused) { root.followArmed = true; root.resetTimelineView() } }
        function onTimelineAdvanced() { root.updateFollow() }
    }
}
