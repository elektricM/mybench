import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Waveform Editor display settings: View, X Axis, the Plot Style legend (plot visible, style,
// width, step interpolation, the full Trace style dialog, Copy data) and cursors.
Section {
    id: root

    required property WaveformEditor editor
    signal traceStyleRequested()

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
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Whole waveform, the selected segment, or the selected component alone")
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
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Time, or the power spectrum of the view (dBVrms)")
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
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Common plot styles; Trace style… has every option")
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
                toolTip: qsTr("Line width")
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
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Hold each sample until the next one")
            onToggled: root.editor.stepPlot = checked
        }
        Item { Layout.fillWidth: true }
        ToggleSwitch {
            text: qsTr("Cursors")
            checked: root.editor.showCursors
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Two cursors with ΔX, ΔY and 1/ΔX below the plot")
            onToggled: root.editor.showCursors = checked
        }
    }
    CheckBox {
        text: qsTr("Plot visible")
        checked: root.editor.plotVisible
        onToggled: root.editor.plotVisible = checked
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingXs
        ActionButton {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("Trace style…")
            compact: true
            toolTip: qsTr("Color, line style and width, points, interpolation and fill of the trace")
            onClicked: root.traceStyleRequested()
        }
        ActionButton {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("Copy data")
            iconName: "copy"
            compact: true
            toolTip: qsTr("Copy the rendered waveform to the clipboard (Time and Amplitude, tab separated)")
            onClicked: root.editor.copyData()
        }
    }
}
