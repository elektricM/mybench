pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

// The measurement strip of a display (docs/UI.md §7, §13.8): rows of measurements × channel columns.
// "+" lists the `available` measurements (checked = shown; picking one adds or removes it); a row's
// context menu copies or removes it. `collapsed` shows only `summaryText` on one line (short windows).
//     MeasurementTable {
//         columns: [{ title: "CH0", color: Theme.signalCh0 }, { title: "CH1", color: Theme.signalCh1 }]
//         rows: [{ id: "rms", title: "RMS", values: ["0.707 V", "—"] }]
//         available: [{ id: "rms", title: "RMS" }, { id: "freq", title: "Frequency" }]
//         onAddRequested: id => ctl.addMeasurement(id)
//         onRemoveRequested: id => ctl.removeMeasurement(id)
//     }
Item {
    id: root

    property var columns: []          // [{ title, color }]
    property var rows: []             // [{ id, title, values: [string] }]
    property var available: []        // [{ id, title }]
    property string summaryText       // one-line summary when collapsed ("" = from the first rows)
    property bool collapsed: false
    property int maxVisibleRows: 5
    property string title: qsTr("Measurement")
    readonly property string effectiveSummary: summaryText.length > 0 ? summaryText
        : rows.slice(0, 3).map(r => r.title + " " + ((r.values ?? []).find(v => v && v.length > 0) ?? "—")).join(" · ")

    signal addRequested(string id)
    signal removeRequested(string id)

    function shown(id: string): bool {
        return rows.some(r => r.id === id)
    }
    function rowText(id: string): string {
        const row = rows.find(r => r.id === id)
        if (!row)
            return ""
        return [row.title].concat((row.values ?? []).map(v => v && v.length > 0 ? v : "—")).join("\t")
    }
    function copyText(text: string) {
        clipboard.text = text
        clipboard.selectAll()
        clipboard.copy()
    }
    TextEdit {
        id: clipboard
        visible: false
    }

    implicitWidth: table.implicitWidth
    implicitHeight: collapsed ? Theme.controlHeightS : table.implicitHeight

    DataTable {
        id: table
        visible: !root.collapsed
        anchors.fill: parent
        cornerTitle: root.title
        columns: root.columns
        rows: root.rows
        maxVisibleRows: root.maxVisibleRows
        emptyText: qsTr("No measurements. Add one with +.")
        onRowContextMenuRequested: (id, position) => {
            rowMenu.rowId = id
            rowMenu.popup(root, position)
        }
        tools: ToolButton {
            id: addButton
            visible: root.available.length > 0
            implicitWidth: Theme.controlHeightS
            implicitHeight: Theme.controlHeightS
            icon.source: Theme.icon("plus")
            icon.width: Theme.iconSizeS
            icon.height: Theme.iconSizeS
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: qsTr("Add or remove measurements")
            Accessible.name: qsTr("Measurements")
            onClicked: addMenu.popup(addButton, 0, addButton.height)
        }
    }

    Text {
        visible: root.collapsed
        x: Theme.spacingM
        width: parent.width - 2 * Theme.spacingM
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        text: root.effectiveSummary
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        font.features: Theme.tabularFigures
        elide: Text.ElideRight
    }

    Menu {
        id: addMenu
        // Instantiator + insertItem: the documented way to fill a (possibly native) menu.
        Instantiator {
            model: root.available
            delegate: MenuItem {
                required property var modelData
                text: modelData.title
                checkable: true
                checked: root.shown(modelData.id)
                onTriggered: root.shown(modelData.id) ? root.removeRequested(modelData.id) : root.addRequested(modelData.id)
            }
            onObjectAdded: (index, object) => addMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => addMenu.removeItem(object as MenuItem)
        }
    }
    Menu {
        id: rowMenu
        property string rowId
        MenuItem {
            text: qsTr("Copy")
            onTriggered: root.copyText(root.rowText(rowMenu.rowId))
        }
        MenuItem {
            text: qsTr("Remove")
            onTriggered: root.removeRequested(rowMenu.rowId)
        }
    }
}
