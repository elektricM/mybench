pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Data Logger panel (docs/instruments/log.md): live recording chart (follow the latest data or
// scroll back through hours of it), per-channel statistics, recording to a delimited text file.
Item {
    id: root

    required property LogController controller

    readonly property string pauseIcon: "qrc:/qt/qml/MyBench/Instruments/Log/assets/pause.svg"
    readonly property bool analog: controller.ai0Enabled || controller.ai1Enabled
    readonly property var spans: [10, 60, 600, 3600, 0]
    property int spanIndex: 1

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
        const last = root.latestX()
        const span = root.spans[index]
        if (span <= 0) {
            main.xAxis.follow = false
            main.xAxis.setViewRange(root.firstX(), Math.max(last, root.firstX() + 1))
        } else {
            main.xAxis.setViewRange(last - span, last)
            main.xAxis.follow = true
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
        runBarControls: [
            ActionButton {
                text: root.controller.recording || root.controller.recordPending ? qsTr("Stop Recording") : qsTr("Record")
                iconName: root.controller.recording || root.controller.recordPending ? "stop" : "record"
                tone: root.controller.recording ? "danger" : "subtle"
                color: Theme.danger
                compact: true
                toolTip: root.controller.recording ? qsTr("Close %1").arg(root.fileName(root.controller.recordFile))
                                                   : qsTr("Write the data to a new file in %1").arg(root.controller.folder)
                onClicked: root.controller.recording || root.controller.recordPending ? root.controller.stopRecording()
                                                                                      : root.controller.startRecording()
            },
            ActionButton {
                visible: root.controller.recording
                text: root.controller.recordPaused ? qsTr("Resume") : qsTr("Pause")
                iconName: root.controller.recordPaused ? "record" : root.pauseIcon
                compact: true
                toolTip: root.controller.recordPaused ? qsTr("Continue writing to the file")
                                                      : qsTr("Stop writing to the file for now; the chart keeps running")
                onClicked: root.controller.setRecordPaused(!root.controller.recordPaused)
            }
        ]

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingS

            // --- Channels and recording state -------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: false
                spacing: Theme.spacingS

                LogChannelCard {
                    visible: root.controller.ai0Enabled
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    name: "AI 0"
                    unit: "V"
                    accent: Theme.ch0
                    stats: root.entry("ai0")
                    stale: !root.controller.running
                    compact: root.width < 700
                }
                LogChannelCard {
                    visible: root.controller.ai1Enabled
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    name: "AI 1"
                    unit: "V"
                    accent: Theme.ch1
                    stats: root.entry("ai1")
                    stale: !root.controller.running
                    compact: root.width < 700
                }
                LogChannelCard {
                    visible: root.controller.dmmEnabled
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    name: qsTr("DMM · %1").arg(root.controller.dmmFunctionNames[root.controller.dmmFunction])
                    unit: root.controller.dmmUnit
                    accent: Theme.math
                    stats: root.entry("dmm")
                    stale: !root.controller.running
                    compact: root.width < 700
                }

                // Recording state.
                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: Math.max(170, recColumn.implicitWidth + 2 * Theme.spacingM)
                    radius: Theme.radiusM
                    readonly property bool active: root.controller.recording && !root.controller.recordPaused
                    color: active ? Theme.alpha(Theme.danger, Theme.dark ? 0.12 : 0.08) : "transparent"
                    border.color: active ? Theme.alpha(Theme.danger, 0.55) : Theme.displayBorder
                    ColumnLayout {
                        id: recColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spacingS
                        anchors.leftMargin: Theme.spacingM
                        spacing: 2
                        RowLayout {
                            spacing: Theme.spacingS
                            Rectangle {
                                id: recDot
                                implicitWidth: 9
                                implicitHeight: 9
                                radius: 4.5
                                color: root.controller.recording ? (root.controller.recordPaused ? Theme.warning : Theme.danger)
                                                                 : Theme.textMuted
                                SequentialAnimation on opacity {
                                    running: root.controller.recording && !root.controller.recordPaused && Theme.animations
                                    loops: Animation.Infinite
                                    NumberAnimation { to: 0.25; duration: 700 }
                                    NumberAnimation { to: 1; duration: 700 }
                                    onStopped: recDot.opacity = 1
                                }
                            }
                            Text {
                                text: root.controller.recording ? (root.controller.recordPaused ? qsTr("PAUSED") : qsTr("REC"))
                                                                : root.controller.running ? qsTr("MONITORING") : qsTr("NOT RECORDING")
                                color: root.controller.recording ? (root.controller.recordPaused ? Theme.warning : Theme.danger)
                                                                 : Theme.textMuted
                                font.pixelSize: Theme.fontSizeS
                                font.weight: Font.Bold
                                font.letterSpacing: 1.0
                            }
                        }
                        Text {
                            text: root.controller.recording ? root.duration(root.controller.recordSeconds)
                                                            : root.controller.running ? root.duration(root.controller.runSeconds) : "—"
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeTitle
                            font.weight: Font.Light
                            font.features: Theme.tabularFigures
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 240
                            text: root.controller.recordFile.length > 0
                                  ? root.fileName(root.controller.recordFile) + (root.controller.recording
                                        ? "  ·  " + Units.format(root.controller.recordBytes, "B", 3) : "")
                                  : qsTr("Press Record to save")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeXs
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            // --- View controls -----------------------------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                SegmentedControl {
                    compact: true
                    model: ["10 s", "1 min", "10 min", "1 h", qsTr("All")]
                    currentIndex: main.xAxis.follow || root.spanIndex === 4 ? root.spanIndex : -1
                    onActivated: index => root.showSpan(index)
                }
                ActionButton {
                    compact: true
                    tone: main.xAxis.follow ? "subtle" : "neutral"
                    text: main.xAxis.follow ? qsTr("Following latest") : qsTr("Follow latest")
                    iconName: "chevron-right"
                    toolTip: qsTr("Keep the newest data at the right edge (scrolling back turns it off)")
                    onClicked: {
                        if (main.xAxis.follow)
                            main.xAxis.follow = false
                        else
                            root.showSpan(root.spanIndex === 4 ? 1 : root.spanIndex)
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: qsTr("View %1").arg(main.format(main.xAxis.max - main.xAxis.min, "s", 3))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                }
                IconButton {
                    iconName: "trash"
                    toolTip: qsTr("Clear the chart and the statistics (the file is not affected)")
                    onClicked: root.controller.clearChart()
                }
            }

            // --- Chart ------------------------------------------------------------------------
            BenchPlot {
                id: main
                visible: root.analog
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                hoverReadout: true
                legendVisible: root.controller.ai0Enabled && root.controller.ai1Enabled
                leftMargin: Math.max(implicitLeftMargin, dmmPlot.visible ? dmmPlot.implicitLeftMargin : 0)
                placeholderText: root.controller.running ? qsTr("Waiting for samples…") : qsTr("Press Run to start logging")
                xAxis { labelFormat: PlotAxis.Time; follow: true; tickSpacing: 110 }
                yAxis {
                    autoScale: root.controller.autoScale
                    min: root.controller.minimum
                    max: root.controller.maximum
                    unit: "V"
                    unitInLabels: true
                }
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                PlotTrace { visible: root.controller.ai0Enabled; name: "AI 0"; unit: "V"; buffer: root.controller.ai0; color: Theme.ch0; width: 1.3 }
                PlotTrace { visible: root.controller.ai1Enabled; name: "AI 1"; unit: "V"; buffer: root.controller.ai1; color: Theme.ch1; width: 1.3 }
            }

            BenchPlot {
                id: dmmPlot
                visible: root.controller.dmmEnabled
                Layout.fillWidth: true
                Layout.fillHeight: !root.analog
                Layout.preferredHeight: root.analog ? Math.max(90, main.height * 0.35) : -1
                interactive: !root.analog
                hoverReadout: true
                leftMargin: main.visible ? main.leftMargin : -1
                placeholderText: qsTr("DMM")
                xAxis { labelFormat: PlotAxis.Time; follow: !root.analog; tickSpacing: 110 }
                yAxis { autoScale: true; unit: root.controller.dmmUnit; unitInLabels: true; title: qsTr("DMM") }
                Component.onCompleted: xAxis.setRange(Date.now() / 1000 - 60, Date.now() / 1000)
                PlotTrace { name: qsTr("DMM"); unit: root.controller.dmmUnit; buffer: root.controller.dmm; color: Theme.math; width: 1.3; pointsVisible: true }
                Connections {
                    target: main.xAxis
                    enabled: root.analog
                    function onRangeChanged() { dmmPlot.xAxis.setViewRange(main.xAxis.min, main.xAxis.max) }
                }
            }

            // Overview of the whole recording with the visible window.
            BenchPlot {
                id: overview
                visible: root.analog
                Layout.fillWidth: true
                Layout.preferredHeight: 64
                interactive: false
                leftMargin: main.leftMargin
                yLabelsVisible: false
                xAxis { labelFormat: PlotAxis.Time; autoScale: true; interactive: false; tickSpacing: 110 }
                yAxis { autoScale: true; gridVisible: false; interactive: false }
                PlotTrace { visible: root.controller.ai0Enabled; buffer: root.controller.ai0; color: Theme.alpha(Theme.ch0, 0.8); width: 1 }
                PlotTrace { visible: root.controller.ai1Enabled; buffer: root.controller.ai1; color: Theme.alpha(Theme.ch1, 0.8); width: 1 }
                PlotRegion {
                    from: main.xAxis.min
                    to: main.xAxis.max
                    draggable: true
                    color: Theme.alpha(Theme.accent, 0.20)
                    onMoved: (from, to) => {
                        main.xAxis.follow = false
                        main.xAxis.setViewRange(from, to)
                    }
                }
            }
        }

        inspector: [
            Section {
                title: qsTr("Channel Select")
                summary: panel.displayTitle
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    enabled: !root.controller.running
                    RowLayout {
                        Layout.fillWidth: true
                        ChannelChip { text: "AI 0"; color: Theme.ch0; checked: root.controller.ai0Enabled; onToggled: root.controller.ai0Enabled = checked }
                        ChannelChip { text: "AI 1"; color: Theme.ch1; checked: root.controller.ai1Enabled; onToggled: root.controller.ai1Enabled = checked }
                        ChannelChip {
                            text: qsTr("DMM")
                            color: Theme.math
                            checked: root.controller.dmmEnabled
                            toolTip: qsTr("Log the multimeter too (its own reading rate, about 10/s DC, 3/s AC)")
                            onToggled: root.controller.dmmEnabled = checked
                        }
                        Item { Layout.fillWidth: true }
                    }
                    FieldRow {
                        visible: root.controller.dmmEnabled
                        Field {
                            label: qsTr("DMM Function")
                            ComboBox {
                                Layout.fillWidth: true
                                model: root.controller.dmmFunctionNames
                                currentIndex: root.controller.dmmFunction
                                onActivated: index => root.controller.dmmFunction = index
                            }
                        }
                        Field {
                            label: qsTr("Range")
                            ComboBox {
                                Layout.fillWidth: true
                                model: root.controller.dmmRangeNames
                                currentIndex: root.controller.dmmRange
                                onActivated: index => root.controller.dmmRange = index
                            }
                        }
                    }
                    Text {
                        visible: root.controller.running
                        Layout.fillWidth: true
                        text: qsTr("Stop the logger to change the channels.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                        wrapMode: Text.WordWrap
                    }
                }
            },
            Section {
                visible: root.analog
                title: qsTr("Sampling")
                summary: Units.format(root.controller.samplingRate, "S/s", 4)
                Field {
                    label: qsTr("Sampling Rate")
                    trailing: "1 – 1000 S/s"
                    SiField {
                        Layout.fillWidth: true
                        enabled: !root.controller.running
                        unit: "S/s"
                        engineering: false
                        decimals: 2
                        stepSize: 1
                        from: 1
                        to: 1000
                        value: root.controller.samplingRate
                        onValueModified: v => root.controller.samplingRate = v
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Actual %1 per channel · the chart keeps the last %2; the file keeps everything")
                              .arg(Units.format(root.controller.coercedRate, "S/s", 7))
                              .arg(root.controller.chartCapacity)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                visible: root.analog
                title: qsTr("Input Limits")
                summary: qsTr("±%1 V").arg(root.controller.hardwareRange)
                FieldRow {
                    enabled: !root.controller.running
                    Field {
                        label: qsTr("Maximum (V)")
                        SiField {
                            Layout.fillWidth: true
                            unit: "V"
                            engineering: false
                            decimals: 2
                            stepSize: 0.1
                            from: -10
                            to: 10
                            value: root.controller.maximum
                            onValueModified: v => root.controller.maximum = v
                        }
                    }
                    Field {
                        label: qsTr("Minimum (V)")
                        SiField {
                            Layout.fillWidth: true
                            unit: "V"
                            engineering: false
                            decimals: 2
                            stepSize: 0.1
                            from: -10
                            to: 10
                            value: root.controller.minimum
                            onValueModified: v => root.controller.minimum = v
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Hardware range ±%1 V (limits within ±2 V use the finer range)").arg(root.controller.hardwareRange)
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
                ToggleSwitch {
                    text: qsTr("Auto-scale graph")
                    checked: root.controller.autoScale
                    onToggled: root.controller.autoScale = checked
                }
            },
            Section {
                title: qsTr("Log File")
                summary: root.controller.fileFormat === LogController.CommaSeparated ? "CSV" : qsTr("Tab")
                Field {
                    label: qsTr("Folder")
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        TextField {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 60
                            text: root.controller.folder
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
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
                }
                FieldRow {
                    Field {
                        label: qsTr("File Name")
                        TextField {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 60
                            text: root.controller.baseName
                            onEditingFinished: root.controller.baseName = text
                        }
                    }
                    Field {
                        label: qsTr("Format")
                        SegmentedControl {
                            Layout.fillWidth: true
                            model: [{ text: qsTr("Tab"), toolTip: qsTr("Tab-separated (.tsv)") }, { text: "CSV", toolTip: qsTr("Comma-separated (.csv)") }]
                            currentIndex: root.controller.fileFormat
                            onActivated: index => root.controller.fileFormat = index
                        }
                    }
                }
                ToggleSwitch {
                    text: qsTr("Date and time in the name")
                    checked: root.controller.appendTimestamp
                    onToggled: root.controller.appendTimestamp = checked
                }
                ToggleSwitch {
                    text: qsTr("Timestamp column")
                    checked: root.controller.timestampColumn
                    onToggled: root.controller.timestampColumn = checked
                }
                ToggleSwitch {
                    text: qsTr("Record when Run starts")
                    checked: root.controller.recordOnRun
                    onToggled: root.controller.recordOnRun = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    ToggleSwitch {
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
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WrapAnywhere
                }
                ActionButton {
                    text: qsTr("Show Folder")
                    iconName: "folder-open"
                    compact: true
                    onClicked: Qt.openUrlExternally(root.controller.folderUrl())
                }
            }
        ]
    }
}
