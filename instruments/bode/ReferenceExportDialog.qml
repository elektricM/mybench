import QtQuick
import QtQuick.Dialogs
import MyBench.Framework

// Save dialog for one Bode reference curve (CSV/TSV through DataExport).
FileDialog {
    id: dialog

    required property BodeController controller
    property int slot: -1

    title: qsTr("Export reference")
    fileMode: FileDialog.SaveFile
    nameFilters: DataExport.nameFilters
    currentFolder: DataExport.lastFolder
    onAccepted: {
        if (controller.exportReference(slot, selectedFile))
            Notifications.notify(Notifications.Success, qsTr("Reference exported"), DataExport.localPath(selectedFile))
    }
}
