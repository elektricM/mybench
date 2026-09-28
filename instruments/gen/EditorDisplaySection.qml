import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Waveform Editor display settings: View, X Axis, plot style and cursors.
Section {
    id: root

    required property WaveformEditor editor

    title: qsTr("Display")
    iconName: "grid"
    summary: [qsTr("Waveform"), qsTr("Segment"), qsTr("Component")][editor.view] + " · " + [qsTr("Time"), qsTr("Frequency")][editor.xAxis]

    FieldRow {
        Layout.fillWidth: true
        Field {
            label: qsTr("View")
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("Waveform"), qsTr("Segment"), qsTr("Component")]
                enabled: !root.editor.drawMode
                currentIndex: root.editor.view
                onActivated: index => root.editor.view = index
            }
        }
        Field {
            label: qsTr("X Axis")
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("Time"), qsTr("Frequency")]
                enabled: !root.editor.drawMode
                currentIndex: root.editor.xAxis
                onActivated: index => root.editor.xAxis = index
            }
        }
    }
    FieldRow {
        Layout.fillWidth: true
        Field {
            label: qsTr("Plot Style")
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("Line"), qsTr("Line + Points"), qsTr("Points")]
                currentIndex: root.editor.plotStyle
                onActivated: index => root.editor.plotStyle = index
            }
        }
        Field {
            label: qsTr("Line Width")
            SiField {
                Layout.fillWidth: true
                engineering: false
                decimals: 1
                stepSize: 0.5
                from: 1
                to: 5
                unit: "px"
                value: root.editor.lineWidth
                onValueModified: v => root.editor.lineWidth = v
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        CheckBox {
            text: qsTr("Step interpolation")
            checked: root.editor.stepPlot
            onToggled: root.editor.stepPlot = checked
        }
        Item { Layout.fillWidth: true }
        ToggleSwitch {
            text: qsTr("Cursors")
            checked: root.editor.showCursors
            onToggled: root.editor.showCursors = checked
        }
    }
}
