// Deep memory: 2 x 200 000 samples replaced every frame (min/max decimation), hover readout.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo
    property real fps: 0

    FrameAnimation {
        id: frames
        running: true
        property int count: 0
        property real elapsed: 0
        onTriggered: {
            count += 1
            elapsed += frameTime
            if (elapsed >= 0.5) {
                page.fps = count / elapsed
                count = 0
                elapsed = 0
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        PageHeader {
            theme: page.theme
            title: "Deep memory"
            subtitle: "2 × " + (page.demo.deepSamples / 1000).toFixed(0) + " k samples replaced every frame from a worker thread · pixel-column min/max decimation · hover for values"
        }
        RowLayout {
            spacing: 8
            Chip { theme: page.theme; accent: page.theme.ch0; label: "CH0"; value: "1 V/div" }
            Chip { theme: page.theme; accent: page.theme.ch1; label: "CH1"; value: "1 V/div" }
            Chip { theme: page.theme; label: "Timebase"; value: "100 ms/div" }
            Chip { theme: page.theme; label: "Record"; value: (page.demo.deepSamples / 1000).toFixed(0) + " kS / channel" }
            Item { Layout.fillWidth: true }
            Chip { theme: page.theme; accent: page.theme.accent; label: "Display"; value: page.fps > 0 ? page.fps.toFixed(0) + " fps" : "—" }
        }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            Plot {
                id: plot
                objectName: "deepPlot"
                anchors.fill: parent
                colorScheme: page.theme.plotScheme
                backgroundColor: page.theme.card
                graticule: Plot.Scope
                hoverReadout: true
                legendVisible: true
                xAxis { min: 0; max: 1.0; unit: "s"; unitInLabels: true; divisions: 10; lowerLimit: 0; upperLimit: 1.0 }
                yAxis { min: -4; max: 4; divisions: 8; labelsVisible: false; interactive: false }
                PlotTrace { id: ch0; name: "CH0 AM 1.2 kHz"; unit: "V"; buffer: page.demo.deep0; color: page.theme.ch0; width: 1.2; yOffset: 1.6 }
                PlotTrace { id: ch1; name: "CH1 5 Hz square"; unit: "V"; buffer: page.demo.deep1; color: page.theme.ch1; width: 1.2; yScale: 0.6; yOffset: -2.2 }
                PlotLine { value: 1.6; handle: PlotLine.Start; label: "0"; color: page.theme.ch0; lineMode: PlotLine.WhileDragging; onMoved: value => ch0.yOffset = value }
                PlotLine { value: -2.2; handle: PlotLine.Start; label: "1"; color: page.theme.ch1; lineMode: PlotLine.WhileDragging; onMoved: value => ch1.yOffset = value }
                PlotCursor { position: 0.3; label: "C1"; color: page.theme.cursor1 }
                PlotCursor { position: 0.5; label: "C2"; color: page.theme.cursor2 }
            }
        }
    }
}
