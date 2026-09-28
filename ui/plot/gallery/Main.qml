// MyBench.Plot gallery: one page per plot use case. --page <id> selects the page.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Plot.Gallery

Rectangle {
    id: root
    width: 1280
    height: 800
    property string page: "scope"
    property bool dark: true
    property bool benchmark: false
    color: theme.window

    readonly property GalleryTheme theme: GalleryTheme {
        dark: root.dark
    }

    GalleryData {
        id: galleryData
    }

    readonly property var pages: [
        { id: "scope", title: "Oscilloscope", icon: "◠" },
        { id: "deep", title: "Deep memory", icon: "≋" },
        { id: "bode", title: "Bode", icon: "⌁" },
        { id: "spectrum", title: "Spectrum", icon: "▁▃▅" },
        { id: "waterfall", title: "Waterfall", icon: "▤" },
        { id: "trend", title: "DMM trend", icon: "∿" },
        { id: "xy", title: "XY mode", icon: "∞" },
        { id: "recorder", title: "Recorder", icon: "⏺" },
        { id: "logic", title: "Digital I/O", icon: "⎍" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 208
            color: root.theme.sidebar
            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: root.theme.border
            }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 4
                Column {
                    Layout.bottomMargin: 14
                    Layout.leftMargin: 6
                    Layout.topMargin: 4
                    spacing: 2
                    Text {
                        text: "MyBench.Plot"
                        color: root.theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: "GPU plotting gallery"
                        color: root.theme.muted
                        font.pixelSize: 12
                    }
                }
                Repeater {
                    model: root.pages
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        readonly property bool current: root.page === modelData.id
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        radius: 7
                        color: current ? (root.dark ? "#1b2230" : "#e3ebfb") : (hover.hovered ? root.theme.raised : "transparent")
                        Rectangle {
                            visible: navItem.current
                            width: 3
                            height: 16
                            radius: 1.5
                            color: root.theme.accent
                            anchors.left: parent.left
                            anchors.leftMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 14
                            text: navItem.modelData.title
                            color: navItem.current ? root.theme.text : root.theme.muted
                            font.pixelSize: 13
                            font.weight: navItem.current ? Font.DemiBold : Font.Normal
                        }
                        HoverHandler {
                            id: hover
                        }
                        TapHandler {
                            onTapped: root.page = navItem.modelData.id
                        }
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
                Text {
                    Layout.leftMargin: 6
                    text: "Wheel: zoom · drag: pan\nShift-drag: zoom box\nDouble-click: reset"
                    color: root.theme.muted
                    font.pixelSize: 11
                    lineHeight: 1.25
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: {
                switch (root.page) {
                case "deep": return deepPage
                case "bode": return bodePage
                case "spectrum": return spectrumPage
                case "waterfall": return waterfallPage
                case "trend": return trendPage
                case "xy": return xyPage
                case "recorder": return recorderPage
                case "logic": return logicPage
                default: return scopePage
                }
            }
        }
    }

    Component { id: scopePage; ScopePage { theme: root.theme; demo: galleryData } }
    Component { id: deepPage; DeepPage { theme: root.theme; demo: galleryData } }
    Component { id: bodePage; BodePage { theme: root.theme; demo: galleryData } }
    Component { id: spectrumPage; SpectrumPage { theme: root.theme; demo: galleryData } }
    Component { id: waterfallPage; WaterfallPage { theme: root.theme; demo: galleryData } }
    Component { id: trendPage; TrendPage { theme: root.theme; demo: galleryData } }
    Component { id: xyPage; XYPage { theme: root.theme; demo: galleryData } }
    Component { id: recorderPage; RecorderPage { theme: root.theme; demo: galleryData } }
    Component { id: logicPage; LogicPage { theme: root.theme; demo: galleryData } }
}
