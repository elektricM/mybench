// XY mode: CH1 against CH0 (unsorted data drawn as a path) with persistence.
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
            title: "XY mode"
            subtitle: "CH1 versus CH0 · 6000-point Lissajous (3 : 2, drifting phase) · path de-duplicated per pixel · 600 ms persistence"
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            Card {
                theme: page.theme
                Layout.fillHeight: true
                Layout.preferredWidth: height * 1.02
                Plot {
                    id: plot
                    anchors.fill: parent
                    colorScheme: page.theme.plotScheme
                    backgroundColor: page.theme.card
                    graticule: Plot.Scope
                    xAxis { min: -4; max: 4; divisions: 8; unit: "V"; title: "CH0" }
                    yAxis { min: -4; max: 4; divisions: 8; unit: "V"; title: "CH1" }
                    PlotTrace { buffer: page.demo.xy; color: page.theme.ch0; width: 1.4; persistence: 600 }
                }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                spacing: 10
                Readout { theme: page.theme; label: "CH0"; value: "1 V/div"; accent: page.theme.ch0 }
                Readout { theme: page.theme; label: "CH1"; value: "1 V/div"; accent: page.theme.ch1 }
                Readout { theme: page.theme; label: "Frequency ratio"; value: "3 : 2" }
                Readout { theme: page.theme; label: "Points"; value: page.demo.xy.count.toString() }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
