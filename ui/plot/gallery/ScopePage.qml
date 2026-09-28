// Oscilloscope: 2 channels on a 10 x 8 division graticule, channel position markers, trigger
// level (right edge) and trigger position (top) handles, snapping cursors, persistence.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo
    property real vdiv0: 0.5
    property real vdiv1: 1.0
    property real pos0: 0.6      // divisions
    property real pos1: -1.2
    property real trigLevel: 0.4 // V
    property real trigTime: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        PageHeader {
            theme: page.theme
            title: "Oscilloscope"
            subtitle: "Scope graticule · drag the channel markers (left), trigger level (right), trigger position (top) and cursors · 350 ms persistence"
        }
        RowLayout {
            spacing: 8
            Chip { theme: page.theme; accent: page.theme.ch0; label: "CH0"; value: plot.format(page.vdiv0, "V", 3) + "/div" }
            Chip { theme: page.theme; accent: page.theme.ch1; label: "CH1"; value: plot.format(page.vdiv1, "V", 3) + "/div" }
            Chip { theme: page.theme; label: "Timebase"; value: "200 µs/div" }
            Chip { theme: page.theme; accent: page.theme.amber; label: "Trigger"; value: "CH0  ↑  " + plot.format(page.trigLevel, "V", 3) }
            Item { Layout.fillWidth: true }
            Chip { theme: page.theme; accent: page.theme.ch0; label: "Running"; value: "200 kS/s" }
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
                graticule: Plot.Scope
                xAxis { min: -1e-3; max: 1e-3; unit: "s"; unitInLabels: true; divisions: 10 }
                yAxis { min: -4; max: 4; divisions: 8; labelsVisible: false; interactive: false }

                PlotTrace {
                    id: ch0
                    name: "CH0"
                    unit: "V"
                    buffer: page.demo.scope0
                    color: page.theme.ch0
                    width: 1.6
                    yScale: 1 / page.vdiv0
                    yOffset: page.pos0
                    persistence: 350
                }
                PlotTrace {
                    id: ch1
                    name: "CH1"
                    unit: "V"
                    buffer: page.demo.scope1
                    color: page.theme.ch1
                    width: 1.6
                    yScale: 1 / page.vdiv1
                    yOffset: page.pos1
                    persistence: 350
                }
                // Channel ground markers (display units = divisions).
                PlotLine {
                    value: page.pos0
                    handle: PlotLine.Start
                    label: "0"
                    color: page.theme.ch0
                    lineMode: PlotLine.WhileDragging
                    onMoved: value => page.pos0 = value
                }
                PlotLine {
                    value: page.pos1
                    handle: PlotLine.Start
                    label: "1"
                    color: page.theme.ch1
                    lineMode: PlotLine.WhileDragging
                    onMoved: value => page.pos1 = value
                }
                // Trigger level in volts of CH0 (mapped through its V/div and position).
                PlotLine {
                    trace: ch0
                    value: page.trigLevel
                    handle: PlotLine.End
                    label: "T"
                    color: page.theme.amber
                    style: PlotElement.Dash
                    onMoved: value => page.trigLevel = value
                }
                // Trigger position (time).
                PlotLine {
                    orientation: PlotElement.Vertical
                    value: page.trigTime
                    handle: PlotLine.Start
                    label: "T"
                    color: page.theme.amber
                    lineMode: PlotLine.WhileDragging
                    onMoved: value => page.trigTime = value
                }
                PlotCursor {
                    id: c1
                    position: -0.75e-3
                    label: "C1"
                    color: page.theme.cursor1
                    trace: ch0
                    snap: true
                }
                PlotCursor {
                    id: c2
                    position: 0.25e-3
                    label: "C2"
                    color: page.theme.cursor2
                    trace: ch0
                    snap: true
                }
            }
        }
        RowLayout {
            spacing: 10
            Readout { theme: page.theme; label: "CH0 frequency"; value: "1.000 kHz"; accent: page.theme.ch0 }
            Readout { theme: page.theme; label: "CH0 Vpp"; value: "3.44 V"; accent: page.theme.ch0 }
            Readout { theme: page.theme; label: "CH1 rise time"; value: "61.8 µs"; accent: page.theme.ch1 }
            Item { Layout.fillWidth: true }
            Readout { theme: page.theme; label: "C1"; value: plot.format(c1.value, "V") }
            Readout { theme: page.theme; label: "C2"; value: plot.format(c2.value, "V") }
            Readout { theme: page.theme; label: "ΔV"; value: plot.format(c2.value - c1.value, "V") }
            Readout { theme: page.theme; label: "Δt"; value: plot.format(c2.position - c1.position, "s") }
        }
    }
}
