pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import MyBench.Framework

// The toolbar's device button (docs/UI.md §7): a state dot and the device name ("Simulated myDAQ",
// "myDAQ 036CD1C6", "Disconnected — waiting for device"); its DeviceMenu lists the devices and
// the device commands. Switching while instruments run and resetting ask first.
//     DeviceButton { onDeviceInfoRequested: deviceInfo.show(); onSimulatorSettingsRequested: ... }
// Built on the template (own content and background), so platform styles never have to accept
// customized parts; it looks like a MyBench tool button everywhere.
T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 0
    horizontalPadding: Theme.spacingS
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.pixelSize: Theme.fontSizeBody

    // Name only, no state word (narrow toolbars).
    property bool compact: false
    readonly property alias menu: deviceMenu
    readonly property string label: {
        switch (DeviceHub.connectionState) {
        case DeviceHub.NoDevice:
            return qsTr("No Device")
        case DeviceHub.Disconnected:
            return compact ? qsTr("Disconnected") : qsTr("Disconnected — waiting for device")
        case DeviceHub.Resetting:
            return qsTr("Resetting %1…").arg(deviceName)
        default:
            return deviceName
        }
    }
    readonly property string deviceName: DeviceHub.simulated ? qsTr("Simulated myDAQ")
                                         : DeviceHub.serialNumber.length > 0 ? qsTr("myDAQ %1").arg(DeviceHub.serialNumber)
                                         : DeviceHub.name
    readonly property color stateColor: DeviceHub.connectionState === DeviceHub.Connected ? Theme.positive
                                        : DeviceHub.connectionState === DeviceHub.Resetting ? Theme.caution
                                        : DeviceHub.connectionState === DeviceHub.Disconnected ? Theme.negative
                                        : Theme.textTertiary

    signal deviceInfoRequested()
    signal simulatorSettingsRequested()

    // Selects a device, asking first while instruments run.
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
    function requestReset() {
        resetConfirm.open()
    }

    down: pressed || deviceMenu.visible
    onClicked: deviceMenu.popup(control, 0, control.height)
    Accessible.name: qsTr("Device: %1").arg(label)
    ToolTip.visible: hovered && !deviceMenu.visible
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: DeviceHub.currentId.length > 0 ? qsTr("%1 · %2").arg(DeviceHub.productType).arg(DeviceHub.backend)
                                                 : qsTr("Connect a myDAQ with USB")

    contentItem: RowLayout {
        spacing: Theme.spacingS
        Rectangle {
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: control.stateColor
        }
        Text {
            objectName: "deviceButtonLabel"
            Layout.fillWidth: true
            text: control.label
            color: DeviceHub.connectionState === DeviceHub.Disconnected ? Theme.negative : Theme.text
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Icon {
            name: "chevron-down"
            size: 12
            color: Theme.textSecondary
        }
    }

    background: Rectangle {
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl
        color: control.down ? Theme.pressedFill : control.hovered ? Theme.hoverFill : "transparent"
        FocusRing { shown: control.visualFocus }
    }

    DeviceMenu {
        id: deviceMenu
        onSelectRequested: deviceId => control.requestSelect(deviceId)
        onResetRequested: control.requestReset()
        onDeviceInfoRequested: control.deviceInfoRequested()
        onSimulatorSettingsRequested: control.simulatorSettingsRequested()
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
        title: qsTr("Reset %1?").arg(control.deviceName)
        text: qsTr("All outputs go to 0 V and all digital lines become inputs.")
        acceptText: qsTr("Reset")
        destructive: true
        onAccepted: DeviceHub.reset()
    }
}
