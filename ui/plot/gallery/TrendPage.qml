// DMM trend: clock-time axis following the latest reading, continuous y autoscale, ±1σ band.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        PageHeader {
            theme: page.theme
            title: "Multimeter trend"
            subtitle: "Rolling 3000-reading buffer (append from the worker) · time-of-day axis following the latest reading · autoscale with hysteresis · mean ± σ band"
        }
        RowLayout {
            spacing: 16
            Rectangle {
                Layout.preferredWidth: 300
                Layout.preferredHeight: 86
                radius: 10
                color: page.theme.card
                border.color: page.theme.border
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 18
                    spacing: 0
                    Text {
                        text: "DC VOLTAGE · 20 V RANGE"
                        color: page.theme.muted
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        font.letterSpacing: 0.4
                    }
                    Row {
                        spacing: 8
                        Text {
                            text: page.demo.trendLast.toFixed(5)
                            color: page.theme.text
                            font.pixelSize: 40
                            font.weight: Font.Medium
                            font.features: { "tnum": 1 }
                        }
                        Text {
                            anchors.baseline: parent.children[0].baseline
                            text: "V"
                            color: page.theme.ch0
                            font.pixelSize: 20
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
            Readout { theme: page.theme; label: "Mean"; value: plot.format(page.demo.trendMean, "V", 6) }
            Readout { theme: page.theme; label: "Std dev"; value: plot.format(page.demo.trendSigma, "V", 3) }
            Readout { theme: page.theme; label: "Readings"; value: page.demo.trend.count.toString() }
            Item { Layout.fillWidth: true }
            Chip { theme: page.theme; accent: plot.xAxis.follow ? page.theme.ch0 : page.theme.muted; label: "Follow"; value: plot.xAxis.follow ? "on" : "off" }
        }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            Plot {
                id: plot
                anchors.fill: parent
                colorScheme: page.theme.plotScheme
                backgroundColor: page.theme.card
                hoverReadout: true
                xAxis { labelFormat: PlotAxis.Time; follow: true; tickSpacing: 110 }
                yAxis { autoScale: true; autoScaleMargin: 0.15; unit: "V"; title: "Voltage"; tickSpacing: 48 }
                PlotRegion {
                    orientation: PlotElement.Horizontal
                    from: page.demo.trendMean - page.demo.trendSigma
                    to: page.demo.trendMean + page.demo.trendSigma
                    color: Qt.alpha(page.theme.ch0, 0.09)
                    edgesVisible: false
                    label: "±1σ"
                }
                PlotLine {
                    value: page.demo.trendMean
                    handle: PlotLine.NoHandle
                    draggable: false
                    color: Qt.alpha(page.theme.ch0, 0.55)
                    style: PlotElement.Dash
                }
                PlotTrace {
                    name: "DMM"
                    unit: "V"
                    buffer: page.demo.trend
                    color: page.theme.ch0
                    width: 1.5
                    pointsVisible: true
                    pointSize: 4
                }
                Component.onCompleted: {
                    const last = page.demo.trend.xMaximum()
                    xAxis.setRange(last - 60, last)
                }
            }
        }
    }
}
