pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// One entry of the sidebar (docs/UI.md §7): a 20 px icon, the name and a trailing state dot
// (running = positive, starting or paused = caution, error = negative, stopped = none), and a
// second line with the instrument's key value. On hover a small Run/Stop button replaces the dot.
// Compact (56 px sidebar): the icon with the dot on its corner; the tooltip gives name, state
// and key value. `paneId` "" is the Overview entry.
Item {
    id: row

    property string paneId
    property string title
    property string iconName
    property InstrumentController controller: null
    property bool child: false               // a view of an instrument (indented)
    property bool selected: false            // shown in the focused pane
    property bool shownBeside: false         // shown in the other pane of the split
    property bool current: false             // keyboard cursor of the sidebar
    property bool listFocused: false         // the sidebar has the keyboard focus
    property bool compact: false
    property Menu contextMenu: null

    readonly property string keyValue: controller && !child ? controller.keyValue : ""
    readonly property bool running: controller !== null && controller.running
    readonly property string status: !controller || child ? ""
        : controller.paused ? "paused"
        : controller.state === InstrumentController.Starting ? "starting"
        : controller.running ? "running"
        : controller.hasError ? "error" : ""
    readonly property color statusColor: status === "running" ? Theme.positive
        : status === "paused" || status === "starting" ? Theme.caution
        : status === "error" ? Theme.negative : "transparent"
    readonly property string statusText: status === "running" ? qsTr("Running")
        : status === "paused" ? qsTr("Paused") : status === "starting" ? qsTr("Starting")
        : status === "error" ? qsTr("Error") : controller && !child ? qsTr("Stopped") : ""
    readonly property bool hovered: hover.hovered
    readonly property bool canToggle: controller !== null && !child && (running || controller.canRun)
    readonly property bool activeSelection: selected && listFocused && Window.active
    readonly property color foreground: activeSelection ? Theme.selectionText : Theme.text

    signal clicked(int modifiers)

    implicitWidth: compact ? Theme.sidebarCompactWidth : Theme.sidebarWidth
    implicitHeight: compact ? Theme.controlHeight + Theme.spacingS
                            : Theme.spacingS + nameText.implicitHeight + (secondLine.visible ? secondLine.implicitHeight : 0) + Theme.spacingS - 2

    Accessible.role: Accessible.ListItem
    Accessible.name: title
    Accessible.description: [statusText, keyValue].filter(s => s.length > 0).join(", ")
    Accessible.selected: selected

    HoverHandler { id: hover }
    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton
        onTapped: row.clicked(tap.point.modifiers)
    }
    ContextMenu.menu: row.contextMenu

    ToolTip.visible: compact && hover.hovered
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: [title, statusText, keyValue].filter(s => s.length > 0).join(" — ")

    // Selection / hover background, inset like a source list.
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: row.compact ? Theme.spacingS : Theme.spacingS
        anchors.rightMargin: row.compact ? Theme.spacingS : Theme.spacingS
        anchors.topMargin: 1
        anchors.bottomMargin: 1
        radius: Theme.radiusControl
        color: row.activeSelection ? Theme.selection
             : row.selected ? Theme.selectionInactive
             : row.shownBeside ? Theme.hoverFill
             : row.hovered ? Theme.hoverFill : "transparent"
        border.width: row.current && row.listFocused && !row.selected ? Theme.focusWidth : 0
        border.color: Theme.focusRing
    }

    // --- Expanded -------------------------------------------------------------------------------
    RowLayout {
        visible: !row.compact
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingL + (row.child ? Theme.iconSizeL + Theme.spacingS : 0)
        anchors.rightMargin: Theme.spacingL
        spacing: Theme.spacingS

        Icon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Math.round((nameText.implicitHeight - height) / 2) + Theme.spacingS - 1
            name: row.iconName
            size: row.child ? Theme.iconSize : Theme.iconSizeL
            color: row.activeSelection ? Theme.selectionText : row.selected ? Theme.accent : Theme.textSecondary
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            Text {
                id: nameText
                Layout.fillWidth: true
                text: row.title
                color: row.foreground
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
            }
            Text {
                id: secondLine
                Layout.fillWidth: true
                visible: row.keyValue.length > 0
                text: row.keyValue
                color: row.activeSelection ? Theme.selectionText : Theme.textSecondary
                font.pixelSize: Theme.fontSizeCaption
                font.features: Theme.tabularFigures
                elide: Text.ElideRight
            }
        }
        // Trailing: the state dot, or Run/Stop while hovered.
        Item {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Theme.spacingS - 4
            implicitWidth: Theme.controlHeightS
            implicitHeight: Theme.controlHeightS
            visible: row.controller !== null && !row.child

            Rectangle {
                anchors.centerIn: parent
                visible: !runButton.visible && row.status.length > 0
                width: Theme.spacingS - 1
                height: width
                radius: width / 2
                color: row.statusColor
            }
            ToolButton {
                id: runButton
                objectName: "sidebarRun-" + row.paneId
                anchors.fill: parent
                visible: row.hovered && row.canToggle
                icon.source: Theme.icon(row.running ? "stop-filled" : "play")
                icon.width: Theme.iconSize - 2
                icon.height: Theme.iconSize - 2
                icon.color: row.activeSelection ? Theme.selectionText : row.running ? Theme.text : Theme.accent
                Accessible.name: row.running ? qsTr("Stop %1").arg(row.title) : qsTr("Run %1").arg(row.title)
                ToolTip.visible: hovered
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: Accessible.name
                onClicked: row.controller.toggle()
            }
        }
    }

    // --- Compact --------------------------------------------------------------------------------
    Item {
        visible: row.compact
        anchors.centerIn: parent
        width: Theme.iconSizeL
        height: Theme.iconSizeL
        Icon {
            anchors.centerIn: parent
            name: row.iconName
            size: Theme.iconSizeL
            color: row.activeSelection ? Theme.selectionText : row.selected ? Theme.accent : Theme.textSecondary
        }
        Rectangle {
            visible: row.status.length > 0
            x: parent.width - width / 2 - 1
            y: -height / 2 + 1
            width: Theme.spacingS
            height: width
            radius: width / 2
            color: row.statusColor
            border.width: Theme.borderWidth + 1
            border.color: Theme.sidebar
        }
    }
}
