pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// The status bar (docs/UI.md §7, 24 px): who uses which part of the device ("AI: Oscilloscope ·
// AO: Waveform Generator"), the data rate while streaming, and the message counter that opens the
// event log. No device name, version or running count (the toolbar has those).
Rectangle {
    id: bar

    // Samples per second moved by every running instrument.
    readonly property real throughput: {
        let total = 0
        for (const id of InstrumentRegistry.ids) {
            const c = InstrumentRegistry.controller(id)
            if (c && c.running && c.throughput > 0)
                total += c.throughput
        }
        return total
    }
    readonly property int issues: EventLog.unreadIssueCount

    signal logRequested()

    implicitHeight: Theme.statusBarHeight
    color: Theme.window

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: Theme.borderWidth
        color: Theme.separator
    }

    ResourceChips {
        id: resources
        visible: false
        textForm: true
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.borderWidth
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingXs
        spacing: Theme.spacingS

        Text {
            objectName: "statusResources"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: resources.summaryText.length > 0 ? resources.summaryText : qsTr("Device idle")
            color: resources.summaryText.length > 0 ? Theme.textSecondary : Theme.textTertiary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
            ToolTip.visible: resourcesHover.hovered && truncated
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: text
            HoverHandler { id: resourcesHover }
        }
        Text {
            objectName: "statusThroughput"
            visible: bar.throughput > 0
            text: Units.format(bar.throughput, "S/s", 3)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            font.features: Theme.tabularFigures
        }

        // Message counter → event log.
        T.AbstractButton {
            id: messages
            objectName: "statusMessages"
            Layout.fillHeight: true
            Layout.topMargin: 2
            Layout.bottomMargin: 2
            implicitWidth: messagesRow.implicitWidth + 2 * Theme.spacingS
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            onClicked: bar.logRequested()
            Accessible.name: messagesText.text
            ToolTip.visible: hovered
            ToolTip.delay: Theme.toolTipDelay
            ToolTip.text: qsTr("Event Log")
            background: Rectangle {
                radius: Theme.radiusControl
                color: messages.down ? Theme.pressedFill : messages.hovered ? Theme.hoverFill : "transparent"
                FocusRing { shown: messages.visualFocus }
            }
            contentItem: Item {
                Row {
                    id: messagesRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingXs + 1
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: EventLog.unreadErrorCount > 0 ? "error" : EventLog.unreadWarningCount > 0 ? "warning" : "list"
                        size: Theme.iconSize - 2
                        color: EventLog.unreadErrorCount > 0 ? Theme.negative
                             : EventLog.unreadWarningCount > 0 ? Theme.caution : Theme.textTertiary
                    }
                    Text {
                        id: messagesText
                        anchors.verticalCenter: parent.verticalCenter
                        text: bar.issues > 0 ? qsTr("%n message(s)", "", bar.issues) : qsTr("No new messages")
                        color: bar.issues > 0 ? Theme.textSecondary : Theme.textTertiary
                        font.pixelSize: Theme.fontSizeSmall
                        font.features: Theme.tabularFigures
                    }
                }
            }
        }
    }
}
