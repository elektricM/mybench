pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework

// Help drawer: docs/help/<topic>.md rendered as Markdown, a topic menu, back navigation, and
// links between pages (other links open in the browser).
Rectangle {
    id: root

    property string topic: "app"
    property var history: []
    readonly property string pageHtml: Help.html(topic, {
        text: Theme.text.toString(), heading: Theme.text.toString(), muted: Theme.textSecondary.toString(),
        accent: Theme.accent.toString(), border: Theme.borderStrong.toString(),
        codeBackground: Theme.surfaceSunken.toString(), bodySize: Theme.fontSizeBody
    })

    signal closeRequested()

    function show(id: string) {
        if (id.length === 0 || id === topic)
            return
        history = history.concat([topic])
        topic = id
        flick.contentY = 0
    }
    function back() {
        if (history.length === 0)
            return
        topic = history[history.length - 1]
        history = history.slice(0, history.length - 1)
        flick.contentY = 0
    }

    implicitWidth: 400
    color: Theme.surface

    Divider {
        vertical: true
        anchors.left: parent.left
        height: parent.height
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        spacing: 0

        // Header
        Item {
            Layout.fillWidth: true
            implicitHeight: 44
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingXs
                IconButton {
                    visible: root.history.length > 0
                    iconName: "chevron-left"
                    toolTip: qsTr("Back")
                    onClicked: root.back()
                }
                Icon {
                    visible: root.history.length === 0
                    name: "book"
                    color: Theme.accent
                }
                ActionButton {
                    Layout.fillWidth: true
                    Layout.leftMargin: 2
                    tone: "ghost"
                    text: Help.title(root.topic)
                    iconName: ""
                    font.weight: Font.DemiBold
                    onClicked: topicMenu.popup()
                    Menu {
                        id: topicMenu
                        Repeater {
                            model: Help.topics
                            delegate: MenuItem {
                                required property var modelData
                                text: modelData.title
                                checkable: true
                                checked: modelData.id === root.topic
                                onTriggered: root.show(modelData.id)
                            }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("About %1").arg(AppInfo.name)
                            icon.source: Theme.icon("info")
                            onTriggered: Launcher.showAbout()
                        }
                    }
                }
                IconButton {
                    iconName: "close"
                    toolTip: qsTr("Close help")
                    shortcutText: "F1"
                    onClicked: root.closeRequested()
                }
            }
            Divider {
                anchors.bottom: parent.bottom
                width: parent.width
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: page.implicitHeight + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            TextEdit {
                id: page
                x: 22
                y: 18
                width: flick.width - 44
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.RichText
                wrapMode: TextEdit.Wrap
                text: root.pageHtml
                color: Theme.text
                selectionColor: Theme.alpha(Theme.accent, 0.35)
                selectedTextColor: Theme.text
                font.pixelSize: Theme.fontSizeBody
                onLinkActivated: link => {
                    const id = Help.topicForLink(link)
                    if (id.length > 0)
                        root.show(id)
                    else
                        Qt.openUrlExternally(link)
                }
                HoverHandler {
                    cursorShape: page.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
            }
        }
    }
}
