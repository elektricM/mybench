pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// The compact table under a display (docs/UI.md §7 "Measurement strip"): one row per quantity,
// one column per channel (swatch + name), values right-aligned with tabular figures. Up to
// `maxVisibleRows` rows, then it scrolls. Used by MeasurementTable and StatisticsTable.
//     DataTable { cornerTitle: qsTr("Measurement"); columns: [{ title: "CH0", color: Theme.signalCh0 }]
//                 rows: [{ id: "rms", title: "RMS", values: ["0.707 V"] }] }
Item {
    id: root

    property string cornerTitle
    property var columns: []            // [{ title, color }]
    property var rows: []               // [{ id, title, values: [string], toolTip }]
    property int maxVisibleRows: 5
    property string emptyText
    property alias tools: toolsRow.data  // header items on the right (e.g. "+" or Reset)
    readonly property int rowHeight: Theme.controlHeightS
    readonly property int headerHeight: Theme.controlHeightS + 2
    readonly property int padding: Theme.spacingM

    signal rowContextMenuRequested(string id, point position)

    // Column widths from the content, so values never jump.
    FontMetrics {
        id: metrics
        font.pixelSize: Theme.fontSizeSmall
        font.features: Theme.tabularFigures
    }
    readonly property real titleWidth: {
        let w = metrics.advanceWidth(cornerTitle)
        for (const row of rows)
            w = Math.max(w, metrics.advanceWidth(row.title ?? ""))
        return Math.ceil(Math.max(96, w + Theme.spacingL))
    }
    readonly property real columnWidth: {
        let w = 0
        for (const column of columns)
            w = Math.max(w, metrics.advanceWidth(column.title ?? "") + 14)
        for (const row of rows)
            for (const value of (row.values ?? []))
                w = Math.max(w, metrics.advanceWidth(String(value)))
        return Math.ceil(Math.max(88, w + Theme.spacingL))
    }

    implicitWidth: 2 * padding + titleWidth + columns.length * columnWidth + toolsRow.implicitWidth + Theme.spacingS
    implicitHeight: headerHeight + Theme.borderWidth
                    + Math.max(1, Math.min(rows.length, maxVisibleRows)) * rowHeight + Theme.spacingXs

    // Header.
    Item {
        id: header
        width: parent.width
        height: root.headerHeight

        Text {
            x: root.padding
            width: root.titleWidth
            height: parent.height
            verticalAlignment: Text.AlignVCenter
            text: root.cornerTitle
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
        }
        Row {
            x: root.padding + root.titleWidth
            height: parent.height
            Repeater {
                model: root.columns
                delegate: Item {
                    id: headerCell
                    required property var modelData
                    width: root.columnWidth
                    height: header.height
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingXs + 2
                        Rectangle {
                            visible: headerCell.modelData.color !== undefined
                            anchors.verticalCenter: parent.verticalCenter
                            width: 8
                            height: 8
                            radius: 2
                            color: headerCell.modelData.color ?? "transparent"
                        }
                        Text {
                            text: headerCell.modelData.title ?? ""
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }
        Row {
            id: toolsRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingXs
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXs
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.borderWidth
            color: Theme.separator
        }
    }

    ListView {
        id: list
        y: header.height + Theme.borderWidth
        width: parent.width
        height: parent.height - y
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        model: root.rows
        ScrollBar.vertical: ScrollBar {}
        WheelScroll { flickable: list }

        delegate: Item {
            id: rowItem
            required property var modelData
            required property int index
            width: list.width
            height: root.rowHeight

            HoverHandler { id: rowHover }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: (point, button) => root.rowContextMenuRequested(rowItem.modelData.id ?? "",
                                                                         rowItem.mapToItem(root, point.position.x, point.position.y))
            }
            Rectangle {
                anchors.fill: parent
                color: rowHover.hovered ? Theme.hoverFill : "transparent"
            }
            Text {
                x: root.padding
                width: root.titleWidth
                height: parent.height
                verticalAlignment: Text.AlignVCenter
                text: rowItem.modelData.title ?? ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
            Row {
                x: root.padding + root.titleWidth
                height: parent.height
                Repeater {
                    model: root.columns.length
                    delegate: Text {
                        required property int index
                        readonly property var value: (rowItem.modelData.values ?? [])[index]
                        width: root.columnWidth
                        height: rowItem.height
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        text: value === undefined || value === null || value === "" ? "—" : String(value)
                        color: value === undefined || value === null || value === "" ? Theme.textTertiary : Theme.text
                        font.pixelSize: Theme.fontSizeSmall
                        font.features: Theme.tabularFigures
                    }
                }
            }
        }
    }

    Text {
        visible: root.rows.length === 0 && root.emptyText.length > 0
        x: root.padding
        y: header.height + Theme.borderWidth
        height: root.rowHeight
        verticalAlignment: Text.AlignVCenter
        text: root.emptyText
        color: Theme.textTertiary
        font.pixelSize: Theme.fontSizeSmall
    }
}
