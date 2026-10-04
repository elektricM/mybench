pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

GalleryPage {
    id: page

    title: qsTr("Indicators and Messages")
    description: qsTr("State is a small steady dot with short text; channels are a swatch and a name. Errors and conflicts are an inline banner in the affected pane; toasts are for global, transient events only.")

    property bool ch1: true

    GallerySpec {
        title: qsTr("Status")
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingXl
            StatusIndicator { status: "running"; text: qsTr("Running · 200 kS/s") }
            StatusIndicator { status: "waiting"; text: qsTr("Waiting for trigger") }
            StatusIndicator { status: "paused"; text: qsTr("Paused — AI used by Bode") }
            StatusIndicator { status: "error"; text: qsTr("Device error") }
            StatusIndicator { status: "stopped"; text: qsTr("Stopped") }
            StatusIndicator { status: "neutral"; text: qsTr("Simulated myDAQ") }
            Row {
                spacing: Theme.spacingS
                StatusIndicator { status: "running"; dotOnly: true }
                StatusIndicator { status: "waiting"; dotOnly: true }
                StatusIndicator { status: "error"; dotOnly: true }
                StatusIndicator { status: "stopped"; dotOnly: true }
            }
        }
    }

    GallerySpec {
        title: qsTr("Channels")
        note: qsTr("Signal colors identify channels and nothing else. Checkable badges show or hide a trace.")
        Row {
            spacing: Theme.spacingM
            ChannelBadge { color: Theme.signalCh0; text: "CH0" }
            ChannelBadge { color: Theme.signalCh1; text: "CH1"; checkable: true; checked: page.ch1; onToggled: on => page.ch1 = on }
            ChannelBadge { color: Theme.signalAo0; text: "AO0"; detail: "1 kHz sine" }
            ChannelBadge { color: Theme.signalMath; text: qsTr("Math"); checkable: true; checked: false }
            ChannelBadge { color: Theme.signalRef; text: qsTr("Ref A") }
        }
    }

    GallerySpec {
        title: qsTr("Banners")
        note: qsTr("Under the pane header, closable, with actions. The tone shows only in the icon.")
        ColumnLayout {
            Layout.preferredWidth: 720
            spacing: Theme.spacingS
            Banner {
                Layout.fillWidth: true
                tone: "warning"
                text: qsTr("Paused — the analog inputs are used by the Bode Analyzer")
                Button { flat: true; text: qsTr("Resume") }
            }
            Banner {
                Layout.fillWidth: true
                tone: "info"
                text: qsTr("Uses AI, now held by Oscilloscope — Run pauses it")
                closable: true
            }
            Banner {
                Layout.fillWidth: true
                tone: "danger"
                text: qsTr("The device stopped responding")
                detail: qsTr("Outputs were set to 0 V. Reconnect the myDAQ or switch to the simulator.")
                closable: true
                Button { flat: true; text: qsTr("Details") }
            }
            Banner {
                Layout.fillWidth: true
                tone: "success"
                text: qsTr("Device reconnected")
                Button { flat: true; text: qsTr("Resume") }
            }
        }
    }

    GallerySpec {
        title: qsTr("Toast and empty state")
        RowLayout {
            spacing: Theme.spacingXl
            Toast {
                Layout.alignment: Qt.AlignTop
                level: Notifications.Success
                title: qsTr("Data exported")
                message: "~/Documents/MYBENCH/scope-2026-10-04.csv"
                actionText: qsTr("Show in Finder")
            }
            Toast {
                Layout.alignment: Qt.AlignTop
                level: Notifications.Error
                title: qsTr("Image not saved")
                message: qsTr("The folder is read-only.")
                repeat: 2
            }
            Rectangle {
                Layout.preferredWidth: 360
                Layout.preferredHeight: 150
                color: Theme.display
                EmptyState {
                    anchors.centerIn: parent
                    iconName: "play"
                    title: qsTr("Press Run or Space to start")
                    textColor: Theme.displayTextSecondary
                }
            }
        }
    }

    GallerySpec {
        title: qsTr("Compatibility wrappers")
        note: qsTr("v1 components still used by instruments that have not migrated, rendered in the v2 language (deleted in the cleanup phase).")
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingL
            Badge { text: "×3" }
            Led { on: true; text: qsTr("Triggered") }
            Led { on: false; text: qsTr("Timeout") }
            StatusDot { color: Theme.positive }
            ChannelChip { text: "CH 0"; color: Theme.signalCh0; detail: "1 V/div" }
            ChannelChip { text: "CH 1"; color: Theme.signalCh1; checked: false }
            ActionButton { text: qsTr("Autoscale"); iconName: "autoscale" }
            ActionButton { text: qsTr("Run"); tone: "accent"; iconName: "play" }
            ActionButton { text: qsTr("Stop"); tone: "danger" }
            IconButton { iconName: "help-circle"; toolTip: qsTr("Help"); badgeCount: 3 }
            ToggleSwitch { text: qsTr("Null offset"); checked: true }
        }
    }
}
