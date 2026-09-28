pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Waveform Editor document card: Sample Rate, Units, totals, and the link to a channel
// ("Use in AO 0 / AO 1", live update).
Section {
    id: root

    required property GenController controller
    required property WaveformEditor editor

    title: qsTr("Waveform Editor")
    iconName: "edit"
    summary: editor.windowTitle

    Text {
        Layout.fillWidth: true
        text: root.editor.windowTitle
        color: Theme.text
        font.pixelSize: Theme.fontSizeL
        font.weight: Font.DemiBold
        elide: Text.ElideRight
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("%1 samples · %2 · %n segment(s)", "", root.editor.segmentCount)
              .arg(root.editor.totalSamples.toLocaleString(Qt.locale("en_US"), "f", 0))
              .arg(Units.format(root.editor.totalDuration, "s", 4, true))
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        font.features: Theme.tabularFigures
    }
    FieldRow {
        Layout.fillWidth: true
        Field {
            label: qsTr("Sample Rate")
            SiField {
                // Samples per second in the document's Units (1.0 ... 1 000 000.0, one decimal).
                Layout.fillWidth: true
                unit: root.editor.rateUnit
                engineering: false
                decimals: 1
                stepSize: 1000
                from: 1
                to: 1000000
                toolTip: qsTr("Rendering rate of the document, 1.0 to 1 000 000.0 %1; durations are kept, sample counts change").arg(root.editor.rateUnit)
                value: root.editor.sampleRate / root.editor.displayScale
                onValueModified: v => root.editor.sampleRate = v * root.editor.displayScale
            }
        }
        Field {
            label: qsTr("Units")
            hint: qsTr("Unit system of the document: Hz with seconds and S/s, kHz with milliseconds and kS/s, MHz with microseconds and MS/s. Switching keeps the numbers and changes their meaning (10 s becomes 10 ms), as in ELVISmx; undoable.")
            ComboBox {
                Layout.fillWidth: true
                model: ["Hz", "kHz", "MHz"]
                currentIndex: root.editor.units
                onActivated: index => root.editor.units = index
            }
        }
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
        text: root.editor.linkedChannel >= 0 ? qsTr("Live update of %1").arg(root.controller.channel(root.editor.linkedChannel).name)
                                             : qsTr("Live update (use in a channel first)")
        checked: root.editor.liveUpdate
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Every edit is sent to the linked channel, also while it plays")
        onToggled: root.editor.liveUpdate = checked
    }
}
