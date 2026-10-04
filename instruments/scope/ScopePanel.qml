pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The Oscilloscope pane (docs/instruments/scope.md, docs/UI.md), PicoScope-7 style:
//   instrument bar  (pane header content) Run/Stop with the trigger state, Single, Timebase knob,
//                   Trigger level knob + Auto / Normal / None, View YT / XY / FFT, Autoscale, ⋯
//   display         ScopeDisplay edge to edge (markers, cursors, info line; FFT plot under it)
//   right column    CH 0 and CH 1 cards (switch, Scale and Position knobs, source, range, facts),
//                   Math and References cards, the tool palette (Measure, Cursors, Math, FFT,
//                   References, Display, Trigger, Export); a tool opens a floating panel over the
//                   right part of the display, next to the column
//   cards           one measurement card per shown measurement (value + statistics), cursors, + Add
// Every setting exists once; settings that do not apply are hidden. No inspector.
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
    // The open tool panel ("" = none): measure, cursors, math, fft, references, display, trigger, export.
    // --scope-tool=<id> opens one at start (screenshots of the panels).
    property string tool: AppInfo.value("scope-tool")
    // Split screen / small windows: knobs give way to the fields, the palette shows icons only.
    readonly property bool narrow: width < 1000
    readonly property bool barCompact: width < 1320
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

    // The display's read-only summary: the horizontal and trigger settings (the vertical scales
    // are the legend under the graph).
    readonly property string summary: {
        const parts = []
        if (controller.displayMode === ScopeController.XY)
            parts.push(qsTr("XY"))
        parts.push(controller.timeDivLabel + "/div")
        if (controller.rolling)
            parts.push(qsTr("Roll"))
        else if (!edge)
            parts.push(qsTr("Immediate"))
        else
            parts.push(qsTr("Edge %1 CH%2 %3").arg(controller.triggerSlope === ScopeController.Rising ? "↑" : "↓")
                                              .arg(triggerChannel)
                                              .arg(controller.formatMeasurement(controller.triggerLevel, "V")))
        if (controller.averaging > 1)
            parts.push(qsTr("Average of %1").arg(controller.averaging))
        if (controller.persistence > 0)
            parts.push(qsTr("Persistence %1").arg(Units.format(controller.persistence / 1000, "s", 2)))
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


    // Trigger level and mode: in the bar, or in the right column in a narrow pane.
    Component {
        id: triggerControls
        GridLayout {
            columns: root.narrow ? 1 : 2
            columnSpacing: root.barCompact ? Theme.spacingS : Theme.spacingM
            rowSpacing: Theme.spacingXs
            KnobField {
                objectName: "triggerLevel"
                // The knob covers ±5 div of the source channel; typing takes −100…100 V.
                readonly property real span: 5 * (root.triggerChannel === 1 ? root.ch1.voltsPerDiv : root.ch0.voltsPerDiv)
                enabled: root.edge
                label: qsTr("Trigger level · CH%1 %2").arg(root.triggerChannel)
                                                       .arg(root.controller.triggerSlope === ScopeController.Rising ? "↑" : "↓")
                unit: "V"
                compact: false
                knobSize: 44
                valueWidth: root.narrow ? 70 : 80
                accentColor: display.traceColor(root.triggerChannel)
                from: -span
                to: span
                stepSize: span / 50
                defaultValue: 0
                value: root.controller.triggerLevel
                toolTip: qsTr("−100 to 100 V; drag the T marker at the right edge of the graph")
                onValueModified: v => root.controller.triggerLevel = Math.max(-100, Math.min(100, Number(v.toPrecision(4))))
            }
            ColumnLayout {
                spacing: 3
                Text {
                    text: qsTr("Trigger")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                }
                SegmentedControl {
                    objectName: "triggerMode"
                    compact: root.narrow
                    // Auto: Edge, free-runs after the timeout; Normal: Edge, waits; None: Immediate.
                    model: [{ text: qsTr("Auto"), toolTip: qsTr("Edge trigger; without an edge the signal is shown untriggered after the timeout") },
                            { text: root.narrow ? qsTr("Norm") : qsTr("Normal"), toolTip: qsTr("Edge trigger; waits for an edge (the last trace stays, faded)") },
                            { text: qsTr("None"), toolTip: qsTr("Free running: every record is shown") }]
                    currentIndex: !root.edge ? 2 : root.controller.noTriggerMode === ScopeController.ShowUntriggered ? 0 : 1
                    onActivated: index => {
                        if (index === 2) {
                            root.controller.triggerType = ScopeController.Immediate
                            return
                        }
                        root.controller.triggerType = ScopeController.Edge
                        if (index === 0)
                            root.controller.noTriggerMode = ScopeController.ShowUntriggered
                        else if (root.controller.noTriggerMode === ScopeController.ShowUntriggered)
                            root.controller.noTriggerMode = ScopeController.KeepLastTrace
                    }
                }
            }

        }
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
                        { icon: "bar-chart", text: root.barCompact ? "" : "FFT", toolTip: qsTr("FFT: time view with the spectrum of a channel under it") }]
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

                Rectangle { visible: !root.barCompact; implicitWidth: 1; Layout.fillHeight: true; Layout.margins: 6; color: Theme.separator }

                Loader {
                    active: !root.narrow
                    visible: active
                    sourceComponent: triggerControls
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
            { name: qsTr("Cursor C1"), style: root.controller.cursor1.style, color: Theme.signalCursor },
            { name: qsTr("Cursor C2"), style: root.controller.cursor2.style, color: Theme.signalCursor }
        ]

        Item {
            anchors.fill: parent

            ScopeDisplay {
                id: display
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left }
                controller: root.controller
            }

            // Right column: channel cards, Math, References, tool palette.
            Rectangle {
                id: column
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: root.narrow ? 212 : 262
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

                        Loader {
                            Layout.alignment: Qt.AlignHCenter
                            active: root.narrow
                            visible: active
                            sourceComponent: viewControls
                        }
                        Loader {
                            Layout.fillWidth: true
                            active: root.narrow
                            visible: active
                            sourceComponent: triggerControls
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

                        // Math: on/off here, the operation and scales in the Math panel.
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 40
                            radius: Theme.radiusM
                            color: Theme.cardFill
                            border.color: root.tool === "math" ? display.traceColor(2) : Theme.cardBorder
                            TapHandler { onTapped: root.toggleTool("math") }
                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.spacingS + 6; rightMargin: Theme.spacingS }
                                Text {
                                    text: qsTr("Math")
                                    color: root.math.enabled ? display.traceColor(2) : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.math.functionName
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                    elide: Text.ElideRight
                                }
                                Switch {
                                    objectName: "mathSwitch"
                                    checked: root.math.enabled
                                    Accessible.name: qsTr("Math")
                                    onToggled: root.math.enabled = checked
                                }
                            }
                            Rectangle {
                                width: 3; radius: 1.5
                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 6 }
                                color: display.traceColor(2)
                                opacity: root.math.enabled ? 1 : 0.35
                            }
                        }
                        // References: what is stored; the panel saves, shows, imports and clears.
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 40
                            radius: Theme.radiusM
                            color: refHover.hovered ? Theme.hoverFill : Theme.cardFill
                            border.color: root.tool === "references" ? Theme.signalRef : Theme.cardBorder
                            HoverHandler { id: refHover }
                            TapHandler { onTapped: root.toggleTool("references") }
                            Rectangle {
                                width: 3; radius: 1.5
                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 6 }
                                color: Theme.signalRef
                                opacity: root.controller.refAAvailable || root.controller.refBAvailable ? 1 : 0.35
                            }
                            RowLayout {
                                anchors { fill: parent; leftMargin: Theme.spacingS + 6; rightMargin: Theme.spacingS }
                                Text {
                                    text: qsTr("References")
                                    color: Theme.text
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    text: (root.controller.refAAvailable ? "A" : "–") + " · " + (root.controller.refBAvailable ? "B" : "–")
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeCaption
                                }
                            }
                        }

                        // Tool palette.
                        ToolPalette {
                            objectName: "toolPalette"
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingXs
                            columns: 4
                            cellHeight: root.narrow ? 34 : 52
                            currentId: root.tool
                            model: [{ id: "measure", text: root.narrow ? "" : qsTr("Measure"), icon: "ruler", toolTip: qsTr("Measurements") },
                                    { id: "cursors", text: root.narrow ? "" : qsTr("Cursors"), icon: "crosshair", toolTip: qsTr("Cursors") },
                                    { id: "math", text: root.narrow ? "" : qsTr("Math"), icon: "activity", toolTip: qsTr("Math trace") },
                                    { id: "fft", text: root.narrow ? "" : qsTr("FFT"), icon: "bar-chart", toolTip: qsTr("Spectrum") },
                                    { id: "references", text: root.narrow ? "" : qsTr("Refs"), icon: "layers", toolTip: qsTr("Reference traces") },
                                    { id: "display", text: root.narrow ? "" : qsTr("Display"), icon: "grid", toolTip: qsTr("Persistence, averaging, record length") },
                                    { id: "trigger", text: root.narrow ? "" : qsTr("Trigger"), icon: "zap", toolTip: qsTr("Trigger source, slope, holdoff, hysteresis, timeout") },
                                    { id: "export", text: root.narrow ? "" : qsTr("Export"), icon: "export", toolTip: qsTr("Export and print") }]
                            onActivated: id => root.toggleTool(id)
                        }
                    }
                }
            }

            // The open tool panel floats over the right part of the display, next to the column.
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: 280
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
                                    checkable: true
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
                                        checked = Qt.binding(() => typeButton.shown)
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
                        Text { Layout.fillWidth: true; text: qsTr("Time cursors A, B"); color: Theme.text; font.pixelSize: Theme.fontSizeBody }
                        Switch {
                            objectName: "cursorsSwitch"
                            checked: root.controller.cursorsOn
                            Accessible.name: qsTr("Cursors")
                            onToggled: root.controller.cursorsOn = checked
                        }
                    }
                    PanelRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("A follows")
                        SegmentedControl {
                            objectName: "cursor1Trace"
                            Layout.fillWidth: true
                            model: root.traceChoices
                            currentIndex: root.controller.cursor1.channel
                            onActivated: index => root.controller.cursor1.channel = index
                        }
                    }
                    PanelRow {
                        visible: root.controller.cursorsOn
                        label: qsTr("B follows")
                        SegmentedControl {
                            objectName: "cursor2Trace"
                            Layout.fillWidth: true
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
                    Caption { text: qsTr("Drag the cursors on the graph. Their readings are a card under the display.") }
                }

                ToolPanel {
                    objectName: "mathPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "math"
                    width: parent.width
                    title: qsTr("Math")
                    onCloseRequested: root.tool = ""
                    PanelRow {
                        label: qsTr("Operation")
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            rowSpacing: 4
                            columnSpacing: 4
                            Repeater {
                                id: mathOps
                                // ScopeMath.Operation order (no FFT: that is the FFT view).
                                model: ["CH0 + CH1", "CH0 − CH1", "CH0 × CH1", "−CH0", "−CH1"]
                                delegate: Button {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    checkable: true
                                    checked: root.math.operation === index
                                    text: modelData
                                    font.pixelSize: Theme.fontSizeXs
                                    focusPolicy: Qt.TabFocus
                                    onClicked: {
                                        root.math.operation = index
                                        root.math.enabled = true
                                    }
                                }
                            }
                        }
                    }
                    KnobField {
                        objectName: "mathScale"
                        Layout.fillWidth: true
                        label: qsTr("Scale")
                        accentColor: display.traceColor(2)
                        knobSize: 34
                        values: root.math.scaleValues
                        labels: root.math.scaleLabels.map(l => l + "/div")
                        currentIndex: root.math.scaleIndex
                        onIndexActivated: index => root.math.scaleIndex = index
                    }
                    KnobField {
                        objectName: "mathPosition"
                        Layout.fillWidth: true
                        label: qsTr("Position")
                        unit: "div"
                        accentColor: display.traceColor(2)
                        knobSize: 34
                        from: -5
                        to: 5
                        stepSize: 0.1
                        defaultValue: 0
                        formatter: v => Number(v.toFixed(2)) + " div"
                        value: root.math.position
                        onValueModified: v => root.math.position = Math.round(v * 100) / 100
                    }
                    Caption {
                        readonly property bool needsBoth: root.math.operation <= ScopeMath.Product
                        readonly property bool missing: needsBoth ? !(root.ch0.enabled && root.ch1.enabled)
                                                                 : !(root.math.operation === ScopeMath.InvertA ? root.ch0.enabled : root.ch1.enabled)
                        visible: root.math.enabled && missing
                        text: needsBoth ? qsTr("Needs Channel 0 and Channel 1 on.")
                                        : qsTr("Needs Channel %1 on.").arg(root.math.operation === ScopeMath.InvertA ? 0 : 1)
                    }
                }

                ToolPanel {
                    objectName: "fftPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "fft"
                    width: parent.width
                    title: qsTr("FFT")
                    onCloseRequested: root.tool = ""
                    PanelRow {
                        label: qsTr("Spectrum of")
                        SegmentedControl {
                            objectName: "fftSource"
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Off") },
                                    { text: "CH0", enabled: root.ch0.enabled || root.controller.fftSource === ScopeController.FftChannel0 },
                                    { text: "CH1", enabled: root.ch1.enabled || root.controller.fftSource === ScopeController.FftChannel1 }]
                            currentIndex: root.controller.fftSource
                            onActivated: index => root.controller.fftSource = index
                        }
                    }
                    Caption {
                        text: qsTr("Spectrum of the shown record under the graph (Hann window, dBV RMS). For averaging and distortion, use the Spectrum Analyzer.")
                    }
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

                ToolPanel {
                    objectName: "triggerPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "trigger"
                    width: parent.width
                    title: qsTr("Trigger")
                    onCloseRequested: root.tool = ""
                    Caption {
                        visible: !root.edge
                        text: qsTr("Trigger is None: every record is shown. Choose Auto or Normal in the bar for an edge trigger.")
                    }
                    PanelRow {
                        visible: root.edge
                        label: qsTr("Source")
                        SegmentedControl {
                            objectName: "triggerSource"
                            Layout.fillWidth: true
                            // A channel that is off cannot trigger.
                            model: [{ text: "CH0", enabled: root.ch0.enabled, toolTip: root.ch0.enabled ? qsTr("Channel 0") : qsTr("Channel 0 is off") },
                                    { text: "CH1", enabled: root.ch1.enabled, toolTip: root.ch1.enabled ? qsTr("Channel 1") : qsTr("Channel 1 is off") }]
                            currentIndex: root.triggerChannel
                            onActivated: index => root.controller.triggerSource = index
                        }
                    }
                    PanelRow {
                        visible: root.edge
                        label: qsTr("Slope")
                        SegmentedControl {
                            objectName: "triggerSlope"
                            Layout.fillWidth: true
                            model: [{ icon: "slope-rising", text: qsTr("Rising") }, { icon: "slope-falling", text: qsTr("Falling") }]
                            currentIndex: root.controller.triggerSlope
                            onActivated: index => root.controller.triggerSlope = index
                        }
                    }
                    PanelRow {
                        visible: root.edge && !root.controller.rolling
                        label: qsTr("Pre-trigger (position of T)")
                        SiField {
                            objectName: "horizontalPosition"
                            Layout.fillWidth: true
                            label: qsTr("Horizontal position")
                            engineering: false
                            decimals: 0
                            unit: "%"
                            from: 0
                            to: 100
                            stepSize: 1
                            value: root.controller.horizontalPosition
                            toolTip: qsTr("Where the trigger point sits on the screen, 0 to 100 % (drag the T marker at the top)")
                            onValueModified: v => root.controller.horizontalPosition = v
                        }
                    }
                    RowLayout {
                        visible: root.edge && root.controller.noTriggerMode !== ScopeController.ShowUntriggered
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: qsTr("Clear the graph while waiting"); color: Theme.text; font.pixelSize: Theme.fontSizeBody; wrapMode: Text.WordWrap }
                        Switch {
                            objectName: "triggerClear"
                            checked: root.controller.noTriggerMode === ScopeController.ClearGraph
                            Accessible.name: qsTr("Clear the graph while waiting")
                            onToggled: root.controller.noTriggerMode = checked ? ScopeController.ClearGraph : ScopeController.KeepLastTrace
                        }
                    }
                    PanelRow {
                        visible: root.edge
                        label: qsTr("Timeout")
                        StepSelector {
                            objectName: "triggerTimeout"
                            readonly property var values: [0.1, 0.25, 0.5, 1, 2, 5, 10, 0]
                            Layout.fillWidth: true
                            label: qsTr("Trigger timeout")
                            model: values.map(v => ({ value: v, text: v === 0 ? qsTr("Never") : Units.format(v, "s", 3) }))
                            currentIndex: Math.max(0, values.indexOf(root.controller.triggerTimeout))
                            onActivated: index => root.controller.triggerTimeout = values[index]
                        }
                    }
                    PanelRow {
                        visible: root.edge
                        label: qsTr("Holdoff")
                        SiField {
                            objectName: "triggerHoldoff"
                            Layout.fillWidth: true
                            label: qsTr("Holdoff")
                            unit: "s"
                            from: 0
                            to: 10
                            stepSize: 0.01
                            digits: 3
                            value: root.controller.triggerHoldoff
                            toolTip: qsTr("Minimum time between two shown triggers, 0 to 10 s (steadies bursts)")
                            onValueModified: v => root.controller.triggerHoldoff = v
                        }
                    }
                    PanelRow {
                        visible: root.edge
                        label: qsTr("Hysteresis")
                        SiField {
                            objectName: "triggerHysteresis"
                            Layout.fillWidth: true
                            label: qsTr("Hysteresis")
                            engineering: false
                            decimals: 2
                            unit: "div"
                            from: 0
                            to: 2
                            stepSize: 0.05
                            value: root.controller.triggerHysteresis
                            toolTip: qsTr("0 to 2 div of the source channel; larger values ignore noise around the level")
                            onValueModified: v => root.controller.triggerHysteresis = v
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
                }

                ToolPanel {
                    objectName: "exportPanel"
                    maximumHeight: toolArea.height
                    onVisibleChanged: if (visible) forceActiveFocus()
                    visible: root.tool === "export"
                    width: parent.width
                    title: qsTr("Export")
                    onCloseRequested: root.tool = ""
                    Button { Layout.fillWidth: true; text: qsTr("Export Data…"); icon.source: Theme.icon("download"); onClicked: { root.tool = ""; panel.exportData() } }
                    Button { Layout.fillWidth: true; text: qsTr("Export Image…"); icon.source: Theme.icon("image"); onClicked: { root.tool = ""; panel.exportImage(false) } }
                    Button { Layout.fillWidth: true; text: qsTr("Export PDF…"); icon.source: Theme.icon("file"); onClicked: { root.tool = ""; panel.exportImage(true) } }
                    Button { Layout.fillWidth: true; text: qsTr("Print…"); icon.source: Theme.icon("printer"); onClicked: { root.tool = ""; panel.printPanel() } }
                    Caption { text: qsTr("Data: CSV / TSV of the shown record, or the ELVISmx log format.") }
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
