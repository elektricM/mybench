pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Framework

// Running statistics of one or more channels (docs/UI.md §13.8): Count, Minimum, Maximum,
// Peak-to-peak, Mean, Std dev, Overloads as rows, one column per channel, Reset in the header.
//     StatisticsTable {
//         columns: [{ title: "DC voltage", unit: "V", count: s.count, minimum: s.min, maximum: s.max,
//                     peakToPeak: s.pp, mean: s.mean, stdDev: s.sigma, overloads: s.overloads }]
//         onResetRequested: ctl.resetStatistics()
//     }
Item {
    id: root

    property var columns: []    // [{ title, color, unit, count, minimum, maximum, peakToPeak, mean, stdDev, overloads }]
    property bool showReset: true
    property int digits: 5
    property bool collapsed: false
    property string title: qsTr("Statistics")

    signal resetRequested()

    function format(value: var, unit: string): string {
        if (value === undefined || value === null || !isFinite(value))
            return ""
        return Units.format(value, unit, root.digits, false)
    }
    function count(value: var): string {
        return value === undefined || value === null || !isFinite(value) ? "" : Number(value).toLocaleString(Qt.locale(), "f", 0)
    }
    readonly property var tableRows: [
        { id: "count", title: qsTr("Count"), values: columns.map(c => count(c.count)) },
        { id: "minimum", title: qsTr("Minimum"), values: columns.map(c => format(c.minimum, c.unit ?? "")) },
        { id: "maximum", title: qsTr("Maximum"), values: columns.map(c => format(c.maximum, c.unit ?? "")) },
        { id: "peakToPeak", title: qsTr("Peak-to-peak"), values: columns.map(c => format(c.peakToPeak, c.unit ?? "")) },
        { id: "mean", title: qsTr("Mean"), values: columns.map(c => format(c.mean, c.unit ?? "")) },
        { id: "stdDev", title: qsTr("Std deviation"), values: columns.map(c => format(c.stdDev, c.unit ?? "")) },
        { id: "overloads", title: qsTr("Overloads"), values: columns.map(c => count(c.overloads)) }
    ]
    readonly property string summaryText: columns.length === 0 ? ""
        : qsTr("Mean %1 · σ %2 · %3 readings").arg(format(columns[0].mean, columns[0].unit ?? "") || "—")
                                              .arg(format(columns[0].stdDev, columns[0].unit ?? "") || "—")
                                              .arg(count(columns[0].count) || "0")

    implicitWidth: table.implicitWidth
    implicitHeight: collapsed ? Theme.controlHeightS : table.implicitHeight

    DataTable {
        id: table
        visible: !root.collapsed
        anchors.fill: parent
        cornerTitle: root.title
        columns: root.columns.map(c => ({ title: c.title, color: c.color }))
        rows: root.tableRows
        maxVisibleRows: 7
        tools: Button {
            visible: root.showReset
            flat: true
            implicitHeight: Theme.controlHeightS
            text: qsTr("Reset")
            font.pixelSize: Theme.fontSizeSmall
            onClicked: root.resetRequested()
        }
    }

    Text {
        visible: root.collapsed
        x: Theme.spacingM
        width: parent.width - 2 * Theme.spacingM
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        text: root.summaryText
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        font.features: Theme.tabularFigures
        elide: Text.ElideRight
    }
}
