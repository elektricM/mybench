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
// panel over the display. The Waveform Editor view keeps its inspector sections.
Item {
    id: root

    required property GenController controller
    readonly property bool editorTab: controller.activeTab === 1
    readonly property WaveformEditor editor: controller.editor
    // Narrow pane (e.g. beside the Scope): knob fields without knobs, palette icons only.
    readonly property bool compact: panel.width < 760
    // Open tool panel ("" = none).
    property string tool: ""
    readonly property var channels: [controller.channel0, controller.channel1]

    function channelColor(index: int): color {
        return index === 0 ? Theme.signalAo0 : Theme.signalAo1
    }
    function shapeName(channel: GenChannel): string {
        switch (channel.shape) {
        case GenChannel.Square: return qsTr("square")
        case GenChannel.Triangle: return qsTr("triangle")
        default: return qsTr("sine")
        }
    }
    // "AO 0 · 1.000 kHz sine · 2.00 Vpp · 0 V offset"
    function readoutLine(channel: GenChannel): string {
        let parts = [channel.name]
        switch (channel.mode) {
        case GenChannel.Function:
            parts.push(Units.format(channel.frequency, "Hz", 4) + " " + shapeName(channel))
            parts.push(Units.fixed(channel.amplitude, "Vpp", 2))
            parts.push(qsTr("%1 offset").arg(Units.fixed(channel.offset, "V", 2)))
            if (channel.shape === GenChannel.Square)
                parts.push(qsTr("%1 % duty").arg(Math.round(channel.dutyCycle)))
            break
        case GenChannel.Dc:
            parts.push(qsTr("DC %1").arg(Units.fixed(channel.dcLevel, "V", 2)))
            break
        case GenChannel.Arbitrary:
            parts.push(channel.hasWaveform ? channel.waveformName : qsTr("no waveform"))
            parts.push(Units.format(channel.updateRate, "S/s", 4))
            parts.push(qsTr("gain %1").arg(channel.gain.toFixed(2)))
            break
        default:
            parts.push(qsTr("off"))
        }
        if (!channel.outputOn && channel.mode !== GenChannel.Off)
            parts.push(qsTr("output off"))
        return parts.join(" · ")
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
        // The generator view keeps every control beside its display; only the editor view uses
        // the inspector (its document, selection and display sections).
        hasInspector: root.editorTab
        displayTitle: root.editorTab ? qsTr("Waveform Editor · %1").arg(root.editor.windowTitle) : ""
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
            },
            Button {
                objectName: "backToGenerator"
                visible: root.editorTab
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: qsTr("Generator")
                icon.source: Theme.icon("chevron-left")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Back to the generator controls")
                onClicked: root.controller.activeTab = 0
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

                    // Display: one readout line and one preview per channel that is not Off.
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
                            delegate: RowLayout {
                                id: readoutRow
                                required property GenChannel modelData
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Rectangle {
                                    implicitWidth: 4
                                    implicitHeight: readoutText.implicitHeight
                                    radius: 2
                                    color: readoutRow.modelData.outputOn ? root.channelColor(readoutRow.modelData.index)
                                                                         : Theme.alpha(root.channelColor(readoutRow.modelData.index), 0.35)
                                }
                                Text {
                                    id: readoutText
                                    objectName: "readoutLine" + readoutRow.modelData.index
                                    Layout.fillWidth: true
                                    text: root.readoutLine(readoutRow.modelData)
                                    color: readoutRow.modelData.outputOn ? Theme.displayText : Theme.displayTextSecondary
                                    elide: Text.ElideRight
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                    font.pixelSize: root.compact ? Theme.fontSizeTitle : Theme.fontSizeReadoutM
                                    font.weight: Font.DemiBold
                                    font.features: { "tnum": 1 }
                                }
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
                        Layout.preferredWidth: root.compact ? 232 : 288
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
                    width: Math.min(340, Math.max(220, displayColumn.width - 2 * Theme.spacingS))
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
                controller: root.controller
                editor: root.editor
            }
        }

        inspector: [
            // The editor's shortcuts also apply while a field of its cards has the focus.
            EditorDocumentSection {
                visible: root.editorTab
                controller: root.controller
                editor: root.editor
                Keys.onShortcutOverride: event => event.accepted = editorView.claimsShortcut(event)
                Keys.onPressed: event => editorView.handleShortcut(event)
            },
            EditorSelectionSection {
                visible: root.editorTab
                editor: root.editor
                Keys.onShortcutOverride: event => event.accepted = editorView.claimsShortcut(event)
                Keys.onPressed: event => editorView.handleShortcut(event)
            },
            EditorDisplaySection {
                visible: root.editorTab
                editor: root.editor
                onTraceStyleRequested: editorView.openTraceStyle()
                Keys.onShortcutOverride: event => event.accepted = editorView.claimsShortcut(event)
                Keys.onPressed: event => editorView.handleShortcut(event)
            }
        ]
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
            } else
                editorView.showScene(scene)
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
