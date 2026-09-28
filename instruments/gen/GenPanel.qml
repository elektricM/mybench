pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Waveform Generator panel (docs/instruments/gen.md): the ELVISmx Function Generator, Arbitrary
// Waveform Generator, Waveform Editor and DC Level in one panel. Display: a "Generator" tab (a
// readout and a live preview per channel) and a "Waveform Editor" tab. Inspector: the Output card
// and one card per channel, or the editor's cards while the editor tab is shown.
Item {
    id: root

    required property GenController controller
    readonly property bool editorTab: controller.activeTab === 1
    readonly property WaveformEditor editor: controller.editor

    InstrumentPanel {
        id: panel
        anchors.fill: parent
        controller: root.controller
        displayTitle: root.editorTab ? qsTr("Waveform Editor · %1").arg(root.editor.windowTitle) : ""
        displayInfo: root.controller.strategyText
        runBar.singleToolTip: root.controller.singleEnabled ? qsTr("Play the Arbitrary channels once, then stop")
                                                            : qsTr("Load a waveform into an Arbitrary channel to play it once")
        // The editor's trace in the panel menu and the plots' "Trace style…" (editor tab).
        traceStyles: root.editorTab ? [{ name: root.editor.documentName, style: root.editor.traceStyle,
                                         color: root.editor.plotColor.a > 0 ? root.editor.plotColor : Theme.accent }] : []
        runBar.runText: qsTr("Run")

        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.spacingS

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                SegmentedControl {
                    compact: true
                    model: [ { text: qsTr("Generator"), icon: "instrument-gen" },
                             { text: qsTr("Waveform Editor"), icon: "edit" } ]
                    currentIndex: root.controller.activeTab
                    onActivated: index => root.controller.activeTab = index
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: !root.editorTab && panel.width > 700
                    text: qsTr("Display")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeS
                }
                SegmentedControl {
                    visible: !root.editorTab
                    compact: true
                    model: [ { text: qsTr("Time"), toolTip: qsTr("Amplitude (V) against time") },
                             { text: qsTr("Frequency"), toolTip: qsTr("Spectrum of each output (dBV)") } ]
                    currentIndex: root.controller.displayMode
                    onActivated: index => root.controller.displayMode = index
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.controller.activeTab

                // --- Generator view -----------------------------------------------------------
                ColumnLayout {
                    id: generatorView
                    spacing: Theme.spacingS
                    readonly property bool stacked: width < 560
                    readonly property int offCount: (root.controller.channel0.mode === GenChannel.Off ? 1 : 0)
                                                    + (root.controller.channel1.mode === GenChannel.Off ? 1 : 0)

                    GridLayout {
                        Layout.fillWidth: true
                        columns: generatorView.stacked ? 1 : 2
                        columnSpacing: Theme.spacingS
                        rowSpacing: Theme.spacingS
                        ChannelReadout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            controller: root.controller
                            channel: root.controller.channel0
                            compact: generatorView.stacked
                        }
                        ChannelReadout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            controller: root.controller
                            channel: root.controller.channel1
                            compact: generatorView.stacked
                        }
                    }
                    ChannelPreview {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 40
                        Layout.preferredHeight: root.controller.channel0.mode === GenChannel.Off ? 40 : 300
                        Layout.maximumHeight: root.controller.channel0.mode === GenChannel.Off && generatorView.offCount < 2 ? 40 : Number.POSITIVE_INFINITY
                        controller: root.controller
                        channel: root.controller.channel0
                    }
                    ChannelPreview {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 40
                        Layout.preferredHeight: root.controller.channel1.mode === GenChannel.Off ? 40 : 300
                        Layout.maximumHeight: root.controller.channel1.mode === GenChannel.Off && generatorView.offCount < 2 ? 40 : Number.POSITIVE_INFINITY
                        controller: root.controller
                        channel: root.controller.channel1
                    }
                }

                // --- Waveform Editor view --------------------------------------------------------
                EditorView {
                    id: editorView
                    controller: root.controller
                    editor: root.editor
                }
            }
        }

        inspector: [
            Section {
                title: qsTr("Instrument Control")
                visible: !root.editorTab
                iconName: "sliders"
                summary: root.controller.outputPair === GenController.AudioOutputs ? qsTr("Audio out") : qsTr("AO 0 / AO 1")
                Field {
                    Layout.fillWidth: true
                    label: qsTr("Output Channel")
                    hint: qsTr("Both channels always use the same pair: the screw terminals AO 0 / AO 1 (±10 V) or the AUDIO OUT jack (±2 V, AC coupled)")
                    SegmentedControl {
                        Layout.fillWidth: true
                        compact: true
                        model: [ { text: qsTr("AO 0 / AO 1") }, { text: qsTr("AudioOutput L / R") } ]
                        currentIndex: root.controller.outputPair
                        onActivated: index => root.controller.outputPair = index
                    }
                }
                FieldRow {
                    Layout.fillWidth: true
                    Field {
                        label: qsTr("Engine")
                        hint: qsTr("Auto (default): a continuous stream: phase-continuous live changes, exact frequencies, linear interpolation, channels independent; DC-only outputs use on-demand writes. Regenerate: the ELVISmx looped buffer whenever it can represent the outputs (changes restart the buffer). Stream: always stream, DC included.")
                        ComboBox {
                            Layout.fillWidth: true
                            model: [qsTr("Auto"), qsTr("Regenerate"), qsTr("Stream")]
                            currentIndex: root.controller.engineMode
                            onActivated: index => root.controller.engineMode = index
                        }
                    }
                    Field {
                        label: qsTr("Presets")
                        ActionButton {
                            Layout.fillWidth: true
                            text: qsTr("Presets")
                            iconName: "layers"
                            compact: true
                            onClicked: presetMenu.popup()
                            Menu {
                                id: presetMenu
                                Repeater {
                                    model: root.controller.builtInPresets
                                    delegate: MenuItem {
                                        required property string modelData
                                        required property int index
                                        text: modelData
                                        onTriggered: root.controller.loadBuiltInPreset(index)
                                    }
                                }
                                MenuSeparator { visible: root.controller.presetNames.length > 0 }
                                Repeater {
                                    model: root.controller.presetNames
                                    delegate: MenuItem {
                                        required property string modelData
                                        text: modelData
                                        icon.source: Theme.icon("file")
                                        onTriggered: root.controller.loadPreset(modelData)
                                    }
                                }
                                MenuSeparator {}
                                MenuItem {
                                    text: qsTr("Save current settings as preset…")
                                    icon.source: Theme.icon("save")
                                    onTriggered: presetDialog.open()
                                }
                                Menu {
                                    title: qsTr("Delete preset")
                                    enabled: root.controller.presetNames.length > 0
                                    Repeater {
                                        model: root.controller.presetNames
                                        delegate: MenuItem {
                                            required property string modelData
                                            text: modelData
                                            onTriggered: root.controller.deletePreset(modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ToggleSwitch {
                        Layout.fillWidth: true
                        text: qsTr("Link update rates")
                        checked: root.controller.linkUpdateRates
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Both Arbitrary channels share one Update Rate (as ELVISmx)")
                        onToggled: root.controller.linkUpdateRates = checked
                    }
                    ActionButton {
                        text: qsTr("Align phases")
                        compact: true
                        tone: "ghost"
                        enabled: root.controller.strategy === GenController.Streaming
                        toolTip: qsTr("Restart both Function channels at phase 0 on the same sample (streaming engine)")
                        onClicked: root.controller.alignPhases()
                    }
                }
            },
            ChannelSection {
                visible: !root.editorTab
                controller: root.controller
                channel: root.controller.channel0
            },
            ChannelSection {
                visible: !root.editorTab
                controller: root.controller
                channel: root.controller.channel1
            },
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

        runBarControls: [
            ActionButton {
                text: panel.width < 640 ? "" : qsTr("Sweep")
                iconName: "sweep"
                compact: true
                enabled: root.controller.canSweep
                toolTip: qsTr("Sweep every stopped Function channel from Start to Stop Frequency")
                onClicked: root.controller.sweepAll()
            }
        ]
    }

    // Screenshot scenes (dev executable, MYBENCH_GEN_SCENE): open a dialog or state once loaded.
    Timer {
        interval: 300
        running: root.controller.devScene().length > 0
        onTriggered: editorView.showScene(root.controller.devScene())
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
