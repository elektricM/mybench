pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The Oscilloscope pane (docs/instruments/scope.md, docs/UI.md), PicoScope-7 style:
//   instrument bar  (pane header content) Run/Stop with the trigger state, Single, the horizontal
//                   controls (Timebase knob, trigger Position knob), View YT / XY / FFT, Autoscale
//   display         ScopeDisplay edge to edge (markers, cursors; FFT plot with its source under it)
//   right column    InstrumentColumn: CH 0 and CH 1 cards (switch, Scale and Position knobs, source,
//                   range), the Trigger card (type, source, slope, level, mode; advanced settings
//                   behind its sliders button), the Math card; the tool palette pinned at the bottom
//                   (Measure, Cursors, References, Display; export and print are in the ⋯ menu).
//                   An open tool panel pushes the display aside when it has room, else floats
//                   over its right part.
//   cards           one card per cursor pair (C1–C2: Δt, 1/Δt, ΔV; H1–H2: ΔV), one measurement
//                   card per shown measurement (value + statistics), + Add
// Every setting has exactly one control; settings that do not apply are hidden. No inspector.
Item {
    id: root

    required property ScopeController controller
    readonly property ScopeChannel ch0: controller.channel0
    readonly property ScopeChannel ch1: controller.channel1
    readonly property ScopeMath math: controller.math
    readonly property bool edge: controller.triggerType === ScopeController.Edge
    readonly property int triggerChannel: controller.triggerSource === ScopeController.Chan1Source ? 1 : 0
    readonly property alias panel: panel
    readonly property alias display: display
    readonly property alias measurementModel: measurementModel
    // The open tool panel ("" = none): measure, cursors, references, display.
    // --scope-tool=<id> opens one at start (screenshots of the panels).
    property string tool: AppInfo.value("scope-tool")
    // Split screen / small windows: knobs give way to the fields, the palette shows icons only.
    readonly property bool narrow: width < Theme.instrumentCompactWidth
    // (The bar holds Run, Single, two knobs, the view and Autoscale: labels fit from 1100 px.)
    readonly property bool barCompact: width < 1100
    // An open tool panel pushes the display aside (the traces stay visible) while the display
    // keeps at least this width; in a narrower pane it floats over the display's right part.
    readonly property bool toolPushes: tool.length > 0
                                       && width - (narrow ? Theme.instrumentColumnCompactWidth : Theme.instrumentColumnWidth)
                                          - Theme.toolPanelWidth - 2 * Theme.spacingS >= 480
    // Pictograms of the measurement types (SVG paths on 32×18).
    readonly property var measurementGlyphs: ({
        [ScopeChannel.Rms]: "M2 9 C6 1 10 1 14 9 S22 17 26 9 30 5 30 5",
        [ScopeChannel.Frequency]: "M2 15 V4 H9 V15 H16 V4 H23 V15 H30",
        [ScopeChannel.PeakToPeak]: "M6 2 H26 M6 16 H26 M16 3 V15",
        [ScopeChannel.Peak]: "M2 16 H30 M16 2 V16 M10 2 H22",
        [ScopeChannel.Mean]: "M2 9 H30",
        [ScopeChannel.Minimum]: "M2 4 C10 4 12 15 16 15 S22 4 30 4 M10 17 H22",
        [ScopeChannel.Maximum]: "M2 14 C10 14 12 3 16 3 S22 14 30 14 M10 1 H22",
        [ScopeChannel.Amplitude]: "M4 3 H28 M4 15 H28 M10 3 V15",
        [ScopeChannel.Period]: "M3 4 V14 M29 4 V14 M3 9 H29",
        [ScopeChannel.DutyCycle]: "M2 15 H6 V3 H16 V15 H30",
        [ScopeChannel.RiseTime]: "M2 15 H10 L22 3 H30",
        [ScopeChannel.FallTime]: "M2 3 H10 L22 15 H30",
        [ScopeChannel.PositiveWidth]: "M2 15 H9 V3 H23 V15 H30",
        [ScopeChannel.NegativeWidth]: "M2 3 H9 V15 H23 V3 H30",
        [ScopeChannel.Overshoot]: "M2 15 H10 V2 L13 5 H30",
        [ScopeChannel.Phase]: "M2 9 C6 1 10 1 14 9 S22 17 26 9 M8 9 C12 1 16 1 20 9",
        [ScopeChannel.CounterFrequency]: "M2 15 V4 H9 V15 H16 V4 H23 V15 H30 M5 2 V6 M19 2 V6",
        [ScopeChannel.AcRms]: "M2 9 C6 1 10 1 14 9 S22 17 26 9 M2 9 H30",
        [ScopeChannel.CrestFactor]: "M2 12 C6 8 10 8 14 12 L16 2 L18 12 C22 8 26 8 30 12",
        [ScopeChannel.Preshoot]: "M2 15 H10 L13 17 V3 H30",
        [ScopeChannel.SlewRate]: "M2 16 L30 2 M18 10 H26 V2"
    })
    // Picture-button captions that fit a third of the panel (the tooltip has the full name).
    readonly property var measurementShortLabels: ({
        [ScopeChannel.PeakToPeak]: qsTr("Pk-pk"),
        [ScopeChannel.CounterFrequency]: qsTr("Edge freq."),
        [ScopeChannel.PositiveWidth]: qsTr("+ Width"),
        [ScopeChannel.NegativeWidth]: qsTr("− Width")
    })
    property int statistics: 1          // measurement cards: 0 off, 1 last 100, 2 all
    property int cardSource: -1         // measurement cards: -1 every trace, else 0 / 1 / 2
    function toggleTool(id: string) {
        tool = tool === id ? "" : id
    }

    // The display's status: only what the bar does not show (timebase and trigger live in their
    // knobs). Empty in the usual case, so the display keeps the whole height.
    readonly property string summary: {
        const parts = []
        if (controller.rolling)
            parts.push(qsTr("Roll"))
        if (controller.averaging > 1)
            parts.push(qsTr("Avg ×%1").arg(controller.averaging))
        if (controller.persistence > 0)
            parts.push(qsTr("Persist %1").arg(Units.format(controller.persistence / 1000, "s", 2)))
        return parts.join("  ·  ")
    }
    // The traces a cursor or the level cursors can follow (index 0, 1, 2 = math).
    readonly property var traceChoices: [{ text: "CH0" }, { text: "CH1" }, { text: qsTr("Math") }]

    // A labelled row of a tool panel.
    component PanelRow: ColumnLayout {
        property alias label: rowLabel.text
        Layout.fillWidth: true
        spacing: 3
        Text {
            id: rowLabel
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
        }
    }
    // Caption: a fact, not a control.
    component Caption: Text {
        Layout.fillWidth: true
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSizeCaption
        font.features: Theme.tabularFigures
        wrapMode: Text.WordWrap
    }

    ScopeMeasurements {
        id: measurementModel
        controller: root.controller
        ch0Color: display.traceColor(0)
        ch1Color: display.traceColor(1)
        mathColor: display.traceColor(2)
    }


    // View (YT / XY / FFT) and Autoscale: in the bar, or at the top of the right column in a
    // narrow pane (one instance at a time).
    Component {
        id: viewControls
        RowLayout {
            spacing: Theme.spacingS
            SegmentedControl {
                objectName: "displayMode"
                // YT, XY (CH1 over CH0) and FFT (YT with the spectrum of a channel under it).
                readonly property bool fftOn: root.controller.fftSource !== ScopeController.FftOff
                model: [{ icon: "wave-sine", text: root.barCompact ? "" : "YT", toolTip: qsTr("YT: voltage over time") },
                        { icon: "crosshair", text: root.barCompact ? "" : "XY", toolTip: qsTr("XY: CH1 (Y) over CH0 (X)") },
                        { icon: "bar-chart", text: root.barCompact ? "" : "FFT", toolTip: qsTr("FFT: time view with the spectrum of a channel under it (for averaging and distortion, use the Spectrum Analyzer)") }]
                currentIndex: fftOn ? 2 : root.controller.displayMode
                onActivated: index => {
                    if (index === 2) {
                        root.controller.displayMode = ScopeController.YT
                        if (!fftOn)
                            root.controller.fftSource = root.ch0.enabled ? ScopeController.FftChannel0 : ScopeController.FftChannel1
                    } else {
                        root.controller.fftSource = ScopeController.FftOff
                        root.controller.displayMode = index
                    }
                }
            }
            Button {
                objectName: "autoscaleButton"
                text: root.barCompact ? "" : qsTr("Autoscale")
                icon.source: Theme.icon("autoscale")
                display: root.barCompact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                enabled: !root.controller.autoscaling && !root.controller.busy
                         && (root.controller.running || root.controller.canRun)
                focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Autoscale")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Set the scales and the timebase from the signals")
                onClicked: root.controller.autoscale()
            }
        }
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.summary
        showRunControls: false      // Run/Stop and Single are the first controls of the bar
        runBar.showTimeout: false   // the Run button says it
        inspector: []

        headerContent: [
            RowLayout {
                id: instrumentBar
                spacing: root.barCompact ? Theme.spacingS : Theme.spacingM

                // Run/Stop with the trigger state under the verb.
                RunButton {
                    objectName: "scopeRunButton"
                    running: root.controller.running
                    stateText: panel.paneHeader.statusText
                    busy: root.controller.state === InstrumentController.Starting
                          || root.controller.state === InstrumentController.Stopping
                    enabled: running ? root.controller.state !== InstrumentController.Stopping : root.controller.canRun
                    shortcutText: Theme.keys("Space")
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "scopeSingleButton"
                    text: root.barCompact ? "" : qsTr("Single")
                    icon.source: Theme.icon("step")
                    display: root.barCompact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Accessible.name: qsTr("Single")
                    implicitHeight: Theme.controlHeightL
                    enabled: root.controller.canRun
                             || (root.controller.state === InstrumentController.Running
                                 && root.controller.runMode === InstrumentController.Continuous)
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Capture one record, then stop")
                    onClicked: root.controller.runOnce()
                }

                Rectangle { visible: !root.barCompact; implicitWidth: 1; Layout.fillHeight: true; Layout.margins: 6; color: Theme.separator }

                KnobField {
                    objectName: "timeDiv"
                    label: qsTr("Timebase")
                    unit: "s/div"
                    caption: qsTr("%1 · %2 samples").arg(Units.format(root.controller.plannedRate, "S/s", 4))
                                                   .arg(root.controller.plannedRecordLength.toLocaleString(Qt.locale(), "f", 0))
                    compact: root.narrow
                    knobSize: 44
                    valueWidth: root.narrow ? 74 : 88
                    values: root.controller.timeDivValues
                    formatter: v => Units.format(v, "s", 3) + "/div"
                    currentIndex: root.controller.timeDivIndex
                    onIndexActivated: index => root.controller.timeDivIndex = index
                }

                // The trigger's place on the screen (pre-trigger): Edge only, not while rolling.
                KnobField {
                    objectName: "horizontalPosition"
                    visible: root.edge && !root.controller.rolling
                    label: qsTr("Trigger position")
                    unit: "%"
                    compact: root.narrow
                    knobSize: 44
                    valueWidth: root.narrow ? 56 : 64
                    accentColor: display.traceColor(root.triggerChannel)
                    from: 0
                    to: 100
                    stepSize: 1
                    defaultValue: 50
                    formatter: v => Math.round(v) + " %"
                    value: root.controller.horizontalPosition
                    toolTip: qsTr("Where the trigger point sits on the screen, 0 to 100 %; drag the T marker at the top of the graph")
                    onValueModified: v => root.controller.horizontalPosition = Math.round(v)
                }

                Item { Layout.fillWidth: true }

                Loader {
                    active: !root.narrow
                    visible: active
                    sourceComponent: viewControls
                }
            }
        ]
        menuItems: [
            MenuItem {
                text: qsTr("Center Cursors")
                enabled: root.controller.cursorsOn
                onTriggered: root.controller.resetCursors()
            }
        ]
        traceStyles: [
            { name: "CH0", style: root.ch0.style, color: Theme.signalCh0 },
            { name: "CH1", style: root.ch1.style, color: Theme.signalCh1 },
            { name: qsTr("Math"), style: root.math.style, color: Theme.signalMath },
            { name: "REF A", style: root.controller.refAStyle, color: Theme.signalRef },
            { name: "REF B", style: root.controller.refBStyle, color: Theme.signalRef },
            { name: qsTr("Cursor C1"), style: root.controller.cursor1.style, color: Theme.cursor1 },
            { name: qsTr("Cursor C2"), style: root.controller.cursor2.style, color: Theme.cursor2 }
        ]

        Item {
            anchors.fill: parent

            ScopeDisplay {
                id: display
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom
                          right: root.toolPushes ? toolArea.left : column.left
                          rightMargin: root.toolPushes ? Theme.spacingS : 0 }
                controller: root.controller
            }

            // Right column: channel cards, Trigger, Math; the tool palette pinned at the bottom.
            InstrumentColumn {
                id: column
                objectName: "scopeColumn"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                compact: root.narrow

                Loader {
                    id: narrowViewControls
                    Layout.alignment: Qt.AlignHCenter
                    active: root.narrow
                    visible: active
                    sourceComponent: viewControls
                }
                ScopeChannelCard {
                    Layout.fillWidth: true
                    controller: root.controller
                    channel: root.ch0
                    channelColor: display.traceColor(0)
                    compact: root.narrow
                }
                ScopeChannelCard {
                    Layout.fillWidth: true
                    controller: root.controller
                    channel: root.ch1
                    channelColor: display.traceColor(1)
                    compact: root.narrow
                }
                ScopeTriggerCard {
                    Layout.fillWidth: true
                    controller: root.controller
                    ch0Color: display.traceColor(0)
                    ch1Color: display.traceColor(1)
                    compact: root.narrow
                }
                ScopeMathCard {
                    Layout.fillWidth: true
                    controller: root.controller
                    channelColor: display.traceColor(2)
                    compact: root.narrow
                }

                tools: ToolPalette {
                    objectName: "toolPalette"
                    columns: 4
                    cellHeight: root.narrow ? 34 : 44
                    currentId: root.tool
                    model: [{ id: "measure", text: root.narrow ? "" : qsTr("Measure"), icon: "ruler", toolTip: qsTr("Measurements") },
                            { id: "cursors", text: root.narrow ? "" : qsTr("Cursors"), icon: "crosshair", toolTip: qsTr("Cursors C1, C2 and H1, H2") },
                            { id: "references", text: root.narrow ? "" : qsTr("Refs"), icon: "layers", toolTip: qsTr("Reference traces") },
                            { id: "display", text: root.narrow ? "" : qsTr("Display"), icon: "grid", toolTip: qsTr("Persistence, averaging, record length, roll") }]
                    onActivated: id => root.toggleTool(id)
                }
            }

            // The open tool panel: beside the display (toolPushes) or over its right part, next to the column.
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: Theme.toolPanelWidth
                z: 10

                // Measure: which cards, the measurement types (picture buttons), statistics.
                ToolPanel {
                    objectName: "measurePanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "measure"
                    width: parent.width
                    title: qsTr("Measurements")
                    onCloseRequested: root.tool = ""
                    PanelRow {
                        label: qsTr("Cards for")
                        SegmentedControl {
                            objectName: "measureSource"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("All") }, { text: "CH0" }, { text: "CH1" }, { text: qsTr("Math") }]
                            currentIndex: root.cardSource + 1
                            onActivated: index => root.cardSource = index - 1
                        }
                    }
                    PanelRow {
                        label: qsTr("Add or remove")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                model: root.controller.measurementCatalog
                                delegate: PictureButton {
                                    id: typeButton
                                    required property var modelData
                                    readonly property bool shown: (measurementModel.shownMask & modelData.flag) !== 0
                                    objectName: "measureType" + modelData.flag
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    checked: shown
                                    text: root.measurementShortLabels[modelData.flag] ?? modelData.label
                                    glyph: root.measurementGlyphs[modelData.flag] ?? ""
                                    focusPolicy: Qt.TabFocus
                                    ToolTip.visible: hovered
                                    ToolTip.text: modelData.name
                                    onClicked: {
                                        if (shown)
                                            measurementModel.remove("m" + modelData.flag)
                                        else
                                            measurementModel.add("m" + modelData.flag)
                                    }
                                }
                            }
                        }
                    }
                    PanelRow {
                        label: qsTr("Statistics")
                        SegmentedControl {
                            objectName: "measureStatistics"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Off") }, { text: qsTr("Last 100") }, { text: qsTr("All") }]
                            currentIndex: root.statistics
                            onActivated: index => root.statistics = index
                        }
                    }
                    PanelRow {
                        label: qsTr("Frequency method")
                        SegmentedControl {
                            objectName: "frequencyMethod"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Tone"), toolTip: qsTr("Spectral estimate of the main tone; shown above 0.2 div") },
                                    { text: qsTr("Edges"), toolTip: qsTr("Counts mid-level crossings: better for square and pulse signals") }]
                            currentIndex: root.controller.frequencyMethod
                            onActivated: index => root.controller.frequencyMethod = index
                        }
                    }
                }

                ToolPanel {
                    objectName: "cursorsPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "cursors"
                    width: parent.width
                    title: qsTr("Cursors")
                    onCloseRequested: root.tool = ""
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: qsTr("Time cursors C1, C2"); color: Theme.text; font.pixelSize: Theme.fontSizeBody }
                        Switch {
                            objectName: "cursorsSwitch"
                            checked: root.controller.cursorsOn
                            Accessible.name: qsTr("Cursors")
                            onToggled: root.controller.cursorsOn = checked
                        }
                    }
                    PanelRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("C1 follows")
                        SegmentedControl {
                            objectName: "cursor1Trace"
                            Layout.fillWidth: true
                            accentColor: Theme.cursor1
                            model: root.traceChoices
                            currentIndex: root.controller.cursor1.channel
                            onActivated: index => root.controller.cursor1.channel = index
                        }
                    }
                    PanelRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("C2 follows")
                        SegmentedControl {
                            objectName: "cursor2Trace"
                            Layout.fillWidth: true
                            accentColor: Theme.cursor2
                            model: root.traceChoices
                            currentIndex: root.controller.cursor2.channel
                            onActivated: index => root.controller.cursor2.channel = index
                        }
                    }
                    PanelRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("Style")
                        ComboBox {
                            objectName: "cursorStyle"
                            Layout.fillWidth: true
                            // ScopeCursor.CursorStyle order; one style for both cursors.
                            model: [qsTr("Line"), qsTr("Full crosshair"), qsTr("Small crosshair"), qsTr("Hidden")]
                            currentIndex: root.controller.cursor1.cursorStyle
                            onActivated: index => {
                                root.controller.cursor1.cursorStyle = index
                                root.controller.cursor2.cursorStyle = index
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingS
                        Text { Layout.fillWidth: true; text: qsTr("Level cursors H1, H2"); color: Theme.text; font.pixelSize: Theme.fontSizeBody }
                        Switch {
                            objectName: "levelCursorsSwitch"
                            checked: root.controller.levelCursorsOn
                            Accessible.name: qsTr("Level cursors")
                            onToggled: root.controller.levelCursorsOn = checked
                        }
                    }
                    PanelRow {
                        visible: root.controller.levelCursorsOn
                        label: qsTr("On trace")
                        SegmentedControl {
                            objectName: "levelCursorTrace"
                            Layout.fillWidth: true
                            model: root.traceChoices
                            currentIndex: root.controller.levelCursorChannel
                            onActivated: index => root.controller.levelCursorChannel = index
                        }
                    }
                    Caption { text: qsTr("Drag the cursors on the graph; their readings are a card under the display.") }
                }

                ToolPanel {
                    objectName: "referencesPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "references"
                    width: parent.width
                    title: qsTr("References")
                    onCloseRequested: root.tool = ""
                    Repeater {
                        model: 2
                        delegate: ColumnLayout {
                            id: refRow
                            required property int index
                            readonly property bool available: index === 0 ? root.controller.refAAvailable : root.controller.refBAvailable
                            readonly property bool shown: index === 0 ? root.controller.refAVisible : root.controller.refBVisible
                            readonly property string label: index === 0 ? "REF A" : "REF B"
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: refRow.label; color: Theme.signalRef; font.pixelSize: Theme.fontSizeBody; font.weight: Font.DemiBold }
                                Text {
                                    Layout.fillWidth: true
                                    text: refRow.available ? (refRow.index === 0 ? root.controller.refALabel : root.controller.refBLabel) : qsTr("Empty")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    elide: Text.ElideRight
                                }
                            }
                            Row {
                                spacing: 2
                                Repeater {
                                    model: [{ text: "CH0", src: 0, on: root.ch0.enabled }, { text: "CH1", src: 1, on: root.ch1.enabled },
                                            { text: qsTr("Math"), src: 2, on: root.math.enabled }]
                                    delegate: Button {
                                        required property var modelData
                                        text: qsTr("← %1").arg(modelData.text)
                                        font.pixelSize: Theme.fontSizeXs
                                        implicitHeight: Theme.controlHeightS
                                        enabled: root.controller.hasRecord && modelData.on
                                        ToolTip.visible: hovered
                                        ToolTip.text: qsTr("Keep this trace of the shown record as %1").arg(refRow.label)
                                        onClicked: root.controller.saveReference(refRow.index, modelData.src)
                                    }
                                }
                                ToolButton {
                                    icon.source: Theme.icon(refRow.shown ? "eye" : "eye-off")
                                    enabled: refRow.available
                                    Accessible.name: refRow.shown ? qsTr("Hide") : qsTr("Show")
                                    ToolTip.visible: hovered
                                    ToolTip.text: refRow.shown ? qsTr("Hide") : qsTr("Show")
                                    onClicked: {
                                        if (refRow.index === 0)
                                            root.controller.refAVisible = !refRow.shown
                                        else
                                            root.controller.refBVisible = !refRow.shown
                                    }
                                }
                                ToolButton {
                                    icon.source: Theme.icon("folder-open")
                                    Accessible.name: qsTr("Import")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Import a CSV or TSV file…")
                                    onClicked: refDialog.openFor(refRow.index, false)
                                }
                                ToolButton {
                                    icon.source: Theme.icon("export")
                                    enabled: refRow.available
                                    Accessible.name: qsTr("Export")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Export to a file…")
                                    onClicked: refDialog.openFor(refRow.index, true)
                                }
                                ToolButton {
                                    icon.source: Theme.icon("trash")
                                    enabled: refRow.available
                                    Accessible.name: qsTr("Clear")
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Clear")
                                    onClicked: root.controller.clearReference(refRow.index)
                                }
                            }
                        }
                    }
                }

                ToolPanel {
                    objectName: "displayPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "display"
                    width: parent.width
                    title: qsTr("Display")
                    onCloseRequested: root.tool = ""
                    PanelRow {
                        label: qsTr("Persistence")
                        StepSelector {
                            objectName: "persistence"
                            readonly property var values: [0, 100, 250, 500, 1000, 2000, 5000]
                            Layout.fillWidth: true
                            label: qsTr("Persistence")
                            model: values.map(v => ({ value: v, text: v === 0 ? qsTr("Off") : Units.format(v / 1000, "s", 3) }))
                            currentIndex: Math.max(0, values.indexOf(root.controller.persistence))
                            onActivated: index => root.controller.persistence = values[index]
                        }
                    }
                    PanelRow {
                        label: qsTr("Averaging")
                        StepSelector {
                            objectName: "averaging"
                            readonly property var values: [1, 2, 4, 8, 16, 32, 64]
                            Layout.fillWidth: true
                            label: qsTr("Averaging")
                            model: values.map(v => ({ value: v, text: v === 1 ? qsTr("Off") : qsTr("%1 records").arg(v) }))
                            currentIndex: Math.max(0, values.indexOf(root.controller.averaging))
                            onActivated: index => root.controller.averaging = values[index]
                        }
                    }
                    PanelRow {
                        label: qsTr("Record length")
                        SegmentedControl {
                            objectName: "recordLength"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Normal"), toolTip: qsTr("Up to 2 501 samples per screen") },
                                    { text: qsTr("Long"), toolTip: qsTr("Up to 100 000 samples per screen, for zooming and the FFT") }]
                            currentIndex: root.controller.samplingMode
                            onActivated: index => root.controller.samplingMode = index
                        }
                    }
                    RowLayout {
                        visible: root.controller.rollAvailable
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: qsTr("Roll (from 50 ms/div, untriggered)"); color: Theme.text; font.pixelSize: Theme.fontSizeBody; wrapMode: Text.WordWrap }
                        Switch {
                            objectName: "rollMode"
                            checked: root.controller.rollMode
                            Accessible.name: qsTr("Roll")
                            onToggled: root.controller.rollMode = checked
                        }
                    }
                    Switch {
                        objectName: "autoscaleSetsLevel"
                        text: qsTr("Autoscale sets the trigger level")
                        checked: root.controller.autoscaleSetsLevel
                        onToggled: root.controller.autoscaleSetsLevel = checked
                    }
                    Button {
                        objectName: "traceStylesButton"
                        text: qsTr("Trace Styles…")
                        onClicked: panel.openTraceStyleDialog()
                    }
                }
            }
        }

        measurements: [
            ScopeMeasurementCards {
                objectName: "measurementCards"
                controller: root.controller
                columns: measurementModel.columns
                traces: measurementModel.traces
                rows: measurementModel.rows
                available: measurementModel.available
                summaryText: measurementModel.summaryText
                catalog: root.controller.measurementCatalog
                statistics: root.statistics
                sourceFilter: root.cardSource
                onAddRequested: id => measurementModel.add(id)
                onRemoveRequested: id => measurementModel.remove(id)
                onOpenMeasurePanel: root.tool = "measure"
            }
        ]
    }

    FileDialog {
        id: refDialog
        property int slot: 0
        property bool saving: true
        function openFor(index: int, save: bool) {
            slot = index
            saving = save
            if (save)
                selectedFile = DataExport.suggestedFileUrl("scope-ref", "csv")
            open()
        }
        title: saving ? qsTr("Export Reference") : qsTr("Import Reference")
        fileMode: saving ? FileDialog.SaveFile : FileDialog.OpenFile
        nameFilters: [qsTr("Data (*.csv *.tsv *.txt)"), qsTr("All files (*)")]
        defaultSuffix: "csv"
        currentFolder: DataExport.lastFolder
        onAccepted: {
            if (saving)
                root.controller.exportReference(slot, selectedFile)
            else
                root.controller.importReference(slot, selectedFile)
        }
    }
}
