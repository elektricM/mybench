import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// One specimen group of a gallery page: a heading, a note (the rule it illustrates), then the
// examples.
ColumnLayout {
    id: spec

    property string title
    property string note
    default property alias content: body.data

    Layout.fillWidth: true
    spacing: Theme.spacingS

    Text {
        text: spec.title
        color: Theme.text
        font.pixelSize: Theme.fontSizeHeading
        font.weight: Font.DemiBold
    }
    Text {
        visible: spec.note.length > 0
        Layout.fillWidth: true
        text: spec.note
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }
    ColumnLayout {
        id: body
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingXs
        spacing: Theme.spacingM
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingM
        implicitHeight: Theme.borderWidth
        color: Theme.separator
    }
}
