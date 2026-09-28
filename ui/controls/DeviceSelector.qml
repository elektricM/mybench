pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// Device picker for the top bar: connection LED, device name, SIMULATED tag; the popup lists
// every device (hot-plug), and offers Rescan, Reset device and the simulator's plug switch.
T.AbstractButton {
    id: control

    property bool compact: false
    signal deviceInfoRequested()

    function requestSelect(deviceId: string) {
        if (deviceId === DeviceHub.currentId)
            return
        if (InstrumentRegistry.anyRunning) {
            switchConfirm.deviceId = deviceId
            switchConfirm.open()
        } else {
            DeviceHub.select(deviceId)
        }
    }
    readonly property color stateColor: DeviceHub.connectionState === DeviceHub.Connected
                                        ? (DeviceHub.simulated ? Theme.simulated : Theme.success)
                                        : DeviceHub.connectionState === DeviceHub.Resetting ? Theme.warning : Theme.danger

    implicitHeight: Theme.controlHeight + 4
    implicitWidth: Math.min(320, row.implicitWidth + 24)
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    onClicked: popup.opened ? popup.close() : popup.open()
    Accessible.name: qsTr("Device: %1").arg(DeviceHub.name)

    background: Rectangle {
        radius: Theme.radiusM
        color: control.down || popup.opened ? Theme.surfacePressed : control.hovered ? Theme.surfaceHover : Theme.surfaceRaised
        border.width: 1
        border.color: popup.opened ? Theme.borderStrong : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusM + 3 }
    }

    contentItem: Item {
        RowLayout {
            id: row
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 8
            spacing: 8
            Led {
                on: DeviceHub.connectionState === DeviceHub.Connected
                color: control.stateColor
                size: 8
            }
            Text {
                Layout.fillWidth: true
                text: DeviceHub.currentId.length > 0 ? DeviceHub.name : qsTr("No device")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Medium
                elide: Text.ElideRight
            }
            Badge {
                visible: DeviceHub.simulated && !control.compact
                text: qsTr("Sim")
                color: Theme.simulated
            }
            Text {
                visible: DeviceHub.connectionState === DeviceHub.Disconnected
                text: qsTr("disconnected")
                color: Theme.danger
                font.pixelSize: Theme.fontSizeM
            }
            Icon {
                name: "chevron-down-line"
                size: 12
                color: Theme.textMuted
            }
        }
    }

    Popup {
        id: popup
        y: control.height + 6
        width: Math.max(control.width, 320)
        padding: 6
        focus: true

        contentItem: ColumnLayout {
            spacing: 2

            Text {
                Layout.leftMargin: 8
                Layout.topMargin: 4
                Layout.bottomMargin: 4
                text: qsTr("DEVICES")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.DemiBold
                font.letterSpacing: 1
            }

            Repeater {
                model: DeviceHub.devices
                delegate: ItemDelegate {
                    id: entry
                    required property int index
                    required property string deviceId
                    required property string displayName
                    required property string serialNumber
                    required property string productType
                    required property bool simulated
                    required property bool connected
                    Layout.fillWidth: true
                    highlighted: deviceId === DeviceHub.currentId
                    enabled: connected
                    onClicked: {
                        popup.close()
                        control.requestSelect(deviceId)
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        Led {
                            on: entry.connected
                            color: entry.connected ? (entry.simulated ? Theme.simulated : Theme.success) : Theme.danger
                            size: 8
                        }
                        Column {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                width: parent.width
                                text: entry.displayName
                                color: entry.enabled ? Theme.text : Theme.textMuted
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: entry.highlighted ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: entry.productType + (entry.serialNumber.length > 0 ? "  ·  " + entry.serialNumber : "")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeS
                                elide: Text.ElideRight
                            }
                        }
                        Badge {
                            visible: entry.simulated
                            text: qsTr("Sim")
                            color: Theme.simulated
                        }
                        Icon {
                            visible: entry.highlighted
                            name: "check"
                            color: Theme.accent
                            size: Theme.iconSizeS
                        }
                    }
                }
            }

            Text {
                visible: DeviceHub.devices.count === 0
                Layout.margins: 8
                text: qsTr("No device found. Connect a myDAQ with USB.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeM
            }

            Divider { Layout.fillWidth: true; Layout.topMargin: 4; Layout.bottomMargin: 4 }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 4
                spacing: Theme.spacingS
                ActionButton {
                    text: qsTr("Rescan")
                    iconName: "refresh"
                    compact: true
                    tone: "ghost"
                    onClicked: DeviceHub.rescan()
                }
                ActionButton {
                    text: qsTr("Reset device…")
                    iconName: "reset"
                    compact: true
                    tone: "ghost"
                    enabled: DeviceHub.connected && !DeviceHub.resetting && !InstrumentRegistry.anyRunning
                    toolTip: InstrumentRegistry.anyRunning ? qsTr("Stop the running instruments first")
                                                           : qsTr("Outputs to 0 V, digital lines to inputs")
                    onClicked: {
                        popup.close()
                        resetConfirm.open()
                    }
                }
                ActionButton {
                    text: qsTr("Info…")
                    iconName: "info"
                    compact: true
                    tone: "ghost"
                    enabled: DeviceHub.currentId.length > 0
                    onClicked: {
                        popup.close()
                        control.deviceInfoRequested()
                    }
                }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                visible: DeviceHub.simulatorAvailable
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.bottomMargin: 4
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Simulator plugged in")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeM
                }
                ToggleSwitch {
                    checked: DeviceHub.simulatorPlugged
                    onToggled: DeviceHub.simulatorPlugged = checked
                }
            }
        }
    }

    ConfirmDialog {
        id: switchConfirm
        property string deviceId
        title: qsTr("Switch device?")
        text: qsTr("Switching the device stops all running instruments. Their settings are kept.")
        acceptText: qsTr("Switch")
        onAccepted: DeviceHub.select(deviceId)
    }
    ConfirmDialog {
        id: resetConfirm
        title: qsTr("Reset %1?").arg(DeviceHub.name)
        text: qsTr("All outputs go to 0 V and all digital lines become inputs.")
        acceptText: qsTr("Reset")
        destructive: true
        onAccepted: DeviceHub.reset()
    }
}
