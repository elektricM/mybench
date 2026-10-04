pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Instrument controls (PicoScope style): knobs with typeable values, channel cards, tool
// palette and panel, picture buttons, measurement cards, the Run button.
GalleryPage {
    id: page

    title: qsTr("Instrument Controls")
    description: qsTr("Primary controls stay visible on the right of the display: a knob per main parameter, paired with a typeable value and − / +. Channel cards carry the channel color; tools open floating panels; measurements are cards with statistics.")

    property bool running: true
    property int timeIndex: 9
    property real level: 0.25
    property int scale0: 6
    property int scale1: 4
    property real position0: 0
    property real position1: -1
    property bool ch0: true
    property bool ch1: true
    property bool math: false
    property string tool: "measure"
    property var picks: ["rms", "freq", "vpp"]
    property real frequency: 1000
    readonly property var timeSteps: [5e-6, 1e-5, 2e-5, 5e-5, 1e-4, 2e-4, 5e-4, 1e-3, 2e-3, 5e-3, 1e-2, 2e-2, 5e-2, 0.1, 0.2]
    readonly property var voltSteps: [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5]
    readonly property var measureTypes: [
        { id: "rms", text: qsTr("RMS"), glyph: "M3 9c3-7 5-7 8 0s5 7 8 0s5-7 8 0" },
        { id: "freq", text: qsTr("Frequency"), glyph: "M3 14h5V4h7v10h7V4h7" },
        { id: "vpp", text: qsTr("Peak to peak"), glyph: "M3 3h26M3 15h26M16 4v10" },
        { id: "peak", text: qsTr("Peak"), glyph: "M3 15h26M3 3h26M8 15c2-10 4-12 8-12" },
        { id: "mean", text: qsTr("Mean"), glyph: "M3 9h26M3 9c3-6 5-6 8 0s5 6 8 0s5-6 8 0" },
        { id: "period", text: qsTr("Period"), glyph: "M5 3v12M27 3v12M5 9h22" },
        { id: "rise", text: qsTr("Rise time"), glyph: "M3 15h8l10-12h8" },
        { id: "duty", text: qsTr("Duty cycle"), glyph: "M3 15h4V3h9v12h4V3h9" },
        { id: "max", text: qsTr("Maximum"), glyph: "M3 3h26M3 14c4-8 8-8 12 0s8 8 12 0" }
    ]

    GallerySpec {
        title: qsTr("Scope pane (composition)")
        note: qsTr("Controls on the right of the display. Drag a knob vertically or around its ring, scroll it, or use the arrow keys; click a value to type 2m, 500u, 1.5k; − / + step. Double-click a knob to reset it.")

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 880
            color: Theme.window
            border.width: 1
            border.color: Theme.separator
            radius: Theme.radiusPopup

            // Display (placeholder) with the open tool panel floating over it.
            Rectangle {
                id: display
                x: 1
                y: 1
                width: parent.width - controls.width - 2
                height: parent.height - measurements.height - 2
                color: Theme.display
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Display")
                    color: Theme.displayTextSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
                ToolPanel {
                    id: panel
                    visible: page.tool.length > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingS
                    maximumHeight: parent.height - 2 * Theme.spacingS
                    title: page.tool === "measure" ? qsTr("Measurements") : page.tool === "cursors" ? qsTr("Cursors") : qsTr("Tool")
                    onCloseRequested: page.tool = ""

                    Text {
                        text: qsTr("Add or remove")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 6
                        rowSpacing: 6
                        Repeater {
                            model: page.measureTypes
                            delegate: PictureButton {
                                required property var modelData
                                Layout.fillWidth: true
                                text: modelData.text
                                glyph: modelData.glyph
                                checked: page.picks.indexOf(modelData.id) >= 0
                                onClicked: {
                                    const list = page.picks.filter(p => p !== modelData.id)
                                    if (checked)
                                        list.push(modelData.id)
                                    page.picks = list
                                }
                            }
                        }
                    }
                    Text {
                        text: qsTr("Source")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                    SegmentedControl {
                        Layout.fillWidth: true
                        model: ["CH 0", "CH 1", "Math"]
                        currentIndex: 0
                    }
                    CheckBox { text: qsTr("Statistics"); checked: true }
                }
            }

            // Measurement cards.
            Rectangle {
                id: measurements
                x: 1
                y: parent.height - height - 1
                width: display.width
                height: 82
                color: Theme.window
                Rectangle { width: parent.width; height: 1; color: Theme.separator }
                Row {
                    x: Theme.spacingS + 2
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingS
                    MeasurementCard {
                        title: "CH 0 · RMS"; color: Theme.signalCh0; value: "707.1 mV"
                        stats: ({ min: "706.4", max: "708.0", mean: "707.2", stdDev: "0.31" })
                    }
                    MeasurementCard {
                        title: "CH 0 · Frequency"; color: Theme.signalCh0; value: "1.000 kHz"
                        stats: ({ min: "999.8", max: "1.000k", mean: "1.000k", stdDev: "0.05" })
                    }
                    MeasurementCard {
                        title: "CH 1 · Peak"; color: Theme.signalCh1; value: "1.001 V"
                    }
                }
            }

            // Controls on the right.
            Rectangle {
                id: controls
                x: parent.width - width - 1
                y: 1
                width: 300
                height: parent.height - 2
                color: Theme.window
                Rectangle { width: 1; height: parent.height; color: Theme.separator }

                Flickable {
                    anchors.fill: parent
                    anchors.leftMargin: 1
                    contentHeight: controlColumn.implicitHeight + 2 * Theme.spacingS + 2
                    clip: true
                    ColumnLayout {
                        id: controlColumn
                        x: Theme.spacingS + 2
                        y: Theme.spacingS + 2
                        width: parent.width - 2 * x
                        spacing: Theme.spacingS

                        RowLayout {
                            spacing: Theme.spacingS
                            RunButton {
                                running: page.running
                                stateText: page.running ? qsTr("Triggered") : ""
                                onToggled: page.running = !page.running
                            }
                            ColumnLayout {
                                spacing: Theme.spacingXs
                                Button { text: qsTr("Single"); Layout.fillWidth: true }
                                Button { text: qsTr("Autoscale"); Layout.fillWidth: true }
                            }
                        }

                        ChannelCard {
                            Layout.fillWidth: true
                            title: qsTr("Timebase")
                            checkable: false
                            color: Theme.accent
                            KnobField {
                                Layout.fillWidth: true
                                label: qsTr("Time/Div")
                                unit: "s"
                                values: page.timeSteps
                                currentIndex: page.timeIndex
                                defaultIndex: 9
                                formatter: v => Units.format(v, "s", 3) + "/div"
                                caption: qsTr("200 kS/s · 1 001 samples")
                                onIndexActivated: index => page.timeIndex = index
                            }
                        }

                        ChannelCard {
                            Layout.fillWidth: true
                            title: qsTr("Trigger")
                            subtitle: "CH 0 ↑"
                            checkable: false
                            color: Theme.signalCh0
                            KnobField {
                                Layout.fillWidth: true
                                label: qsTr("Level")
                                unit: "V"
                                from: -10
                                to: 10
                                stepSize: 0.05
                                value: page.level
                                defaultValue: 0
                                accentColor: Theme.signalCh0
                                onValueModified: v => page.level = v
                                SegmentedControl {
                                    Layout.preferredWidth: 150
                                    compact: true
                                    model: [qsTr("Auto"), qsTr("Normal"), qsTr("None")]
                                    currentIndex: 0
                                }
                            }
                        }

                        ChannelCard {
                            Layout.fillWidth: true
                            title: "CH 0"
                            subtitle: "AI 0"
                            color: Theme.signalCh0
                            checked: page.ch0
                            onToggled: on => page.ch0 = on
                            facts: ["DC", qsTr("±10 V range"), qsTr("Differential")]
                            KnobField {
                                Layout.fillWidth: true
                                label: qsTr("Scale")
                                unit: "V"
                                values: page.voltSteps
                                currentIndex: page.scale0
                                formatter: v => Units.format(v, "V", 3) + "/div"
                                accentColor: Theme.signalCh0
                                onIndexActivated: index => page.scale0 = index
                            }
                            KnobField {
                                Layout.fillWidth: true
                                label: qsTr("Position")
                                unit: "div"
                                knobSize: 34
                                from: -5
                                to: 5
                                stepSize: 0.1
                                value: page.position0
                                defaultValue: 0
                                formatter: v => v.toFixed(2) + " div"
                                accentColor: Theme.signalCh0
                                onValueModified: v => page.position0 = v
                            }
                        }

                        ChannelCard {
                            Layout.fillWidth: true
                            title: "CH 1"
                            subtitle: "AI 1"
                            color: Theme.signalCh1
                            checked: page.ch1
                            onToggled: on => page.ch1 = on
                            facts: ["DC", qsTr("±2 V range")]
                            KnobField {
                                Layout.fillWidth: true
                                label: qsTr("Scale")
                                values: page.voltSteps
                                currentIndex: page.scale1
                                formatter: v => Units.format(v, "V", 3) + "/div"
                                accentColor: Theme.signalCh1
                                onIndexActivated: index => page.scale1 = index
                            }
                            KnobField {
                                Layout.fillWidth: true
                                label: qsTr("Position")
                                compact: true
                                valueWidth: 80
                                from: -5
                                to: 5
                                stepSize: 0.1
                                value: page.position1
                                formatter: v => v.toFixed(2) + " div"
                                accentColor: Theme.signalCh1
                                onValueModified: v => page.position1 = v
                            }
                        }

                        ChannelCard {
                            Layout.fillWidth: true
                            title: qsTr("Math")
                            subtitle: "CH 0 − CH 1"
                            color: Theme.signalMath
                            checked: page.math
                            onToggled: on => page.math = on
                        }

                        ToolPalette {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingXs
                            currentId: page.tool
                            model: [
                                { id: "measure", text: qsTr("Measure"), icon: "ruler" },
                                { id: "cursors", text: qsTr("Cursors"), icon: "crosshair" },
                                { id: "math", text: qsTr("Math"), icon: "sliders" },
                                { id: "fft", text: qsTr("FFT"), icon: "bar-chart" },
                                { id: "refs", text: qsTr("Refs"), icon: "layers" },
                                { id: "display", text: qsTr("Display"), icon: "grid" },
                                { id: "trigger", text: qsTr("Trigger"), icon: "slope-rising" },
                                { id: "export", text: qsTr("Export"), icon: "export" }
                            ]
                            onActivated: id => page.tool = page.tool === id ? "" : id
                        }
                    }
                }
            }
        }
    }

    GallerySpec {
        title: qsTr("Knobs")
        note: qsTr("Flat face, tick ring, value arc in the channel color, pointer. 48 px for primary parameters, 34 px for secondary ones. Stepped knobs show one tick per detent.")
        Row {
            spacing: Theme.spacingXl
            Knob { size: 48; values: page.timeSteps; currentIndex: page.timeIndex; onIndexMoved: index => page.timeIndex = index }
            Knob { size: 48; from: 0.2; to: 20000; logarithmic: true; value: page.frequency; accentColor: Theme.signalAo0; onMoved: v => page.frequency = v }
            Knob { size: 34; from: -5; to: 5; value: page.position0; accentColor: Theme.signalCh0; onMoved: v => page.position0 = v }
            Knob { size: 48; from: 0; to: 10; value: 4; minLabel: "0"; maxLabel: "10"; accentColor: Theme.signalCh1 }
            Knob { size: 48; from: 0; to: 1; value: 0.6; enabled: false }
            Knob { size: 64; label: qsTr("Frequency"); showValue: true; unit: "Hz"; from: 0.2; to: 20000; logarithmic: true; value: page.frequency; onMoved: v => page.frequency = v }
        }
    }

    GallerySpec {
        title: qsTr("Knob fields")
        note: qsTr("Label, value between − / + (tabular, semibold, never wraps), optional caption. `compact` hides the knob for narrow panes.")
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingXl
            KnobField {
                label: qsTr("Frequency")
                unit: "Hz"
                from: 0.2
                to: 20000
                logarithmic: true
                value: page.frequency
                accentColor: Theme.signalAo0
                caption: qsTr("Generates %1").arg(Units.format(page.frequency, "Hz", 5))
                onValueModified: v => page.frequency = v
            }
            KnobField {
                label: qsTr("Time/Div (compact)")
                compact: true
                values: page.timeSteps
                currentIndex: page.timeIndex
                formatter: v => Units.format(v, "s", 3) + "/div"
                onIndexActivated: index => page.timeIndex = index
            }
            KnobField {
                label: qsTr("Disabled")
                enabled: false
                from: 0
                to: 1
                value: 0.5
                unit: "V"
            }
        }
    }

    GallerySpec {
        title: qsTr("Run button")
        note: qsTr("Stopped: accent fill. Running: positive tint with the acquisition state below.")
        Row {
            spacing: Theme.spacingM
            RunButton { running: false }
            RunButton { running: true; stateText: qsTr("Triggered") }
            RunButton { running: true; stateText: qsTr("Waiting for trigger") }
            RunButton { running: false; busy: true }
        }
    }
}
