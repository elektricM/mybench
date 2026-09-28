pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Graph Properties > Plots for one trace: line color, width, style, point style, interpolation
// and fill.
ColumnLayout {
    id: root

    required property ScopeTraceStyle traceStyle
    property color autoColor: Theme.ch0
    readonly property var widths: [1, 1.5, 2, 3, 4, 5]

    spacing: Theme.spacingS

    Field {
        label: qsTr("Line Color")
        Layout.fillWidth: true
        ColorSwatches {
            Layout.fillWidth: true
            colorName: root.traceStyle.lineColor
            autoColor: root.autoColor
            onPicked: name => root.traceStyle.lineColor = name
        }
    }
    FieldRow {
        Field {
            label: qsTr("Line Width")
            ScopeCombo {
                model: root.widths.map(w => w + " px")
                currentIndex: Math.max(0, root.widths.indexOf(root.traceStyle.lineWidth))
                onActivated: index => root.traceStyle.lineWidth = root.widths[index]
            }
        }
        Field {
            label: qsTr("Line Style")
            ScopeCombo {
                model: [qsTr("Solid"), qsTr("Dash"), qsTr("Dot"), qsTr("Dash Dot")]
                currentIndex: root.traceStyle.lineStyle
                onActivated: index => root.traceStyle.lineStyle = index
            }
        }
    }
    FieldRow {
        Field {
            label: qsTr("Interpolation")
            ScopeCombo {
                model: [qsTr("None (points)"), qsTr("Linear"), qsTr("Step")]
                currentIndex: root.traceStyle.interpolation
                onActivated: index => root.traceStyle.interpolation = index
            }
        }
        Field {
            label: qsTr("Fill to")
            ScopeCombo {
                model: [qsTr("<none>"), qsTr("Zero"), qsTr("-Infinity"), qsTr("+Infinity")]
                currentIndex: root.traceStyle.fillTo
                onActivated: index => root.traceStyle.fillTo = index
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        CheckBox {
            text: qsTr("Show points")
            checked: root.traceStyle.points
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Dots at the samples (drawn when the samples are at least 3 px apart)")
            onToggled: root.traceStyle.points = checked
        }
        Item { Layout.fillWidth: true }
        ActionButton {
            text: qsTr("Reset to Default")
            compact: true
            tone: "ghost"
            iconName: "reset"
            onClicked: root.traceStyle.reset()
        }
    }
}
