pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// v1 compatibility (A2 replaces it with OverviewTile): a bordered tile around an instrument's
// <Name>Tile.qml — icon, title, state, and on hover favorite, Run/Stop and Open. The tile content
// is loaded with `controller` as initial property. `closable` adds a remove button.
//     TileFrame { instrumentId: "dmm"; onOpenRequested: shell.open("dmm") }
T.Control {
    id: root

    property string instrumentId
    property bool compact: false
    property bool showFavorite: true
    property bool closable: false
    readonly property bool favorite: Launcher.favorites.indexOf(instrumentId) >= 0
    readonly property bool tools: hovered || visualFocus || activeFocus
    readonly property var info: InstrumentRegistry.info(instrumentId)
    readonly property InstrumentController controller: InstrumentRegistry.controller(instrumentId)
    readonly property string stateLabel: !controller ? ""
                                        : controller.paused ? qsTr("Paused")
                                        : controller.running ? qsTr("Running")
                                        : controller.hasError ? qsTr("Error") : ""
    readonly property string status: !controller ? "neutral"
                                    : controller.paused ? "paused"
                                    : controller.running ? "running"
                                    : controller.hasError ? "error" : "stopped"

    signal openRequested()
    signal closeRequested()

    implicitWidth: 300
    implicitHeight: compact ? 148 : 188
    padding: compact ? Theme.spacingM : Theme.spacingL
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: info.title ?? instrumentId

    onInstrumentIdChanged: tileLoader.reload()
    Component.onCompleted: tileLoader.reload()

    background: Rectangle {
        radius: Theme.radiusPopup
        color: Theme.base
        border.width: Theme.borderWidth
        border.color: root.hovered ? Theme.controlBorder : Theme.separator
        TapHandler {
            onTapped: root.openRequested()
        }
        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }
    }

    component TileTool: ToolButton {
        implicitWidth: Theme.controlHeightS
        implicitHeight: Theme.controlHeightS
        icon.width: Theme.iconSizeS
        icon.height: Theme.iconSizeS
        ToolTip.visible: hovered
        ToolTip.delay: Theme.toolTipDelay
    }

    contentItem: ColumnLayout {
        spacing: root.compact ? Theme.spacingS : Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Icon {
                name: root.info.icon ?? ""
                color: Theme.textSecondary
            }
            Text {
                Layout.fillWidth: true
                text: root.info.title ?? root.instrumentId
                color: Theme.text
                font.pixelSize: Theme.fontSizeHeading
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            StatusIndicator {
                visible: root.stateLabel.length > 0
                status: root.status
                text: root.stateLabel
            }
            TileTool {
                visible: root.showFavorite && (root.favorite || root.tools)
                icon.source: Theme.icon(root.favorite ? "pin" : "pin-off")
                checkable: false
                ToolTip.text: root.favorite ? qsTr("Unpin") : qsTr("Pin to Top")
                Accessible.name: ToolTip.text
                onClicked: Launcher.toggleFavorite(root.instrumentId)
            }
            TileTool {
                visible: root.tools
                icon.source: Theme.icon(root.controller && root.controller.running ? "stop" : "play")
                enabled: root.controller !== null && (root.controller.running || root.controller.canRun)
                ToolTip.text: root.controller && root.controller.running ? qsTr("Stop") : qsTr("Run")
                Accessible.name: ToolTip.text
                onClicked: root.controller.toggle()
            }
            TileTool {
                visible: root.tools && !root.closable
                icon.source: Theme.icon("chevron-right")
                ToolTip.text: qsTr("Open %1").arg(root.info.title ?? "")
                Accessible.name: ToolTip.text
                onClicked: root.openRequested()
            }
            TileTool {
                visible: root.closable && root.tools
                icon.source: Theme.icon("close")
                ToolTip.text: qsTr("Remove %1").arg(root.info.title ?? "")
                Accessible.name: ToolTip.text
                onClicked: root.closeRequested()
            }
        }

        Loader {
            id: tileLoader
            Layout.fillWidth: true
            Layout.fillHeight: true

            function reload() {
                if (root.controller && root.info.tileUrl)
                    setSource(root.info.tileUrl, { controller: root.controller })
            }

            Text {
                anchors.centerIn: parent
                visible: tileLoader.status === Loader.Error
                text: qsTr("No preview")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeSmall
            }
        }
    }
}
