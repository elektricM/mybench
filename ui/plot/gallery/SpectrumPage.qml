// Spectrum analyzer: dBV spectrum with gradient fill, peak-hold overlay, harmonic markers.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo
    readonly property var harmonics: demo.harmonics
    readonly property real thd: {
        if (harmonics.length < 2)
            return NaN
        let sum = 0
        for (let i = 1; i < harmonics.length; ++i)
            sum += Math.pow(10, harmonics[i].level / 10)
        return 10 * Math.log10(sum) - harmonics[0].level
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        PageHeader {
            theme: page.theme
            title: "Spectrum analyzer"
            subtitle: "8192-point FFT, Hanning window · gradient fill to the plot bottom · peak hold · harmonic markers from an Instantiator"
        }
        RowLayout {
            spacing: 8
            Chip { theme: page.theme; accent: page.theme.ch0; label: "AI0"; value: "±2 V" }
            Chip { theme: page.theme; label: "Span"; value: "0 – 10 kHz" }
            Chip { theme: page.theme; label: "Resolution"; value: "6.25 Hz" }
            Chip { theme: page.theme; label: "Window"; value: "Hanning" }
            Chip { theme: page.theme; label: "Averaging"; value: "RMS × 8" }
            Item { Layout.fillWidth: true }
            Chip { theme: page.theme; accent: page.theme.amber; label: "Peak hold"; value: "on" }
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
                legendVisible: true
                xAxis { min: 0; max: 10000; unit: "Hz"; title: "Frequency"; lowerLimit: 0; upperLimit: 25600 }
                yAxis { min: -140; max: 0; unit: "dBV"; title: "Magnitude"; tickSpacing: 44 }
                PlotTrace { name: "Peak hold"; buffer: page.demo.spectrumAverage; color: Qt.alpha(page.theme.amber, 0.8); width: 1 }
                PlotTrace { id: spectrum; name: "AI0  (dBV rms)"; buffer: page.demo.spectrum; color: page.theme.ch0; width: 1.3; fill: true; fillOpacity: 0.28 }
            }
            Instantiator {
                model: page.harmonics
                delegate: PlotMarker {
                    required property var modelData
                    required property int index
                    plot: plot
                    x: modelData.x
                    y: modelData.y
                    shape: PlotMarker.TriangleDown
                    size: 9
                    color: index === 0 ? page.theme.amber : page.theme.text
                    label: modelData.name + "  " + plot.format(modelData.level, "dBV", 3)
                }
            }
        }
        RowLayout {
            spacing: 10
            Readout { theme: page.theme; label: "Fundamental"; value: page.harmonics.length ? plot.format(page.harmonics[0].x, "Hz") : "—"; accent: page.theme.amber }
            Readout { theme: page.theme; label: "Level"; value: page.harmonics.length ? plot.format(page.harmonics[0].level, "dBV") : "—" }
            Readout { theme: page.theme; label: "THD"; value: plot.format(page.thd, "dB", 3) }
            Readout { theme: page.theme; label: "THD %"; value: (100 * Math.pow(10, page.thd / 20)).toFixed(3) + " %" }
            Item { Layout.fillWidth: true }
            Readout { theme: page.theme; label: "Noise floor"; value: "−118 dBV" }
        }
    }
}
