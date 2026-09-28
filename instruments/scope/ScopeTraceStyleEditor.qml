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
    readonly property var widths: [0, 1, 1.5, 2, 3, 4, 5]   // 0 = none
    // ScopeTraceStyle.PointStyle order.
    readonly property var pointNames: [qsTr("None"), qsTr("Filled Circle"), qsTr("Circle"), qsTr("Filled Square"),
                                       qsTr("Square"), qsTr("Diamond"), qsTr("Filled Diamond"), qsTr("Cross"),
                                       qsTr("Plus"), qsTr("X")]

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
    Field {
        label: qsTr("Point / Fill Color")
        Layout.fillWidth: true
        ColorSwatches {
            Layout.fillWidth: true
            colorName: root.traceStyle.fillColor
            autoColor: root.autoColor
            dialogTitle: qsTr("Point / Fill Color")
            onPicked: name => root.traceStyle.fillColor = name
        }
    }
    FieldRow {
        Field {
            label: qsTr("Line Width")
            ScopeCombo {
                model: root.widths.map(w => w === 0 ? qsTr("None") : w + " px")
                currentIndex: Math.max(0, root.widths.indexOf(root.traceStyle.lineWidth))
                onActivated: index => root.traceStyle.lineWidth = root.widths[index]
            }
        }
        Field {
            label: qsTr("Line Style")
            ScopeCombo {
                model: [qsTr("Solid"), qsTr("Dash"), qsTr("Dot"), qsTr("Dash Dot"), qsTr("Dash Dot Dot")]
                currentIndex: root.traceStyle.lineStyle
                onActivated: index => root.traceStyle.lineStyle = index
            }
        }
    }
    FieldRow {
        Field {
            label: qsTr("Interpolation")
            ScopeCombo {
                model: [qsTr("None (points)"), qsTr("Linear"), qsTr("Step"), qsTr("Step (vertical first)"),
                        qsTr("Centered step"), qsTr("Centered step (vertical)")]
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
    Field {
        label: qsTr("Point Style")
        Layout.fillWidth: true
        ScopeCombo {
            model: root.pointNames
            currentIndex: root.traceStyle.pointStyle
            toolTip: qsTr("Marker at the samples (drawn when the samples are at least 3 px apart)")
            onActivated: index => root.traceStyle.pointStyle = index
        }
    }
    RowLayout {
        Layout.fillWidth: true
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
