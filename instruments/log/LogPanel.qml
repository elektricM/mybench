pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Data Logger panel (docs/instruments/log.md), PicoScope-style layout (docs/UI.md §13.8b):
// - pane header: Run/Stop, Record (Pause while recording), sampling-rate knob;
// - display (left, largest): the multi-channel trend (10 s … All in its toolbar), the DMM's own
//   chart when it is logged next to AI channels, and an overview of the whole run;
// - right column (shared InstrumentColumn): the recording card while a file is written, one
//   ChannelCard per input (AI 0, AI 1, DMM with function pictures and a range knob), tool palette
//   (Log File, Limits, Display) pinned at the bottom, its panels float over the display;
// - bottom strip: per-channel value + statistics cards (same cards as every instrument).
// Channel and sampling settings are locked while the logger runs: the DMM card then shows its
// function and range as facts (no disabled button grid); tooltips say why.
Item {
    id: root

    required property LogController controller

    readonly property string pauseIcon: "qrc:/qt/qml/MyBench/Instruments/Log/assets/pause.svg"
    readonly property bool analog: controller.ai0Enabled || controller.ai1Enabled
    readonly property bool elvismx: controller.fileLayout === LogController.ElvismxLayout
    readonly property bool narrow: width < Theme.instrumentCompactWidth
    // The DMM channel's color (card, trace, value card).
    readonly property color dmmColor: Theme.signalRef
    // DMM functions of the logger (LogController.dmmFunctionNames order) as pictures.
    readonly property var dmmFunctions: [
        { icon: "dmm-vdc", caption: qsTr("DC V") },
        { icon: "dmm-vac", caption: qsTr("AC V") },
        { icon: "dmm-adc", caption: qsTr("DC A") },
        { icon: "dmm-aac", caption: qsTr("AC A") },
        { icon: "dmm-ohm", caption: qsTr("Ohms") }
    ]
    readonly property string lockedText: qsTr("Stop the logger to change this")
    readonly property var rates: [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000]
    property int spanIndex: 1
    property string tool: ""
    property bool overviewVisible: true

    function entry(key: string): var {
        const list = root.controller.channelStats
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === key)
                return list[i]
        }
        return null
    }
    // A time window of the chart: TrendView fits the axis to the data (starts at the first sample,
    // grows with the run up to the window, then slides) while it follows.
    function showSpan(index: int) {
        root.spanIndex = index
        trend.following = true
        trend.fit()
    }
    function duration(seconds: real): string {
        const s = Math.floor(seconds)
        const hh = Math.floor(s / 3600)
        const mm = Math.floor(s / 60) % 60
        const ss = s % 60
        return (hh > 0 ? hh + ":" : "") + String(mm).padStart(hh > 0 ? 2 : 1, "0") + ":" + String(ss).padStart(2, "0")
    }
    function fileName(path: string): string {
        const i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
        return i >= 0 ? path.slice(i + 1) : path
    }
    function rateIndex(rate: real): int {
        let best = 0
        for (let i = 1; i < root.rates.length; ++i) {
            if (Math.abs(Math.log(root.rates[i] / rate)) < Math.abs(Math.log(root.rates[best] / rate)))
                best = i
        }
        return best
    }
    function cardStats(key: string, unit: string): var {
        const e = root.entry(key)
        if (!e || e.count <= 0)
            return null
        // Numbers: the MeasurementCard formats them (4 significant digits, σ 3) in its `unit`.
        const f = v => isFinite(v) ? v : ""
        return { min: f(e.minimum), max: f(e.maximum), mean: f(e.mean), stdDev: f(e.deviation) }
    }
    function cardValue(key: string, unit: string): string {
        const e = root.entry(key)
        if (!e || e.count <= 0)
            return ""
        return e.overload ? qsTr("Over") : Units.format(e.last, unit, 4)
    }
    function toggleTool(id: string) {
        root.tool = root.tool === id ? "" : id
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("Folder for log files")
        currentFolder: root.controller.folderUrl()
        onAccepted: root.controller.setFolderUrl(selectedFolder)
    }

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        // No display title or info line: the channels are the card switches, the rate is its knob.
        showRunControls: false
        inspector: []

        headerContent: [
            RowLayout {
                spacing: Theme.spacingS
                RunButton {
                    objectName: "logRunButton"
                    running: root.controller.running
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    stateText: root.controller.running ? root.duration(root.controller.runSeconds) : ""
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "logRecordButton"
                    readonly property bool active: root.controller.recording || root.controller.recordPending
                    Layout.preferredHeight: Theme.controlHeightL
                    text: active ? qsTr("Stop Recording") : qsTr("Record")
                    icon.source: Theme.icon(active ? "stop" : "record")
                    icon.color: active ? Theme.negative : Theme.text
                    checkable: true
                    checked: active
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.recording ? qsTr("Close %1").arg(root.fileName(root.controller.recordFile))
                                                            : qsTr("Write the data to a new file in %1").arg(root.controller.folder)
                    onClicked: {
                        if (active)
                            root.controller.stopRecording()
                        else
                            root.controller.startRecording()
                        checked = Qt.binding(() => active)
                    }
                }
                Button {
                    visible: root.controller.recording
                    Layout.preferredHeight: Theme.controlHeightL
                    text: root.controller.recordPaused ? qsTr("Resume") : qsTr("Pause")
                    icon.source: root.controller.recordPaused ? Theme.icon("record") : root.pauseIcon
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.recordPaused ? qsTr("Continue writing to the file")
                                                               : qsTr("Stop writing to the file for now; the chart keeps running")
                    onClicked: root.controller.setRecordPaused(!root.controller.recordPaused)
                }
                // Wrapped so the "locked while running" tooltip shows over the disabled knob.
                Item {
                    visible: root.analog
                    implicitWidth: rateKnob.implicitWidth
                    implicitHeight: rateKnob.implicitHeight
                    HoverHandler { id: rateHover }
                    ToolTip.visible: root.controller.running && rateHover.hovered
                    ToolTip.text: root.lockedText
                    KnobField {
                        id: rateKnob
                        objectName: "logRateKnob"
                        anchors.fill: parent
                        label: qsTr("Sampling Rate")
                        unit: "S/s"
                        knobSize: 34
                        compact: root.narrow
                        values: root.rates
                        currentIndex: root.rateIndex(root.controller.samplingRate)
                        enabled: !root.controller.running
                        toolTip: qsTr("Samples per second, per channel (1 – 1000 S/s); the chart keeps the last %1, the file everything")
                                     .arg(root.controller.chartCapacity)
                        onIndexActivated: index => root.controller.samplingRate = root.rates[index]
                    }
                }
            }
        ]

        Item {
            anchors.fill: parent

            // --- Display ----------------------------------------------------------------------
            ColumnLayout {
                id: display
                objectName: "logDisplay"
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left; margins: Theme.spacingS }
                spacing: Theme.spacingS

                TrendView {
                    id: trend
                    objectName: "logTrend"
                    visible: root.analog
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: Theme.controlHeightL * 3
                    windows: [ { text: "10 s", seconds: 10 }, { text: qsTr("1 min"), seconds: 60 }, { text: qsTr("10 min"), seconds: 600 },
                               { text: qsTr("1 h"), seconds: 3600 }, { text: qsTr("All"), seconds: 0 } ]
                    windowIndex: root.spanIndex
                    onWindowActivated: index => root.showSpan(index)
                    onClearRequested: root.controller.clearChart()
                    plot.objectName: "logPlot"
                    plot.hoverReadout: true
                    plot.legendVisible: root.controller.ai0Enabled && root.controller.ai1Enabled
                    plot.leftMargin: Math.max(trend.plot.implicitLeftMargin, dmmPlot.visible ? dmmPlot.implicitLeftMargin : 0)
                    plot.placeholderText: root.controller.running ? qsTr("Waiting for samples…") : qsTr("Press Run to start logging")
                    plot.xAxis.labelFormat: PlotAxis.Time
                    plot.xAxis.tickSpacing: 110
                    plot.yAxis.autoScale: true
                    plot.yAxis.min: root.controller.minimum
                    plot.yAxis.max: root.controller.maximum
                    plot.yAxis.unit: "V"
                    plot.yAxis.unitInLabels: true
                    PlotTrace { visible: root.controller.ai0Enabled; name: "AI 0"; unit: "V"; buffer: root.controller.ai0; color: Theme.signalCh0; width: 1.3 }
                    PlotTrace { visible: root.controller.ai1Enabled; name: "AI 1"; unit: "V"; buffer: root.controller.ai1; color: Theme.signalCh1; width: 1.3 }
                    buffers: [root.controller.ai0, root.controller.ai1, root.controller.dmm]
                }

                BenchPlot {
                    id: dmmPlot
                    objectName: "logDmmPlot"
                    visible: root.controller.dmmEnabled
                    Layout.fillWidth: true
                    Layout.fillHeight: !root.analog
                    Layout.preferredHeight: root.analog ? Math.max(Theme.controlHeightL * 3, display.height * 0.26) : -1   // not trend.height: polish loop
                    interactive: !root.analog
                    hoverReadout: true
                    legendVisible: true      // names the chart ("DMM") in its trace color
                    leftMargin: trend.visible ? trend.plot.leftMargin : -1
                    placeholderText: qsTr("DMM")
                    xAxis { labelFormat: PlotAxis.Time; follow: !root.analog; tickSpacing: 110 }
                    yAxis { autoScale: true; unit: root.controller.dmmUnit; unitInLabels: true }
                    Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                    PlotTrace { name: qsTr("DMM"); unit: root.controller.dmmUnit; buffer: root.controller.dmm; color: root.dmmColor; width: 1.3; pointsVisible: true }
                    Connections {
                        target: trend.plot.xAxis
                        enabled: root.analog
                        function onRangeChanged() { dmmPlot.xAxis.setViewRange(trend.plot.xAxis.min, trend.plot.xAxis.max) }
                    }
                }

                // Overview of the whole run with the visible window (drag it to scroll back).
                BenchPlot {
                    id: overview
                    visible: root.analog && root.overviewVisible
                             && (root.controller.ai0.count > 0 || root.controller.ai1.count > 0)
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.controlHeightL * 2
                    interactive: false
                    leftMargin: trend.plot.leftMargin
                    yLabelsVisible: false
                    xAxis { labelFormat: PlotAxis.Time; autoScale: true; interactive: false; tickSpacing: 110 }
                    yAxis { autoScale: true; gridVisible: false; interactive: false }
                    PlotTrace { visible: root.controller.ai0Enabled; buffer: root.controller.ai0; color: Theme.alpha(Theme.signalCh0, 0.8); width: 1 }
                    PlotTrace { visible: root.controller.ai1Enabled; buffer: root.controller.ai1; color: Theme.alpha(Theme.signalCh1, 0.8); width: 1 }
                    PlotRegion {
                        from: trend.plot.xAxis.min
                        to: trend.plot.xAxis.max
                        draggable: true
                        color: Theme.alpha(Theme.accent, 0.20)
                        onMoved: (from, to) => {
                            trend.following = false
                            trend.plot.xAxis.setViewRange(from, to)
                        }
                    }
                }
            }

            // --- Right column ---------------------------------------------------------------------
            InstrumentColumn {
                id: column
                objectName: "logControls"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                compact: root.narrow

                // Recording: only while a file is written (the Record button shows the rest).
                Rectangle {
                    objectName: "logRecordState"
                    readonly property bool active: root.controller.recording && !root.controller.recordPaused
                    visible: root.controller.recording
                    Layout.fillWidth: true
                    implicitHeight: recColumn.implicitHeight + 2 * Theme.spacingS
                    radius: Theme.radiusPopup
                    color: active ? Theme.alpha(Theme.negative, Theme.dark ? 0.14 : 0.08) : Theme.cardFill
                    border.color: active ? Theme.alpha(Theme.negative, 0.55) : Theme.cardBorder
                    ColumnLayout {
                        id: recColumn
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.spacingS }
                        spacing: 2
                        RowLayout {
                            spacing: Theme.spacingS
                            Rectangle {
                                id: recDot
                                implicitWidth: Theme.spacingS
                                implicitHeight: Theme.spacingS
                                radius: width / 2
                                color: root.controller.recordPaused ? Theme.caution : Theme.negative
                                SequentialAnimation on opacity {
                                    running: root.controller.recording && !root.controller.recordPaused && Theme.animations
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                    onStopped: recDot.opacity = 1
                                }
                            }
                            Text {
                                text: root.controller.recordPaused ? qsTr("Paused") : qsTr("Recording")
                                color: root.controller.recordPaused ? Theme.caution : Theme.negative
                                font.pixelSize: Theme.fontSizeCaption
                                font.weight: Font.DemiBold
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: root.duration(root.controller.recordSeconds)
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeBody
                                font.features: Theme.tabularFigures
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.fileName(root.controller.recordFile) + "  ·  " + Units.format(root.controller.recordBytes, "B", 3)
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeCaption
                            elide: Text.ElideMiddle
                        }
                    }
                }

                // The channel cards are locked while the logger runs; hovering them says why.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    HoverHandler { id: channelsHover }
                    ToolTip.visible: root.controller.running && channelsHover.hovered
                    ToolTip.text: root.lockedText

                    ChannelCard {
                        objectName: "logCardAi0"
                        Layout.fillWidth: true
                        title: "AI 0"
                        color: Theme.signalCh0
                        compact: root.narrow
                        checked: root.controller.ai0Enabled
                        enabled: !root.controller.running
                        facts: [qsTr("±%1 V").arg(root.controller.hardwareRange)]
                        onToggled: on => root.controller.ai0Enabled = on
                    }
                    ChannelCard {
                        objectName: "logCardAi1"
                        Layout.fillWidth: true
                        title: "AI 1"
                        color: Theme.signalCh1
                        compact: root.narrow
                        checked: root.controller.ai1Enabled
                        enabled: !root.controller.running
                        facts: [qsTr("±%1 V").arg(root.controller.hardwareRange)]
                        onToggled: on => root.controller.ai1Enabled = on
                    }
                    ChannelCard {
                        id: dmmCard
                        objectName: "logCardDmm"
                        Layout.fillWidth: true
                        title: qsTr("DMM")
                        color: root.dmmColor
                        compact: root.narrow
                        collapseWhenOff: true          // off = header only (no dimmed button grid)
                        checked: root.controller.dmmEnabled
                        enabled: !root.controller.running
                        // While running (settings locked) and while off: function and range as facts.
                        readonly property bool editable: root.controller.dmmEnabled && !root.controller.running
                        facts: editable ? [] : [root.dmmFunctions[root.controller.dmmFunction]?.caption ?? "",
                                                root.controller.dmmRangeNames[root.controller.dmmRange] ?? ""]
                        onToggled: on => root.controller.dmmEnabled = on

                        GridLayout {
                            objectName: "logDmmFunctions"
                            visible: dmmCard.editable
                            Layout.fillWidth: true
                            columns: 3
                            rowSpacing: Theme.spacingXs
                            columnSpacing: Theme.spacingXs
                            Repeater {
                                model: root.dmmFunctions
                                delegate: PictureButton {
                                    required property var modelData
                                    required property int index
                                    objectName: "logDmmFunction" + index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    implicitWidth: 56
                                    text: modelData.caption
                                    iconName: modelData.icon
                                    accentColor: root.dmmColor
                                    checked: index === root.controller.dmmFunction
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.controller.dmmFunctionNames[index] ?? ""
                                    onClicked: root.controller.dmmFunction = index
                                }
                            }
                        }
                        KnobField {
                            objectName: "logDmmRange"
                            visible: dmmCard.editable
                            Layout.fillWidth: true
                            label: qsTr("Range")
                            knobSize: 34
                            compact: root.narrow
                            accentColor: root.dmmColor
                            values: root.controller.dmmRangeNames.map((n, i) => i)
                            labels: root.controller.dmmRangeNames
                            currentIndex: root.controller.dmmRange
                            toolTip: qsTr("Auto, or a fixed range of the function")
                            onIndexActivated: index => root.controller.dmmRange = index
                        }
                    }
                }

                tools: ToolPalette {
                    objectName: "logTools"
                    columns: 3
                    currentId: root.tool
                    model: [
                        { id: "file", text: qsTr("Log File"), icon: "folder-open" },
                        { id: "limits", text: qsTr("Limits"), icon: "sliders" },
                        { id: "display", text: qsTr("Display"), icon: "settings" }
                    ]
                    onActivated: id => root.toggleTool(id)
                }
            }

            // --- Tool panels: float over the right part of the display -----------------------------
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: Math.min(Theme.toolPanelWidth, Math.max(Theme.instrumentColumnCompactWidth, display.width * 0.5))
                z: 10

                ToolPanel {
                    objectName: "filePanel"
                    visible: root.tool === "file"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Log File")
                    onCloseRequested: root.tool = ""

                    Text { text: qsTr("Folder"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        TextField {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 60
                            text: root.controller.folder
                            ToolTip.visible: hovered
                            ToolTip.text: text
                            onEditingFinished: root.controller.folder = text
                        }
                        IconButton {
                            iconName: "folder-open"
                            bordered: true
                            toolTip: qsTr("Choose the folder…")
                            onClicked: folderDialog.open()
                        }
                    }
                    Text { text: qsTr("File Name"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                    TextField {
                        Layout.fillWidth: true
                        text: root.controller.baseName
                        onEditingFinished: root.controller.baseName = text
                    }
                    Switch {
                        text: qsTr("Date and time in the name")
                        checked: root.controller.appendTimestamp
                        onToggled: root.controller.appendTimestamp = checked
                    }
                    Text { text: qsTr("Format"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeCaption }
                    SegmentedControl {
                        objectName: "logFileType"
                        Layout.fillWidth: true
                        model: [ { text: qsTr("Tab"), toolTip: qsTr("Tab-separated (.tsv)") },
                                 { text: "CSV", toolTip: qsTr("Comma-separated (.csv)") },
                                 { text: qsTr("ELVISmx"), toolTip: qsTr("The text layout of the NI ELVISmx Data Logger (.txt), for scripts written for it") } ]
                        currentIndex: root.controller.fileType
                        onActivated: index => root.controller.fileType = index
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.elvismx ? qsTr("Time / myDAQ1/ai0 pairs, absolute date-time, CR LF. AI channels only; the DMM is not written.")
                                           : qsTr("Header lines, then seconds since the start.")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                    Switch {
                        visible: root.elvismx
                        text: qsTr("Append to an existing file")
                        checked: root.controller.appendToExisting
                        onToggled: root.controller.appendToExisting = checked
                    }
                    Switch {
                        visible: !root.elvismx
                        text: qsTr("Timestamp column")
                        checked: root.controller.timestampColumn
                        onToggled: root.controller.timestampColumn = checked
                    }
                    Switch {
                        text: qsTr("Record when Run starts")
                        checked: root.controller.recordOnRun
                        onToggled: root.controller.recordOnRun = checked
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Switch {
                            text: qsTr("Stop after")
                            checked: root.controller.stopAfterEnabled
                            onToggled: root.controller.stopAfterEnabled = checked
                        }
                        SiField {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 60
                            enabled: root.controller.stopAfterEnabled
                            unit: "min"
                            engineering: false
                            decimals: 2
                            stepSize: 1
                            from: 0.01
                            to: 100000
                            value: root.controller.stopAfterMinutes
                            onValueModified: v => root.controller.stopAfterMinutes = v
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.controller.recording ? qsTr("Writing %1 · %2 rows").arg(root.controller.recordFile)
                                                                                     .arg(Math.round(root.controller.recordRows))
                                                        : qsTr("Next file: %1").arg(root.controller.nextFilePath)
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WrapAnywhere
                    }
                    Button {
                        text: qsTr("Show Folder")
                        icon.source: Theme.icon("folder-open")
                        onClicked: Qt.openUrlExternally(root.controller.folderUrl())
                    }
                }

                ToolPanel {
                    objectName: "limitsPanel"
                    visible: root.tool === "limits"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Input Limits")
                    onCloseRequested: root.tool = ""
                    KnobField {
                        Layout.fillWidth: true
                        label: qsTr("Maximum")
                        unit: "V"
                        knobSize: 34
                        compact: root.narrow
                        from: -10
                        to: 10
                        stepSize: 0.1
                        value: root.controller.maximum
                        enabled: !root.controller.running
                        toolTip: root.controller.running ? root.lockedText : qsTr("Highest expected input voltage")
                        onValueModified: v => root.controller.maximum = v
                    }
                    KnobField {
                        Layout.fillWidth: true
                        label: qsTr("Minimum")
                        unit: "V"
                        knobSize: 34
                        compact: root.narrow
                        from: -10
                        to: 10
                        stepSize: 0.1
                        value: root.controller.minimum
                        enabled: !root.controller.running
                        toolTip: root.controller.running ? root.lockedText : qsTr("Lowest expected input voltage")
                        onValueModified: v => root.controller.minimum = v
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.controller.minimum >= root.controller.maximum
                        text: qsTr("Minimum must be below Maximum")
                        color: Theme.negative
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                ToolPanel {
                    objectName: "displayPanel"
                    visible: root.tool === "display"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Display")
                    onCloseRequested: root.tool = ""
                    Switch {
                        text: qsTr("Follow the latest data")
                        checked: trend.following
                        onToggled: {
                            trend.following = checked
                            checked = Qt.binding(() => trend.following)
                        }
                    }
                    Switch {
                        text: qsTr("Overview of the whole run")
                        checked: root.overviewVisible
                        onToggled: root.overviewVisible = checked
                    }
                }
            }
        }

        // --- Bottom: per-channel value + statistics cards ----------------------------------------
        measurements: [
            Flow {
                objectName: "logCards"
                width: parent ? parent.width : 0
                spacing: Theme.spacingS
                padding: Theme.spacingS
                component ChannelValue: MeasurementCard {
                    property string key
                    removable: false
                    width: Math.max(implicitWidth, root.narrow ? 160 : 220)
                    value: root.cardValue(key, unit)
                    stats: root.cardStats(key, unit)
                }
                ChannelValue {
                    objectName: "logValueAi0"
                    visible: root.controller.ai0Enabled
                    key: "ai0"
                    title: "AI 0"
                    color: Theme.signalCh0
                    unit: "V"
                }
                ChannelValue {
                    objectName: "logValueAi1"
                    visible: root.controller.ai1Enabled
                    key: "ai1"
                    title: "AI 1"
                    color: Theme.signalCh1
                    unit: "V"
                }
                ChannelValue {
                    objectName: "logValueDmm"
                    visible: root.controller.dmmEnabled
                    key: "dmm"
                    title: qsTr("DMM")
                    color: root.dmmColor
                    unit: root.controller.dmmUnit
                }
            }
        ]
    }
}
