import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Scale tool of the Waveform Editor (floating ToolPanel): stretches the selection in time
// (durations × factor, frequencies ÷ factor) and scales its amplitude. Applies to the selected
// segment, component or the whole waveform (Select All).
ColumnLayout {
    id: root

    required property WaveformEditor editor

    // Back to 1 × 1 whenever the tool is shown again.
    function reset() {
        timeFactor.value = 1
        amplitudeFactor.value = 1
    }

    spacing: Theme.spacingS
    onVisibleChanged: if (visible) reset()

    KnobField {
        id: timeFactor
        objectName: "scaleTimeField"
        Layout.fillWidth: true
        label: qsTr("Time ×")
        knobSize: 34
        digits: 4
        from: 0.0001
        to: 10000
        logarithmic: true
        value: 1
        onValueModified: v => value = v
    }
    KnobField {
        id: amplitudeFactor
        objectName: "scaleAmplitudeField"
        Layout.fillWidth: true
        label: qsTr("Amplitude ×")
        knobSize: 34
        digits: 4
        from: -10000
        to: 10000
        stepSize: 0.1
        value: 1
        onValueModified: v => value = v
    }
    Button {
        objectName: "scaleApply"
        Layout.fillWidth: true
        text: qsTr("Scale Selection")
        highlighted: true
        onClicked: {
            root.editor.scaleSelection(timeFactor.value, amplitudeFactor.value)
            root.reset()
        }
    }
}
