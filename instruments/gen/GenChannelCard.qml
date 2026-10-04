pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Right-column card of one output (AO 0 / AO 1, or audio L / R): the header switch is the only
// per-channel output on/off; waveform picture buttons (Sine, Square, Triangle, DC, Arbitrary);
// then the primary parameters of the chosen waveform as knob fields. Values apply live while the
// channel generates. Sweep, the arbitrary file details and presets live in the tool panels.
ChannelCard {
    id: root

    required property GenController controller
    required property GenChannel channel
    readonly property color channelColor: channel.index === 0 ? Theme.signalAo0 : Theme.signalAo1
    readonly property bool isFunction: channel.mode === GenChannel.Function
    readonly property bool isArbitrary: channel.mode === GenChannel.Arbitrary
    readonly property bool isDc: channel.mode === GenChannel.Dc
    readonly property real primaryKnob: compact ? 34 : 48

    signal arbitraryToolRequested()

    objectName: "genChannelCard" + channel.index
    title: channel.name
    color: channelColor
    // The card stays open while the output is off, so the next output can be set up first.
    checkable: false
    subtitle: root.channel.errorText.length > 0 ? root.channel.errorText
              : root.channel.state === GenChannel.Done ? qsTr("Done")
              : root.channel.state === GenChannel.Paused ? qsTr("Paused")
              : root.channel.sweeping ? qsTr("Sweep %1/%2").arg(root.channel.sweepStepIndex).arg(root.channel.sweepStepCount)
              : root.channel.mode !== GenChannel.Off && !root.channel.outputOn ? qsTr("0 V")
              : ""   // the switch and the waveform pictures say the rest

    headerExtras: [
        Switch {
            objectName: "outputSwitch"
            enabled: root.channel.mode !== GenChannel.Off
            checked: root.channel.outputOn
            Accessible.name: qsTr("%1 output").arg(root.channel.name)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Output of %1 on / off").arg(root.channel.name)
            onToggled: root.controller.setOutput(root.channel.index, checked)
        }
    ]

    // --- Waveform pictures ---------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        columns: 5
        columnSpacing: 3
        rowSpacing: 3
        Repeater {
            model: [
                { id: "Sine", text: qsTr("Sine"), name: qsTr("Sine"), icon: "wave-sine", mode: GenChannel.Function, shape: GenChannel.Sine },
                { id: "Square", text: qsTr("Square"), name: qsTr("Square"), icon: "wave-square", mode: GenChannel.Function, shape: GenChannel.Square },
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
                text: root.compact ? "" : modelData.text
                Accessible.name: modelData.name
                iconName: modelData.icon
                accentColor: root.channelColor
                checkable: false
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

    // --- Arbitrary (compact; file, library and timing details in the Arbitrary tool) --------------
    RowLayout {
        visible: root.isArbitrary
        Layout.fillWidth: true
        spacing: Theme.spacingS
        Text {
            Layout.fillWidth: true
            text: root.channel.hasWaveform ? root.channel.waveformName : qsTr("No waveform loaded")
            color: root.channel.hasWaveform ? Theme.text : Theme.textMuted
            elide: Text.ElideMiddle
            font.pixelSize: Theme.fontSizeS
        }
        Button {
            text: qsTr("Choose…")
            onClicked: root.arbitraryToolRequested()
        }
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
    Text {
        visible: root.isArbitrary && root.channel.clippedSamples > 0
        Layout.fillWidth: true
        text: qsTr("%1 samples exceed ±%2 V after gain and are clipped.").arg(root.channel.clippedSamples).arg(root.channel.outputRange)
        color: Theme.caution
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }
}
