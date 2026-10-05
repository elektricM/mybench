pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Right-column card of one output (AO 0 / AO 1, or audio L / R): the header switch is the only
// per-channel output on/off (on an Off channel it brings the last waveform back); waveform picture
// buttons (Sine, Square, Triangle, DC, Arbitrary); then the parameters of the chosen waveform as
// knob fields. Arbitrary: the waveform (open a file, a library function, Open in Editor) and its
// timing, all in the card. An Off channel shows only its pictures (icons only), marked "Off".
// Values apply live while the channel generates. Sweep and presets live in the tool panels.
ChannelCard {
    id: root

    required property GenController controller
    required property GenChannel channel
    readonly property color channelColor: channel.index === 0 ? Theme.signalAo0 : Theme.signalAo1
    readonly property bool isFunction: channel.mode === GenChannel.Function
    readonly property bool isArbitrary: channel.mode === GenChannel.Arbitrary
    readonly property bool isDc: channel.mode === GenChannel.Dc
    readonly property bool isOff: channel.mode === GenChannel.Off
    readonly property real primaryKnob: compact ? 34 : 48

    objectName: "genChannelCard" + channel.index
    title: channel.name
    color: channelColor
    // The card stays open while the output is off (collapseWhenOff false), so the next output
    // can be set up first.
    subtitle: root.channel.errorText.length > 0 ? root.channel.errorText
              : root.channel.state === GenChannel.Done ? qsTr("Done")
              : root.channel.state === GenChannel.Paused ? qsTr("Paused")
              : root.channel.sweeping ? qsTr("Sweep %1/%2").arg(root.channel.sweepStepIndex).arg(root.channel.sweepStepCount)
              : root.isOff ? qsTr("Off")
              : ""   // the switch and the waveform pictures say the rest

    // The header switch is the channel's output on/off (ChannelCard's own switch re-binds after a
    // click, so a refused request leaves it showing the real state).
    checkable: true
    checked: root.channel.outputOn
    onToggled: on => root.controller.setOutput(root.channel.index, on)

    // --- Waveform pictures ---------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        columns: 5
        columnSpacing: 3
        rowSpacing: 3
        Repeater {
            model: [
                { id: "Sine", text: qsTr("Sine"), name: qsTr("Sine"), icon: "wave-sine", mode: GenChannel.Function, shape: GenChannel.Sine },
                { id: "Square", text: qsTr("Sq"), name: qsTr("Square"), icon: "wave-square", mode: GenChannel.Function, shape: GenChannel.Square },
                { id: "Triangle", text: qsTr("Tri"), name: qsTr("Triangle"), icon: "wave-triangle", mode: GenChannel.Function, shape: GenChannel.Triangle },
                { id: "DC", text: qsTr("DC"), name: qsTr("DC level"), icon: "wave-dc", mode: GenChannel.Dc, shape: -1 },
                { id: "Arbitrary", text: qsTr("Arb"), name: qsTr("Arbitrary waveform"), icon: "wave-arbitrary", mode: GenChannel.Arbitrary, shape: -1 }
            ]
            delegate: PictureButton {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitWidth: 40
                leftPadding: 2
                rightPadding: 2
                objectName: "wave" + modelData.id
                text: root.compact || root.isOff ? "" : modelData.text
                Accessible.name: modelData.name
                iconName: modelData.icon
                accentColor: root.channelColor
                enabled: !(modelData.mode === GenChannel.Dc && root.channel.audio) && !root.channel.sweeping
                checked: root.channel.mode === modelData.mode
                         && (modelData.shape < 0 || root.channel.shape === modelData.shape)
                ToolTip.visible: hovered
                ToolTip.text: modelData.mode === GenChannel.Dc && root.channel.audio
                              ? qsTr("Not available on the AC-coupled audio outputs")
                              : checked ? qsTr("%1 · click again to set the channel Off").arg(modelData.name) : modelData.name
                onClicked: {
                    // A second click on the picked waveform sets the channel Off: Run leaves it out.
                    if (checked) {
                        root.channel.mode = GenChannel.Off
                        return
                    }
                    if (modelData.shape >= 0)
                        root.channel.shape = modelData.shape
                    root.channel.mode = modelData.mode
                }
            }
        }
    }

    // --- Function ------------------------------------------------------------------------------
    KnobField {
        visible: root.isFunction
        Layout.fillWidth: true
        id: frequencyKnob
        objectName: "frequencyField"
        label: qsTr("Frequency")
        unit: "Hz"
        caption: Math.abs(root.channel.expectedFrequency - root.channel.frequency) > 1e-9 * root.channel.frequency
                 && root.channel.expectedFrequency > 0
                 ? qsTr("Generates %1").arg(Units.format(root.channel.expectedFrequency, "Hz", 5)) : ""
        accentColor: root.channelColor
        knobSize: root.primaryKnob
        compact: root.compact
        digits: 5
        from: 0.2
        to: 20000
        logarithmic: true
        enabled: !root.channel.sweeping
        value: root.channel.frequency
        onValueModified: v => root.channel.frequency = frequencyKnob.knob.dragging ? Number(v.toPrecision(3)) : v
    }
    KnobField {
        visible: root.isFunction
        Layout.fillWidth: true
        id: amplitudeKnob
        objectName: "amplitudeField"
        label: qsTr("Amplitude")
        unit: "Vpp"
        accentColor: root.channelColor
        knobSize: root.primaryKnob
        compact: root.compact
        digits: 3
        from: 0
        to: root.channel.audio ? 4 : 10
        stepSize: 0.1
        value: root.channel.amplitude
        onValueModified: v => root.channel.amplitude = amplitudeKnob.knob.dragging ? Math.round(v * 100) / 100 : v
    }
    KnobField {
        visible: root.isFunction
        Layout.fillWidth: true
        id: offsetKnob
        objectName: "offsetField"
        label: qsTr("Offset")
        unit: "V"
        accentColor: root.channelColor
        knobSize: root.primaryKnob
        compact: root.compact
        digits: 3
        from: -5
        to: 5
        stepSize: 0.1
        defaultValue: 0
        value: root.channel.offset
        onValueModified: v => root.channel.offset = offsetKnob.knob.dragging ? Math.round(v * 50) / 50 : v
    }
    KnobField {
        visible: root.isFunction && root.channel.shape === GenChannel.Square
        Layout.fillWidth: true
        id: dutyKnob
        objectName: "dutyField"
        label: qsTr("Duty Cycle")
        unit: "%"
        accentColor: root.channelColor
        knobSize: 34
        compact: root.compact
        digits: 3
        from: 0
        to: 100
        stepSize: 1
        defaultValue: 50
        value: root.channel.dutyCycle
        onValueModified: v => root.channel.dutyCycle = dutyKnob.knob.dragging ? Math.round(v) : v
    }
    KnobField {
        visible: root.isFunction
        Layout.fillWidth: true
        id: phaseKnob
        objectName: "phaseField"
        label: qsTr("Phase")
        unit: "°"
        accentColor: root.channelColor
        knobSize: 34
        compact: root.compact
        digits: 4
        from: -180
        to: 180
        stepSize: 15
        defaultValue: 0
        value: root.channel.phase
        onValueModified: v => root.channel.phase = phaseKnob.knob.dragging ? Math.round(v) : v
    }

    // --- DC --------------------------------------------------------------------------------------
    KnobField {
        visible: root.isDc
        Layout.fillWidth: true
        id: levelKnob
        objectName: "levelField"
        label: qsTr("Level")
        unit: "V"
        accentColor: root.channelColor
        knobSize: root.primaryKnob
        compact: root.compact
        digits: 3
        from: -root.channel.outputRange
        to: root.channel.outputRange
        stepSize: 0.1
        defaultValue: 0
        value: root.channel.dcLevel
        onValueModified: v => root.channel.dcLevel = levelKnob.knob.dragging ? Math.round(v * 100) / 100 : v
    }

    // --- Arbitrary: the waveform and its source -----------------------------------------------------
    RowLayout {
        visible: root.isArbitrary
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        Text {
            objectName: "waveformName"
            Layout.fillWidth: true
            text: root.channel.hasWaveform ? root.channel.waveformName : qsTr("No waveform loaded")
            color: root.channel.hasWaveform ? Theme.text : Theme.textSecondary
            elide: Text.ElideMiddle
            font.pixelSize: Theme.fontSizeS
            font.weight: root.channel.hasWaveform ? Font.DemiBold : Font.Normal
            ToolTip.visible: nameHover.hovered && root.channel.hasWaveform
            ToolTip.text: root.channel.waveformDetails
            HoverHandler { id: nameHover }
        }
        IconButton {
            objectName: "openWaveform"
            iconName: "folder-open"
            toolTip: qsTr("Open a waveform file…")
            onClicked: openDialog.open()
        }
        IconButton {
            objectName: "libraryWaveform"
            visible: root.controller.libraryNames.length > 0
            iconName: "book"
            toolTip: qsTr("Library waveforms")
            onClicked: libraryMenu.popup(this, 0, height)
            Menu {
                id: libraryMenu
                Instantiator {
                    model: root.controller.libraryNames
                    delegate: MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        onTriggered: root.controller.loadLibraryWaveform(root.channel.index, index)
                    }
                    onObjectAdded: (index, object) => libraryMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => libraryMenu.removeItem(object)
                }
                MenuSeparator {}
                MenuItem {
                    text: qsTr("Clear Waveform")
                    enabled: root.channel.hasWaveform
                    onTriggered: root.controller.clearWaveform(root.channel.index)
                }
            }
        }
    }
    Button {
        objectName: "openInEditor"
        visible: root.isArbitrary
        Layout.fillWidth: true
        text: qsTr("Open in Editor")
        icon.source: Theme.icon("edit")
        ToolTip.visible: hovered
        ToolTip.text: root.channel.hasWaveform ? qsTr("Edit this waveform in the Waveform Editor")
                                               : qsTr("Draw a new waveform in the Waveform Editor")
        onClicked: root.controller.editChannel(root.channel.index)
    }
    KnobField {
        visible: root.isArbitrary
        Layout.fillWidth: true
        objectName: "gainField"
        label: qsTr("Gain")
        accentColor: root.channelColor
        knobSize: 34
        compact: true
        digits: 3
        from: -10
        to: 10
        stepSize: 0.1
        defaultValue: 1
        value: root.channel.gain
        onValueModified: v => root.channel.gain = v
    }
    KnobField {
        visible: root.isArbitrary
        Layout.fillWidth: true
        objectName: "updateRateField"
        label: qsTr("Update Rate")
        unit: "S/s"
        accentColor: root.channelColor
        knobSize: 34
        compact: true
        digits: 4
        from: 0.1
        to: DeviceHub.maxAoRate > 0 ? DeviceHub.maxAoRate : 200000
        logarithmic: true
        value: root.channel.updateRate
        onValueModified: v => root.channel.updateRate = v
    }
    SegmentedControl {
        objectName: "generationMode"
        visible: root.isArbitrary
        Layout.fillWidth: true
        compact: true
        accentColor: root.channelColor
        model: [ { text: qsTr("Continuous"), toolTip: qsTr("The waveform repeats until the output is switched off") },
                 { text: qsTr("Once"), toolTip: qsTr("The waveform plays once, then holds its last sample") } ]
        currentIndex: root.channel.generationMode
        onActivated: index => root.channel.generationMode = index
    }
    SegmentedControl {
        objectName: "interpolation"
        visible: root.isArbitrary
        Layout.fillWidth: true
        compact: true
        accentColor: root.channelColor
        model: [ { text: qsTr("Hold"), toolTip: qsTr("Each sample is held until the next one") },
                 { text: qsTr("Linear"), toolTip: qsTr("Straight lines between the samples") } ]
        currentIndex: root.channel.interpolation
        onActivated: index => root.channel.interpolation = index
    }
    Switch {
        visible: root.isArbitrary && root.channel.generationMode === GenChannel.RunOnce
        Layout.fillWidth: true
        text: qsTr("Then 0 V")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("After playing once, return to 0 V instead of holding the last sample")
        checked: root.channel.zeroAfterRunOnce
        onToggled: root.channel.zeroAfterRunOnce = checked
    }
    // Both channels Arbitrary: one shared Update Rate (ELVISmx), shown on the second card.
    Switch {
        objectName: "linkRates"
        visible: root.isArbitrary && root.channel.index === 1 && root.controller.channel0.mode === GenChannel.Arbitrary
        Layout.fillWidth: true
        text: qsTr("Same rate as %1").arg(root.controller.channel0.name)
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Both Arbitrary channels share one Update Rate")
        checked: root.controller.linkUpdateRates
        onToggled: root.controller.linkUpdateRates = checked
    }
    Text {
        visible: root.isArbitrary && root.channel.clippedSamples > 0
        Layout.fillWidth: true
        text: qsTr("%1 samples clipped at ±%2 V").arg(root.channel.clippedSamples).arg(root.channel.outputRange)
        color: Theme.caution
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open waveform for %1").arg(root.channel.name)
        currentFolder: root.controller.lastWaveformFolder
        nameFilters: [qsTr("Waveforms (*.mbw *.wav *.csv *.txt *.bin *.wdt *.wfm)"), qsTr("All files (*)")]
        onAccepted: root.controller.loadWaveform(root.channel.index, selectedFile)
    }
}
