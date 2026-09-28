// Spectrogram: live spectrum over a scrolling waterfall (GPU texture ring, colormap), sharing the
// frequency axis.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo
    readonly property real leftMargin: Math.max(live.implicitLeftMargin, fall.implicitLeftMargin)

    PlotAxis {
        id: frequency
        min: 0
        max: 10240
        unit: "Hz"
        title: "Frequency"
        lowerLimit: 0
        upperLimit: 10240
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        PageHeader {
            theme: page.theme
            title: "Waterfall"
            subtitle: "512-bin spectra every 50 ms into a 240-row texture ring (only new rows are uploaded) · Inferno colormap · shared frequency axis"
        }
        RowLayout {
            spacing: 8
            Chip { theme: page.theme; accent: page.theme.ch1; label: "Audio in"; value: "20.48 kS/s" }
            Chip { theme: page.theme; label: "FFT"; value: "1024 · Blackman-Harris" }
            Chip { theme: page.theme; label: "History"; value: "12 s" }
            Item { Layout.fillWidth: true }
            Chip { theme: page.theme; label: "Range"; value: "−130 … −10 dBV" }
        }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Plot {
                    id: live
                    Layout.fillWidth: true
                    Layout.preferredHeight: parent.height * 0.32
                    Layout.rightMargin: 64
                    colorScheme: page.theme.plotScheme
                    backgroundColor: page.theme.card
                    xAxis: frequency
                    xLabelsVisible: false
                    leftMargin: page.leftMargin
                    yAxis { min: -140; max: 0; unit: "dBV"; title: "Level"; tickSpacing: 40 }
                    PlotTrace { buffer: page.demo.liveSpectrum; color: page.theme.ch1; width: 1.2; fill: true; fillOpacity: 0.3 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    Plot {
                        id: fall
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        colorScheme: page.theme.plotScheme
                        backgroundColor: page.theme.card
                        xAxis: frequency
                        leftMargin: page.leftMargin
                        gridColor: Qt.rgba(1, 1, 1, 0.10)
                        minorGridColor: "transparent"
                        yAxis { min: -12; max: 0; unit: "s"; title: "Time"; interactive: false; tickSpacing: 60 }
                        PlotWaterfall {
                            id: waterfall
                            buffer: page.demo.waterfall
                            minimum: -130
                            maximum: -10
                            rowHeight: 0.05
                            colormap: PlotWaterfall.Inferno
                        }
                    }
                    // Color bar.
                    Item {
                        Layout.preferredWidth: 64
                        Layout.fillHeight: true
                        Rectangle {
                            id: bar
                            x: 14
                            y: fall.plotArea.y
                            width: 12
                            height: fall.plotArea.height
                            radius: 3
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: waterfall.colorAt(1.0) }
                                GradientStop { position: 0.125; color: waterfall.colorAt(0.875) }
                                GradientStop { position: 0.25; color: waterfall.colorAt(0.75) }
                                GradientStop { position: 0.375; color: waterfall.colorAt(0.625) }
                                GradientStop { position: 0.5; color: waterfall.colorAt(0.5) }
                                GradientStop { position: 0.625; color: waterfall.colorAt(0.375) }
                                GradientStop { position: 0.75; color: waterfall.colorAt(0.25) }
                                GradientStop { position: 0.875; color: waterfall.colorAt(0.125) }
                                GradientStop { position: 1.0; color: waterfall.colorAt(0.0) }
                            }
                        }
                        Repeater {
                            model: [-10, -40, -70, -100, -130]
                            delegate: Text {
                                required property var modelData
                                x: bar.x + bar.width + 6
                                y: bar.y + (-10 - modelData) / 120 * bar.height - height / 2
                                text: fall.format(modelData, "", 3).replace(".0", "")
                                color: page.theme.muted
                                font.pixelSize: 11
                                font.features: { "tnum": 1 }
                            }
                        }
                    }
                }
            }
        }
    }
}
