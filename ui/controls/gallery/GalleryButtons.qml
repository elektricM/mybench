pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

GalleryPage {
    id: page

    title: qsTr("Buttons and Inputs")
    description: qsTr("Desktop metrics from the body line height: small %1 px (plot toolbars, dense tables), regular %2 px, large %3 px (Run and dialog buttons only). Radius %4 px on controls.")
                 .arg(Theme.controlHeightS).arg(Theme.controlHeight).arg(Theme.controlHeightL).arg(Theme.radiusControl)

    property real frequency: 1000
    property real level: 0.25
    property int timeIndex: 9
    readonly property var timeSteps: [5e-6, 1e-5, 2e-5, 5e-5, 1e-4, 2e-4, 5e-4, 1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 0.1, 0.2]

    GallerySpec {
        title: qsTr("Buttons")
        note: qsTr("One primary (accent) button per view. Push buttons use the palette's button color with a hairline; flat buttons show a fill only on hover. Never green or red fills.")
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingS
            Button { text: qsTr("Run"); highlighted: true; icon.source: Theme.icon("play"); implicitHeight: Theme.controlHeightL }
            Button { text: qsTr("Single") }
            Button { text: qsTr("Autoscale"); icon.source: Theme.icon("autoscale") }
            Button { text: qsTr("Export…") }
            Button { text: qsTr("Flat"); flat: true }
            Button { text: qsTr("Checked"); checkable: true; checked: true }
            Button { text: qsTr("Disabled"); enabled: false }
            Button { text: qsTr("Primary disabled"); highlighted: true; enabled: false }
            Button { icon.source: Theme.icon("settings"); display: AbstractButton.IconOnly; ToolTip.visible: hovered; ToolTip.text: qsTr("Settings") }
        }
        Row {
            spacing: Theme.spacingXs
            ToolButton { icon.source: Theme.icon("pointer"); checkable: true; checked: true }
            ToolButton { icon.source: Theme.icon("zoom-box") }
            ToolButton { icon.source: Theme.icon("hand") }
            ToolSeparator {}
            ToolButton { icon.source: Theme.icon("autoscale") }
            ToolButton { icon.source: Theme.icon("more-horizontal") }
            ToolSeparator {}
            ToolButton { text: qsTr("Stop All") }
            ToolButton { icon.source: Theme.icon("sidebar-right"); enabled: false }
        }
    }

    GallerySpec {
        title: qsTr("Menus and lists")
        note: qsTr("Menus and context menus are native; this in-window rendering is used in screenshots and tests. The pop-up list of a combo box marks the current entry.")
        Item {
            Layout.fillWidth: true
            implicitHeight: 250
            Menu {
                id: menuPreview
                x: 0
                y: 0
                closePolicy: Popup.NoAutoClose
                MenuItem { text: qsTr("Autoscale"); icon.source: Theme.icon("autoscale") }
                MenuItem { text: qsTr("Reset View") }
                MenuItem { text: qsTr("Cursors"); checkable: true; checked: true }
                Menu {
                    title: qsTr("Time Window")
                    MenuItem { text: qsTr("10 s") }
                }
                MenuSeparator {}
                Action { text: qsTr("Copy Image"); shortcut: "Ctrl+Shift+C" }
                MenuItem { text: qsTr("Export Data…"); enabled: false }
            }
            ComboBox {
                id: comboPreview
                x: 260
                width: 180
                model: ["5 mV", "10 mV", "20 mV", "50 mV", "100 mV", "200 mV"]
                currentIndex: 2
                popup.closePolicy: Popup.NoAutoClose
            }
            Component.onCompleted: Qt.callLater(() => {
                menuPreview.open()
                comboPreview.popup.open()
            })
        }
    }

    GallerySpec {
        title: qsTr("Choices")
        note: qsTr("2–4 short options: a segmented control sized to its content. More options: a pop-up button (press-drag-release, the wheel only when focused or at the start of a gesture). On/off: a switch in forms and section headers, check boxes in lists and dialogs.")
        RowLayout {
            spacing: Theme.spacingL
            SegmentedControl { model: [qsTr("Immediate"), qsTr("Edge")]; currentIndex: 1 }
            SegmentedControl { model: ["AC", "DC"]; currentIndex: 0 }
            SegmentedControl {
                model: [{ icon: "wave-sine", toolTip: qsTr("Sine") }, { icon: "wave-square", toolTip: qsTr("Square") },
                        { icon: "wave-triangle", toolTip: qsTr("Triangle") }]
                currentIndex: 0
            }
            SegmentedControl { model: [qsTr("Auto"), qsTr("Normal"), qsTr("Single")]; currentIndex: 2; compact: true }
            SegmentedControl { model: [qsTr("On"), qsTr("Off")]; enabled: false }
        }
        RowLayout {
            spacing: Theme.spacingL
            ComboBox { model: ["AI 0", "AI 1", "Audio Input Left", "Audio Input Right"] }
            ComboBox { model: [qsTr("Rising"), qsTr("Falling")]; enabled: false }
            ComboBox { editable: true; model: ["1 kHz", "10 kHz"] }
            Switch { checked: true }
            Switch { text: qsTr("Roll mode") }
            Switch { text: qsTr("Disabled"); enabled: false; checked: true }
        }
        RowLayout {
            spacing: Theme.spacingL
            CheckBox { text: qsTr("Show grid"); checked: true }
            CheckBox { text: qsTr("Partially"); tristate: true; checkState: Qt.PartiallyChecked }
            CheckBox { text: qsTr("Unchecked") }
            RadioButton { text: qsTr("Portrait"); checked: true }
            RadioButton { text: qsTr("Landscape") }
            CheckBox { text: qsTr("Disabled"); enabled: false; checked: true }
        }
    }

    GallerySpec {
        title: qsTr("Numbers")
        note: qsTr("SiField takes typed SI values (\"1.5k\"); ↑/↓ tune the digit under the caret, the steppers step, dragging the row label scrubs. Invalid input is outlined and explained; a clamped value is announced. 1-2-5 sequences use the step selector: ‹ value ▾ ›, the wheel, or the list.")
        RowLayout {
            spacing: Theme.spacingL
            SiField {
                Layout.preferredWidth: 140
                label: qsTr("Frequency")
                unit: "Hz"
                from: 0.2
                to: 20000
                value: page.frequency
                onValueModified: v => page.frequency = v
            }
            SiField {
                Layout.preferredWidth: 140
                label: qsTr("Level")
                unit: "V"
                from: -10
                to: 10
                value: page.level
                steppers: false
                onValueModified: v => page.level = v
            }
            SiField { Layout.preferredWidth: 140; unit: "s"; value: 0.005; enabled: false }
            SpinBox { from: 1; to: 64; value: 8 }
            StepSelector {
                Layout.preferredWidth: 150
                model: page.timeSteps
                unit: "s"
                currentIndex: page.timeIndex
                onActivated: index => page.timeIndex = index
            }
        }
        RowLayout {
            spacing: Theme.spacingL
            TextField { placeholderText: qsTr("Session name") }
            TextField { text: qsTr("Bench 3 — op-amp lab") }
            TextField { text: qsTr("Read only"); readOnly: true }
            TextField { placeholderText: qsTr("Disabled"); enabled: false }
            Slider { from: 0; to: 1; value: 0.35 }
        }
        RowLayout {
            Layout.topMargin: Theme.spacingXl + Theme.spacingS
            spacing: Theme.spacingM
            Text {
                text: qsTr("Invalid entry (outline and message):")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            SiField {
                id: invalidField
                Layout.preferredWidth: 140
                unit: "V"
                value: 1
                Component.onCompleted: {
                    text = "abc"
                    dirty = true
                }
            }
        }
    }

    GallerySpec {
        title: qsTr("Tabs, progress")
        RowLayout {
            spacing: Theme.spacingXl
            TabBar {
                Layout.preferredWidth: 340
                TabButton { text: qsTr("Spectrum") }
                TabButton { text: qsTr("Octave") }
                TabButton { text: qsTr("Spectrogram") }
            }
            ColumnLayout {
                spacing: Theme.spacingS
                ProgressBar { value: 0.62 }
                ProgressBar { indeterminate: true }
            }
            BusyIndicator { running: true }
            Row {
                spacing: Theme.spacingXs
                KeyHint { keys: "Ctrl+K" }
                KeyHint { keys: "Space" }
                KeyHint { keys: "Ctrl+Shift+R" }
            }
        }
    }

}
