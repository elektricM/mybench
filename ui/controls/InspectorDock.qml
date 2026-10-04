pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// The right column of the shell (docs/UI.md §7 "Inspector dock", §13.4): it shows the inspector of
// the focused pane, or that instrument's help page. It claims `panel.inspectorContent` (one
// Inspector item per panel, moved here, never duplicated, so bindings, popups and the scroll
// position stay) and hands it back through `panel.releaseInspector()` when the panel changes or
// the dock goes away. While a dock exists, panels never dock their inspector locally.
//     InspectorDock {
//         panel: PanelRegistry.panel(workspace.focused)
//         page: workspace.dockPage
//         onPageActivated: page => workspace.dockPage = page
//     }
Rectangle {
    id: dock

    property InstrumentPanel panel: null
    property string page: "inspector"     // "inspector" | "help"
    property string helpTopic: ""          // "" = the panel's help page (else "app")
    // An instrument pane is on screen (the shell binds it; `panel` can still be null while the
    // pane loads). The "Show an instrument…" hint only appears without one.
    property bool paneShown: panel !== null
    readonly property string effectiveHelpTopic: helpTopic.length > 0 ? helpTopic
                                                 : panel ? panel.helpId : "app"
    readonly property string instrumentTitle: panel ? panel.paneTitle : ""
    // The panel whose inspector is in the dock now (follows `panel`).
    readonly property InstrumentPanel claimedPanel: privateState.claimed

    // The user picked Settings or Help in the header (the dock also switches by itself).
    signal pageActivated(string page)

    implicitWidth: Theme.inspectorWidth
    color: Theme.window

    QtObject {
        id: privateState
        property InstrumentPanel claimed: null
    }

    function claim() {
        if (privateState.claimed === panel)
            return
        release()
        if (!panel || !panel.inspectorContent)
            return
        privateState.claimed = panel
        panel.inspectorContent.parent = host   // the Inspector fills its parent
    }
    function release() {
        const previous = privateState.claimed
        privateState.claimed = null
        if (previous)
            previous.releaseInspector()
    }
    function showHelp(topic: string) {
        helpTopic = topic
        page = "help"
    }

    onPanelChanged: claim()
    Component.onCompleted: {
        PanelRegistry.dockCount += 1
        claim()
    }
    Component.onDestruction: {
        release()
        PanelRegistry.dockCount -= 1
    }

    // Left hairline.
    Rectangle {
        width: 1
        height: parent.height
        color: Theme.separator
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        // Header: instrument name, Settings | Help.
        Item {
            Layout.fillWidth: true
            implicitHeight: Theme.paneHeaderHeight

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingS
                Text {
                    Layout.fillWidth: true
                    text: dock.instrumentTitle
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                SegmentedControl {
                    id: pageSwitch
                    objectName: "dockPageSwitch"
                    compact: true
                    model: [qsTr("Settings"), qsTr("Help")]
                    currentIndex: dock.page === "help" ? 1 : 0
                    onActivated: index => {
                        dock.page = index === 1 ? "help" : "inspector"
                        dock.pageActivated(dock.page)
                    }
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.separator
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // Host of the claimed Inspector.
            Item {
                id: host
                objectName: "inspectorDockHost"
                anchors.fill: parent
                visible: dock.page !== "help"
            }

            Text {
                objectName: "dockEmptyHint"
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacingL
                visible: dock.page !== "help" && dock.panel === null && !dock.paneShown
                text: qsTr("Show an instrument to see its settings here.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeBody
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Loader {
                id: helpLoader
                anchors.fill: parent
                active: dock.page === "help"
                sourceComponent: HelpView {
                    topic: dock.effectiveHelpTopic
                    onCloseRequested: {
                        dock.page = "inspector"
                        dock.pageActivated(dock.page)
                    }
                }
            }
        }
    }
}
