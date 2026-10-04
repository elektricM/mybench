pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

GalleryPage {
    id: page

    title: qsTr("Inspector and Forms")
    description: qsTr("The inspector is a flat column of disclosure sections separated by hairlines: no cards, no backgrounds. Each row is a label column (40 %, 88–120 px) and the control; a control too wide for its column takes the full width under its label.")

    property bool ch0: true
    property bool ch1: false
    property int source: 0
    property int scaleIndex: 7
    property real position: 0
    property int timeIndex: 9
    property int trigger: 1
    property real level: 0.5
    readonly property var scales: [0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5]
    readonly property var times: [5e-6, 1e-5, 2e-5, 5e-5, 1e-4, 2e-4, 5e-4, 1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 0.1, 0.2]

    component SampleInspector: Inspector {
        id: inspector
        Section {
            sectionId: "channel0"
            title: qsTr("Channel 0")
            swatchColor: Theme.signalCh0
            summary: "AI 0 · " + Units.format(page.scales[page.scaleIndex], "V", 3, true) + "/div"
            trailing: Switch {
                checked: page.ch0
                onToggled: page.ch0 = checked
                Accessible.name: qsTr("Channel 0 Enabled")
            }
            FormRow {
                label: qsTr("Source")
                ComboBox {
                    Layout.fillWidth: true
                    model: ["AI 0", "AI 1", "Audio Input Left", "Audio Input Right"]
                    currentIndex: page.source
                    onActivated: index => page.source = index
                }
            }
            FormRow {
                label: qsTr("Scale")
                StepSelector {
                    Layout.fillWidth: true
                    model: page.scales
                    unit: "V"
                    currentIndex: page.scaleIndex
                    onActivated: index => page.scaleIndex = index
                }
            }
            FormRow {
                label: qsTr("Position")
                trailing: qsTr("div")
                SiField {
                    Layout.fillWidth: true
                    engineering: false
                    decimals: 2
                    from: -5
                    to: 5
                    stepSize: 0.1
                    value: page.position
                    onValueModified: v => page.position = v
                }
            }
            FormRow {
                label: qsTr("Coupling")
                disabled: true
                hint: qsTr("The myDAQ inputs are DC coupled")
                SegmentedControl { model: ["DC", "AC"]; enabled: false }
            }
        }
        Section {
            sectionId: "channel1"
            title: qsTr("Channel 1")
            swatchColor: Theme.signalCh1
            summary: page.ch1 ? "AI 1 · 1 V/div" : qsTr("Off")
            collapsed: true
            trailing: Switch { checked: page.ch1; onToggled: page.ch1 = checked }
        }
        Section {
            sectionId: "timebase"
            title: qsTr("Timebase")
            summary: Units.format(page.times[page.timeIndex], "s", 3, true) + "/div"
            FormRow {
                label: qsTr("Time/div")
                StepSelector {
                    Layout.fillWidth: true
                    model: page.times
                    unit: "s"
                    currentIndex: page.timeIndex
                    onActivated: index => page.timeIndex = index
                }
            }
            FormRow {
                label: qsTr("Sample rate")
                Text {
                    Layout.fillWidth: true
                    text: "200 kS/s · 10 000 samples"
                    elide: Text.ElideRight
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.features: Theme.tabularFigures
                }
            }
            FormRow {
                label: qsTr("Roll mode")
                Switch {}
            }
        }
        Section {
            sectionId: "trigger"
            title: qsTr("Trigger")
            summary: page.trigger === 0 ? qsTr("Immediate") : qsTr("Edge ↑ CH0 %1").arg(Units.format(page.level, "V", 3, true))
            FormRow {
                label: qsTr("Type")
                SegmentedControl {
                    model: [qsTr("Immediate"), qsTr("Edge")]
                    currentIndex: page.trigger
                    onActivated: index => page.trigger = index
                }
            }
            FormRow {
                visible: page.trigger === 1
                label: qsTr("Source")
                ComboBox { Layout.fillWidth: true; model: ["CH 0", "CH 1"] }
            }
            FormRow {
                visible: page.trigger === 1
                label: qsTr("Slope")
                SegmentedControl {
                    model: [{ icon: "slope-rising", toolTip: qsTr("Rising") }, { icon: "slope-falling", toolTip: qsTr("Falling") }]
                }
            }
            FormRow {
                visible: page.trigger === 1
                label: qsTr("Level")
                SiField {
                    Layout.fillWidth: true
                    unit: "V"
                    from: -10
                    to: 10
                    value: page.level
                    onValueModified: v => page.level = v
                }
            }
            FormRow {
                visible: page.trigger === 1
                label: qsTr("Mode")
                SegmentedControl { model: [qsTr("Auto"), qsTr("Normal"), qsTr("Single shot")] }
            }
        }
        Section {
            sectionId: "math"
            title: qsTr("Math")
            summary: qsTr("Off")
            collapsed: true
        }
        Section {
            sectionId: "cursors"
            title: qsTr("Cursors")
            summary: qsTr("Off")
            collapsed: true
        }
        Section {
            sectionId: "display"
            title: qsTr("Display")
            collapsed: true
        }
    }

    GallerySpec {
        title: qsTr("Inspector at 300, 260 and 420 px")
        note: qsTr("The host draws the window color behind the inspector; it keeps its scroll position when moved between hosts.")
        RowLayout {
            spacing: Theme.spacingXl
            Repeater {
                model: [Theme.inspectorWidth, Theme.inspectorMinWidth, Theme.inspectorMaxWidth]
                delegate: Rectangle {
                    id: host
                    required property int modelData
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: modelData
                    implicitHeight: 600
                    color: Theme.window
                    border.width: Theme.borderWidth
                    border.color: Theme.separator
                    SampleInspector {
                        anchors.fill: parent
                        anchors.margins: Theme.borderWidth
                    }
                }
            }
        }
    }

    GallerySpec {
        title: qsTr("Form rows")
        note: qsTr("Labels are sentence case in secondary text; a unit or range note follows the label; \"top\" puts the label above wide controls such as lists.")
        ColumnLayout {
            Layout.preferredWidth: 420
            spacing: Theme.formRowSpacing
            FormRow {
                label: qsTr("Frequency")
                trailing: "0.2 Hz – 20 kHz"
                SiField { Layout.fillWidth: true; unit: "Hz"; value: 1000; from: 0.2; to: 20000 }
            }
            FormRow {
                label: qsTr("A long label that wraps onto two lines")
                ComboBox { Layout.fillWidth: true; model: [qsTr("Continuous")] }
            }
            FormRow {
                label: qsTr("Log file")
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "~/Documents/MYBENCH/bench-3.csv"
                        elide: Text.ElideMiddle
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                    }
                    Button { text: qsTr("Choose…") }
                }
            }
            FormRow {
                label: qsTr("Bands")
                labelPosition: "top"
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 60
                    color: Theme.base
                    radius: Theme.radiusControl
                    border.width: Theme.borderWidth
                    border.color: Theme.separator
                }
            }
        }
    }
}
