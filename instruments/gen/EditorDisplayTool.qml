import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Display tool of the Waveform Editor (floating ToolPanel): View, X Axis, the Plot Style legend (plot visible, style,
// width, step interpolation, the full Trace style dialog, Copy data) and cursors.
ColumnLayout {
    id: root

    required property WaveformEditor editor
    signal traceStyleRequested()

    spacing: Theme.spacingS

    // View, X axis and plot style as segmented choices (Trace style… has every option).
    SegmentedControl {
        objectName: "editorViewChoice"
        Layout.fillWidth: true
        compact: true
        enabled: !root.editor.drawMode
        model: [ { text: qsTr("Waveform"), toolTip: qsTr("The whole waveform") },
                 { text: qsTr("Segment"), toolTip: qsTr("The selected segment alone") },
                 { text: qsTr("Component"), toolTip: qsTr("The selected component alone") } ]
        currentIndex: root.editor.view
        onActivated: index => root.editor.view = index
    }
    SegmentedControl {
        objectName: "editorAxisChoice"
        Layout.fillWidth: true
        compact: true
        enabled: !root.editor.drawMode
        model: [ { text: qsTr("Time"), toolTip: qsTr("Amplitude against time") },
                 { text: qsTr("Frequency"), toolTip: qsTr("Power spectrum of the view (dBVrms)") } ]
        currentIndex: root.editor.xAxis
        onActivated: index => root.editor.xAxis = index
    }
    SegmentedControl {
        objectName: "editorPlotStyleChoice"
        Layout.fillWidth: true
        compact: true
        model: [ { text: qsTr("Line") }, { text: qsTr("Line + Points") }, { text: qsTr("Points") } ]
        currentIndex: root.editor.plotStyle
        onActivated: index => root.editor.plotStyle = index
    }
    FieldRow {
        Layout.fillWidth: true
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
