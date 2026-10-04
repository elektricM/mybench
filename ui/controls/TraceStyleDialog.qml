pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Plot

// "Trace style" dialog (app.md 8.5, the ELVISmx Graph Properties > Plots page) for a list of
// traces: line and point/fill colors, line width (None = points only) and style, point style,
// interpolation, fill. Changes apply live; Cancel (or Esc) restores the values from when the
// dialog opened; Reset to Default restores the style's defaults.
//     TraceStyleDialog {
//         id: styles
//         entries: [{ name: "CH 0", style: ch0Style, color: Theme.ch0 },   // color: the "Auto" swatch
//                   { name: "CH 1", style: ch1Style, color: Theme.ch1 }]
//     }
//     styles.openFor(0)
Dialog {
    id: dialog

    // [{ name: string, style: TraceStyle, color: color (optional, the trace's own color) }]
    property var entries: []
    property int currentIndex: 0
    readonly property var entry: currentIndex >= 0 && currentIndex < entries.length ? entries[currentIndex] : null
    readonly property TraceStyle current: entry ? entry.style : null
    readonly property color autoColor: entry && entry.color !== undefined ? entry.color : Theme.accent
    property var snapshot: []

    readonly property var widths: [0, 1, 1.5, 2, 3, 4, 5]
    // The signal palette of the current display scheme, then neutral display text colors.
    readonly property var swatches: [Theme.signalCh0, Theme.signalCh1, Theme.signalAo0, Theme.signalAo1, Theme.signalMath,
                                     Theme.signalRef, Theme.displayText, Theme.displayTextSecondary].map(c => c.toString())

    function openFor(index: int) {
        currentIndex = Math.max(0, Math.min(index, entries.length - 1))
        open()
    }

    title: qsTr("Trace Style")
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 460
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    onAboutToShow: snapshot = entries.map(e => e.style ? e.style.toJson() : ({}))
    onRejected: {
        for (let i = 0; i < entries.length && i < snapshot.length; ++i) {
            if (entries[i].style)
                entries[i].style.fromJson(snapshot[i])
        }
    }

    component Swatch: AbstractButton {
        id: swatch
        property color fill
        property bool selected: false
        implicitWidth: 20
        implicitHeight: 20
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        background: Rectangle {
            radius: 4
            color: swatch.fill
            border.width: swatch.selected ? 2 : 1
            border.color: swatch.selected ? Theme.accent : (swatch.hovered ? Theme.controlBorder : Theme.separator)
            FocusRing { shown: swatch.visualFocus; radius: 7 }
        }
        contentItem: Item {}
    }

    // A row of color swatches: Auto, the palette, and a custom color.
    component ColorRow: Flow {
        id: colorRow
        property color value
        property color autoColor
        property string autoText: qsTr("Auto")
        signal picked(var value)   // a color, or undefined for Auto
        spacing: 4
        Layout.fillWidth: true

        Swatch {
            fill: colorRow.autoColor
            selected: !colorRow.value.valid
            ToolTip.visible: hovered
            ToolTip.text: colorRow.autoText
            Accessible.name: colorRow.autoText
            onClicked: colorRow.picked(undefined)
            Text {
                anchors.centerIn: parent
                text: "A"
                font.pixelSize: 10
                font.weight: Font.Bold
                color: colorRow.autoColor.hslLightness > 0.6 ? "black" : "white"
            }
        }
        Repeater {
            model: dialog.swatches
            delegate: Swatch {
                required property string modelData
                fill: modelData
                selected: colorRow.value.valid && Qt.colorEqual(colorRow.value, modelData)
                ToolTip.visible: hovered
                ToolTip.text: modelData
                Accessible.name: modelData
                onClicked: colorRow.picked(Qt.color(modelData))
            }
        }
        Swatch {
            readonly property bool custom: colorRow.value.valid
                                           && !dialog.swatches.some(c => Qt.colorEqual(c, colorRow.value))
            fill: custom ? colorRow.value : "transparent"
            selected: custom
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Custom color…")
            Accessible.name: qsTr("Custom color")
            onClicked: colorDialog.open()
            Icon {
                anchors.centerIn: parent
                visible: !parent.custom
                name: "plus"
                size: 12
                color: Theme.textSecondary
            }
        }
        ColorDialog {
            id: colorDialog
            selectedColor: colorRow.value.valid ? colorRow.value : colorRow.autoColor
            onAccepted: colorRow.picked(selectedColor)
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Field {
            label: qsTr("Trace")
            visible: dialog.entries.length > 1
            ComboBox {
                Layout.fillWidth: true
                model: dialog.entries.map(e => e.name)
                currentIndex: dialog.currentIndex
                onActivated: index => dialog.currentIndex = index
            }
        }

        // Live preview of the style on a short sampled wave.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: Theme.radiusControl
            color: Theme.display
            border.width: Theme.borderWidth
            border.color: Theme.plotFrame
            // A plain Plot (BenchPlot uses this dialog), themed like a bare BenchPlot.
            Plot {
                anchors.fill: parent
                anchors.margins: Theme.spacingS
                colorScheme: Theme.displayDark ? Plot.Dark : Plot.Light
                backgroundColor: "transparent"
                plotAreaColor: "transparent"
                seriesColors: Theme.seriesColors
                frameVisible: false
                interactive: false
                xLabelsVisible: false
                yLabelsVisible: false
                leftMargin: 0
                rightMargin: 0
                topMargin: 0
                bottomMargin: 0
                xAxis { min: 0; max: 16; gridVisible: false }
                yAxis { min: -1.25; max: 1.25; gridVisible: false }
                PlotTrace {
                    color: dialog.autoColor
                    userStyle: dialog.current
                    buffer: TraceBuffer {
                        Component.onCompleted: {
                            const y = []
                            for (let i = 0; i <= 16; ++i)
                                y.push(Math.sin(i * 2 * Math.PI / 12) * (i % 3 === 0 ? 0.6 : 0.9))
                            setUniformValues(0, 1, y)
                        }
                    }
                }
            }
        }

        Field {
            label: qsTr("Line color")
            ColorRow {
                value: dialog.current ? dialog.current.lineColor : "transparent"
                autoColor: dialog.autoColor
                autoText: qsTr("Auto (trace color)")
                onPicked: value => dialog.current.lineColor = value
            }
        }
        Field {
            label: qsTr("Point / fill color")
            ColorRow {
                value: dialog.current ? dialog.current.fillColor : "transparent"
                autoColor: dialog.current && dialog.current.lineColor.valid ? dialog.current.lineColor : dialog.autoColor
                autoText: qsTr("Auto (line color)")
                onPicked: value => dialog.current.fillColor = value
            }
        }
        FieldRow {
            Field {
                label: qsTr("Line width")
                ComboBox {
                    Layout.fillWidth: true
                    model: dialog.widths.map(w => w === 0 ? qsTr("None") : w + " px")
                    currentIndex: dialog.current ? Math.max(0, dialog.widths.indexOf(dialog.current.lineWidth)) : 0
                    onActivated: index => dialog.current.lineWidth = dialog.widths[index]
                }
            }
            Field {
                label: qsTr("Line style")
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Solid"), qsTr("Dash"), qsTr("Dot"), qsTr("Dash dot"), qsTr("Dash dot dot")]
                    currentIndex: dialog.current ? dialog.current.lineStyle : 0
                    onActivated: index => dialog.current.lineStyle = index
                }
            }
        }
        FieldRow {
            Field {
                label: qsTr("Point style")
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("None"), qsTr("Circle"), qsTr("Filled circle"), qsTr("Square"), qsTr("Filled square"),
                            qsTr("Diamond"), qsTr("Filled diamond"), qsTr("Cross"), qsTr("Plus"), qsTr("X")]
                    currentIndex: dialog.current ? dialog.current.pointStyle : 0
                    onActivated: index => dialog.current.pointStyle = index
                }
            }
            Field {
                label: qsTr("Interpolation")
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Points only"), qsTr("Linear"), qsTr("Step"), qsTr("Step vertical"),
                            qsTr("Center step"), qsTr("Center step vertical")]
                    currentIndex: dialog.current ? dialog.current.interpolation : 1
                    onActivated: index => dialog.current.interpolation = index
                }
            }
        }
        FieldRow {
            Field {
                label: qsTr("Fill to")
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("None"), qsTr("Zero"), qsTr("−Infinity"), qsTr("+Infinity")]
                    currentIndex: dialog.current ? dialog.current.fillTo : 0
                    onActivated: index => dialog.current.fillTo = index
                }
            }
            Item { Layout.fillWidth: true }
        }
    }

    footer: RowLayout {
        spacing: Theme.spacingS
        Button {
            Layout.leftMargin: Theme.spacingL + 4
            Layout.bottomMargin: Theme.spacingL
            implicitHeight: Theme.controlHeightL
            text: qsTr("Reset to Default")
            flat: true
            enabled: dialog.current !== null && !dialog.current.isDefault
            onClicked: dialog.current.reset()
        }
        Item { Layout.fillWidth: true }
        Button {
            Layout.bottomMargin: Theme.spacingL
            implicitHeight: Theme.controlHeightL
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
        Button {
            Layout.bottomMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL + 4
            implicitHeight: Theme.controlHeightL
            text: qsTr("OK")
            highlighted: true
            onClicked: dialog.accept()
        }
    }
}
