pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import MyBench.Framework

// Print / Export image options (the ELVISmx Print dialog, app.md 10.2): destination (PNG image,
// PDF, printer), content (display area or whole panel), Ink saver, user name / computer name /
// time stamp, settings table, orientation, printer and copies. The choices are remembered
// (Settings "print"). InstrumentPanel opens it for Export image… / Export PDF… / Print….
//     PrintDialog { id: printDialog; onAccepted: panel.producePrint(printDialog.options()) }
Dialog {
    id: dialog

    // "image", "pdf" or "printer" (set before open()).
    property string destination: "pdf"
    property string instrumentTitle

    // options() for DataExport.writeImage / writePdf / print.
    function options(): var {
        return {
            destination: destination,
            target: dialog.store.target,
            inkSaver: dialog.store.inkSaver,
            userName: dialog.store.userName,
            computerName: dialog.store.computerName,
            timeStamp: dialog.store.timeStamp,
            settingsTable: dialog.store.settingsTable,
            orientation: dialog.store.orientation,
            copies: dialog.store.copies,
            printer: dialog.store.printer.length > 0 ? dialog.store.printer : DataExport.defaultPrinter
        }
    }

    // Choices per kind of output: a PNG stays a plain grab by default, pages (PDF, printer) get
    // the time stamp and the settings table like ELVISmx prints.
    component PrintSettings: Settings {
        property string target: "display"
        property bool inkSaver: false
        property bool userName: false
        property bool computerName: false
        property bool timeStamp: false
        property bool settingsTable: false
        property string orientation: "landscape"
        property int copies: 1
        property string printer: ""
    }
    PrintSettings {
        id: imageStore
        category: "print/image"
    }
    PrintSettings {
        id: pageStore
        category: "print/page"
        timeStamp: true
        settingsTable: true
    }
    readonly property PrintSettings store: destination === "image" ? imageStore : pageStore

    readonly property var destinations: DataExport.printingAvailable ? ["image", "pdf", "printer"] : ["image", "pdf"]

    anchors.centerIn: Overlay.overlay
    width: 460
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    title: instrumentTitle.length > 0 ? qsTr("Print / Export %1").arg(instrumentTitle) : qsTr("Print / Export")

    component OptionRow: RowLayout {
        id: optionRow
        property string label
        property string detail
        property alias checked: toggle.checked
        signal toggled(bool checked)
        Layout.fillWidth: true
        spacing: Theme.spacingM
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text { Layout.fillWidth: true; text: optionRow.label; color: Theme.text; font.pixelSize: Theme.fontSizeBody }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: optionRow.detail
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
        }
        Switch {
            id: toggle
            Accessible.name: optionRow.label
            onToggled: optionRow.toggled(checked)
        }
    }
    component Caption: Text {
        Layout.topMargin: Theme.spacingXs
        color: Theme.text
        font.pixelSize: Theme.fontSizeBody
        font.weight: Font.Medium
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingM

        Caption { text: qsTr("Destination") }
        SegmentedControl {
            objectName: "destination"
            model: dialog.destinations.map(d => d === "image" ? { text: qsTr("Image (PNG)"), icon: "image" }
                                               : d === "pdf" ? { text: qsTr("PDF"), icon: "file" }
                                                              : { text: qsTr("Printer"), icon: "printer" })
            currentIndex: Math.max(0, dialog.destinations.indexOf(dialog.destination))
            onActivated: index => dialog.destination = dialog.destinations[index]
        }

        Caption { text: qsTr("Content") }
        SegmentedControl {
            model: [qsTr("Display"), qsTr("Whole panel")]
            currentIndex: dialog.store.target === "panel" ? 1 : 0
            onActivated: index => dialog.store.target = index === 1 ? "panel" : "display"
        }

        OptionRow {
            label: qsTr("Ink saver")
            detail: qsTr("White paper, dark grid and text, coloured traces")
            checked: dialog.store.inkSaver
            onToggled: checked => dialog.store.inkSaver = checked
        }
        OptionRow {
            label: qsTr("User name")
            detail: DataExport.identity.user ?? ""
            checked: dialog.store.userName
            onToggled: checked => dialog.store.userName = checked
        }
        OptionRow {
            label: qsTr("Computer name")
            detail: DataExport.identity.computer ?? ""
            checked: dialog.store.computerName
            onToggled: checked => dialog.store.computerName = checked
        }
        OptionRow {
            label: qsTr("Time stamp")
            checked: dialog.store.timeStamp
            onToggled: checked => dialog.store.timeStamp = checked
        }
        OptionRow {
            label: qsTr("Settings table")
            detail: qsTr("The instrument's settings under the image")
            checked: dialog.store.settingsTable
            onToggled: checked => dialog.store.settingsTable = checked
        }

        FormRow {
            visible: dialog.destination !== "image"
            label: qsTr("Orientation")
            SegmentedControl {
                model: [qsTr("Landscape"), qsTr("Portrait")]
                currentIndex: dialog.store.orientation === "portrait" ? 1 : 0
                onActivated: index => dialog.store.orientation = index === 1 ? "portrait" : "landscape"
            }
        }
        FormRow {
            visible: dialog.destination === "printer"
            label: qsTr("Printer")
            ComboBox {
                Layout.fillWidth: true
                model: DataExport.printers.length > 0 ? DataExport.printers : [qsTr("No printer")]
                enabled: DataExport.printers.length > 0
                currentIndex: Math.max(0, DataExport.printers.indexOf(dialog.store.printer.length > 0 ? dialog.store.printer : DataExport.defaultPrinter))
                onActivated: index => dialog.store.printer = DataExport.printers[index] ?? ""
            }
        }
        FormRow {
            visible: dialog.destination === "printer"
            label: qsTr("Copies")
            SpinBox {
                from: 1
                to: 99
                value: dialog.store.copies
                onValueModified: dialog.store.copies = value
            }
        }
    }

    footer: RowLayout {
        spacing: Theme.spacingS
        Item { Layout.fillWidth: true }
        Button {
            Layout.bottomMargin: Theme.spacingL
            implicitHeight: Theme.controlHeightL
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
        Button {
            objectName: "acceptButton"
            Layout.bottomMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL + 4
            implicitHeight: Theme.controlHeightL
            text: dialog.destination === "printer" ? qsTr("Print") : qsTr("Save…")
            highlighted: true
            enabled: dialog.destination !== "printer" || DataExport.printers.length > 0
            onClicked: dialog.accept()
        }
    }
}
