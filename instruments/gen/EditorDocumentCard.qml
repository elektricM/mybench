pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Waveform Editor document card (right column): name, points and duration, the sample rate as a
// knob field, the unit system, and the link to a channel ("Use in AO 0 / AO 1", live update).
ChannelCard {
    id: root

    required property GenController controller
    required property WaveformEditor editor

    objectName: "editorDocumentCard"
    title: editor.windowTitle
    checkable: false
    color: Theme.accent

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingS
        Readout {
            objectName: "documentPoints"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            size: "s"
            label: qsTr("Points")
            valueText: root.editor.totalSamples.toLocaleString(Qt.locale("en_US"), "f", 0)
        }
        Readout {
            objectName: "documentDuration"
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            size: "s"
            label: qsTr("Duration")
            value: root.editor.totalDuration
            unit: "s"
        }
    }
    KnobField {
        id: rateKnob
        objectName: "documentRateField"
        Layout.fillWidth: true
        label: qsTr("Sample Rate")
        // Samples per second in the document's Units (1.0 ... 1 000 000.0).
        unit: root.editor.rateUnit
        knobSize: 40
        compact: root.compact
        digits: 7
        from: 1
        to: 1000000
        logarithmic: true
        toolTip: qsTr("Rendering rate, 1.0 to 1 000 000.0 %1; durations are kept").arg(root.editor.rateUnit)
        value: root.editor.sampleRate / root.editor.displayScale
        onValueModified: v => root.editor.sampleRate = (rateKnob.knob.dragging ? Number(v.toPrecision(3)) : Math.round(v * 10) / 10)
                                                       * root.editor.displayScale
    }
    SegmentedControl {
        objectName: "documentUnits"
        Layout.fillWidth: true
        compact: true
        // Unit system: Hz · s · S/s, kHz · ms · kS/s, MHz · µs · MS/s. Switching keeps the numbers
        // and changes their meaning (10 s becomes 10 ms), as in ELVISmx; undoable.
        model: [ { text: "Hz", toolTip: qsTr("Hz · s · S/s") }, { text: "kHz", toolTip: qsTr("kHz · ms · kS/s") },
                 { text: "MHz", toolTip: qsTr("MHz · µs · MS/s") } ]
        currentIndex: root.editor.units
        onActivated: index => root.editor.units = index
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        Text {
            text: qsTr("Use in")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeS
        }
        ActionButton {
            Layout.fillWidth: true
            text: root.controller.channel0.name
            compact: true
            tone: "subtle"
            color: Theme.ch0
            toolTip: qsTr("Play this waveform on %1 (Arbitrary mode)").arg(root.controller.channel0.name)
            onClicked: root.editor.useInChannel(0)
        }
        ActionButton {
            Layout.fillWidth: true
            toolTip: qsTr("Play this waveform on %1 (Arbitrary mode)").arg(root.controller.channel1.name)
            text: root.controller.channel1.name
            compact: true
            tone: "subtle"
            color: Theme.ch1
            onClicked: root.editor.useInChannel(1)
        }
    }
    ToggleSwitch {
        Layout.fillWidth: true
        enabled: root.editor.linkedChannel >= 0
        text: root.editor.linkedChannel >= 0 ? qsTr("Live update · %1").arg(root.controller.channel(root.editor.linkedChannel).name)
                                             : qsTr("Live update")
        checked: root.editor.liveUpdate
        ToolTip.visible: hovered
        ToolTip.text: root.editor.linkedChannel >= 0 ? qsTr("Every edit is sent to the linked channel, also while it plays")
                                                     : qsTr("Use the waveform in a channel first")
        onToggled: root.editor.liveUpdate = checked
    }
}
