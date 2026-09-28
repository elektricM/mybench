import QtQuick
import QtQuick.Layouts
import MyBench.Framework

// The near-black display surface of an instrument (plots, readouts). Shows a SIMULATED tag
// when the device is simulated, an optional header line (left: `title`, right: `info`, e.g.
// "Sample Rate 200 kS/s") and overlay tools in the top-right corner.
//     DisplayArea { title: "CH0 · CH1"; info: "200 kS/s"; Plot { anchors.fill: parent } }
Rectangle {
    id: root

    property string title
    property string info
    property bool showSimulated: true
    property int padding: Theme.spacingM
    default property alias content: contentArea.data
    property alias tools: toolsRow.data
    property alias trailingTools: trailingRow.data   // framework tools (InstrumentPanel's focus toggle)
    property bool alwaysShowHeader: false
    readonly property bool headerVisible: alwaysShowHeader || title.length > 0 || info.length > 0
                                          || (showSimulated && DeviceHub.simulated) || toolsRow.children.length > 0
    // Content rectangle (below the header).
    readonly property alias contentItem: contentArea

    radius: Theme.radiusL
    color: Theme.display
    border.width: 1
    border.color: Theme.displayBorder
    clip: true

    RowLayout {
        id: header
        visible: root.headerVisible
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: root.padding
        anchors.rightMargin: Theme.spacingS
        height: 34
        spacing: Theme.spacingS

        Badge {
            visible: root.showSimulated && DeviceHub.simulated
            text: qsTr("Simulated")
            color: Theme.simulated
        }
        Text {
            Layout.fillWidth: true
            text: root.title
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeM
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Text {
            visible: root.info.length > 0
            text: root.info
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeM
            font.features: Theme.tabularFigures
        }
        Row {
            id: toolsRow
            spacing: 2
        }
        Row {
            id: trailingRow
            spacing: 2
        }
    }

    Item {
        id: contentArea
        anchors.fill: parent
        anchors.margins: root.padding
        anchors.topMargin: root.headerVisible ? header.height : root.padding
    }
}
