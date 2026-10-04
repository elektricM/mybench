pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// The toolbar (docs/UI.md §7): global items only. Sidebar toggle, the session title ("— Edited"
// while modified), "N running · Stop All" while something runs, the device button, the command
// palette field and the inspector toggle. With the unified title bar on macOS it is the title
// bar: it starts after the window buttons (`leadingInset`), drags the window and zooms on a
// double click.
Rectangle {
    id: bar

    required property AppActions actions
    required property ShellDialogs dialogs
    property bool unified: false
    property real leadingInset: 0
    // Narrow windows: the device button without its state word, the palette field as an icon.
    readonly property bool narrow: width < Theme.minWindowWidth + Theme.sidebarWidth
    readonly property alias deviceButton: deviceButton

    signal moveRequested()
    signal zoomRequested()

    implicitHeight: Theme.toolbarHeight
    color: Theme.window

    DragHandler {
        enabled: bar.unified
        target: null
        onActiveChanged: {
            if (active)
                bar.moveRequested()
        }
    }
    TapHandler {
        enabled: bar.unified
        gesturePolicy: TapHandler.DragThreshold
        onDoubleTapped: bar.zoomRequested()
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.separator
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Math.max(Theme.spacingS, bar.leadingInset)
        anchors.rightMargin: Theme.spacingS
        anchors.bottomMargin: Theme.borderWidth
        spacing: Theme.spacingXs

        // Sidebar toggle (the sidebar-right glyph, mirrored).
        ToolButton {
            id: sidebarButton
            objectName: "sidebarToggle"
            implicitWidth: Theme.controlHeight
            onClicked: bar.actions.toggleSidebar.trigger()
            Accessible.name: ToolTip.text
            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: "sidebar-right"
                    size: Theme.iconSizeL
                    color: sidebarButton.hovered ? Theme.text : Theme.textSecondary
                    transform: Scale { origin.x: Theme.iconSizeL / 2; xScale: -1 }
                }
            }
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: (bar.actions.sidebarShown ? qsTr("Hide Sidebar") : qsTr("Show Sidebar"))
                          + "   " + Theme.keys(bar.actions.keysText(bar.actions.toggleSidebar))
        }

        // The session: name, then "— Edited" while modified.
        Row {
            objectName: "toolbarTitle"
            Layout.leftMargin: Theme.spacingS
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            clip: true
            spacing: 0
            Text {
                text: SessionManager.currentName
                color: Theme.text
                font.pixelSize: Theme.fontSizeHeading
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth, parent.width)
            }
            Text {
                visible: SessionManager.modified
                text: " — " + qsTr("Edited")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeHeading
            }
        }

        // "N running · Stop All", only while something runs.
        RowLayout {
            objectName: "runningIndicator"
            visible: InstrumentRegistry.runningCount > 0
            spacing: Theme.spacingXs
            Rectangle {
                width: Theme.spacingS - 1
                height: width
                radius: width / 2
                color: Theme.positive
            }
            Text {
                text: qsTr("%n running", "", InstrumentRegistry.runningCount)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.features: Theme.tabularFigures
            }
            Button {
                objectName: "stopAllButton"
                flat: true
                text: bar.actions.stopAll.text
                enabled: bar.actions.stopAll.enabled
                onClicked: bar.actions.stopAll.trigger()
                ToolTip.visible: hovered
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: qsTr("Stop every instrument") + "   " + Theme.keys(bar.actions.keysText(bar.actions.stopAll))
            }
        }

        ToolSeparator {
            visible: InstrumentRegistry.runningCount > 0
        }

        DeviceButton {
            id: deviceButton
            objectName: "deviceButton"
            compact: bar.narrow
            Layout.maximumWidth: bar.narrow ? 160 : 240
            onDeviceInfoRequested: bar.dialogs.showDeviceInfo()
            onSimulatorSettingsRequested: bar.dialogs.showSimulatorSettings()
        }

        // The command palette field (⌘K): looks like a search field, opens the palette.
        T.AbstractButton {
            id: paletteField
            objectName: "paletteField"
            Layout.leftMargin: Theme.spacingXs
            Layout.preferredWidth: bar.narrow ? Theme.controlHeight : 200
            implicitHeight: Theme.controlHeight
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            onClicked: bar.actions.commandPalette.trigger()
            Accessible.name: qsTr("Search commands")
            ToolTip.visible: bar.narrow && hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: qsTr("Search commands") + "   " + Theme.keys("Ctrl+K")
            background: Rectangle {
                radius: Theme.radiusControl
                color: bar.narrow ? (paletteField.hovered ? Theme.hoverFill : "transparent") : Theme.controlFill
                border.width: bar.narrow ? 0 : Theme.borderWidth
                border.color: paletteField.hovered ? Theme.controlBorder : Theme.separator
                FocusRing { shown: paletteField.visualFocus }
            }
            contentItem: RowLayout {
                spacing: Theme.spacingXs + 2
                Icon {
                    Layout.leftMargin: bar.narrow ? (Theme.controlHeight - Theme.iconSize) / 2 : Theme.spacingS
                    name: "search"
                    color: Theme.textSecondary
                }
                Text {
                    visible: !bar.narrow
                    Layout.fillWidth: true
                    text: qsTr("Search")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeBody
                    elide: Text.ElideRight
                }
                Text {
                    visible: !bar.narrow
                    Layout.rightMargin: Theme.spacingS
                    text: Theme.keys("Ctrl+K")
                    color: Theme.textTertiary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }

        ToolButton {
            objectName: "inspectorToggle"
            onClicked: bar.actions.toggleInspector.trigger()
            Accessible.name: ToolTip.text
            icon.source: Theme.icon("sidebar-right")
            icon.width: Theme.iconSizeL
            icon.height: Theme.iconSizeL
            icon.color: hovered ? Theme.text : Theme.textSecondary
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: (bar.actions.inspectorShown ? qsTr("Hide Inspector") : qsTr("Show Inspector"))
                          + "   " + Theme.keys(bar.actions.keysText(bar.actions.toggleInspector))
        }
    }
}
