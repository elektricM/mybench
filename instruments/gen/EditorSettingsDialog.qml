pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Controls

// Edit > Settings… of the Waveform Editor (ELVISmx "Waveform Editor Settings", gen.md 4.3.8):
// plot, selection, grid, cursor and plot-area colors (transparent = follow the light/dark theme), Max
// Points, Animate Graphs, Show Cursors. Saved as user preferences; OK applies, Cancel reverts.
Dialog {
    id: root

    required property WaveformEditor editor

    // Working copy (applied on OK).
    property color plotColor
    property color selectionColor
    property color gridColor
    property color cursorColor
    property color backgroundColor
    property bool animate: false
    property bool cursors: false

    function load() {
        plotColor = editor.plotColor
        selectionColor = editor.selectionColor
        gridColor = editor.gridColor
        cursorColor = editor.cursorColor
        backgroundColor = editor.backgroundColor
        animate = editor.animateGraphs
        cursors = editor.showCursors
        maxPoints.value = editor.maxPoints
    }

    title: qsTr("Waveform Editor Settings")
    anchors.centerIn: Overlay.overlay
    modal: true
    width: 420
    standardButtons: Dialog.Ok | Dialog.Cancel | Dialog.RestoreDefaults
    onOpened: load()
    onAccepted: {
        editor.plotColor = plotColor
        editor.selectionColor = selectionColor
        editor.gridColor = gridColor
        editor.cursorColor = cursorColor
        editor.backgroundColor = backgroundColor
        editor.maxPoints = maxPoints.value
        editor.animateGraphs = animate
        editor.showCursors = cursors
    }
    onReset: {
        editor.resetPreferences()
        load()
    }

    component ColorRow: RowLayout {
        id: colorRow
        required property string label
        required property color value
        required property color themeColor
        signal picked(color c)
        Layout.fillWidth: true
        spacing: Theme.spacingS
        Text {
            Layout.fillWidth: true
            text: colorRow.label
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeM
        }
        Rectangle {
            implicitWidth: 44
            implicitHeight: 22
            radius: Theme.radiusS
            color: colorRow.value.a > 0 ? colorRow.value : colorRow.themeColor
            border.width: 1
            border.color: Theme.borderStrong
            ToolTip.visible: swatchHover.hovered
            ToolTip.text: colorRow.value.a > 0 ? colorRow.value.toString() : qsTr("Theme color")
            HoverHandler { id: swatchHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: picker.open() }
        }
        ActionButton {
            text: qsTr("Theme")
            compact: true
            tone: "ghost"
            enabled: colorRow.value.a > 0
            toolTip: qsTr("Follow the light / dark theme")
            onClicked: colorRow.picked("transparent")
        }
        ColorDialog {
            id: picker
            title: colorRow.label
            selectedColor: colorRow.value.a > 0 ? colorRow.value : colorRow.themeColor
            onAccepted: colorRow.picked(selectedColor)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacingS

        ColorRow {
            label: qsTr("Plot Color")
            value: root.plotColor
            themeColor: Theme.accent
            onPicked: c => root.plotColor = c
        }
        ColorRow {
            label: qsTr("Selection Color")
            value: root.selectionColor
            themeColor: Theme.cursor1
            onPicked: c => root.selectionColor = c
        }
        ColorRow {
            label: qsTr("Grid Color")
            value: root.gridColor
            themeColor: Theme.border
            onPicked: c => root.gridColor = c
        }
        ColorRow {
            label: qsTr("Cursor Color")
            value: root.cursorColor
            themeColor: Theme.cursor1
            onPicked: c => root.cursorColor = c
        }
        ColorRow {
            label: qsTr("Plot Background")
            value: root.backgroundColor
            themeColor: Theme.display
            onPicked: c => root.backgroundColor = c
        }
        Divider { Layout.fillWidth: true }
        Field {
            Layout.fillWidth: true
            label: qsTr("Max Points")
            hint: qsTr("Largest number of samples a waveform may have (ELVISmx: 500 000). Edits that would exceed it are refused.")
            SiField {
                id: maxPoints
                Layout.fillWidth: true
                engineering: false
                decimals: 0
                stepSize: 100000
                from: 1000
                to: 10000000
                toolTip: qsTr("1 000 to 10 000 000 samples")
                onValueModified: v => value = v
            }
        }
        CheckBox {
            text: qsTr("Animate Graphs")
            checked: root.animate
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Glide the plot to the new range when the selection or view changes")
            onToggled: root.animate = checked
        }
        CheckBox {
            text: qsTr("Show Cursors")
            checked: root.cursors
            onToggled: root.cursors = checked
        }
    }
}
