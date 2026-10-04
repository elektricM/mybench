pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework

// docs/help/<topic>.md rendered as Markdown, with back navigation, a topic menu and links between
// pages (other links open in the browser). On the window color; the host draws the edge hairline.
// `showTitle: false` drops the title row (the InspectorDock header names the page); a Back button
// then appears above the page once the user has followed a link.
Rectangle {
    id: root

    property string topic: "app"
    property var history: []
    property bool showTitle: true
    property bool closable: true
    readonly property string pageHtml: Help.html(topic, {
        text: Theme.text.toString(), heading: Theme.text.toString(), muted: Theme.textSecondary.toString(),
        accent: Theme.link.toString(), border: Theme.composite(Theme.separator, Theme.window).toString(),
        codeBackground: Theme.composite(Theme.hoverFill, Theme.window).toString(), bodySize: Theme.fontSizeBody
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
    color: Theme.window

    Menu {
        id: topicMenu
        Instantiator {
            model: Help.topics
            delegate: MenuItem {
                required property var modelData
                text: modelData.title
                checkable: true
                checked: modelData.id === root.topic
                onTriggered: root.show(modelData.id)
            }
            onObjectAdded: (index, object) => topicMenu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => topicMenu.removeItem(object as MenuItem)
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("About %1").arg(AppInfo.name)
            onTriggered: Launcher.showAbout()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Title row: back, the page title (opens the topic list), close.
        Item {
            visible: root.showTitle
            Layout.fillWidth: true
            implicitHeight: Theme.paneHeaderHeight
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingS
                anchors.rightMargin: Theme.spacingS
                spacing: Theme.spacingXs
                ToolButton {
                    visible: root.history.length > 0
                    icon.source: Theme.icon("chevron-left")
                    ToolTip.visible: hovered
                    ToolTip.delay: Theme.toolTipDelay
                    ToolTip.text: qsTr("Back")
                    Accessible.name: qsTr("Back")
                    onClicked: root.back()
                }
                ToolButton {
                    id: titleButton
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    text: Help.title(root.topic) + "  ▾"
                    font.pixelSize: Theme.fontSizeHeading
                    font.weight: Font.DemiBold
                    Accessible.name: qsTr("Help topics")
                    onClicked: topicMenu.popup(titleButton, 0, titleButton.height)
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    visible: root.closable
                    icon.source: Theme.icon("close")
                    ToolTip.visible: hovered
                    ToolTip.delay: Theme.toolTipDelay
                    ToolTip.text: qsTr("Close Help") + "  " + Theme.keys("F1")
                    Accessible.name: qsTr("Close Help")
                    onClicked: root.closeRequested()
                }
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.borderWidth
                color: Theme.separator
            }
        }

        Button {
            visible: !root.showTitle && root.history.length > 0
            Layout.leftMargin: Theme.spacingM
            Layout.topMargin: Theme.spacingS
            flat: true
            text: qsTr("Back")
            icon.source: Theme.icon("chevron-left")
            onClicked: root.back()
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: page.implicitHeight + 2 * Theme.spacingL
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            WheelScroll { flickable: flick }

            TextEdit {
                id: page
                x: Theme.spacingL
                y: Theme.spacingL
                width: flick.width - 2 * Theme.spacingL
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.RichText
                wrapMode: TextEdit.Wrap
                text: root.pageHtml
                color: Theme.text
                selectionColor: Theme.selection
                selectedTextColor: Theme.selectionText
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
