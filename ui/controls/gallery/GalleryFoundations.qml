pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls

GalleryPage {
    id: page

    title: qsTr("Foundations")
    description: qsTr("Every chrome color derives from the effective palette (the system palette when it matches the mode, else the reference palette). Signal colors identify channels only; status colors appear as dots and short text, never as fills.")

    component Swatch: ColumnLayout {
        id: swatch
        required property string name
        required property color value
        property color surface: Theme.window
        spacing: Theme.spacingXs
        Layout.preferredWidth: 120
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            radius: Theme.radiusControl
            color: swatch.surface
            border.width: Theme.borderWidth
            border.color: Theme.separator
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Theme.radiusControl - 1
                color: swatch.value
            }
        }
        Text {
            text: swatch.name
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
        }
        Text {
            text: swatch.value.toString().toUpperCase()
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.family: Theme.monoFamily
        }
    }

    GallerySpec {
        title: qsTr("Surfaces")
        note: qsTr("Panes, display, sidebar and inspector have no radius and no border of their own; a hairline separates regions.")
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            Swatch { name: "window"; value: Theme.window }
            Swatch { name: "sidebar"; value: Theme.sidebar }
            Swatch { name: "base"; value: Theme.base }
            Swatch { name: "display"; value: Theme.display }
            Swatch { name: "controlFill"; value: Theme.controlFill }
            Swatch { name: "separator"; value: Theme.separator }
            Swatch { name: "controlBorder"; value: Theme.controlBorder }
            Swatch { name: "hoverFill"; value: Theme.hoverFill }
            Swatch { name: "pressedFill"; value: Theme.pressedFill }
            Swatch { name: "selectionInactive"; value: Theme.selectionInactive }
        }
    }

    GallerySpec {
        title: qsTr("Accent, text and status")
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingM
            Swatch { name: "accent"; value: Theme.accent }
            Swatch { name: "selection"; value: Theme.selection }
            Swatch { name: "text"; value: Theme.text }
            Swatch { name: "textSecondary"; value: Theme.textSecondary }
            Swatch { name: "textTertiary"; value: Theme.textTertiary }
            Swatch { name: "textDisabled"; value: Theme.textDisabled }
            Swatch { name: "positive"; value: Theme.positive }
            Swatch { name: "caution"; value: Theme.caution }
            Swatch { name: "negative"; value: Theme.negative }
        }
    }

    GallerySpec {
        title: qsTr("Signals")
        note: qsTr("On the display: CH0/AI0, CH1/AI1, AO0, AO1, Math, Reference; cursors use the display text color, dashed and labelled.")
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: signalRow.implicitHeight + 2 * Theme.spacingL
            color: Theme.display
            Row {
                id: signalRow
                anchors.centerIn: parent
                spacing: Theme.spacingXl
                Repeater {
                    model: [{ n: "CH0", c: Theme.signalCh0 }, { n: "CH1", c: Theme.signalCh1 }, { n: "AO0", c: Theme.signalAo0 },
                            { n: "AO1", c: Theme.signalAo1 }, { n: "Math", c: Theme.signalMath }, { n: "Ref", c: Theme.signalRef },
                            { n: "Cursor", c: Theme.signalCursor }]
                    delegate: Column {
                        id: signal
                        required property var modelData
                        spacing: Theme.spacingXs
                        Rectangle {
                            width: 96
                            height: 2
                            color: signal.modelData.c
                        }
                        Text {
                            text: signal.modelData.n
                            color: signal.modelData.c
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }
        }
    }

    GallerySpec {
        title: qsTr("Type")
        note: qsTr("The system UI font at the platform size (%1 px here). Regular and Medium; DemiBold only for headings. Numbers use tabular figures.").arg(Theme.fontSizeBase)
        Repeater {
            model: [{ n: "title", s: Theme.fontSizeTitle, w: Font.DemiBold, t: qsTr("Overview") },
                    { n: "heading", s: Theme.fontSizeHeading, w: Font.DemiBold, t: qsTr("Channel 0") },
                    { n: "body", s: Theme.fontSizeBody, w: Font.Normal, t: qsTr("Coupling DC · 1× probe · ±10 V") },
                    { n: "small", s: Theme.fontSizeSmall, w: Font.Normal, t: qsTr("Waiting for trigger") },
                    { n: "caption", s: Theme.fontSizeCaption, w: Font.Normal, t: qsTr("CH0 1 V/div · 5 ms/div · Edge ↑ 0.00 V") }]
            delegate: RowLayout {
                id: typeRow
                required property var modelData
                spacing: Theme.spacingL
                Text {
                    Layout.preferredWidth: 120
                    text: typeRow.modelData.n + " " + typeRow.modelData.s
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeSmall
                }
                Text {
                    text: typeRow.modelData.t
                    color: Theme.text
                    font.pixelSize: typeRow.modelData.s
                    font.weight: typeRow.modelData.w
                }
            }
        }
        RowLayout {
            spacing: Theme.spacingXl
            Readout { size: "m"; value: 1.00012; unit: "kHz"; digits: 6; label: qsTr("Frequency") }
            Readout { size: "l"; value: 0.7071; unit: "V"; label: qsTr("RMS") }
            Readout { size: "xl"; value: 1.23456; unit: "V"; digits: 6; label: qsTr("DC Voltage") }
        }
    }

    GallerySpec {
        title: qsTr("Metrics")
        note: qsTr("Spacing 4 · 8 · 12 · 16 · 24 only; form rows 6 apart. Line height %1 px.").arg(Theme.lineHeight)
        Row {
            spacing: Theme.spacingL
            Repeater {
                model: [Theme.spacingXs, Theme.spacingS, Theme.spacingM, Theme.spacingL, Theme.spacingXl]
                delegate: Column {
                    id: space
                    required property int modelData
                    spacing: Theme.spacingXs
                    Rectangle {
                        width: space.modelData
                        height: space.modelData
                        color: Theme.accentMuted
                        border.width: Theme.borderWidth
                        border.color: Theme.accent
                    }
                    Text {
                        text: space.modelData
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }
            Repeater {
                model: [{ n: "S", h: Theme.controlHeightS }, { n: "M", h: Theme.controlHeight }, { n: "L", h: Theme.controlHeightL }]
                delegate: Column {
                    id: height
                    required property var modelData
                    spacing: Theme.spacingXs
                    Rectangle {
                        width: 64
                        height: height.modelData.h
                        radius: Theme.radiusControl
                        color: Theme.controlFill
                        border.width: Theme.borderWidth
                        border.color: Theme.controlBorder
                    }
                    Text {
                        text: height.modelData.n + " " + height.modelData.h
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeCaption
                    }
                }
            }
            Repeater {
                model: ["play", "stop", "autoscale", "crosshair", "export", "settings", "help-circle", "instrument-scope", "instrument-dmm", "wave-sine"]
                delegate: Icon {
                    required property string modelData
                    name: modelData
                    color: Theme.textSecondary
                }
            }
        }
    }
}
