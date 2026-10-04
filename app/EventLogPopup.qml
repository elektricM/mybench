pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The event log popover (status bar message counter, Help ▸ Event Log): errors, device events and
// notifications, newest first, with the technical details of device errors. Opening it marks
// everything read.
Popup {
    id: popup

    width: 560
    height: 420
    padding: 0
    onOpened: EventLog.markRead()

    background: PopupFrame {}

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingS
            Layout.topMargin: Theme.spacingS
            Layout.bottomMargin: Theme.spacingS
            spacing: Theme.spacingXs
            Text {
                Layout.fillWidth: true
                text: qsTr("Event Log")
                color: Theme.text
                font.pixelSize: Theme.fontSizeHeading
                font.weight: Font.DemiBold
            }
            Button {
                flat: true
                text: qsTr("Copy")
                enabled: EventLog.count > 0
                onClicked: {
                    copyHelper.text = EventLog.toText()
                    copyHelper.selectAll()
                    copyHelper.copy()
                }
            }
            Button {
                flat: true
                text: qsTr("Clear")
                enabled: EventLog.count > 0
                onClicked: EventLog.clear()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.borderWidth
            color: Theme.separator
        }

        ListView {
            id: list
            objectName: "eventLogList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: EventLog
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Item {
                id: entry
                required property string timeText
                required property int level
                required property string source
                required property string message
                required property string details
                width: ListView.view.width
                implicitHeight: row.implicitHeight + Theme.spacingS + 2
                RowLayout {
                    id: row
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spacingL
                    anchors.rightMargin: Theme.spacingL
                    spacing: Theme.spacingS
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        name: entry.level === EventLog.Error ? "error" : entry.level === EventLog.Warning ? "warning" : "info"
                        size: Theme.iconSize - 2
                        color: entry.level === EventLog.Error ? Theme.negative
                             : entry.level === EventLog.Warning ? Theme.caution : Theme.textTertiary
                    }
                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: entry.timeText
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontSizeSmall
                        font.features: Theme.tabularFigures
                    }
                    Text {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 72
                        text: entry.source
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                    Column {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            width: parent.width
                            text: entry.message
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WordWrap
                        }
                        // Technical details (error code name and the backend's message).
                        Text {
                            visible: entry.details.length > 0
                            width: parent.width
                            text: entry.details
                            color: Theme.textTertiary
                            font.pixelSize: Theme.fontSizeCaption
                            font.family: Theme.monoFamily
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacingXl
                visible: EventLog.count === 0
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("No messages. Device events, errors and notifications appear here.")
                color: Theme.textTertiary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
            }
        }
    }

    TextEdit {
        id: copyHelper
        visible: false
    }
}
