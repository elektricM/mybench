pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// Workspace tabs: Home, one tab per open instrument (or two side by side), "+" to open one, and
// the split actions of the current tab.
Rectangle {
    id: strip

    required property WorkspaceState workspace

    implicitHeight: 40
    color: Theme.chrome

    component StripTab: T.AbstractButton {
        id: tab

        property bool selected: false
        property var icons: []
        property bool running: false
        property bool closable: true
        signal closeRequested()

        implicitHeight: strip.height - 6
        implicitWidth: row.implicitWidth + (closable ? 40 : 24)
        hoverEnabled: true
        focusPolicy: Qt.TabFocus

        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.bottomMargin: -Theme.radiusL
                radius: Theme.radiusL
                color: tab.selected ? Theme.surface : tab.hovered ? Theme.surfaceHover : "transparent"
                border.width: tab.selected ? 1 : 0
                border.color: Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
            // hide the bottom border of the selected tab so it joins the panel
            Rectangle {
                visible: tab.selected
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -1
                x: 1
                width: parent.width - 2
                height: 2
                color: Theme.surface
            }
            FocusRing { shown: tab.visualFocus; radius: Theme.radiusL + 3 }
        }
        contentItem: Item {
            Row {
                id: row
                anchors.verticalCenter: parent.verticalCenter
                x: 12
                spacing: 7
                Repeater {
                    model: tab.icons
                    delegate: Icon {
                        required property string modelData
                        y: (row.height - height) / 2
                        name: modelData
                        size: Theme.iconSizeS
                        color: tab.selected ? Theme.text : Theme.textSecondary
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tab.text.length > 0
                    text: tab.text
                    color: tab.selected ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: tab.selected ? Font.DemiBold : Font.Medium
                }
                StatusDot {
                    visible: tab.running
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.success
                    pulse: true
                    size: 6
                }
            }
            IconButton {
                visible: tab.closable && (tab.hovered || tab.selected)
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                size: 20
                iconSize: 12
                iconName: "close"
                toolTip: qsTr("Close tab")
                shortcutText: "⌘W"
                onClicked: tab.closeRequested()
            }
        }
    }

    Divider {
        anchors.bottom: parent.bottom
        width: parent.width
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingS
        anchors.rightMargin: Theme.spacingS
        anchors.topMargin: 6
        spacing: 2

        StripTab {
            Layout.alignment: Qt.AlignBottom
            icons: ["home"]
            text: qsTr("Home")
            closable: false
            selected: strip.workspace.home
            onClicked: strip.workspace.showHome()
        }

        Repeater {
            model: strip.workspace.tabs
            delegate: StripTab {
                id: instrumentTab
                required property var modelData
                required property int index
                readonly property var primary: InstrumentRegistry.info(modelData.primary)
                readonly property var secondary: modelData.secondary.length > 0 ? InstrumentRegistry.info(modelData.secondary) : null
                readonly property InstrumentController primaryController: InstrumentRegistry.controller(modelData.primary)
                readonly property InstrumentController secondaryController: modelData.secondary.length > 0 ? InstrumentRegistry.controller(modelData.secondary) : null
                Layout.alignment: Qt.AlignBottom
                icons: secondary ? [primary.icon, secondary.icon] : [primary.icon]
                text: secondary ? primary.shortTitle + "  ·  " + secondary.shortTitle : primary.title
                selected: strip.workspace.currentIndex === index
                running: (primaryController !== null && primaryController.running)
                         || (secondaryController !== null && secondaryController.running)
                onClicked: strip.workspace.select(index)
                onCloseRequested: strip.workspace.closeTab(index)
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 4
            iconName: "plus"
            size: 26
            iconSize: Theme.iconSizeS
            toolTip: qsTr("Open an instrument")
            onClicked: openMenu.popup()
            Menu {
                id: openMenu
                Repeater {
                    model: InstrumentRegistry
                    delegate: MenuItem {
                        required property string instrumentId
                        required property string title
                        required property var model   // roles; `icon` is a (FINAL) MenuItem property
                        text: title
                        icon.source: Theme.icon(model.icon)
                        onTriggered: strip.workspace.open(instrumentId)
                    }
                }
            }
        }

        Item { Layout.fillWidth: true }

        // Split actions of the current tab.
        ActionButton {
            visible: !strip.workspace.home
            tone: "ghost"
            compact: true
            iconName: "split"
            text: strip.workspace.split ? qsTr("Unsplit") : qsTr("Split")
            toolTip: strip.workspace.split ? qsTr("Move the right panel to its own tab") : qsTr("Open another instrument beside this one")
            onClicked: {
                if (strip.workspace.split)
                    strip.workspace.unsplit(strip.workspace.currentIndex)
                else
                    besideMenu.popup()
            }
            Menu {
                id: besideMenu
                title: qsTr("Open beside")
                Repeater {
                    model: InstrumentRegistry
                    delegate: MenuItem {
                        required property string instrumentId
                        required property string title
                        required property var model   // roles; `icon` is a (FINAL) MenuItem property
                        text: title
                        icon.source: Theme.icon(model.icon)
                        enabled: strip.workspace.currentTab !== null && strip.workspace.currentTab.primary !== instrumentId
                        onTriggered: strip.workspace.openBeside(instrumentId)
                    }
                }
            }
        }
        IconButton {
            visible: strip.workspace.split
            iconName: "refresh"
            size: 26
            iconSize: Theme.iconSizeS
            toolTip: qsTr("Swap sides")
            onClicked: strip.workspace.swap(strip.workspace.currentIndex)
        }
    }
}
