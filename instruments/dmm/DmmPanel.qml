pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls
import MyBench.Plot

// Multimeter panel (docs/instruments/dmm.md), round-2 layout (docs/UI.md "Round 2"):
// - pane header: Run/Stop, Single, Hold, Relative (null offset);
// - display (left, largest): the big readout, % FS bar, one status line, the trend;
// - right column: function picture buttons (keys 1–7), range buttons (Auto + the function's
//   ranges), tool palette (Logging, Connections, Options) whose panels float over the display;
// - bottom strip: statistics cards (Min, Max, Mean, σ, Count) with Reset.
Item {
    id: root

    required property DmmController controller

    readonly property bool narrow: width < 820     // split screen: narrower column, icon-only tools
    readonly property bool compact: width < 600    // very narrow: icon-only bar buttons, 3 statistics cards
    property string tool: ""

    // Function buttons. `fn` = DmmController.MeterFunction.
    readonly property var functions: [
        { fn: DmmController.DcVoltage, icon: "dmm-vdc", caption: qsTr("DC V"), toolTip: qsTr("DC Voltage") },
        { fn: DmmController.AcVoltage, icon: "dmm-vac", caption: qsTr("AC V"), toolTip: qsTr("AC Voltage") },
        { fn: DmmController.DcCurrent, icon: "dmm-adc", caption: qsTr("DC A"), toolTip: qsTr("DC Current") },
        { fn: DmmController.AcCurrent, icon: "dmm-aac", caption: qsTr("AC A"), toolTip: qsTr("AC Current") },
        { fn: DmmController.Resistance, icon: "dmm-ohm", caption: qsTr("Ω"), toolTip: qsTr("Resistance") },
        { fn: DmmController.Diode, icon: "dmm-diode", caption: qsTr("Diode"), toolTip: qsTr("Diode") },
        { fn: DmmController.Continuity, icon: "dmm-continuity", caption: qsTr("Continuity"), shortCaption: qsTr("Cont."), toolTip: qsTr("Continuity") }
    ]
    readonly property bool currentFunction: root.controller.selectedFunction === DmmController.DcCurrent
                                            || root.controller.selectedFunction === DmmController.AcCurrent
    readonly property bool continuity: root.controller.selectedFunction === DmmController.Continuity
    // One short status line under the readout, only what no control shows: the range Auto picked,
    // the null value while Relative is on, the diode state. Empty otherwise.
    readonly property string statusText: {
        const parts = []
        if (root.controller.effectiveAutoRange && root.controller.activeRangeLabel.length > 0)
            parts.push(qsTr("Range %1").arg(root.controller.activeRangeLabel))
        if (root.controller.nullOffset)
            parts.push(root.controller.nullArmed ? qsTr("Null pending") : qsTr("Null %1").arg(root.controller.nullText))
        if (root.controller.diodeStatus.length > 0)
            parts.push(root.controller.diodeStatus === "GOOD" ? qsTr("Conducts") : qsTr("Open"))
        return parts.join("  ·  ")
    }

    function selectButton(index: int) {
        const f = root.functions[index]
        if (f && f.fn >= 0)
            root.controller.selectedFunction = f.fn
    }
    function toggleTool(id: string) {
        root.tool = root.tool === id ? "" : id
    }

    // Keys 1-7 select the function buttons in order (panel focused).
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
        objectName: "dmmPane"
        anchors.fill: parent
        controller: root.controller
        showRunControls: false
        inspector: []

        headerContent: [
            RowLayout {
                spacing: Theme.spacingS
                RunButton {
                    objectName: "dmmRunButton"
                    running: root.controller.running
                    busy: root.controller.state === InstrumentController.Stopping
                    enabled: root.controller.running || root.controller.canRun
                    stateText: root.controller.running ? panel.paneHeader.statusText : ""
                    onToggled: root.controller.toggle()
                }
                Button {
                    objectName: "dmmSingleButton"
                    text: qsTr("Single")
                    Layout.preferredHeight: Theme.controlHeightL
                    enabled: root.controller.canRun
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Take one reading, then stop")
                    onClicked: root.controller.runOnce()
                }
                Button {
                    objectName: "dmmHoldButton"
                    text: qsTr("Hold")
                    icon.source: Theme.icon("pause")
                    display: root.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    Layout.preferredHeight: Theme.controlHeightL
                    checkable: true
                    checked: root.controller.hold
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Freeze the readout (measuring, trend and statistics continue)")
                    onClicked: {
                        root.controller.hold = checked
                        checked = Qt.binding(() => root.controller.hold)
                    }
                }
                Button {
                    objectName: "dmmRelativeButton"
                    text: root.compact ? "Δ" : qsTr("Relative")
                    Accessible.name: qsTr("Relative")
                    Layout.preferredHeight: Theme.controlHeightL
                    checkable: true
                    checked: root.controller.nullOffset
                    focusPolicy: Qt.TabFocus
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Null offset: show readings relative to the next reading")
                    onClicked: {
                        root.controller.nullOffset = checked
                        checked = Qt.binding(() => root.controller.nullOffset)
                    }
                }
            }
        ]

        // --- Display + right column ------------------------------------------------------------
        Item {
            anchors.fill: parent

            Item {
                id: display
                objectName: "dmmDisplay"
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: column.left }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingS
                    spacing: Theme.spacingS

                    // The readout.
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: readoutColumn.implicitHeight

                        Column {
                            id: readoutColumn
                            anchors.centerIn: parent
                            width: parent.width
                            spacing: Theme.spacingS

                            DmmReadoutText {
                                objectName: "dmmReadout"
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.controller.readingText
                                unit: root.controller.readingUnit
                                color: root.controller.overload ? Theme.negative : Theme.text
                                stale: !root.controller.running || root.controller.hold
                                fontSize: Math.max(Theme.fontSizeReadoutM * 1.6,
                                                   Math.min(display.width / 5.6, display.height / 4.2))
                                secondaryText: root.continuity && root.controller.hasReading && !root.controller.continuityClosed ? qsTr("Open") : ""
                            }
                            DmmFsBar {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(parent.width - 2 * Theme.spacingL, display.width * 0.7)
                                value: root.controller.fullScaleFraction
                                overload: root.controller.overload
                                color: Theme.accent
                            }
                            Text {
                                objectName: "dmmStatusLine"
                                visible: text.length > 0
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width - 2 * Theme.spacingS
                                horizontalAlignment: Text.AlignHCenter
                                text: root.statusText
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.features: Theme.tabularFigures
                                elide: Text.ElideRight
                            }
                        }
                    }

                    TrendView {
                        id: trend
                        objectName: "trendView"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(Theme.controlHeightL * 4, display.height * 0.42)
                        windows: [ { text: qsTr("1 min"), seconds: 60 }, { text: qsTr("10 min"), seconds: 600 },
                                   { text: qsTr("1 h"), seconds: 3600 }, { text: qsTr("All"), seconds: 0 } ]
                        windowIndex: root.controller.trendWindow
                        onWindowActivated: index => root.controller.trendWindow = index
                        onClearRequested: root.controller.clearTrend()
                        plot.objectName: "trendPlot"
                        plot.placeholderText: qsTr("Trend — press Run to measure")
                        plot.xAxis.labelFormat: PlotAxis.Time
                        plot.xAxis.follow: true
                        plot.yAxis.autoScale: true
                        plot.yAxis.unit: root.controller.unit
                        plot.yAxis.unitInLabels: true
                        plot.yAxis.minimumSpan: root.controller.trendMinimumSpan
                        PlotTrace {
                            buffer: root.controller.trend
                            unit: root.controller.unit
                            color: Theme.text
                            pointsVisible: true
                            pointSize: 4
                        }
                        Component.onCompleted: root.applyTrendWindow()
                    }
                }
            }

            // Right column: function, range, tool palette.
            Rectangle {
                id: column
                objectName: "dmmControls"
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
                        spacing: Theme.spacingM

                        Label {
                            text: qsTr("Function")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                        GridLayout {
                            objectName: "functionGrid"
                            Layout.fillWidth: true
                            columns: column.width < 190 ? 2 : 3
                            rowSpacing: Theme.spacingXs
                            columnSpacing: Theme.spacingXs
                            Repeater {
                                model: root.functions
                                delegate: PictureButton {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    implicitWidth: 56
                                    text: root.narrow && modelData.shortCaption ? modelData.shortCaption : modelData.caption
                                    iconName: modelData.icon
                                    checked: modelData.fn === root.controller.selectedFunction
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("%1 (key %2)").arg(modelData.toolTip).arg(index + 1)
                                    onClicked: {
                                        root.controller.selectedFunction = modelData.fn
                                        checked = Qt.binding(() => modelData.fn === root.controller.selectedFunction)
                                    }
                                }
                            }
                        }

                        Label {
                            text: qsTr("Range")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                        GridLayout {
                            objectName: "rangeGrid"
                            Layout.fillWidth: true
                            columns: column.width < 230 ? 2 : 3
                            rowSpacing: Theme.spacingXs
                            columnSpacing: Theme.spacingXs
                            Repeater {
                                // Auto, then the function's ranges; Continuity always ranges automatically.
                                model: root.continuity ? [qsTr("Auto")] : [qsTr("Auto")].concat(root.controller.rangeLabels)
                                delegate: Button {
                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    implicitWidth: Theme.controlHeightL
                                    leftPadding: Theme.spacingXs
                                    rightPadding: Theme.spacingXs
                                    text: modelData
                                    checkable: true
                                    highlighted: checked
                                    checked: index === 0 ? root.controller.effectiveAutoRange
                                                         : !root.controller.effectiveAutoRange && root.controller.rangeIndex === index - 1
                                    focusPolicy: Qt.TabFocus
                                    onClicked: {
                                        if (index === 0) {
                                            root.controller.autoRange = true
                                        } else {
                                            root.controller.rangeIndex = index - 1
                                            root.controller.autoRange = false
                                        }
                                        checked = Qt.binding(() => index === 0 ? root.controller.effectiveAutoRange
                                                                               : !root.controller.effectiveAutoRange && root.controller.rangeIndex === index - 1)
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: root.continuity
                            text: qsTr("Continuity always ranges automatically.")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeCaption
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            text: qsTr("Tools")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                        ToolPalette {
                            objectName: "dmmTools"
                            Layout.fillWidth: true
                            columns: 3
                            currentId: root.tool
                            model: [
                                { id: "logging", text: root.narrow ? "" : qsTr("Logging"), icon: "record" },
                                { id: "connections", text: root.narrow ? "" : qsTr("Connections"), icon: "instrument-dmm" },
                                { id: "options", text: root.narrow ? "" : qsTr("Options"), icon: "settings" }
                            ]
                            onActivated: id => root.toggleTool(id)
                        }
                    }
                }
            }

            // The open tool panel floats over the right part of the display, next to the column.
            Item {
                id: toolArea
                anchors { right: column.left; top: parent.top; bottom: parent.bottom; margins: Theme.spacingS }
                width: Math.min(300, Math.max(220, display.width * 0.45))
                z: 10

                ToolPanel {
                    objectName: "loggingPanel"
                    visible: root.tool === "logging"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Logging")
                    onCloseRequested: root.tool = ""
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: root.controller.recording ? qsTr("Stop recording") : qsTr("Record")
                            icon.source: Theme.icon(root.controller.recording ? "stop" : "record")
                            onClicked: root.controller.recording = !root.controller.recording
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("%n row(s)", "", root.controller.logCount)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.features: Theme.tabularFigures
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Also write to file")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.controller.logFile.toString().length > 0 ? DataExport.localPath(root.controller.logFile) : qsTr("none")
                            elide: Text.ElideMiddle
                            color: root.controller.logFile.toString().length > 0 ? Theme.text : Theme.textTertiary
                            font.pixelSize: Theme.fontSizeBody
                        }
                        IconButton {
                            iconName: "folder-open"
                            toolTip: qsTr("Choose the log file…")
                            onClicked: {
                                streamDialog.selectedFile = DataExport.suggestedFileUrl("dmm-log", "tsv")
                                streamDialog.open()
                            }
                        }
                        IconButton {
                            visible: root.controller.logFile.toString().length > 0
                            iconName: "close"
                            toolTip: qsTr("Do not write to a file")
                            onClicked: root.controller.logFile = ""
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("While recording, every reading is appended to the file at once (tab-separated; CSV for a .csv name).")
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            text: qsTr("Save Log…")
                            enabled: root.controller.logCount > 0
                            onClicked: {
                                saveDialog.selectedFile = DataExport.suggestedFileUrl("dmm-log", "tsv")
                                saveDialog.open()
                            }
                        }
                        Button {
                            text: qsTr("Clear")
                            flat: true
                            enabled: root.controller.logCount > 0
                            onClicked: root.controller.clearLog()
                        }
                    }
                }

                ToolPanel {
                    objectName: "connectionsPanel"
                    visible: root.tool === "connections"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Connections")
                    onCloseRequested: root.tool = ""
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
                        font.pixelSize: Theme.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                ToolPanel {
                    objectName: "optionsPanel"
                    visible: root.tool === "options"
                    width: parent.width
                    maximumHeight: parent.height
                    title: qsTr("Options")
                    onCloseRequested: root.tool = ""
                    Switch {
                        text: qsTr("Continuity tone")
                        checked: root.controller.continuityBeep
                        enabled: root.controller.toneAvailable
                        onToggled: root.controller.continuityBeep = checked
                        ToolTip.visible: hovered
                        ToolTip.text: root.controller.toneAvailable ? qsTr("1 kHz tone while the probes are shorted (below 15 Ω)")
                                                                    : qsTr("No sound output available")
                    }
                    Switch {
                        text: qsTr("Start measuring when shown")
                        checked: root.controller.autoStart
                        onToggled: root.controller.autoStart = checked
                    }
                }
            }
        }

        // --- Bottom: statistics cards ------------------------------------------------------------
        measurements: [
            RowLayout {
                objectName: "dmmStatistics"
                spacing: Theme.spacingS
                Repeater {
                    model: [
                        { title: qsTr("Min"), value: root.controller.formatValue(root.controller.statMinimum) },
                        { title: qsTr("Max"), value: root.controller.formatValue(root.controller.statMaximum) },
                        { title: qsTr("Mean"), value: root.controller.formatValue(root.controller.statMean, true) },
                        { title: qsTr("σ"), value: root.controller.formatValue(root.controller.statStdDev, true) },
                        { title: qsTr("Count"), value: root.controller.statCount
                                                       + (root.controller.statOverloads > 0 ? qsTr(" · %1 over").arg(root.controller.statOverloads) : "") }
                    ]
                    delegate: MeasurementCard {
                        required property var modelData
                        required property int index
                        visible: !root.compact || index < 3        // very narrow: Min, Max, Mean
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        title: modelData.title
                        color: Theme.textSecondary
                        value: root.controller.statCount > 0 || index === 4 ? modelData.value : ""
                        stats: null
                        removable: false
                    }
                }
                Button {
                    objectName: "dmmResetStatistics"
                    text: qsTr("Reset")
                    icon.source: Theme.icon("reset")
                    display: root.compact ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    ToolTip.visible: hovered
                    ToolTip.text: root.controller.nullOffset ? qsTr("Restart the statistics (relative values; overloads excluded)")
                                                             : qsTr("Restart the statistics (overloads excluded)")
                    onClicked: root.controller.resetStatistics()
                }
            }
        ]
    }

    function applyTrendWindow() {
        const span = root.controller.trendSeconds
        const now = Date.now() / 1000
        const axis = trend.plot.xAxis
        if (span > 0) {
            axis.setRange(now - span, now)
        } else {
            const first = root.controller.trend.count > 0 ? root.controller.trend.xMinimum() : now - 60
            axis.setRange(first, Math.max(now, first + 10))
        }
        axis.follow = true
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
            if (!trend.plot.xAxis.zoomed && isFinite(first) && isFinite(last))
                trend.plot.xAxis.setRange(first, Math.max(last, first + 10))
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
