pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// "Graph Properties" (the ELVISmx dialog as an inspector card): Plots (per trace style and the
// graph background), Cursors (per cursor style, color and linked plot) and Measurements (which
// channel and cursor readouts are displayed). Changes apply immediately.
Section {
    id: root

    required property ScopeController controller
    property int plotIndex: 0
    property int cursorIndex: 0
    property int measurementChannel: 0

    readonly property ScopeCursor cursor: cursorIndex === 0 ? controller.cursor1 : controller.cursor2
    readonly property ScopeChannel measuredChannel: measurementChannel === 0 ? controller.channel0 : controller.channel1

    component MeasurementCheck: CheckBox {
        required property var modelData
        Layout.fillWidth: true
        text: modelData.name
        checked: (root.measuredChannel.measurementMask & modelData.flag) !== 0
        onToggled: root.measuredChannel.setShown(modelData.flag, checked)
    }
    Component {
        id: measurementCheck
        MeasurementCheck {}
    }
    Component {
        id: shortCheck
        MeasurementCheck {
            text: modelData.label
            ToolTip.visible: hovered
            ToolTip.text: modelData.name
        }
    }

    title: qsTr("Graph Properties")
    iconName: "sliders"
    collapsed: true

    TabBar {
        id: tabs
        Layout.fillWidth: true
        TabButton { text: qsTr("Plots"); width: implicitWidth + 8 }
        TabButton { text: qsTr("Cursors"); width: implicitWidth + 8 }
        TabButton { text: qsTr("Measurements"); width: implicitWidth + 8 }
    }

    StackLayout {
        id: stack
        Layout.fillWidth: true
        // Only as tall as the visible tab.
        Layout.preferredHeight: stack.children[stack.currentIndex] ? (stack.children[stack.currentIndex] as Item).implicitHeight : 0
        currentIndex: tabs.currentIndex

        // ---- Plots -------------------------------------------------------------------------------
        ColumnLayout {
            spacing: Theme.spacingS
            Field {
                label: qsTr("Plot")
                Layout.fillWidth: true
                SegmentedControl {
                    Layout.fillWidth: true
                    model: ["CH 0", "CH 1", qsTr("Math")]
                    currentIndex: root.plotIndex
                    onActivated: index => root.plotIndex = index
                }
            }
            ScopeTraceStyleEditor {
                Layout.fillWidth: true
                traceStyle: root.plotIndex === 0 ? root.controller.channel0.style
                          : root.plotIndex === 1 ? root.controller.channel1.style : root.controller.math.style
                autoColor: root.plotIndex === 0 ? Theme.ch0 : root.plotIndex === 1 ? Theme.ch1 : Theme.math
            }
            Divider { Layout.fillWidth: true }
            Field {
                label: qsTr("Background color")
                Layout.fillWidth: true
                SegmentedControl {
                    Layout.fillWidth: true
                    model: [qsTr("Theme"), qsTr("Black"), qsTr("White")]
                    currentIndex: root.controller.background
                    onActivated: index => root.controller.background = index
                }
            }
        }

        // ---- Cursors -----------------------------------------------------------------------------
        ColumnLayout {
            spacing: Theme.spacingS
            enabled: root.controller.cursorsOn
            Field {
                label: qsTr("Cursor")
                Layout.fillWidth: true
                SegmentedControl {
                    Layout.fillWidth: true
                    model: ["C1", "C2"]
                    currentIndex: root.cursorIndex
                    onActivated: index => root.cursorIndex = index
                }
            }
            Field {
                label: qsTr("Cursor color")
                Layout.fillWidth: true
                ColorSwatches {
                    Layout.fillWidth: true
                    colorName: root.cursor.color
                    autoColor: root.cursorIndex === 0 ? Theme.cursor1 : Theme.cursor2
                    onPicked: name => root.cursor.color = name
                }
            }
            FieldRow {
                Field {
                    label: qsTr("Line Style")
                    ScopeCombo {
                        model: [qsTr("Solid"), qsTr("Dash"), qsTr("Dot"), qsTr("Dash Dot")]
                        currentIndex: root.cursor.lineStyle
                        onActivated: index => root.cursor.lineStyle = index
                    }
                }
                Field {
                    label: qsTr("Cursor Style")
                    ScopeCombo {
                        model: [qsTr("Vertical Line"), qsTr("Full Crosshair")]
                        currentIndex: root.cursor.cursorStyle
                        onActivated: index => root.cursor.cursorStyle = index
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Field {
                    label: qsTr("Linked plot")
                    Layout.fillWidth: true
                    ScopeCombo {
                        model: ["CH 0", "CH 1", qsTr("Math")]
                        currentIndex: root.cursor.channel
                        onActivated: index => root.cursor.channel = index
                    }
                }
                ActionButton {
                    Layout.alignment: Qt.AlignBottom
                    text: qsTr("Reset to Default")
                    compact: true
                    tone: "ghost"
                    iconName: "reset"
                    onClicked: root.cursor.resetStyle()
                }
            }
            Text {
                visible: !root.controller.cursorsOn
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Turn Cursors On (Cursors Settings) to edit the cursors.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
            }
        }

        // ---- Measurements ------------------------------------------------------------------------
        ColumnLayout {
            spacing: Theme.spacingS
            Field {
                label: qsTr("Channel")
                Layout.fillWidth: true
                SegmentedControl {
                    Layout.fillWidth: true
                    model: ["CH 0", "CH 1"]
                    currentIndex: root.measurementChannel
                    onActivated: index => root.measurementChannel = index
                }
            }
            Text {
                text: qsTr("Channel measurements")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.weight: Font.DemiBold
            }
            Repeater {
                model: root.controller.measurementCatalog.filter(e => !e.extension)
                delegate: measurementCheck
            }
            Text {
                text: qsTr("More measurements")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.weight: Font.DemiBold
                topPadding: Theme.spacingXs
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 0
                columnSpacing: Theme.spacingS
                Repeater {
                    model: root.controller.measurementCatalog.filter(e => e.extension)
                    delegate: shortCheck
                }
            }
            Divider { Layout.fillWidth: true }
            Text {
                text: qsTr("Cursor measurements")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.weight: Font.DemiBold
            }
            ColumnLayout {
                spacing: 0
                enabled: root.controller.cursorsOn
                CheckBox {
                    text: qsTr("C1 Volts")
                    checked: root.controller.cursor1.showValue
                    onToggled: root.controller.cursor1.showValue = checked
                }
                CheckBox {
                    text: qsTr("C2 Volts")
                    checked: root.controller.cursor2.showValue
                    onToggled: root.controller.cursor2.showValue = checked
                }
                CheckBox {
                    text: qsTr("delta X-Value (dT, 1/dT)")
                    checked: root.controller.showDeltaTime
                    onToggled: root.controller.showDeltaTime = checked
                }
            }
        }
    }
}
