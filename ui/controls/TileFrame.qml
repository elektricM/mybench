pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// Dashboard card around an instrument's <Name>Tile.qml: icon, title, run state, a favorite star
// (Launcher.favorites; shown on hover and while starred), a quick Run/Stop button and Open. The
// tile content is loaded with `controller` as initial property.
//     TileFrame { instrumentId: "dmm"; onOpenRequested: shell.open("dmm") }
// `closable` adds an × (closeRequested: the dashboard column unpins the tile).
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
    readonly property color stateColor: !controller ? Theme.textMuted
                                       : controller.paused ? Theme.warning
                                       : controller.running ? Theme.success
                                       : controller.hasError ? Theme.danger : Theme.textMuted

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
        radius: Theme.radiusXl
        color: Theme.surfaceRaised
        border.width: 1
        border.color: root.hovered ? Theme.borderStrong : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        // running glow along the top edge
        Rectangle {
            visible: root.controller !== null && root.controller.running
            x: Theme.radiusXl
            width: parent.width - 2 * Theme.radiusXl
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.5; color: Theme.alpha(Theme.success, 0.9) }
                GradientStop { position: 1; color: "transparent" }
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openRequested()
        }
    }

    contentItem: ColumnLayout {
        spacing: root.compact ? Theme.spacingS : Theme.spacingM

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingS

            Rectangle {
                implicitWidth: 28
                implicitHeight: 28
                radius: Theme.radiusM
                color: Theme.surfaceSunken
                border.width: 1
                border.color: Theme.border
                Icon {
                    anchors.centerIn: parent
                    name: root.info.icon ?? ""
                    color: root.controller && root.controller.running ? Theme.success : Theme.text
                    size: Theme.iconSize
                }
            }
            Text {
                Layout.fillWidth: true
                text: root.info.title ?? root.instrumentId
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Row {
                visible: root.stateLabel.length > 0
                spacing: 6
                StatusDot {
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.stateColor
                    pulse: root.controller !== null && root.controller.state === InstrumentController.Running
                    size: 7
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.stateLabel
                    color: root.stateColor
                    font.pixelSize: Theme.fontSizeS
                    font.weight: Font.DemiBold
                }
            }
            IconButton {
                visible: root.showFavorite && (root.favorite || root.tools)
                size: 26
                iconSize: Theme.iconSizeS
                iconName: root.favorite ? "star-filled" : "star"
                iconColor: root.favorite ? Theme.warning : Theme.textMuted
                toolTip: root.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                Accessible.name: toolTip
                onClicked: Launcher.toggleFavorite(root.instrumentId)
            }
            IconButton {
                visible: root.tools
                size: 26
                iconSize: Theme.iconSizeS
                iconName: root.controller && root.controller.running ? "stop-filled" : "play"
                iconColor: root.controller && root.controller.running ? Theme.stop : Theme.run
                enabled: root.controller !== null && (root.controller.running || root.controller.canRun)
                toolTip: root.controller && root.controller.running ? qsTr("Stop") : qsTr("Run")
                onClicked: root.controller.toggle()
            }
            IconButton {
                visible: root.tools && !root.closable
                size: 26
                iconSize: Theme.iconSizeS
                iconName: "chevron-right"
                toolTip: qsTr("Open %1").arg(root.info.title ?? "")
                onClicked: root.openRequested()
            }
            IconButton {
                visible: root.closable && root.tools
                size: 26
                iconSize: Theme.iconSizeS
                iconName: "close"
                toolTip: qsTr("Remove %1 from the dashboard").arg(root.info.title ?? "")
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
                text: qsTr("No tile")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
            }
        }
    }
}
