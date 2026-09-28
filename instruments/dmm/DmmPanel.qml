pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Multimeter panel (docs/instruments/dmm.md): the big 3½-digit readout with the % FS gauge,
// status badges (range / AUTO, NULL, HOLD, diode, continuity), live statistics and the trend
// chart in the display; the ELVISmx "Measurement Settings" (function, Mode, Range, Null Offset),
// the banana-jack connection diagram, Statistics and Trend & Logging in the inspector.
Item {
    id: root

    required property DmmController controller

    // ELVISmx function buttons, in ELVISmx order. `fn` = DmmController.MeterFunction (-1: n/a).
    readonly property var functions: [
        { fn: DmmController.DcVoltage, icon: "dmm-vdc", caption: qsTr("DC V"), toolTip: qsTr("DC Voltage") },
        { fn: DmmController.AcVoltage, icon: "dmm-vac", caption: qsTr("AC V"), toolTip: qsTr("AC Voltage") },
        { fn: DmmController.DcCurrent, icon: "dmm-adc", caption: qsTr("DC A"), toolTip: qsTr("DC Current") },
        { fn: DmmController.AcCurrent, icon: "dmm-aac", caption: qsTr("AC A"), toolTip: qsTr("AC Current") },
        { fn: DmmController.Resistance, icon: "dmm-ohm", caption: qsTr("Ω"), toolTip: qsTr("Resistance") },
        { fn: -1, icon: "dmm-capacitance-line", caption: qsTr("Cap"), toolTip: qsTr("Capacitance — not available on NI myDAQ") },
        { fn: -1, icon: "dmm-inductance-line", caption: qsTr("Ind"), toolTip: qsTr("Inductance — not available on NI myDAQ") },
        { fn: DmmController.Diode, icon: "dmm-diode-line", caption: qsTr("Diode"), toolTip: qsTr("Diode") },
        { fn: DmmController.Continuity, icon: "dmm-continuity-line", caption: qsTr("Cont."), toolTip: qsTr("Continuity") }
    ]
    readonly property bool currentFunction: root.controller.selectedFunction === DmmController.DcCurrent
                                            || root.controller.selectedFunction === DmmController.AcCurrent
    readonly property string rangeText: root.controller.effectiveAutoRange
                                        ? (root.controller.activeRangeLabel.length > 0 ? qsTr("Auto · %1").arg(root.controller.activeRangeLabel) : qsTr("Auto"))
                                        : root.controller.selectedRangeLabel
    readonly property color readoutColor: Theme.ch1

    function selectButton(index: int) {
        const f = root.functions[index]
        if (f && f.fn >= 0)
            root.controller.selectedFunction = f.fn
    }

    // Keys 1-9 select the function buttons in order (panel focused).
    focus: true
    Keys.onPressed: event => {
        if (event.modifiers === Qt.NoModifier && event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
            root.selectButton(event.key - Qt.Key_1)
            event.accepted = true
        }
    }
    Component.onCompleted: root.controller.autoStartIfEnabled()

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.controller.functionName
        displayInfo: root.rangeText

        // --- Display -----------------------------------------------------------------------
        Item {
            id: display
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingS
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingS

                // Status badges.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    Badge {
                        text: root.controller.effectiveAutoRange ? qsTr("Auto") : qsTr("Range")
                        color: root.controller.effectiveAutoRange ? Theme.accent : Theme.textSecondary
                        solid: root.controller.effectiveAutoRange
                    }
                    Text {
                        text: root.controller.effectiveAutoRange ? (root.controller.activeRangeLabel || "—") : root.controller.selectedRangeLabel
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeM
                        font.weight: Font.DemiBold
                        font.features: Theme.tabularFigures
                    }
                    Badge {
                        visible: root.controller.nullOffset
                        text: root.controller.nullArmed ? qsTr("Null · armed") : qsTr("Null value: %1").arg(root.controller.nullText)
                        caps: false
                        color: Theme.math
                    }
                    Badge {
                        visible: root.controller.hold
                        text: qsTr("Hold")
                        color: Theme.warning
                        solid: true
                    }
                    Badge {
                        visible: root.controller.diodeStatus.length > 0
                        text: qsTr("Diode %1").arg(root.controller.diodeStatus)
                        color: root.controller.diodeStatus === "GOOD" ? Theme.success : Theme.danger
                    }
                    Badge {
                        visible: root.controller.selectedFunction === DmmController.Continuity && root.controller.hasReading
                        text: root.controller.continuityClosed ? qsTr("Continuity") : qsTr("Open")
                        iconName: "dmm-continuity-line"
                        color: root.controller.continuityClosed ? Theme.success : Theme.textSecondary
                        solid: root.controller.continuityClosed
                    }
                    Item { Layout.fillWidth: true }
                    // Activity dot: blinks on every reading.
                    Rectangle {
                        id: activity
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: Theme.success
                        opacity: 0
                        Connections {
                            target: root.controller
                            function onReadingSerialChanged() { blink.restart() }
                        }
                        SequentialAnimation {
                            id: blink
                            NumberAnimation { target: activity; property: "opacity"; to: 1; duration: 40 }
                            NumberAnimation { target: activity; property: "opacity"; to: 0.15; duration: 360 }
                        }
                    }
                }

                // The readout.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 110
                    Layout.preferredHeight: 200

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.spacingXs
                        width: parent.width

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.controller.functionName.toUpperCase()
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeS
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.2
                        }
                        DmmReadoutText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.controller.readingText
                            unit: root.controller.readingUnit
                            color: root.controller.overload ? Theme.danger : root.readoutColor
                            stale: !root.controller.running || root.controller.hold
                            fontSize: Math.max(52, Math.min(150, display.width / 6.2, display.height / 3.4))
                            secondaryText: root.controller.selectedFunction === DmmController.Continuity && root.controller.hasReading && !root.controller.continuityClosed ? qsTr("OPEN") : ""
                        }
                        DmmFsBar {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(parent.width - 2 * Theme.spacingXl, 620)
                            value: root.controller.fullScaleFraction
                            overload: root.controller.overload
                            color: root.readoutColor
                        }
                    }
                }

                // Statistics strip.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 760
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 0
                    Repeater {
                        model: [
                            { label: qsTr("Min"), value: root.controller.formatValue(root.controller.statMinimum) },
                            { label: qsTr("Max"), value: root.controller.formatValue(root.controller.statMaximum) },
                            { label: qsTr("Average"), value: root.controller.formatValue(root.controller.statMean, true) },
                            { label: qsTr("σ"), value: root.controller.formatValue(root.controller.statStdDev, true) },
                            { label: qsTr("Readings"), value: root.controller.statCount + (root.controller.statOverloads > 0 ? qsTr(" · %1 over").arg(root.controller.statOverloads) : "") }
                        ]
                        delegate: ColumnLayout {
                            id: cell
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 2
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: cell.modelData.label === qsTr("σ") ? cell.modelData.label : cell.modelData.label.toUpperCase()
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.8
                            }
                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: root.controller.statCount > 0 || cell.modelData.label === qsTr("Readings") ? cell.modelData.value : "—"
                                color: Theme.text
                                font.pixelSize: Theme.fontSizeL
                                font.features: Theme.tabularFigures
                            }
                        }
                    }
                }

                // Trend chart.
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(150, display.height * 0.36)
                    Layout.minimumHeight: 120

                    BenchPlot {
                        id: trendPlot
                        anchors.fill: parent
                        placeholderText: qsTr("Trend — press Run to measure")
                        xAxis {
                            labelFormat: PlotAxis.Time
                            follow: true
                        }
                        yAxis {
                            autoScale: true
                            unit: root.controller.unit
                            unitInLabels: true
                            minimumSpan: root.controller.trendMinimumSpan
                        }
                        PlotTrace {
                            buffer: root.controller.trend
                            unit: root.controller.unit
                            color: root.readoutColor
                            pointsVisible: true
                            pointSize: 4
                        }
                        Component.onCompleted: root.applyTrendWindow()
                    }
                    Row {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 6
                        anchors.rightMargin: 10
                        spacing: Theme.spacingXs
                        SegmentedControl {
                            compact: true
                            model: [qsTr("1 min"), qsTr("10 min"), qsTr("1 h"), qsTr("All")]
                            currentIndex: root.controller.trendWindow
                            onActivated: index => root.controller.trendWindow = index
                        }
                        IconButton {
                            iconName: "trash"
                            size: Theme.controlHeightS
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("Clear the trend")
                            onClicked: root.controller.clearTrend()
                        }
                    }
                }
            }
        }

        // --- Inspector ---------------------------------------------------------------------
        inspector: [
            Section {
                id: measurementSection
                title: qsTr("Measurement Settings")
                summary: measurementSection.collapsed ? root.controller.functionName + " · " + root.rangeText : ""

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    rowSpacing: Theme.spacingXs + 2
                    columnSpacing: Theme.spacingXs + 2
                    Repeater {
                        model: root.functions
                        delegate: DmmFunctionButton {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            iconName: modelData.icon
                            caption: modelData.caption
                            toolTip: modelData.toolTip
                            shortcutText: String(index + 1)
                            enabled: modelData.fn >= 0
                            checked: modelData.fn === root.controller.selectedFunction
                            onClicked: root.controller.selectedFunction = modelData.fn
                        }
                    }
                }
                Field {
                    label: qsTr("Mode")
                    SegmentedControl {
                        Layout.fillWidth: true
                        HoverHandler { id: modeHover }
                        ToolTip.visible: modeHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Mode")
                        model: [
                            { text: qsTr("Auto") },
                            { text: qsTr("Specify Range"), enabled: root.controller.selectedFunction !== DmmController.Continuity,
                              toolTip: root.controller.selectedFunction === DmmController.Continuity ? qsTr("Continuity always ranges automatically") : "" }
                        ]
                        currentIndex: root.controller.effectiveAutoRange ? 0 : 1
                        onActivated: index => root.controller.autoRange = index === 0
                    }
                }
                Field {
                    label: qsTr("Range")
                    trailing: root.controller.effectiveAutoRange && root.controller.selectedFunction !== DmmController.Continuity ? qsTr("chosen automatically") : ""
                    ComboBox {
                        id: rangeCombo
                        Layout.fillWidth: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Range")
                        enabled: root.controller.rangeSelectable
                        model: root.controller.rangeLabels
                        currentIndex: root.controller.rangeIndex
                        // Auto: shows the range in use (empty until the first reading).
                        displayText: root.controller.selectedFunction === DmmController.Continuity ? qsTr("No Range")
                                     : root.controller.effectiveAutoRange ? root.controller.activeRangeLabel
                                     : currentText
                        onActivated: index => root.controller.rangeIndex = index
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ToggleSwitch {
                        text: qsTr("Null Offset")
                        HoverHandler { id: nullHover }
                        ToolTip.visible: nullHover.hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Null Offset")
                        checked: root.controller.nullOffset
                        onToggled: root.controller.nullOffset = checked
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: root.controller.nullOffset
                        text: root.controller.nullArmed ? qsTr("next reading") : root.controller.nullText
                        color: Theme.math
                        font.pixelSize: Theme.fontSizeM
                        font.features: Theme.tabularFigures
                    }
                }
            },
            Section {
                id: jackSection
                title: qsTr("Banana Jack Connections")
                summary: jackSection.collapsed ? (root.currentFunction ? qsTr("A HI · COM") : qsTr("V Ω HI · COM")) : ""
                DmmJackDiagram {
                    Layout.fillWidth: true
                    Layout.preferredHeight: implicitHeight
                    currentMode: root.currentFunction
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: root.currentFunction
                          ? qsTr("Red lead on the A HI jack, black lead on COM. Current flows through the meter: connect it in series.")
                          : qsTr("Red lead on the V Ω HI jack, black lead on COM.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
            },
            Section {
                title: qsTr("Statistics")
                summary: qsTr("%n reading(s)", "", root.controller.statCount)
                Repeater {
                    model: [
                        [qsTr("Count"), String(root.controller.statCount)],
                        [qsTr("Minimum"), root.controller.formatValue(root.controller.statMinimum)],
                        [qsTr("Maximum"), root.controller.formatValue(root.controller.statMaximum)],
                        [qsTr("Peak to peak"), root.controller.formatValue(root.controller.statMaximum - root.controller.statMinimum)],
                        [qsTr("Average"), root.controller.formatValue(root.controller.statMean, true)],
                        [qsTr("Std deviation σ"), root.controller.formatValue(root.controller.statStdDev, true)],
                        [qsTr("Overloads"), String(root.controller.statOverloads)]
                    ]
                    delegate: RowLayout {
                        id: statRow
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: statRow.modelData[0]
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeM
                        }
                        Text {
                            text: statRow.modelData[1]
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeM
                            font.features: Theme.tabularFigures
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.controller.nullOffset ? qsTr("Relative values (Null Offset on). Overloads are excluded.")
                                                     : qsTr("Displayed values since the last reset or function change. Overloads are excluded.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                    wrapMode: Text.WordWrap
                }
                ActionButton {
                    text: qsTr("Reset statistics")
                    iconName: "reset"
                    compact: true
                    onClicked: root.controller.resetStatistics()
                }
            },
            Section {
                title: qsTr("Logging")
                summary: root.controller.recording ? qsTr("Recording · %1").arg(root.controller.logCount) : qsTr("%n row(s)", "", root.controller.logCount)
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        text: root.controller.recording ? qsTr("Stop recording") : qsTr("Record")
                        iconName: root.controller.recording ? "stop-filled" : "record"
                        tone: root.controller.recording ? "danger" : "neutral"
                        compact: true
                        onClicked: root.controller.recording = !root.controller.recording
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("%n row(s)", "", root.controller.logCount)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeM
                        font.features: Theme.tabularFigures
                    }
                }
                Field {
                    label: qsTr("Also write to file")
                    hint: qsTr("While recording, every reading is appended to this file at once (tab-separated; CSV for a .csv name).")
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.controller.logFile.toString().length > 0 ? DataExport.localPath(root.controller.logFile) : qsTr("none")
                            elide: Text.ElideMiddle
                            color: root.controller.logFile.toString().length > 0 ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontSizeM
                        }
                        IconButton {
                            iconName: "folder-open"
                            size: Theme.controlHeightS
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("Choose the log file…")
                            onClicked: {
                                streamDialog.selectedFile = DataExport.suggestedFileUrl("dmm-log", "tsv")
                                streamDialog.open()
                            }
                        }
                        IconButton {
                            visible: root.controller.logFile.toString().length > 0
                            iconName: "close"
                            size: Theme.controlHeightS
                            iconSize: Theme.iconSizeS
                            toolTip: qsTr("Do not write to a file")
                            onClicked: root.controller.logFile = ""
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        text: qsTr("Save log…")
                        iconName: "save"
                        compact: true
                        enabled: root.controller.logCount > 0
                        onClicked: {
                            saveDialog.selectedFile = DataExport.suggestedFileUrl("dmm-log", "tsv")
                            saveDialog.open()
                        }
                    }
                    ActionButton {
                        text: qsTr("Clear")
                        iconName: "trash"
                        compact: true
                        tone: "ghost"
                        enabled: root.controller.logCount > 0
                        onClicked: root.controller.clearLog()
                    }
                }
            },
            Section {
                title: qsTr("Options")
                collapsed: true
                ToggleSwitch {
                    text: qsTr("Continuity tone")
                    checked: root.controller.continuityBeep
                    enabled: root.controller.toneAvailable
                    onToggled: root.controller.continuityBeep = checked
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.toneAvailable ? qsTr("1 kHz tone while the probes are shorted (below 15 Ω)")
                                                                : qsTr("No sound output available")
                }
                ToggleSwitch {
                    text: qsTr("Signed overload (−Over)")
                    checked: root.controller.signedOverload
                    onToggled: root.controller.signedOverload = checked
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Show −Over for negative DC overloads (NI ELVISmx shows +Over for both signs)")
                }
                ToggleSwitch {
                    text: qsTr("Start measuring when the panel opens")
                    checked: root.controller.autoStart
                    onToggled: root.controller.autoStart = checked
                }
            }
        ]

        runBarControls: [
            ActionButton {
                text: qsTr("Hold")
                iconName: "pause"
                checkable: true
                checked: root.controller.hold
                tone: root.controller.hold ? "subtle" : "neutral"
                color: Theme.warning
                toolTip: qsTr("Freeze the readout (measuring, trend and statistics continue)")
                onClicked: root.controller.hold = !root.controller.hold
            }
        ]
    }

    function applyTrendWindow() {
        const span = root.controller.trendSeconds
        const now = Date.now() / 1000
        if (span > 0) {
            trendPlot.xAxis.setRange(now - span, now)
        } else {
            const first = root.controller.trend.count > 0 ? root.controller.trend.xMinimum() : now - 60
            trendPlot.xAxis.setRange(first, Math.max(now, first + 10))
        }
        trendPlot.xAxis.follow = true
    }
    Connections {
        target: root.controller
        function onTrendWindowChanged() { root.applyTrendWindow() }
        function onFunctionChanged() { root.applyTrendWindow() }
    }
    Connections {
        target: root.controller.trend
        enabled: root.controller.trendSeconds === 0
        function onChanged() {
            const first = root.controller.trend.xMinimum()
            const last = root.controller.trend.xMaximum()
            if (!trendPlot.xAxis.zoomed && isFinite(first) && isFinite(last))
                trendPlot.xAxis.setRange(first, Math.max(last, first + 10))
        }
    }

    FileDialog {
        id: streamDialog
        title: qsTr("Write the Multimeter log to")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Tab-separated text (*.tsv *.txt)"), qsTr("CSV (*.csv)")]
        defaultSuffix: "tsv"
        currentFolder: DataExport.lastFolder
        onAccepted: root.controller.logFile = selectedFile
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save the Multimeter log")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Tab-separated text (*.tsv *.txt)"), qsTr("CSV (*.csv)")]
        defaultSuffix: "tsv"
        currentFolder: DataExport.lastFolder
        onAccepted: root.controller.saveLog(selectedFile)
    }
}
