pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Framework

// The Device menu (docs/UI.md §7 toolbar device button, §9 "Device" menu): the devices (radio),
// Simulated Device, Rescan, Reset Device…, Device Info…, Simulator Settings…. Content only: the
// owner confirms switching and resetting (DeviceButton does it; a menu bar can call the
// DeviceButton's requestSelect()/requestReset(), or confirm on its own).
//     MenuBar { DeviceMenu { title: qsTr("&Device"); onSelectRequested: id => button.requestSelect(id) ... } }
//     DeviceMenu { id: menu }  menu.popup(button, 0, button.height)
Menu {
    id: menu

    signal selectRequested(string deviceId)
    signal resetRequested()
    signal deviceInfoRequested()
    signal simulatorSettingsRequested()

    title: qsTr("Device")
    popupType: AppInfo.offscreen ? Popup.Item : Popup.Native

    // Devices (inserted at the top; native menus ignore `visible`, entries come and go).
    Instantiator {
        id: devices
        model: DeviceHub.devices
        delegate: MenuItem {
            required property string deviceId
            required property string displayName
            required property string serialNumber
            required property bool simulated
            required property bool connected
            text: displayName
            checkable: true
            checked: deviceId === DeviceHub.currentId
            enabled: connected
            onTriggered: {
                checked = Qt.binding(() => deviceId === DeviceHub.currentId)
                menu.selectRequested(deviceId)
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => menu.removeItem(object as Item)
    }
    Instantiator {
        model: DeviceHub.devices.count === 0 ? 1 : 0
        delegate: MenuItem {
            text: qsTr("No Device Found")
            enabled: false
        }
        onObjectAdded: (index, object) => menu.insertItem(0, object as Item)
        onObjectRemoved: (index, object) => menu.removeItem(object as Item)
    }
    MenuSeparator {}
    Instantiator {
        model: DeviceHub.simulatorAvailable ? 1 : 0
        delegate: MenuItem {
            text: qsTr("Simulated Device")
            checkable: true
            checked: DeviceHub.simulatorPlugged
            onTriggered: DeviceHub.simulatorPlugged = !DeviceHub.simulatorPlugged
        }
        onObjectAdded: (index, object) => menu.insertItem(menu.indexOfItem(rescanItem), object as Item)
        onObjectRemoved: (index, object) => menu.removeItem(object as Item)
    }
    MenuItem {
        id: rescanItem
        text: qsTr("Rescan")
        onTriggered: DeviceHub.rescan()
    }
    MenuSeparator {}
    MenuItem {
        text: qsTr("Reset Device…")
        enabled: DeviceHub.connected && !DeviceHub.resetting && !InstrumentRegistry.anyRunning
        onTriggered: menu.resetRequested()
    }
    MenuItem {
        text: qsTr("Device Info…")
        enabled: DeviceHub.currentId.length > 0
        onTriggered: menu.deviceInfoRequested()
    }
    Instantiator {
        model: DeviceHub.simulatorAvailable ? 1 : 0
        delegate: MenuItem {
            text: qsTr("Simulator Settings…")
            onTriggered: menu.simulatorSettingsRequested()
        }
        onObjectAdded: (index, object) => menu.addItem(object as Item)
        onObjectRemoved: (index, object) => menu.removeItem(object as Item)
    }

    function indexOfItem(item: Item): int {
        for (let i = 0; i < count; ++i) {
            if (itemAt(i) === item)
                return i
        }
        return count
    }
}
