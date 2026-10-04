pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Data Logger panel (docs/instruments/log.md), round-2 layout (docs/UI.md "Round 2"):
// - pane header: Run/Stop, Record (Pause while recording), sampling-rate knob;
// - display (left, largest): the multi-channel trend (10 s … All in its toolbar), the DMM's own
//   chart when it is logged next to AI channels, and an overview of the whole run;
// - right column: recording state, one ChannelCard per input (AI 0, AI 1, DMM with function and
//   range buttons), tool palette (Log File, Limits, Display) whose panels float over the display;
// - bottom strip: per-channel value + statistics cards.
// Channel and sampling settings are locked while the logger runs; their tooltips say why.
Item {
    id: root

    required property LogController controller

    readonly property string pauseIcon: "qrc:/qt/qml/MyBench/Instruments/Log/assets/pause.svg"
    readonly property bool analog: controller.ai0Enabled || controller.ai1Enabled
    readonly property bool elvismx: controller.fileLayout === LogController.ElvismxLayout
    readonly property bool narrow: width < 820
    readonly property string lockedText: qsTr("Stop the logger to change this")
    readonly property var spans: [10, 60, 600, 3600, 0]
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
    function latestX(): real {
        let last = NaN
        for (const b of [root.controller.ai0, root.controller.ai1, root.controller.dmm]) {
            if (b.count > 0) {
                const x = b.xMaximum()
                if (isNaN(last) || x > last)
                    last = x
            }
        }
        return isNaN(last) ? Date.now() / 1000 : last
    }
    function firstX(): real {
        let first = NaN
        for (const b of [root.controller.ai0, root.controller.ai1, root.controller.dmm]) {
            if (b.count > 0) {
                const x = b.xMinimum()
                if (isNaN(first) || x < first)
                    first = x
            }
        }
        return isNaN(first) ? Date.now() / 1000 - 60 : first
    }
    function showSpan(index: int) {
        root.spanIndex = index
        const axis = trend.plot.xAxis
        const last = root.latestX()
        const span = root.spans[index]
        if (span <= 0) {
            axis.follow = false
            axis.setViewRange(root.firstX(), Math.max(last, root.firstX() + 1))
        } else {
            axis.setViewRange(last - span, last)
            axis.follow = true
        }
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
        const f = v => isNaN(v) ? "—" : Units.format(v, unit, 4)
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
        displayTitle: {
            const names = []
            if (root.controller.ai0Enabled) names.push("AI 0")
            if (root.controller.ai1Enabled) names.push("AI 1")
            if (root.controller.dmmEnabled) names.push(qsTr("DMM"))
            return names.length > 0 ? names.join(" · ") : qsTr("No channel")
        }
        displayInfo: root.analog ? qsTr("Sampling Rate %1").arg(Units.format(root.controller.coercedRate, "S/s", 4))
                                 : qsTr("DMM readings")
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
                        caption: root.narrow ? "" : qsTr("chart keeps %1").arg(root.controller.chartCapacity)
                        enabled: !root.controller.running
                        toolTip: qsTr("Samples per second, per channel (1 – 1000 S/s)")
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
                    plot.xAxis.follow: true
                    plot.xAxis.tickSpacing: 110
                    plot.yAxis.autoScale: true
                    plot.yAxis.min: root.controller.minimum
                    plot.yAxis.max: root.controller.maximum
                    plot.yAxis.unit: "V"
                    plot.yAxis.unitInLabels: true
                    PlotTrace { visible: root.controller.ai0Enabled; name: "AI 0"; unit: "V"; buffer: root.controller.ai0; color: Theme.signalCh0; width: 1.3 }
                    PlotTrace { visible: root.controller.ai1Enabled; name: "AI 1"; unit: "V"; buffer: root.controller.ai1; color: Theme.signalCh1; width: 1.3 }
                    Component.onCompleted: trend.plot.xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                }

                BenchPlot {
                    id: dmmPlot
                    objectName: "logDmmPlot"
                    visible: root.controller.dmmEnabled
                    Layout.fillWidth: true
                    Layout.fillHeight: !root.analog
                    Layout.preferredHeight: root.analog ? Math.max(Theme.controlHeightL * 3, trend.height * 0.35) : -1
                    interactive: !root.analog
                    hoverReadout: true
                    leftMargin: trend.visible ? trend.plot.leftMargin : -1
                    placeholderText: qsTr("DMM")
                    xAxis { labelFormat: PlotAxis.Time; follow: !root.analog; tickSpacing: 110 }
                    yAxis { autoScale: true; unit: root.controller.dmmUnit; unitInLabels: true; title: qsTr("DMM") }
                    Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                    PlotTrace { name: qsTr("DMM"); unit: root.controller.dmmUnit; buffer: root.controller.dmm; color: Theme.text; width: 1.3; pointsVisible: true }
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
                            trend.plot.xAxis.follow = false
                            trend.plot.xAxis.setViewRange(from, to)
                        }
                    }
                }
            }

            // --- Right column ---------------------------------------------------------------------
            Rectangle {
                id: column
                objectName: "logControls"
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                // Split screen: the column shrinks with the pane so the display stays the larger part.
                width: root.narrow ? Math.round(Math.max(150, Math.min(212, root.width * 0.42))) : 256
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

                        // Recording state.
                        Rectangle {
                            objectName: "logRecordState"
                            readonly property bool active: root.controller.recording && !root.controller.recordPaused
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
                                        color: root.controller.recording ? (root.controller.recordPaused ? Theme.caution : Theme.negative)
                                                                         : Theme.textTertiary
                                        SequentialAnimation on opacity {
                                            running: root.controller.recording && !root.controller.recordPaused && Theme.animations
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.25; duration: 700 }
                                            NumberAnimation { to: 1; duration: 700 }
                                            onStopped: recDot.opacity = 1
                                        }
                                    }
                                    Text {
                                        text: root.controller.recording ? (root.controller.recordPaused ? qsTr("Paused") : qsTr("Recording"))
                                                                        : root.controller.running ? qsTr("Monitoring") : qsTr("Not recording")
                                        color: root.controller.recording ? (root.controller.recordPaused ? Theme.caution : Theme.negative)
                                                                         : Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeCaption
                                        font.weight: Font.DemiBold
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: root.controller.recording ? root.duration(root.controller.recordSeconds) : ""
                                        color: Theme.text
                                        font.pixelSize: Theme.fontSizeBody
                                        font.features: Theme.tabularFigures
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.controller.recordFile.length > 0
                                          ? root.fileName(root.controller.recordFile) + (root.controller.recording
                                                ? "  ·  " + Units.format(root.controller.recordBytes, "B", 3) : "")
                                          : qsTr("Press Record to save to a file")
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
                                subtitle: qsTr("Analog input")
                                color: Theme.signalCh0
                                compact: root.narrow
                                checked: root.controller.ai0Enabled
                                enabled: !root.controller.running
                                facts: [qsTr("±%1 V range").arg(root.controller.hardwareRange)]
                                onToggled: on => root.controller.ai0Enabled = on
                            }
                            ChannelCard {
                                objectName: "logCardAi1"
                                Layout.fillWidth: true
                                title: "AI 1"
                                subtitle: qsTr("Analog input")
                                color: Theme.signalCh1
                                compact: root.narrow
                                checked: root.controller.ai1Enabled
                                enabled: !root.controller.running
                                facts: [qsTr("±%1 V range").arg(root.controller.hardwareRange)]
                                onToggled: on => root.controller.ai1Enabled = on
                            }
                            ChannelCard {
                                objectName: "logCardDmm"
                                Layout.fillWidth: true
                                title: qsTr("DMM")
                                subtitle: root.controller.dmmFunctionNames[root.controller.dmmFunction] ?? ""
                                color: Theme.textSecondary
                                compact: root.narrow
                                checked: root.controller.dmmEnabled
                                enabled: !root.controller.running
                                onToggled: on => root.controller.dmmEnabled = on

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    rowSpacing: Theme.spacingXs
                                    columnSpacing: Theme.spacingXs
                                    Repeater {
                                        model: root.controller.dmmFunctionNames
                                        delegate: Button {
                                            required property string modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 1
                                            text: modelData
                                            checkable: true
                                            highlighted: checked
                                            checked: index === root.controller.dmmFunction
                                            focusPolicy: Qt.TabFocus
                                            onClicked: {
                                                root.controller.dmmFunction = index
                                                checked = Qt.binding(() => index === root.controller.dmmFunction)
                                            }
                                        }
                                    }
                                }
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 3
                                    rowSpacing: Theme.spacingXs
                                    columnSpacing: Theme.spacingXs
                                    Repeater {
                                        model: root.controller.dmmRangeNames
                                        delegate: Button {
                                            required property string modelData
                                            required property int index
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 1
                                            text: modelData
                                            checkable: true
                                            highlighted: checked
                                            checked: index === root.controller.dmmRange
                                            focusPolicy: Qt.TabFocus
                                            onClicked: {
                                                root.controller.dmmRange = index
                                                checked = Qt.binding(() => index === root.controller.dmmRange)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        ToolPalette {
                            objectName: "logTools"
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingXs
                            columns: 3
                            currentId: root.tool
                            model: [
                                { id: "file", text: root.narrow ? "" : qsTr("Log File"), icon: "folder-open" },
                                { id: "limits", text: root.narrow ? "" : qsTr("Limits"), icon: "sliders" },
                                { id: "display", text: root.narrow ? "" : qsTr("Display"), icon: "settings" }
                            ]
                            onActivated: id => root.toggleTool(id)
                        }
                    }
                }
            }

            // --- Tool panels: float over the right part of the display -----------------------------
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: Math.min(320, Math.max(220, display.width * 0.5))
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
                        text: qsTr("Minimum must be below Maximum: Run is refused until it is.")
                        color: Theme.negative
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Hardware range ±%1 V (limits within ±2 V use the finer range).").arg(root.controller.hardwareRange)
                        color: Theme.textTertiary
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
                        checked: trend.plot.xAxis.follow
                        onToggled: {
                            if (checked)
                                root.showSpan(root.spanIndex === 4 ? 1 : root.spanIndex)
                            else
                                trend.plot.xAxis.follow = false
                            checked = Qt.binding(() => trend.plot.xAxis.follow)
                        }
                    }
                    Switch {
                        text: qsTr("Overview of the whole run")
                        checked: root.overviewVisible
                        onToggled: root.overviewVisible = checked
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("The chart keeps the last %1 at %2 per channel; the file keeps everything.")
                                  .arg(root.controller.chartCapacity)
                                  .arg(Units.format(root.controller.coercedRate, "S/s", 7))
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // --- Bottom: per-channel value + statistics cards ----------------------------------------
        measurements: [
            RowLayout {
                objectName: "logCards"
                spacing: Theme.spacingS
                MeasurementCard {
                    visible: root.controller.ai0Enabled
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    title: "AI 0"
                    color: Theme.signalCh0
                    removable: false
                    value: root.cardValue("ai0", "V")
                    stats: root.cardStats("ai0", "V")
                }
                MeasurementCard {
                    visible: root.controller.ai1Enabled
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    title: "AI 1"
                    color: Theme.signalCh1
                    removable: false
                    value: root.cardValue("ai1", "V")
                    stats: root.cardStats("ai1", "V")
                }
                MeasurementCard {
                    visible: root.controller.dmmEnabled
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    title: qsTr("DMM")
                    color: Theme.textSecondary
                    removable: false
                    value: root.cardValue("dmm", root.controller.dmmUnit)
                    stats: root.cardStats("dmm", root.controller.dmmUnit)
                }
            }
        ]
    }
}
