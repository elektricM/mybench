// Bode: gain and phase plots sharing one log-frequency axis (zoom/pan together), aligned
// margins, −3 dB marker with guides, linked cursors, reference sweep overlay.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot
import MyBench.Plot.Gallery

Item {
    id: page
    required property GalleryTheme theme
    required property GalleryData demo
    readonly property real leftMargin: Math.max(gain.implicitLeftMargin, phase.implicitLeftMargin)

    PlotAxis {
        id: frequency
        scale: PlotAxis.Log
        min: 10
        max: 100000
        unit: "Hz"
        title: "Frequency"
        lowerLimit: 1
        upperLimit: 1e6
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        PageHeader {
            theme: page.theme
            title: "Bode analyzer"
            subtitle: "Stacked gain / phase plots share a log frequency axis · −3 dB marker · linked snapping cursors · reference sweep"
        }
        RowLayout {
            spacing: 8
            Chip { theme: page.theme; label: "Start"; value: "10 Hz" }
            Chip { theme: page.theme; label: "Stop"; value: "100 kHz" }
            Chip { theme: page.theme; label: "Steps"; value: "20 / decade" }
            Chip { theme: page.theme; label: "Stimulus"; value: "AO0 · 1 Vpk" }
            Item { Layout.fillWidth: true }
            Chip { theme: page.theme; accent: page.theme.ch0; label: "Measured"; value: "2nd-order low-pass" }
            Chip { theme: page.theme; accent: page.theme.violet; label: "Reference"; value: "RC 2.2 kHz" }
        }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                Plot {
                    id: gain
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 3
                    colorScheme: page.theme.plotScheme
                    backgroundColor: page.theme.card
                    xAxis: frequency
                    xLabelsVisible: false
                    leftMargin: page.leftMargin
                    yAxis { min: -80; max: 10; unit: "dB"; title: "Gain"; tickSpacing: 40 }
                    PlotTrace { id: gainRef; name: "Reference"; buffer: page.demo.bodeGainRef; color: page.theme.violet; width: 1.3; style: PlotElement.Dash }
                    PlotTrace { id: gainTrace; name: "Measured"; buffer: page.demo.bodeGain; color: page.theme.ch0; width: 2; pointsVisible: true; pointSize: 5 }
                    PlotMarker {
                        x: page.demo.cutoff
                        trace: gainTrace
                        shape: PlotMarker.Ring
                        size: 10
                        color: page.theme.amber
                        guides: true
                        labelPosition: PlotMarker.Right
                        label: "−3 dB  " + gain.format(page.demo.cutoff, "Hz")
                    }
                    PlotCursor {
                        id: gainCursor
                        position: 3000
                        label: "C1"
                        color: page.theme.cursor1
                        trace: gainTrace
                        snap: true
                    }
                }
                Plot {
                    id: phase
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 2.4
                    colorScheme: page.theme.plotScheme
                    backgroundColor: page.theme.card
                    xAxis: frequency
                    leftMargin: page.leftMargin
                    yAxis { min: -225; max: 45; divisions: 6; unit: "°"; title: "Phase"; minorTicks: 3 }
                    PlotTrace { name: "Reference"; buffer: page.demo.bodePhaseRef; color: page.theme.violet; width: 1.3; style: PlotElement.Dash }
                    PlotTrace { id: phaseTrace; name: "Measured"; buffer: page.demo.bodePhase; color: page.theme.ch1; width: 2; pointsVisible: true; pointSize: 5 }
                    PlotMarker {
                        x: page.demo.cutoff
                        y: page.demo.phaseAtCutoff
                        shape: PlotMarker.Ring
                        size: 10
                        color: page.theme.amber
                        guides: true
                        labelPosition: PlotMarker.Right
                        label: gain.format(page.demo.phaseAtCutoff, "°", 3) + " at fc"
                    }
                    PlotCursor {
                        position: gainCursor.position
                        label: "C1"
                        color: page.theme.cursor1
                        trace: phaseTrace
                        snap: true
                        onMoved: position => gainCursor.position = position
                    }
                }
            }
        }
        RowLayout {
            spacing: 10
            Readout { theme: page.theme; label: "−3 dB frequency"; value: gain.format(page.demo.cutoff, "Hz"); accent: page.theme.amber }
            Readout { theme: page.theme; label: "Phase at fc"; value: gain.format(page.demo.phaseAtCutoff, "°", 3) }
            Readout { theme: page.theme; label: "Peaking"; value: "+0.9 dB" }
            Item { Layout.fillWidth: true }
            Readout { theme: page.theme; label: "C1 frequency"; value: gain.format(gainCursor.position, "Hz") }
            Readout { theme: page.theme; label: "C1 gain"; value: gain.format(gainCursor.value, "dB") ; accent: page.theme.ch0 }
        }
    }
}
