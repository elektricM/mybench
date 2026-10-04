pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Controls

GalleryPage {
    id: page

    title: qsTr("Plots")
    description: qsTr("Displays follow the theme (light plots in light mode; \"Always Use Dark Displays\" keeps them dark). Every color of the plot — surface, grid, frame, axis text, series, cursors — comes from Theme through BenchPlot. Hover shows the plot toolbar; the context menu is native.")

    readonly property var sine: Array.from({ length: 501 }, (_, i) => Math.sin(i / 500 * 4 * Math.PI))
    readonly property var square: Array.from({ length: 501 }, (_, i) => 0.5 * (Math.sin(i / 500 * 4 * Math.PI + 0.6) > 0 ? 1 : -1))
    readonly property var math: sine.map((v, i) => 0.5 * (v + square[i]) - 0.2)
    readonly property var trend: Array.from({ length: 301 }, (_, i) => 1.2345 + 0.0008 * Math.sin(i / 23) + 0.0003 * Math.sin(i * 1.7))
    property int windowIndex: 1

    component ScopeLike: BenchPlot {
        id: scope
        Layout.fillWidth: true
        Layout.preferredHeight: 300
        toolsAlwaysVisible: true
        cursorsAvailable: true
        xAxis { min: 0; max: 0.01; unit: "s"; divisions: 10 }
        yAxis { min: -1.5; max: 1.5; unit: "V"; divisions: 8 }
        PlotTrace {
            color: Theme.signalCh0
            buffer: TraceBuffer { Component.onCompleted: setUniformValues(0, 0.01 / 500, page.sine) }
        }
        PlotTrace {
            color: Theme.signalCh1
            buffer: TraceBuffer { Component.onCompleted: setUniformValues(0, 0.01 / 500, page.square) }
        }
        PlotTrace {
            color: Theme.signalMath
            buffer: TraceBuffer { Component.onCompleted: setUniformValues(0, 0.01 / 500, page.math) }
        }
        PlotCursor { orientation: PlotElement.Vertical; position: 0.0025; color: Theme.signalCursor; label: "A"; style: PlotElement.Dash }
        PlotCursor { orientation: PlotElement.Vertical; position: 0.0065; color: Theme.signalCursor; label: "B"; style: PlotElement.Dash }
    }

    GallerySpec {
        title: qsTr("Display in the current theme")
        note: qsTr("CH0, CH1 and Math traces; cursors A/B in the display text color, dashed and labelled. Axis labels use the caption size and tabular figures.")
        ScopeLike {}
    }

    GallerySpec {
        title: qsTr("Trend")
        note: qsTr("TrendView: the time window sits in the plot toolbar and in the context menu (Time Window ▸), next to Clear.")
        TrendView {
            Layout.fillWidth: true
            Layout.preferredHeight: 220
            plot.toolsAlwaysVisible: true
            plot.xAxis.min: 0
            plot.xAxis.max: 300
            plot.xAxis.unit: "s"
            plot.yAxis.min: 1.233
            plot.yAxis.max: 1.236
            plot.yAxis.unit: "V"
            windows: [{ text: "10 s", seconds: 10 }, { text: "1 min", seconds: 60 }, { text: "10 min", seconds: 600 }, { text: qsTr("All"), seconds: 0 }]
            windowIndex: page.windowIndex
            onWindowActivated: index => page.windowIndex = index
            PlotTrace {
                color: Theme.displayText
                buffer: TraceBuffer { Component.onCompleted: setUniformValues(0, 1, page.trend) }
            }
        }
    }

    GallerySpec {
        title: qsTr("Tile previews")
        note: qsTr("Bare plots: no frame, labels or interaction, on the tile surface.")
        Row {
            spacing: Theme.spacingL
            Repeater {
                model: [Theme.signalCh0, Theme.signalCh1, Theme.signalAo0]
                delegate: Rectangle {
                    id: tile
                    required property color modelData
                    width: 200
                    height: 64
                    radius: Theme.radiusPopup
                    color: Theme.base
                    border.width: Theme.borderWidth
                    border.color: Theme.separator
                    BenchPlot {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingS
                        bare: true
                        xAxis { min: 0; max: 0.01 }
                        yAxis { min: -1.2; max: 1.2 }
                        PlotTrace {
                            color: tile.modelData
                            buffer: TraceBuffer { Component.onCompleted: setUniformValues(0, 0.01 / 500, page.sine) }
                        }
                    }
                }
            }
        }
    }
}
