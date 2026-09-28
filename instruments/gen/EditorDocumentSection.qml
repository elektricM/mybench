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
              .arg(Units.format(root.editor.totalDuration, "s", 4))
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        font.features: Theme.tabularFigures
    }
    FieldRow {
        Layout.fillWidth: true
        Field {
            label: qsTr("Sample Rate")
            SiField {
                Layout.fillWidth: true
                unit: "S/s"
                from: 1
                to: 1000000
                digits: 6
                toolTip: qsTr("Rendering rate of the document (1 S/s to 1 MS/s); durations are kept")
                value: root.editor.sampleRate
                onValueModified: v => root.editor.sampleRate = v
            }
        }
        Field {
            label: qsTr("Units")
            hint: qsTr("Display unit of the frequency parameters")
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
            onClicked: root.editor.useInChannel(0)
        }
        ActionButton {
            Layout.fillWidth: true
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
