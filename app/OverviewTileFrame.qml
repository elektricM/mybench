pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// An instrument tile of the Overview (the shape of OverviewTile, docs/UI.md §13.8, until it lands
// in MyBench.Controls): icon, name, state, key value and the instrument's own preview
// (<Name>Tile.qml). Click shows the instrument; Run/Stop appears on hover; the context menu has
// Show, Show Beside, Run/Stop and Pin to Top.
Control {
    id: tile

    property string title
    property string iconName
    property string status                  // "running" | "starting" | "paused" | "error" | ""
    property string keyValue
    property Component preview: null
    property bool pinned: false
    property bool running: false
    property bool canToggle: false
    property Menu menu: null

    signal clicked()
    signal runToggled()

    readonly property color statusColor: status === "running" ? Theme.positive
        : status === "paused" || status === "starting" ? Theme.caution
        : status === "error" ? Theme.negative : "transparent"
    readonly property string statusText: status === "running" ? qsTr("Running")
        : status === "paused" ? qsTr("Paused") : status === "starting" ? qsTr("Starting")
        : status === "error" ? qsTr("Error") : ""

    hoverEnabled: true
    padding: Theme.spacingM
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: [statusText, keyValue].filter(s => s.length > 0).join(", ")
    Accessible.onPressAction: tile.clicked()
    Keys.onReturnPressed: tile.clicked()
    Keys.onSpacePressed: tile.clicked()

    ContextMenu.menu: tile.menu

    background: Rectangle {
        radius: Theme.radiusPopup
        color: tile.hovered ? Theme.mix(Theme.base, Theme.text, 0.025) : Theme.base
        border.width: Theme.borderWidth
        border.color: tile.hovered ? Theme.controlBorder : Theme.separator
        FocusRing { shown: tile.visualFocus; radius: Theme.radiusPopup + 3 }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: tile.clicked()
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeightS
            spacing: Theme.spacingS
            Icon {
                name: tile.iconName
                size: Theme.iconSizeL
                color: Theme.textSecondary
            }
            Text {
                Layout.fillWidth: true
                text: tile.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Icon {
                visible: tile.pinned
                name: "pin"
                size: Theme.iconSize - 2
                color: Theme.textTertiary
            }
            // State (dot and text), or Run/Stop while hovered.
            Row {
                visible: !runButton.visible && tile.status.length > 0
                spacing: Theme.spacingXs + 2
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.spacingS - 1
                    height: width
                    radius: width / 2
                    color: tile.statusColor
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: tile.statusText
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
            ToolButton {
                id: runButton
                objectName: "tileRun"
                visible: tile.canToggle && (tile.hovered || tile.visualFocus)
                implicitWidth: Theme.controlHeightS
                implicitHeight: Theme.controlHeightS
                icon.source: Theme.icon(tile.running ? "stop-filled" : "play")
                icon.width: Theme.iconSize - 2
                icon.height: Theme.iconSize - 2
                icon.color: tile.running ? Theme.text : Theme.accent
                Accessible.name: tile.running ? qsTr("Stop %1").arg(tile.title) : qsTr("Run %1").arg(tile.title)
                ToolTip.visible: hovered
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: Accessible.name
                onClicked: tile.runToggled()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: tile.keyValue.length > 0
            text: tile.keyValue
            color: Theme.text
            font.pixelSize: Theme.fontSizeHeading
            font.features: Theme.tabularFigures
            elide: Text.ElideRight
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: tile.preview
            clip: true
        }
    }
}
