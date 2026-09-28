import QtQuick
import QtQuick.Dialogs

// Sketch "Import...": picks a file, then the import wizard replaces the selected sketch's values.
Item {
    id: root

    required property WaveformEditor editor

    function open() {
        fileDialog.open()
    }

    implicitWidth: 0
    implicitHeight: 0

    FileDialog {
        id: fileDialog
        title: qsTr("Import sketch values")
        nameFilters: [qsTr("Waveforms (*.mbw *.wav *.csv *.txt *.bin *.wdt)"), qsTr("All files (*)")]
        onAccepted: {
            wizard.mode = 1
            wizard.openFor(selectedFile)
        }
    }
    ImportDialog {
        id: wizard
        editor: root.editor
    }
}
