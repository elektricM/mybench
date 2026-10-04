pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The living specification of the MYBENCH design system (docs/UI.md v2): one page per component
// family, in the current theme. A positional argument picks the page (screenshots):
//     mybench-controls-gallery forms --theme dark --screenshot forms-dark.png
ApplicationWindow {
    id: window

    readonly property var pages: [
        { id: "foundations", title: qsTr("Foundations"), icon: "grid", source: "GalleryFoundations.qml" },
        { id: "buttons", title: qsTr("Buttons and Inputs"), icon: "sliders", source: "GalleryButtons.qml" },
        { id: "forms", title: qsTr("Inspector and Forms"), icon: "list", source: "GalleryForms.qml" },
        { id: "readouts", title: qsTr("Readouts and Tables"), icon: "activity", source: "GalleryReadouts.qml" },
        { id: "indicators", title: qsTr("Indicators and Messages"), icon: "bell", source: "GalleryIndicators.qml" },
        { id: "plots", title: qsTr("Plots"), icon: "trend", source: "GalleryPlots.qml" },
        { id: "instrument", title: qsTr("Instrument Controls"), icon: "instrument-scope", source: "GalleryInstrument.qml" }
    ]
    property int pageIndex: {
        for (const argument of AppInfo.arguments) {
            const index = pages.findIndex(p => p.id === argument)
            if (index >= 0)
                return index
        }
        return 0
    }

    width: 1440
    height: 900
    minimumWidth: Theme.minWindowWidth
    minimumHeight: Theme.minWindowHeight
    visible: true
    title: qsTr("Design System — %1").arg(AppInfo.name)

    ThemeSync { mode: AppInfo.theme.length > 0 ? AppInfo.theme : "dark" }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Page list.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingS
                spacing: 2

                Text {
                    Layout.leftMargin: Theme.spacingS
                    Layout.topMargin: Theme.spacingS
                    Layout.bottomMargin: Theme.spacingS
                    text: qsTr("Design System")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeCaption
                }
                Repeater {
                    model: window.pages
                    delegate: ItemDelegate {
                        id: row
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        text: modelData.title
                        icon.source: Theme.icon(modelData.icon)
                        highlighted: window.pageIndex === index
                        onClicked: window.pageIndex = index
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.leftMargin: Theme.spacingS
                    text: (Theme.dark ? qsTr("Dark") : qsTr("Light"))
                          + (Theme.followSystem ? qsTr(" · system palette") : qsTr(" · reference palette"))
                          + " · " + Theme.fontSizeBase + " px"
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeCaption
                }
            }
            Rectangle {
                anchors.right: parent.right
                width: Theme.borderWidth
                height: parent.height
                color: Theme.separator
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: window.pages[window.pageIndex].source
        }
    }

    ToastArea {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingL
    }
}
