pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The application event log (errors, device events, notifications), newest first.
Popup {
    id: popup

    width: 560
    height: 420
    padding: 0
    onOpened: EventLog.markRead()

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingM
            spacing: Theme.spacingS
            Icon { name: "list"; color: Theme.textSecondary }
            Text {
                Layout.fillWidth: true
                text: qsTr("Event log")
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
            }
            ActionButton {
                text: qsTr("Copy")
                iconName: "copy"
                compact: true
                tone: "ghost"
                enabled: EventLog.count > 0
                onClicked: {
                    copyHelper.text = EventLog.toText()
                    copyHelper.selectAll()
                    copyHelper.copy()
                }
            }
            ActionButton {
                text: qsTr("Clear")
                iconName: "trash"
                compact: true
                tone: "ghost"
                enabled: EventLog.count > 0
                onClicked: EventLog.clear()
            }
            IconButton {
                iconName: "close"
                toolTip: qsTr("Close")
                onClicked: popup.close()
            }
        }
        Divider { Layout.fillWidth: true }

        ListView {
            id: list
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
                width: list.width
                implicitHeight: row.implicitHeight + 12
                RowLayout {
                    id: row
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spacingM
                    anchors.rightMargin: Theme.spacingM
                    spacing: Theme.spacingS
                    Text {
                        Layout.alignment: Qt.AlignTop
                        text: entry.timeText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                        font.family: Theme.monoFamily
                    }
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        name: entry.level === EventLog.Error ? "error" : entry.level === EventLog.Warning ? "warning" : "info"
                        size: 13
                        color: entry.level === EventLog.Error ? Theme.danger : entry.level === EventLog.Warning ? Theme.warning : Theme.textMuted
                    }
                    Text {
                        Layout.alignment: Qt.AlignTop
                        Layout.preferredWidth: 56
                        text: entry.source
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeS
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: entry.message
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeM
                        wrapMode: Text.WordWrap
                    }
                }
            }
            EmptyState {
                anchors.centerIn: parent
                visible: EventLog.count === 0
                iconName: "success"
                title: qsTr("Nothing to report")
                text: qsTr("Device events, errors and notifications appear here.")
            }
        }
    }

    TextEdit {
        id: copyHelper
        visible: false
    }
}
