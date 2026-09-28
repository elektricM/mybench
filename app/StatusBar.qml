pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework
import MyBench.Controls

// Bottom status bar: device, the focused instrument's state / sample rate / throughput, running
// count, and the event log (unread warning/error counter).
Rectangle {
    id: bar

    required property WorkspaceState workspace
    readonly property InstrumentController focusedController: workspace.focused.length > 0
                                                              ? InstrumentRegistry.controller(workspace.focused) : null

    signal logRequested()

    implicitHeight: Theme.statusBarHeight
    color: Theme.chrome

    Divider {
        anchors.top: parent.top
        width: parent.width
    }

    component Segment: Row {
        spacing: 6
        Layout.alignment: Qt.AlignVCenter
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingM
        anchors.rightMargin: Theme.spacingS
        spacing: Theme.spacingL

        Segment {
            Led {
                anchors.verticalCenter: parent.verticalCenter
                on: DeviceHub.connected
                color: DeviceHub.connected ? (DeviceHub.simulated ? Theme.simulated : Theme.success) : Theme.danger
                size: 7
                glow: false
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: DeviceHub.currentId.length > 0 ? DeviceHub.name : qsTr("No device")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
            }
        }

        Segment {
            visible: bar.focusedController !== null
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: bar.focusedController ? bar.focusedController.title : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
                font.weight: Font.DemiBold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    const c = bar.focusedController
                    if (!c)
                        return ""
                    if (c.running && c.summary.length > 0)
                        return c.summary
                    const parts = [c.running ? qsTr("running") : c.paused ? qsTr("paused") : qsTr("idle")]
                    if (c.running && c.sampleRate > 0)
                        parts.push(Units.format(c.sampleRate, "S/s"))
                    if (c.running && c.throughput > 0)
                        parts.push(qsTr("%1 throughput").arg(Units.format(c.throughput, "S/s", 3)))
                    if (c.resourceSummary.length > 0)
                        parts.push(c.resourceSummary)
                    return parts.join("  ·  ")
                }
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeS
                font.features: Theme.tabularFigures
            }
        }

        Item { Layout.fillWidth: true }

        Segment {
            visible: InstrumentRegistry.runningCount > 0
            StatusDot {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.success
                size: 6
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%n running", "", InstrumentRegistry.runningCount)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeS
            }
        }

        T.AbstractButton {
            id: logButton
            // Unread warnings + errors (app.md 2.6 "⚠ n"); red when one of them is an error.
            readonly property color issueColor: EventLog.unreadErrorCount > 0 ? Theme.danger
                                              : EventLog.unreadWarningCount > 0 ? Theme.warning : Theme.textMuted
            Layout.fillHeight: true
            Accessible.name: ToolTip.text
            implicitWidth: logRow.implicitWidth + 16
            hoverEnabled: true
            onClicked: bar.logRequested()
            background: Rectangle {
                color: logButton.hovered ? Theme.surfaceHover : "transparent"
            }
            contentItem: Item {
                Row {
                    id: logRow
                    anchors.centerIn: parent
                    spacing: 5
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: EventLog.unreadErrorCount > 0 ? "error" : EventLog.unreadWarningCount > 0 ? "warning" : "list"
                        size: 13
                        color: logButton.issueColor
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: EventLog.unreadIssueCount > 0 ? String(EventLog.unreadIssueCount) : qsTr("Log")
                        color: logButton.issueColor
                        font.pixelSize: Theme.fontSizeS
                        font.weight: EventLog.unreadIssueCount > 0 ? Font.DemiBold : Font.Normal
                        font.features: Theme.tabularFigures
                    }
                }
            }
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: {
                const parts = []
                if (EventLog.unreadErrorCount > 0)
                    parts.push(qsTr("%n new error(s)", "", EventLog.unreadErrorCount))
                if (EventLog.unreadWarningCount > 0)
                    parts.push(qsTr("%n new warning(s)", "", EventLog.unreadWarningCount))
                return parts.length > 0 ? qsTr("Event log: %1").arg(parts.join(", ")) : qsTr("Event log")
            }
        }

        Text {
            text: AppInfo.name + " " + AppInfo.version
            color: Theme.textDisabled
            font.pixelSize: Theme.fontSizeS
        }
    }
}
