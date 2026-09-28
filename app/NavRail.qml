pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// Left navigation rail: Home, one button per instrument (with its run state), help at the bottom.
// Click opens/focuses an instrument; Ctrl/⌘+click opens it in the other split pane.
Rectangle {
    id: rail

    required property WorkspaceState workspace
    property int headerHeight: Theme.topBarHeight
    property bool showLogo: true

    signal moveWindowRequested()
    signal preferencesRequested()

    implicitWidth: Theme.navRailWidth
    color: Theme.chrome

    component RailButton: T.AbstractButton {
        id: button

        property string iconName
        property bool selected: false
        property color stateColor: "transparent"
        property bool pulse: false
        property string toolTip
        property string shortcutText

        implicitWidth: rail.width
        implicitHeight: 54
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: toolTip

        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 44
                height: 44
                radius: Theme.radiusL
                color: button.selected ? Theme.accentMuted : button.down ? Theme.surfacePressed
                     : button.hovered ? Theme.surfaceHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                FocusRing { shown: button.visualFocus; radius: Theme.radiusL + 3 }
            }
            Rectangle {
                visible: button.selected
                x: 0
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 22
                radius: 1.5
                color: Theme.accent
            }
        }
        contentItem: Item {
            Column {
                anchors.centerIn: parent
                spacing: 3
                Item {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 22
                    height: 22
                    Icon {
                        anchors.centerIn: parent
                        name: button.iconName
                        size: 20
                        color: button.selected ? Theme.accent : button.hovered ? Theme.text : Theme.textSecondary
                    }
                    StatusDot {
                        visible: button.stateColor.a > 0
                        x: parent.width - 3
                        y: -3
                        size: 7
                        color: button.stateColor
                        pulse: button.pulse
                    }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: button.text
                    color: button.selected ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: button.selected ? Font.DemiBold : Font.Medium
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, rail.width - 8)
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
        ToolTip.visible: hovered && toolTip.length > 0
        ToolTip.delay: 400
        ToolTip.text: shortcutText.length > 0 ? toolTip + "   " + shortcutText : toolTip
    }

    Divider {
        vertical: true
        anchors.right: parent.right
        height: parent.height
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        // Header (title-bar area on macOS: the window buttons sit here).
        Item {
            Layout.fillWidth: true
            implicitHeight: rail.headerHeight
            DragHandler {
                target: null
                onActiveChanged: if (active) rail.moveWindowRequested()
            }
            Rectangle {
                visible: rail.showLogo
                anchors.centerIn: parent
                width: 32
                height: 32
                radius: Theme.radiusL
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.28) }
                    GradientStop { position: 1; color: Theme.alpha(Theme.accent, 0.10) }
                }
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.35)
                Icon {
                    anchors.centerIn: parent
                    name: "app-logo"
                    size: 20
                    color: Theme.accent
                }
            }
        }

        RailButton {
            Layout.topMargin: Theme.spacingXs
            text: qsTr("Home")
            iconName: "home"
            toolTip: qsTr("Home")
            shortcutText: Theme.keys("Ctrl+0")
            selected: rail.workspace.home
            onClicked: rail.workspace.showHome()
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.spacingXs
            Layout.bottomMargin: Theme.spacingXs
            implicitWidth: 28
            implicitHeight: 1
            color: Theme.divider
        }

        Repeater {
            model: InstrumentRegistry
            delegate: RailButton {
                id: instrumentButton
                required property int index
                required property string instrumentId
                required property string shortTitle
                required property string title
                required property var model   // roles; `icon` is a (FINAL) button property
                required property bool running
                required property bool paused
                required property bool hasError
                text: shortTitle
                iconName: model.icon
                toolTip: title
                shortcutText: index < 9 ? Theme.keys("Ctrl+" + (index + 1)) : ""
                selected: rail.workspace.isVisible(instrumentId)
                stateColor: running ? Theme.success : paused ? Theme.warning : hasError ? Theme.danger : "transparent"
                pulse: running
                onClicked: rail.workspace.open(instrumentId)
                // Ctrl/⌘+click: in the other split pane, creating the split if needed (app.md 2.2).
                TapHandler {
                    acceptedModifiers: Qt.ControlModifier
                    gesturePolicy: TapHandler.WithinBounds
                    onTapped: rail.workspace.openInOtherPane(instrumentButton.instrumentId)
                }
            }
        }

        Item { Layout.fillHeight: true }

        RailButton {
            text: qsTr("Help")
            iconName: "book"
            toolTip: qsTr("Help")
            shortcutText: "F1"
            selected: rail.workspace.helpOpen
            onClicked: rail.workspace.toggleHelp()
        }
        RailButton {
            Layout.bottomMargin: Theme.spacingS
            text: qsTr("Settings")
            iconName: "settings"
            toolTip: qsTr("Preferences")
            shortcutText: Theme.keys("Ctrl+,")
            onClicked: rail.preferencesRequested()
        }
    }
}
