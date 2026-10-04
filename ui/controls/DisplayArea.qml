import QtQuick

// The display of an instrument pane (docs/UI.md §7 "Display"): edge to edge in `Theme.display`,
// no frame, no radius, no badges; an optional one-line read-only summary at the top left
// ("CH0 1 V/div · 5 ms/div · Edge ↑ CH0 0.00 V"). InstrumentPanel creates it; its `display`
// children go into the content area.
//     DisplayArea { summary: "5 ms/div"; BenchPlot { anchors.fill: parent } }
Rectangle {
    id: root

    property string summary
    property int padding: Theme.spacingS
    default property alias content: contentArea.data
    readonly property alias contentItem: contentArea
    // v1 names (accepted): `title` is the summary; `info`, `showSimulated`, `alwaysShowHeader`
    // are ignored; `tools`/`trailingTools` sit at the top right of the display.
    property alias title: root.summary
    property string info
    property bool showSimulated: false
    property bool alwaysShowHeader: false
    property alias tools: toolsRow.data
    property alias trailingTools: trailingRow.data

    color: Theme.display
    clip: true

    Text {
        id: summaryLabel
        objectName: "displaySummary"
        visible: root.summary.length > 0
        x: root.padding + Theme.spacingXs
        y: Theme.spacingXs
        width: Math.max(0, root.width - x - (toolsBar.used ? toolsBar.width : 0) - 2 * root.padding)
        text: root.summary
        color: Theme.displayTextSecondary
        font.pixelSize: Theme.fontSizeCaption
        font.features: Theme.tabularFigures
        elide: Text.ElideRight
        maximumLineCount: 1
    }
    Row {
        id: toolsBar
        readonly property bool used: toolsRow.children.length + trailingRow.children.length > 0
        visible: used
        anchors.right: parent.right
        anchors.rightMargin: root.padding
        y: Theme.spacingXs
        spacing: Theme.spacingXs
        Row {
            id: toolsRow
            spacing: Theme.spacingXs
        }
        Row {
            id: trailingRow
            spacing: Theme.spacingXs
        }
    }

    Item {
        id: contentArea
        anchors.fill: parent
        anchors.margins: root.padding
        anchors.topMargin: summaryLabel.visible || toolsBar.used
                           ? Math.max(summaryLabel.visible ? summaryLabel.height : 0, toolsBar.used ? toolsBar.height : 0)
                             + 2 * Theme.spacingXs
                           : root.padding
    }
}
