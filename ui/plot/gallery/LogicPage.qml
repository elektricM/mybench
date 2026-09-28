// Digital I/O timeline: 8 stacked lanes from per-lane edge lists (exact edges when zoomed in,
// activity blocks when edges are denser than pixels), following the latest state.
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
            title: "Digital I/O"
            subtitle: "DIO 0–7 sampled at 200 kHz, stored as edges · edge-accurate at any zoom · dense bursts collapse into activity blocks · Δ cursor with 1/Δt"
        }
        RowLayout {
            spacing: 8
            Repeater {
                model: ["Input", "Input", "Input", "Input", "Input", "Input", "Output", "Output"]
                delegate: Chip {
                    required property var modelData
                    required property int index
                    theme: page.theme
                    accent: plot.paletteColor(index)
                    label: "DIO" + index
                    value: modelData
                }
            }
            Item { Layout.fillWidth: true }
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
                xAxis { unit: "s"; title: "Time"; tickSpacing: 100 }
                yAxis { min: 0; max: 8; divisions: 8; minorTicks: 0; inverted: true; labelsVisible: false; interactive: false; minorGridVisible: false }
                PlotLogicTrace {
                    buffer: page.demo.logic
                    laneNames: ["CLK", "UART TX", "PWM", "CLK / 2", "CLK / 4", "CS", "BURST", "STATUS"]
                    laneColors: [plot.paletteColor(0), plot.paletteColor(1), plot.paletteColor(2), plot.paletteColor(3),
                                 plot.paletteColor(4), plot.paletteColor(5), plot.paletteColor(6), plot.paletteColor(7)]
                }
                PlotCursor { id: c1; label: "C1"; color: page.theme.cursor1; readoutVisible: false }
                PlotCursor { id: c2; label: "C2"; color: page.theme.cursor2; readoutVisible: false }
                Component.onCompleted: {
                    // 60 ms around the latest 20 kHz burst (bursts start every 100 ms, last 12 ms):
                    // the burst lane collapses into an activity block, the others stay edge-exact.
                    const end = page.demo.logic.endX()
                    const burst = Math.floor((end - 0.04) * 10) / 10
                    xAxis.setRange(burst - 0.024, burst + 0.036)
                    c1.position = burst
                    c2.position = burst + 0.012
                }
            }
        }
    }
}
