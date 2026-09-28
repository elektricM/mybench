// Recorder: a 2·10^7-sample (and growing) recording, rendered in O(pixels) through the min/max
// pyramid; overview strip with a draggable window; follow-latest mode.
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
            title: "Recorder"
            subtitle: "AI0 logged at 200 kS/s · min/max pyramid (16× levels) → any zoom in O(pixels) · drag the window in the overview · pan to stop following"
        }
        RowLayout {
            spacing: 8
            Chip { theme: page.theme; accent: page.theme.ch0; label: "AI0"; value: "200 kS/s" }
            Chip { theme: page.theme; label: "Recorded"; value: (page.demo.recordedSamples / 1e6).toFixed(2) + " M samples" }
            Chip { theme: page.theme; label: "Duration"; value: main.format(page.demo.recordedSamples / 200000, "s", 4) }
            Chip { theme: page.theme; label: "View"; value: main.format(main.xAxis.span, "s", 3) }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: followLabel.implicitWidth + 28
                implicitHeight: 28
                radius: 7
                color: main.xAxis.follow ? Qt.alpha(page.theme.accent, 0.18) : page.theme.card
                border.color: main.xAxis.follow ? page.theme.accent : page.theme.border
                Text {
                    id: followLabel
                    anchors.centerIn: parent
                    text: main.xAxis.follow ? "● Following latest" : "Follow latest"
                    color: main.xAxis.follow ? page.theme.text : page.theme.muted
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
                TapHandler {
                    onTapped: main.xAxis.follow = !main.xAxis.follow
                }
            }
        }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 4
                Plot {
                    id: main
                    objectName: "recorderMain"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    colorScheme: page.theme.plotScheme
                    backgroundColor: page.theme.card
                    hoverReadout: true
                    leftMargin: overview.implicitLeftMargin > implicitLeftMargin ? overview.implicitLeftMargin : -1
                    xAxis { labelFormat: PlotAxis.Time; follow: true; tickSpacing: 120 }
                    yAxis { min: -3; max: 3; unit: "V"; title: "AI0" }
                    PlotTrace { name: "AI0"; unit: "V"; buffer: page.demo.recording; color: page.theme.ch0; width: 1.2 }
                    Component.onCompleted: {
                        const last = page.demo.recording.xMaximum()
                        xAxis.setRange(last - 8, last)
                    }
                }
                Plot {
                    id: overview
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    colorScheme: page.theme.plotScheme
                    backgroundColor: page.theme.card
                    leftMargin: main.implicitLeftMargin
                    interactive: false
                    xAxis { labelFormat: PlotAxis.Time; autoScale: true; interactive: false; tickSpacing: 120 }
                    yAxis { min: -3; max: 3; labelsVisible: false; gridVisible: false; interactive: false }
                    PlotTrace { buffer: page.demo.recording; color: Qt.alpha(page.theme.ch0, 0.85); width: 1; fill: true; fillBaseline: 0; fillOpacity: 0.25 }
                    PlotRegion {
                        from: main.xAxis.min
                        to: main.xAxis.max
                        draggable: true
                        color: Qt.alpha(page.theme.accent, 0.20)
                        onMoved: (from, to) => {
                            main.xAxis.follow = false
                            main.xAxis.setViewRange(from, to)
                        }
                    }
                }
            }
        }
    }
}
