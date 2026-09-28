import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Device info (app.md 4): identity and capabilities of the current device.
Dialog {
    id: dialog

    anchors.centerIn: Overlay.overlay
    width: 460
    modal: true
    title: qsTr("Device info")
    standardButtons: Dialog.Close

    contentItem: GridLayout {
        columns: 2
        columnSpacing: Theme.spacingL
        rowSpacing: Theme.spacingS
        Text { text: qsTr("Name"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { Layout.fillWidth: true; text: DeviceHub.name || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
        Text { text: qsTr("Id"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { text: DeviceHub.currentId || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.monoFamily }
        Text { text: qsTr("Product"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { text: DeviceHub.productType || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
        Text { text: qsTr("Serial number"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { text: DeviceHub.serialNumber || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody; font.family: Theme.monoFamily }
        Text { text: qsTr("Backend"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { text: DeviceHub.backend || "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
        Text { text: qsTr("State"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text {
            text: (DeviceHub.connected ? qsTr("connected") : qsTr("disconnected")) + (DeviceHub.simulated ? qsTr(", simulated") : "")
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
        }
        Text { text: qsTr("Max AI rate"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { text: DeviceHub.maxAiRate > 0 ? Units.format(DeviceHub.maxAiRate, "S/s") + qsTr(" per channel") : "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
        Text { text: qsTr("Max AO rate"); color: Theme.textMuted; font.pixelSize: Theme.fontSizeM }
        Text { text: DeviceHub.maxAoRate > 0 ? Units.format(DeviceHub.maxAoRate, "S/s") + qsTr(" per channel") : "—"; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
    }
}
