// Trace styles: point shapes, line styles, interpolation (step) modes and fills (TraceStyle).
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo

    // 13 samples of a slow wave, shared by every trace (offset per row with yOffset).
    TraceBuffer {
        id: wave
        Component.onCompleted: {
            const y = []
            for (let i = 0; i <= 12; ++i)
                y.push(0.3 * Math.sin(i * 2 * Math.PI / 9) + (i % 4 === 1 ? 0.08 : 0))
            setUniformValues(0, 1, y)
        }
    }

    component StylePlot: Plot {
        colorScheme: page.theme.plotScheme
        backgroundColor: page.theme.card
        xLabelsVisible: false
        xAxis { min: -0.5; max: 12.5 }
        legendVisible: true
        legendPosition: Plot.TopRight
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        PageHeader {
            theme: page.theme
            title: "Trace styles"
            subtitle: "PlotTrace point shapes, line styles and interpolation · TraceStyle fills (zero, −∞, +∞) · the Trace style dialog edits these"
        }
        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rowSpacing: 16
            columnSpacing: 16

            Card {
                theme: page.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                StylePlot {
                    id: shapes
                    anchors.fill: parent
                    yAxis { min: -9.5; max: 0.5; labelsVisible: false }
                    Instantiator {
                        model: ["Filled circle", "Circle", "Square", "Filled square", "Diamond", "Filled diamond", "Cross",
                                "Plus", "X"]
                        delegate: PlotTrace {
                            required property int index
                            required property string modelData
                            plot: shapes
                            name: modelData
                            buffer: wave
                            yOffset: -index
                            width: 1
                            color: shapes.paletteColor(index)
                            pointsVisible: true
                            pointSize: 7
                            pointShape: index
                        }
                    }
                }
            }
            Card {
                theme: page.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                StylePlot {
                    id: lines
                    anchors.fill: parent
                    yAxis { min: -5.5; max: 0.5; labelsVisible: false }
                    Instantiator {
                        model: ["Solid", "Dash", "Dot", "Dash dot", "Dash dot dot"]
                        delegate: PlotTrace {
                            required property int index
                            required property string modelData
                            plot: lines
                            name: modelData
                            buffer: wave
                            yOffset: -index
                            width: 2
                            color: lines.paletteColor(index)
                            style: index
                        }
                    }
                }
            }
            Card {
                theme: page.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                StylePlot {
                    id: steps
                    anchors.fill: parent
                    yAxis { min: -5.5; max: 0.5; labelsVisible: false }
                    Instantiator {
                        model: ["Linear", "Step", "Step vertical", "Center step", "Center step vertical"]
                        delegate: PlotTrace {
                            required property int index
                            required property string modelData
                            plot: steps
                            name: modelData
                            buffer: wave
                            yOffset: -index
                            width: 1.5
                            color: steps.paletteColor(index)
                            interpolationMode: index
                            pointsVisible: true
                            pointSize: 5
                        }
                    }
                }
            }
            Card {
                theme: page.theme
                Layout.fillWidth: true
                Layout.fillHeight: true
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    Repeater {
                        model: [{ name: "Fill to zero", fill: TraceStyle.Zero },
                                { name: "Fill to −∞", fill: TraceStyle.MinusInfinity },
                                { name: "Fill to +∞", fill: TraceStyle.PlusInfinity }]
                        delegate: StylePlot {
                            id: fillPlot
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            yAxis { min: -0.6; max: 0.6; labelsVisible: false }
                            PlotTrace {
                                name: fillPlot.modelData.name
                                buffer: wave
                                color: fillPlot.paletteColor(fillPlot.index)
                                userStyle: TraceStyle {
                                    lineWidth: 2
                                    pointStyle: TraceStyle.FilledCircle
                                    fillTo: fillPlot.modelData.fill
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
