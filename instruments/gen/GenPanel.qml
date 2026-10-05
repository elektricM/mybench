pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Waveform Generator panel (docs/instruments/gen.md): the ELVISmx Function Generator, Arbitrary
// Waveform Generator, Waveform Editor and DC Level in one pane.
// Generator view (round 2 layout): instrument bar in the pane header (Run, Single, output pair,
// Time | Frequency); the display on the left (one readout line and one preview per active
// channel); on the right one card per channel (output switch, waveform pictures, knob fields)
// and a tool palette whose tools (Sweep, Arbitrary, Editor, Presets, Export) open a floating
// panel over the display. The Waveform Editor view (EditorView) follows the same pattern.
Item {
    id: root

    required property GenController controller
    readonly property bool editorTab: controller.activeTab === 1
    readonly property WaveformEditor editor: controller.editor
    // Narrow pane (e.g. beside the Scope): knob fields without knobs, palette icons only.
    readonly property bool compact: panel.width < Theme.instrumentCompactWidth
    // Open tool panel ("" = none).
    property string tool: ""
    readonly property var channels: [controller.channel0, controller.channel1]

    InstrumentPanel {
        id: panel
        objectName: "genPane"
        anchors.fill: parent
        controller: root.controller
        showRunControls: false
        // The Run button and the readout lines carry the state; the header only shows a short word
        // (the framework's own text while paused, in error or without a device).
        stateText: root.controller.hasError || root.controller.paused || (!root.controller.running && !root.controller.canRun)
                   ? "" : root.controller.running ? qsTr("Generating") : qsTr("Stopped")
        // Both views keep every control beside their display: no inspector.
        hasInspector: false
        // The editor's trace in the panel menu and the plots' "Trace style…" (editor tab).
        traceStyles: root.editorTab ? [{ name: root.editor.documentName, style: root.editor.traceStyle,
                                         color: root.editor.plotColor.a > 0 ? root.editor.plotColor : Theme.accent }] : []

        headerContent: [
            RunButton {
                objectName: "genRunButton"
                running: root.controller.running
                busy: root.controller.busy
                enabled: root.controller.running || root.controller.canRun
                stateText: !root.controller.running ? qsTr("Stopped")
                           : root.controller.taskRate <= 0 ? qsTr("Generating")
                           : root.compact ? Units.format(root.controller.taskRate, "S/s", 3)
                           : qsTr("Generating · %1").arg(Units.format(root.controller.taskRate, "S/s", 3))
                onToggled: root.controller.toggle()
            },
            Button {
                objectName: "genSingleButton"
                visible: !root.editorTab
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("Single")
                enabled: root.controller.singleEnabled
                ToolTip.visible: hovered
                ToolTip.text: root.controller.singleEnabled ? qsTr("Play the Arbitrary channels once, then stop")
                                                            : qsTr("Load a waveform into an Arbitrary channel to play it once")
                onClicked: root.controller.runOnce()
            },
            SegmentedControl {
                objectName: "outputPair"
                visible: !root.editorTab && !root.compact
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                compact: true
                model: [ { text: qsTr("AO 0 / AO 1"),
                           toolTip: qsTr("Screw terminals AO 0 / AO 1 (±10 V, DC coupled)") },
                         { text: qsTr("Audio out"),
                           toolTip: qsTr("AUDIO OUT jack, left / right (±2 V, AC coupled)") } ]
                currentIndex: root.controller.outputPair
                onActivated: index => root.controller.outputPair = index
            },
            SegmentedControl {
                objectName: "previewMode"
                visible: !root.editorTab && !root.compact
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                compact: true
                model: [ { text: qsTr("Time"), toolTip: qsTr("Amplitude (V) against time") },
                         { text: qsTr("Frequency"), toolTip: qsTr("Spectrum of each output (dBV)") } ]
                currentIndex: root.controller.displayMode
                onActivated: index => root.controller.displayMode = index
            }
        ]

        menuItems: [
            Menu {
                id: presetMenu
                title: qsTr("Presets")
                Instantiator {
                    model: root.controller.builtInPresets
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        onTriggered: root.controller.loadBuiltInPreset(index)
                    }
                    onObjectAdded: (index, object) => presetMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => presetMenu.removeItem(object)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Save Current Settings as Preset…")
                    onTriggered: presetDialog.open()
                }
            },
            MenuItem {
                text: qsTr("Align Phases")
                enabled: root.controller.strategy === GenController.Streaming
                onTriggered: root.controller.alignPhases()
            },
            MenuItem {
                text: qsTr("Sweep All")
                enabled: root.controller.canSweep
                onTriggered: root.controller.sweepAll()
            }
        ]

        StackLayout {
            anchors.fill: parent
            currentIndex: root.controller.activeTab

            // --- Generator view ---------------------------------------------------------------
            Item {
                id: generatorView

                RowLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Display: one preview per channel that is not Off; values live in the channel cards.
                    ColumnLayout {
                        id: displayColumn
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Theme.spacingS
                        spacing: Theme.spacingS
                        readonly property int offCount: (root.controller.channel0.mode === GenChannel.Off ? 1 : 0)
                                                        + (root.controller.channel1.mode === GenChannel.Off ? 1 : 0)

                        // Narrow pane: the output pair and the preview choice leave the header.
                        Flow {
                            objectName: "compactChoices"
                            visible: root.compact
                            Layout.fillWidth: true
                            spacing: Theme.spacingS
                            SegmentedControl {
                                compact: true
                                model: [ { text: qsTr("AO 0 / AO 1") }, { text: qsTr("Audio out") } ]
                                currentIndex: root.controller.outputPair
                                onActivated: index => root.controller.outputPair = index
                            }
                            SegmentedControl {
                                compact: true
                                model: [ { text: qsTr("Time") }, { text: qsTr("Frequency") } ]
                                currentIndex: root.controller.displayMode
                                onActivated: index => root.controller.displayMode = index
                            }
                        }

                        Repeater {
                            model: root.channels
                            delegate: ChannelPreview {
                                required property GenChannel modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: modelData.mode !== GenChannel.Off || displayColumn.offCount === 2
                                Layout.minimumHeight: 60
                                objectName: "genPreview" + modelData.index
                                controller: root.controller
                                channel: modelData
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillHeight: true
                        implicitWidth: 1
                        color: Theme.separator
                    }

                    // Right column: channel cards, then the tool palette.
                    ScrollView {
                        id: controlsColumn
                        objectName: "genControls"
                        Layout.fillHeight: true
                        Layout.preferredWidth: root.compact ? Theme.instrumentColumnCompactWidth : Theme.instrumentColumnWidth
                        contentWidth: availableWidth
                        clip: true

                        ColumnLayout {
                            width: controlsColumn.availableWidth
                            spacing: Theme.spacingS

                            Item { implicitHeight: Theme.spacingXs }
                            Repeater {
                                model: root.channels
                                delegate: GenChannelCard {
                                    required property GenChannel modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: Theme.spacingS
                                    Layout.rightMargin: Theme.spacingS
                                    controller: root.controller
                                    channel: modelData
                                    compact: root.compact
                                    onArbitraryToolRequested: root.tool = "arbitrary"
                                }
                            }
                            ToolPalette {
                                objectName: "genTools"
                                Layout.fillWidth: true
                                Layout.leftMargin: Theme.spacingS
                                Layout.rightMargin: Theme.spacingS
                                Layout.bottomMargin: Theme.spacingS
                                columns: root.compact ? 5 : 3
                                cellHeight: root.compact ? 36 : 52
                                currentId: root.tool
                                model: [
                                    { id: "sweep", text: root.compact ? "" : qsTr("Sweep"), toolTip: root.compact ? qsTr("Sweep") : "", icon: "sweep" },
                                    { id: "arbitrary", text: root.compact ? "" : qsTr("Arbitrary"), toolTip: root.compact ? qsTr("Arbitrary") : "", icon: "wave-arbitrary" },
                                    { id: "editor", text: root.compact ? "" : qsTr("Editor"), toolTip: root.compact ? qsTr("Editor") : "", icon: "edit" },
                                    { id: "presets", text: root.compact ? "" : qsTr("Presets"), toolTip: root.compact ? qsTr("Presets") : "", icon: "layers" },
                                    { id: "export", text: root.compact ? "" : qsTr("Export"), toolTip: root.compact ? qsTr("Export") : "", icon: "export" }
                                ]
                                onActivated: id => {
                                    if (id === "editor") {
                                        root.tool = ""
                                        root.controller.activeTab = 1
                                    } else {
                                        root.tool = root.tool === id ? "" : id
                                    }
                                }
                            }
                        }
                    }
                }

                // Floating tool panel over the display, top right (left of the controls column).
                ToolPanel {
                    id: toolPanel
                    objectName: "genToolPanel"
                    visible: root.tool.length > 0
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.topMargin: Theme.spacingS
                    anchors.rightMargin: controlsColumn.width + Theme.spacingS
                    width: Math.min(Theme.toolPanelWidth, Math.max(220, displayColumn.width - 2 * Theme.spacingS))
                    maximumHeight: parent.height - 2 * Theme.spacingS
                    title: root.tool === "sweep" ? qsTr("Frequency Sweep")
                           : root.tool === "arbitrary" ? qsTr("Arbitrary Waveforms")
                           : root.tool === "presets" ? qsTr("Presets")
                           : root.tool === "export" ? qsTr("Export") : ""
                    onCloseRequested: root.tool = ""

                    GenSweepTool {
                        visible: root.tool === "sweep"
                        Layout.fillWidth: true
                        controller: root.controller
                    }
                    GenArbitraryTool {
                        visible: root.tool === "arbitrary"
                        Layout.fillWidth: true
                        controller: root.controller
                    }
                    ColumnLayout {
                        visible: root.tool === "presets"
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Repeater {
                            model: root.controller.builtInPresets
                            delegate: Button {
                                required property string modelData
                                required property int index
                                Layout.fillWidth: true
                                text: modelData
                                onClicked: root.controller.loadBuiltInPreset(index)
                            }
                        }
                        Repeater {
                            model: root.controller.presetNames
                            delegate: RowLayout {
                                id: presetRow
                                required property string modelData
                                Layout.fillWidth: true
                                Button {
                                    Layout.fillWidth: true
                                    text: presetRow.modelData
                                    icon.source: Theme.icon("file")
                                    onClicked: root.controller.loadPreset(presetRow.modelData)
                                }
                                ToolButton {
                                    icon.source: Theme.icon("trash")
                                    Accessible.name: qsTr("Delete preset %1").arg(presetRow.modelData)
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Delete this preset")
                                    onClicked: root.controller.deletePreset(presetRow.modelData)
                                }
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Save Current Settings…")
                            icon.source: Theme.icon("save")
                            onClicked: presetDialog.open()
                        }
                    }
                    ColumnLayout {
                        visible: root.tool === "export"
                        Layout.fillWidth: true
                        spacing: Theme.spacingXs
                        Repeater {
                            model: root.channels
                            delegate: Button {
                                required property GenChannel modelData
                                Layout.fillWidth: true
                                text: qsTr("Export %1 Waveform…").arg(modelData.name)
                                enabled: modelData.mode !== GenChannel.Off
                                onClicked: {
                                    exportDialog.channelIndex = modelData.index
                                    exportDialog.open()
                                }
                            }
                        }
                        Button {
                            Layout.fillWidth: true
                            text: qsTr("Export Image…")
                            onClicked: panel.exportImage(false)
                        }
                    }
                }
            }

            // --- Waveform Editor view ------------------------------------------------------------
            EditorView {
                id: editorView
                objectName: "editorView"
                controller: root.controller
                editor: root.editor
            }
        }
    }

    // Screenshot scenes (dev executable, MYBENCH_GEN_SCENE): open a dialog, tool or state once loaded.
    Timer {
        interval: 300
        running: root.controller.devScene().length > 0
        onTriggered: {
            const scene = root.controller.devScene()
            if (scene.startsWith("tool-")) {
                root.tool = scene.substring(5)
            } else if (scene.startsWith("preset-")) {
                root.controller.loadBuiltInPreset(Number(scene.substring(7)))
                if (!root.controller.running)
                    root.controller.run()
            } else if (scene === "editor") {
                root.controller.activeTab = 1
            } else {
                // Editor scenes (draw, component, info, menu, settings, editor-tool-…).
                root.controller.activeTab = 1
                editorView.showScene(scene)
            }
        }
    }

    FileDialog {
        id: exportDialog
        property int channelIndex: 0
        title: qsTr("Export waveform")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("CSV (*.csv)"), qsTr("WAV audio (*.wav)")]
        onAccepted: root.controller.exportChannel(channelIndex, selectedFile)
    }

    Dialog {
        id: presetDialog
        title: qsTr("Save preset")
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel
        onOpened: presetName.text = ""
        onAccepted: root.controller.savePreset(presetName.text)
        ColumnLayout {
            spacing: Theme.spacingS
            Text {
                text: qsTr("Name of the preset (both channels and the output pair):")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeM
            }
            TextField {
                id: presetName
                Layout.preferredWidth: 280
                placeholderText: qsTr("My setup")
                onAccepted: presetDialog.accept()
            }
        }
    }
}
