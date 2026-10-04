import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// A scrolling gallery page: title, one-line description, then GallerySpec groups.
ScrollView {
    id: page

    property string title
    property string description
    default property alias specs: column.data

    contentWidth: availableWidth

    ColumnLayout {
        id: column
        x: Theme.spacingXl
        width: page.availableWidth - 2 * Theme.spacingXl
        spacing: Theme.spacingXl

        Column {
            Layout.topMargin: Theme.spacingXl
            Layout.fillWidth: true
            spacing: Theme.spacingXs
            Text {
                text: page.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.DemiBold
            }
            Text {
                width: parent.width
                text: page.description
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
            }
        }
    }
}
