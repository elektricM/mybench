pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// The content of the Preferences window (docs/UI.md §10): General, Appearance, Instruments,
// Device and Tools, chosen in a list on the left. Reusable: the window shows it, and so does the
// main window offscreen (`--preferences`, tests).
//     PreferencesPane { preferences: appPreferences; page: "tools" }
Rectangle {
    id: pane

    required property AppPreferences preferences
    // "general" | "appearance" | "instruments" | "device" | "tools" ("" = general)
    property string page: ""

    signal simulatorSettingsRequested()

    readonly property var pages: [
        { id: "general", title: qsTr("General"), icon: "settings" },
        { id: "appearance", title: qsTr("Appearance"), icon: "sun" },
        { id: "instruments", title: qsTr("Instruments"), icon: "sliders" },
        { id: "device", title: qsTr("Device"), icon: "device" },
        { id: "tools", title: qsTr("Tools"), icon: "grid" }
    ]
    readonly property int pageIndex: Math.max(0, pages.findIndex(p => p.id === page))
    // The generator's controller (its own type, with engineMode), when the build has one.
    readonly property var generator: InstrumentRegistry.controller("gen")

    implicitWidth: 680
    implicitHeight: 460
    color: Theme.window

    onPageIndexChanged: categories.currentIndex = pageIndex

    // A switch with its title and an optional explanation below.
    component SwitchRow: RowLayout {
        id: row
        property string title
        property string hint
        property alias checked: toggle.checked
        signal toggled(bool checked)
        Layout.fillWidth: true
        spacing: Theme.spacingL
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: row.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.hint
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }
        Switch {
            id: toggle
            Layout.alignment: Qt.AlignTop
            Accessible.name: row.title
            onToggled: row.toggled(checked)
        }
    }
    component Hint: Text {
        Layout.fillWidth: true
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        wrapMode: Text.WordWrap
    }
    component PageTitle: Text {
        Layout.bottomMargin: Theme.spacingXs
        color: Theme.text
        font.pixelSize: Theme.fontSizeTitle - 2
        font.weight: Font.DemiBold
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Categories.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 176
            color: Theme.sidebar
            ListView {
                id: categories
                objectName: "preferencesPages"
                anchors.fill: parent
                anchors.topMargin: Theme.spacingM
                model: pane.pages
                activeFocusOnTab: true
                keyNavigationEnabled: true
                Component.onCompleted: currentIndex = pane.pageIndex
                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex !== pane.pageIndex)
                        pane.page = pane.pages[currentIndex].id
                }
                // Template delegates (the source-list look is ours, not a native control's).
                delegate: T.ItemDelegate {
                    id: category
                    required property var modelData
                    required property int index
                    objectName: "preferencesPage-" + modelData.id
                    width: ListView.view.width
                    height: Theme.controlHeight + Theme.spacingXs
                    hoverEnabled: true
                    leftPadding: Theme.spacingS
                    rightPadding: Theme.spacingS
                    onClicked: categories.currentIndex = index
                    background: Rectangle {
                        x: Theme.spacingS
                        width: parent.width - 2 * x
                        radius: Theme.radiusControl
                        color: category.index === categories.currentIndex
                               ? (categories.activeFocus ? Theme.selection : Theme.selectionInactive)
                               : category.hovered ? Theme.hoverFill : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: Theme.spacingS
                        Icon {
                            Layout.leftMargin: Theme.spacingS
                            name: category.modelData.icon
                            color: category.index === categories.currentIndex && categories.activeFocus
                                   ? Theme.selectionText : Theme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: category.modelData.title
                            color: category.index === categories.currentIndex && categories.activeFocus
                                   ? Theme.selectionText : Theme.text
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }
                }
            }
            Rectangle {
                anchors.right: parent.right
                width: Theme.borderWidth
                height: parent.height
                color: Theme.separator
            }
        }

        // The page.
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacingXl
            currentIndex: pane.pageIndex

            // --- General ---------------------------------------------------------------------------
            ColumnLayout {
                spacing: Theme.spacingL
                PageTitle { text: qsTr("General") }
                SwitchRow {
                    objectName: "restoreSessionSwitch"
                    title: qsTr("Restore the last session on start")
                    hint: qsTr("Instrument settings and the layout come back at the next start.")
                    checked: pane.preferences.behaviour.restoreSession
                    onToggled: checked => pane.preferences.behaviour.restoreSession = checked
                }
                SwitchRow {
                    title: qsTr("Resume running instruments when a session opens")
                    hint: qsTr("Off by default, so outputs never start by themselves.")
                    checked: pane.preferences.behaviour.resumeRunning
                    onToggled: checked => pane.preferences.behaviour.resumeRunning = checked
                }
                SwitchRow {
                    title: qsTr("Confirm quitting while instruments run")
                    checked: pane.preferences.behaviour.confirmQuit
                    onToggled: checked => pane.preferences.behaviour.confirmQuit = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingL
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Window layout")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                    }
                    Button {
                        objectName: "resetWindowLayoutButton"
                        text: qsTr("Reset Window Layout")
                        onClicked: pane.preferences.resetWindowLayout()
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // --- Appearance ------------------------------------------------------------------------
            ColumnLayout {
                spacing: Theme.spacingL
                PageTitle { text: qsTr("Appearance") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingL
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Appearance"); color: Theme.text; font.pixelSize: Theme.fontSizeBody }
                        Hint { text: qsTr("System follows the light or dark setting and the accent color of your desktop.") }
                    }
                    SegmentedControl {
                        objectName: "themeModeControl"
                        Layout.alignment: Qt.AlignTop
                        readonly property var modes: ["system", "light", "dark"]
                        model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                        currentIndex: Math.max(0, modes.indexOf(pane.preferences.themeMode))
                        onActivated: index => pane.preferences.setThemeMode(modes[index])
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingL
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Controls")
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody
                        }
                        Hint {
                            objectName: "controlsRestartNote"
                            visible: pane.preferences.controlsRestartNeeded
                            text: qsTr("Restart %1 to apply").arg(AppInfo.name)
                            color: Theme.caution
                        }
                    }
                    SegmentedControl {
                        objectName: "controlsStyleControl"
                        Layout.alignment: Qt.AlignTop
                        readonly property var styles: ["native", "mybench"]
                        model: [qsTr("Native"), qsTr("MYBENCH")]
                        currentIndex: Math.max(0, styles.indexOf(pane.preferences.appearance.controls))
                        onActivated: index => pane.preferences.setControls(styles[index])
                    }
                }
                SwitchRow {
                    objectName: "darkDisplaysSwitch"
                    title: qsTr("Always use dark displays")
                    hint: qsTr("Plots and readouts stay dark in the light appearance.")
                    checked: pane.preferences.alwaysDarkDisplays
                    onToggled: checked => pane.preferences.setAlwaysDarkDisplays(checked)
                }
                Item { Layout.fillHeight: true }
            }

            // --- Instruments -----------------------------------------------------------------------
            ColumnLayout {
                spacing: Theme.spacingL
                PageTitle { text: qsTr("Instruments") }
                RowLayout {
                    visible: pane.generator !== null && pane.generator.engineMode !== undefined
                    Layout.fillWidth: true
                    spacing: Theme.spacingL
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: qsTr("Waveform generator output"); color: Theme.text; font.pixelSize: Theme.fontSizeBody }
                        Hint {
                            text: qsTr("Streamed computes the samples continuously: changes are smooth and one output can start or stop without disturbing the other. Looped buffer repeats one prepared buffer; a change restarts the output. Automatic streams, and loops a buffer only where the device cannot stream.")
                        }
                    }
                    ComboBox {
                        Layout.alignment: Qt.AlignTop
                        model: [qsTr("Automatic"), qsTr("Looped buffer"), qsTr("Streamed")]
                        currentIndex: pane.generator ? Number(pane.generator.engineMode) : 0
                        onActivated: index => pane.generator.engineMode = index
                    }
                }
                Hint {
                    visible: pane.generator === null
                    text: qsTr("No instrument has preferences in this build.")
                }
                Hint { text: qsTr("Every other instrument setting is in the instrument's inspector and is saved with the session.") }
                Item { Layout.fillHeight: true }
            }

            // --- Device ------------------------------------------------------------------------------
            ColumnLayout {
                spacing: Theme.spacingL
                PageTitle { text: qsTr("Device") }
                Hint {
                    text: qsTr("At start, the device used last is connected again when it is present, otherwise the first myDAQ found, otherwise the simulated device.")
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    Repeater {
                        model: DeviceHub.devices
                        delegate: RadioButton {
                            required property string deviceId
                            required property string displayName
                            required property bool connected
                            text: displayName
                            enabled: connected
                            checked: DeviceHub.currentId === deviceId
                            onClicked: DeviceHub.select(deviceId)
                        }
                    }
                }
                RowLayout {
                    spacing: Theme.spacingS
                    Button {
                        text: qsTr("Rescan")
                        onClicked: DeviceHub.rescan()
                    }
                    Button {
                        visible: DeviceHub.simulatorAvailable
                        text: qsTr("Simulator Settings…")
                        onClicked: pane.simulatorSettingsRequested()
                    }
                }
                Item { Layout.fillHeight: true }
            }

            // --- Tools -------------------------------------------------------------------------------
            ColumnLayout {
                spacing: Theme.spacingM
                PageTitle { text: qsTr("Tools") }
                Hint { text: qsTr("Programs, documents, folders and web pages of the Tools menu.") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.base
                    radius: Theme.radiusControl
                    border.width: Theme.borderWidth
                    border.color: Theme.separator
                    ListView {
                        id: tools
                        objectName: "toolsList"
                        anchors.fill: parent
                        anchors.margins: Theme.borderWidth
                        clip: true
                        model: Launcher.tools
                        currentIndex: count > 0 ? 0 : -1
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: T.ItemDelegate {
                            id: tool
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            implicitHeight: implicitContentHeight + topPadding + bottomPadding
                            padding: Theme.spacingXs
                            leftPadding: Theme.spacingS
                            rightPadding: Theme.spacingS
                            hoverEnabled: true
                            highlighted: ListView.isCurrentItem
                            background: Rectangle {
                                color: tool.highlighted ? Theme.selection
                                       : tool.hovered ? Theme.hoverFill : "transparent"
                            }
                            onClicked: tools.currentIndex = index
                            onDoubleClicked: toolDialog.edit(index)
                            contentItem: ColumnLayout {
                                spacing: 0
                                Text {
                                    text: tool.modelData.name
                                    color: tool.highlighted ? Theme.selectionText : Theme.text
                                    font.pixelSize: Theme.fontSizeBody
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: tool.modelData.target
                                    color: tool.highlighted ? Theme.selectionText : Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeSmall
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: tools.count === 0
                            text: qsTr("No tools yet")
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }
                }
                RowLayout {
                    spacing: Theme.spacingS
                    Button { text: qsTr("Add…"); onClicked: toolDialog.add() }
                    Button {
                        text: qsTr("Edit…")
                        enabled: tools.currentIndex >= 0
                        onClicked: toolDialog.edit(tools.currentIndex)
                    }
                    Button {
                        text: qsTr("Remove")
                        enabled: tools.currentIndex >= 0
                        onClicked: Launcher.removeTool(tools.currentIndex)
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Move Up")
                        enabled: tools.currentIndex > 0
                        onClicked: {
                            const index = tools.currentIndex
                            Launcher.moveTool(index, index - 1)
                            tools.currentIndex = index - 1
                        }
                    }
                    Button {
                        text: qsTr("Move Down")
                        enabled: tools.currentIndex >= 0 && tools.currentIndex < tools.count - 1
                        onClicked: {
                            const index = tools.currentIndex
                            Launcher.moveTool(index, index + 1)
                            tools.currentIndex = index + 1
                        }
                    }
                }
                ToolDialog { id: toolDialog }
            }
        }
    }
}
