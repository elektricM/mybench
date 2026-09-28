import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Home's device card: connection, product, serial, simulated tag, reset, simulator controls
// (quick toggles + "Simulator settings…" dialog).
Card {
    id: card

    signal deviceInfoRequested()

    readonly property color stateColor: DeviceHub.connectionState === DeviceHub.Connected
                                        ? (DeviceHub.simulated ? Theme.simulated : Theme.success)
                                        : DeviceHub.connectionState === DeviceHub.Resetting ? Theme.warning : Theme.danger
    readonly property string stateText: DeviceHub.connectionState === DeviceHub.Connected ? qsTr("Connected")
                                      : DeviceHub.connectionState === DeviceHub.Resetting ? qsTr("Resetting…")
                                      : DeviceHub.connectionState === DeviceHub.Disconnected ? qsTr("Disconnected") : qsTr("No device")

    title: qsTr("Device")
    iconName: "device"
    headerTools: Badge {
        visible: DeviceHub.simulated
        text: qsTr("Simulated")
        color: Theme.simulated
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingM
        Rectangle {
            implicitWidth: 44
            implicitHeight: 44
            radius: Theme.radiusL
            color: Theme.tint(card.stateColor)
            border.width: 1
            border.color: Theme.alpha(card.stateColor, 0.35)
            Icon {
                anchors.centerIn: parent
                name: DeviceHub.simulated ? "cpu" : "usb"
                size: 22
                color: card.stateColor
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: DeviceHub.currentId.length > 0 ? DeviceHub.name : qsTr("No device")
                color: Theme.text
                font.pixelSize: Theme.fontSizeL
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Row {
                spacing: 6
                Led {
                    anchors.verticalCenter: parent.verticalCenter
                    on: DeviceHub.connectionState === DeviceHub.Connected
                    color: card.stateColor
                    size: 7
                }
                Text {
                    text: card.stateText
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeM
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingXs
        columns: 2
        columnSpacing: Theme.spacingM
        rowSpacing: 6
        Text { text: qsTr("Product"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { Layout.fillWidth: true; text: DeviceHub.productType || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeM; elide: Text.ElideRight }
        Text { text: qsTr("Serial"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { Layout.fillWidth: true; text: DeviceHub.serialNumber || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeM; font.family: Theme.monoFamily }
        Text { Layout.alignment: Qt.AlignTop; text: qsTr("In use"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text {
            Layout.fillWidth: true
            text: chipsModel.groups.length === 0 ? qsTr("nothing (device idle)")
                  : chipsModel.groups.map(g => g.title + " (" + g.resources + ")").join(", ")
            color: chipsModel.groups.length === 0 ? Theme.textMuted : Theme.text
            font.pixelSize: Theme.fontSizeM
            wrapMode: Text.WordWrap
            ResourceChips { id: chipsModel; visible: false }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingXs
        spacing: Theme.spacingS
        ActionButton {
            text: qsTr("Reset device…")
            iconName: "reset"
            compact: true
            enabled: DeviceHub.connected && !DeviceHub.resetting && !InstrumentRegistry.anyRunning
            toolTip: InstrumentRegistry.anyRunning ? qsTr("Stop the running instruments first")
                                                   : qsTr("Outputs to 0 V, digital lines to inputs")
            onClicked: resetConfirm.open()
        }
        ActionButton {
            text: qsTr("Info…")
            iconName: "info"
            compact: true
            tone: "ghost"
            onClicked: card.deviceInfoRequested()
        }
        ActionButton {
            text: qsTr("Rescan")
            iconName: "refresh"
            compact: true
            tone: "ghost"
            onClicked: DeviceHub.rescan()
        }
        Item { Layout.fillWidth: true }
    }

    // Simulator controls (development and teaching without hardware).
    ColumnLayout {
        visible: DeviceHub.simulatorAvailable && DeviceHub.simulated
        Layout.fillWidth: true
        Layout.topMargin: Theme.spacingXs
        spacing: Theme.spacingS
        Divider { Layout.fillWidth: true }
        Text {
            text: qsTr("SIMULATOR")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.DemiBold
            font.letterSpacing: 1
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("Plugged in"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeM }
            ToggleSwitch { checked: DeviceHub.simulatorPlugged; onToggled: DeviceHub.simulatorPlugged = checked }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("DMM leads open"); color: Theme.textSecondary; font.pixelSize: Theme.fontSizeM }
            ToggleSwitch { checked: DeviceHub.simulatorDmmOpenCircuit; onToggled: DeviceHub.simulatorDmmOpenCircuit = checked }
        }
        ActionButton {
            text: qsTr("Simulator settings…")
            iconName: "settings"
            compact: true
            tone: "ghost"
            toolTip: qsTr("Test signals, noise, RC corner and DMM values of the simulated device")
            onClicked: simulatorDialog.open()
        }
    }

    SimulatorDialog {
        id: simulatorDialog
    }
    // --simulator-settings (screenshots, docs): open the dialog at start.
    Component.onCompleted: if (AppInfo.flag("simulator-settings") && DeviceHub.simulatorAvailable) simulatorDialog.open()

    ConfirmDialog {
        id: resetConfirm
        title: qsTr("Reset %1?").arg(DeviceHub.name)
        text: qsTr("All outputs go to 0 V and all digital lines become inputs.")
        acceptText: qsTr("Reset")
        destructive: true
        onAccepted: DeviceHub.reset()
    }
}
