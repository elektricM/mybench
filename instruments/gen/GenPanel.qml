pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Waveform Generator panel (docs/instruments/gen.md): the ELVISmx Function Generator, Arbitrary
// Waveform Generator, Waveform Editor and DC Level in one pane.
// Generator view: instrument bar in the pane header (Run, Single while an Arbitrary
// channel can play once, the output connector); ONE display with both outputs (GenDisplay, with its
// legend and Time | Frequency above the plot); on the right the InstrumentColumn: one card per
// channel (output switch, waveform pictures, knob fields; Arbitrary: file, library, Open in
// Editor, timing) and the tool palette (Sweep, Presets) whose tools open a floating panel over the
// display. Waveform export lives in the ⋯ menu. The Waveform Editor view (EditorView) follows the
// same pattern.
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

    function exportChannel(index: int) {
        exportDialog.channelIndex = index
        exportDialog.open()
    }

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
            // Single applies only to Arbitrary channels with a waveform: hidden otherwise.
            Button {
                objectName: "genSingleButton"
                visible: !root.editorTab && root.controller.singleEnabled
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("Single")
                icon.source: Theme.icon("step")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Play the Arbitrary channels once, then stop")
                onClicked: root.controller.runOnce()
            },
            // Where the two channels come out: the AO screw terminals or the AUDIO OUT jack.
            RowLayout {
                objectName: "outputRouting"
                visible: !root.editorTab
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: Theme.spacingS
                Text {
                    text: qsTr("Output")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeS
                }
                SegmentedControl {
                    objectName: "outputPair"
                    compact: true
                    model: [ { text: qsTr("AO 0 / AO 1"),
                               toolTip: qsTr("Screw terminals AO 0 / AO 1 (±10 V, DC coupled)") },
                             { text: qsTr("Audio out"),
                               toolTip: qsTr("AUDIO OUT jack, left / right (±2 V, AC coupled)") } ]
                    currentIndex: root.controller.outputPair
                    onActivated: index => root.controller.outputPair = index
                }
            }
        ]

        // ⋯ entries (native menus ignore `visible`: fixed entries, enabled when they apply). The
        // controller's commands (Sweep the Function channels) are added by the header.
        menuItems: [
            MenuItem {
                text: qsTr("Export %1 Waveform…").arg(root.controller.channel0.name)
                enabled: root.controller.channel0.mode !== GenChannel.Off
                onTriggered: root.exportChannel(0)
            },
            MenuItem {
                text: qsTr("Export %1 Waveform…").arg(root.controller.channel1.name)
                enabled: root.controller.channel1.mode !== GenChannel.Off
                onTriggered: root.exportChannel(1)
            },
            MenuItem {
                text: qsTr("Waveform Editor")
                enabled: !root.editorTab
                onTriggered: root.controller.activeTab = 1
            },
            MenuItem {
                text: qsTr("Align Phases")
                enabled: root.controller.strategy === GenController.Streaming
                onTriggered: root.controller.alignPhases()
            }
        ]

        StackLayout {
            anchors.fill: parent
            currentIndex: root.controller.activeTab

            // --- Generator view ---------------------------------------------------------------
            Item {
                id: generatorView

                // Display: one plot with both outputs; values live in the channel cards.
                GenDisplay {
                    id: display
                    objectName: "genDisplay"
                    anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: controlsColumn.left; margins: Theme.spacingS }
                    controller: root.controller
                }

                // Right column (fixed width): channel cards, the tool palette pinned at the bottom.
                InstrumentColumn {
                    id: controlsColumn
                    objectName: "genControls"
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                    compact: root.compact
                    Repeater {
                        model: root.channels
                        delegate: GenChannelCard {
                            required property GenChannel modelData
                            Layout.fillWidth: true
                            controller: root.controller
                            channel: modelData
                            compact: root.compact
                        }
                    }
                    tools: ToolPalette {
                        objectName: "genTools"
                        columns: 2
                        cellHeight: root.compact ? 40 : 52
                        currentId: root.tool
                        model: [
                            { id: "sweep", text: qsTr("Sweep"), icon: "sweep" },
                            { id: "presets", text: qsTr("Presets"), icon: "layers" }
                        ]
                        onActivated: id => root.tool = root.tool === id ? "" : id
                    }
                }

                // Floating tool panel over the display, top right (left of the controls column).
                ToolPanel {
                    id: toolPanel
                    objectName: "genToolPanel"
                    visible: root.tool.length > 0
                    anchors.top: parent.top
                    anchors.right: controlsColumn.left
                    anchors.topMargin: Theme.spacingS
                    anchors.rightMargin: Theme.spacingS
                    width: Math.min(Theme.toolPanelWidth, Math.max(220, display.width - 2 * Theme.spacingS))
                    maximumHeight: parent.height - 2 * Theme.spacingS
                    title: root.tool === "sweep" ? qsTr("Frequency Sweep")
                           : root.tool === "presets" ? qsTr("Presets") : ""
                    onCloseRequested: root.tool = ""

                    GenSweepTool {
                        visible: root.tool === "sweep"
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
            } else if (scene === "arb") {
                // AO 1 plays a library waveform beside AO 0's sine (Arbitrary card, combined plot).
                root.controller.channel1.mode = GenChannel.Arbitrary
                root.controller.loadLibraryWaveform(1, 17)   // Stairstep
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
