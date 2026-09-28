import QtQuick
import QtQuick.Dialogs
import MyBench.Framework

// Exports the instrument's last data (InstrumentController.exportData) to a CSV/TSV file chosen
// in a save dialog (the ELVISmx "Log" button). Enabled when the controller has data.
//     ExportButton { controller: root.controller }
IconButton {
    id: button

    property InstrumentController controller: null
    property bool allowWhileRunning: true
    property string extension: "csv"

    iconName: "export"
    toolTip: qsTr("Export data…")
    enabled: controller !== null && controller.hasData && (allowWhileRunning || !controller.running)
    onClicked: {
        dialog.selectedFile = DataExport.suggestedFileUrl(controller.instrumentId, extension)
        dialog.open()
    }

    FileDialog {
        id: dialog
        title: qsTr("Export %1 data").arg(button.controller ? button.controller.title : "")
        fileMode: FileDialog.SaveFile
        nameFilters: DataExport.nameFilters
        currentFolder: DataExport.lastFolder
        onAccepted: button.controller.exportData(selectedFile)
    }
}
