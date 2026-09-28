pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// Add or edit a user tool of the Home "Tools" card (app.md 18.1): a name and a program, document,
// folder or web address, plus optional arguments for programs.
//     ToolDialog { id: toolDialog }   toolDialog.add()   toolDialog.edit(index)
Dialog {
    id: dialog

    property int index: -1               // -1: new tool
    readonly property string kind: Launcher.targetKind(targetField.text)

    function add() {
        index = -1
        nameField.text = ""
        targetField.text = ""
        argsField.text = ""
        open()
        targetField.forceActiveFocus()
    }
    function edit(toolIndex: int) {
        const tool = Launcher.tools[toolIndex]
        if (!tool)
            return
        index = toolIndex
        nameField.text = tool.name
        targetField.text = tool.target
        argsField.text = tool.arguments
        open()
        nameField.forceActiveFocus()
    }

    anchors.centerIn: Overlay.overlay
    width: 480
    modal: true
    title: index < 0 ? qsTr("Add tool") : qsTr("Edit tool")
    standardButtons: Dialog.Ok | Dialog.Cancel

    Component.onCompleted: standardButton(Dialog.Ok).text = Qt.binding(() => dialog.index < 0 ? qsTr("Add") : qsTr("Save"))
    onOpened: standardButton(Dialog.Ok).enabled = Qt.binding(() => targetField.text.trim().length > 0)

    onAccepted: {
        if (index < 0)
            Launcher.addTool(nameField.text, targetField.text, argsField.text)
        else
            Launcher.updateTool(index, nameField.text, targetField.text, argsField.text)
    }

    FileDialog {
        id: browse
        title: qsTr("Choose a program or file")
        fileMode: FileDialog.OpenFile
        onAccepted: {
            targetField.text = Launcher.normalizedTarget(selectedFile.toString())
            if (nameField.text.trim().length === 0)
                nameField.placeholderText = qsTr("Name (default: file name)")
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingL

        Text {
            Layout.fillWidth: true
            text: qsTr("A tool is a shortcut on Home: a program to start, a document or folder to open, or a web page.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeM
            wrapMode: Text.WordWrap
        }
        Field {
            label: qsTr("Program, file or web address")
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                TextField {
                    id: targetField
                    Layout.fillWidth: true
                    placeholderText: qsTr("/path/to/program or https://…")
                    selectByMouse: true
                    onAccepted: dialog.accept()
                }
                ActionButton {
                    text: qsTr("Browse…")
                    iconName: "folder-open"
                    onClicked: browse.open()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: targetField.text.trim().length > 0
                text: dialog.kind === "url" ? qsTr("Opens in the web browser.")
                    : dialog.kind === "program" ? qsTr("Program: started as a separate process.")
                    : dialog.kind === "folder" ? qsTr("Folder: opens in the file manager.")
                    : dialog.kind === "file" ? qsTr("Document: opens with its default application.")
                    : qsTr("Not found on this computer.")
                color: dialog.kind === "missing" ? Theme.warning : Theme.textMuted
                font.pixelSize: Theme.fontSizeS
            }
        }
        Field {
            label: qsTr("Name")
            TextField {
                id: nameField
                Layout.fillWidth: true
                placeholderText: qsTr("Name (default: file or site name)")
                selectByMouse: true
                onAccepted: dialog.accept()
            }
        }
        Field {
            visible: dialog.kind === "program"
            label: qsTr("Arguments")
            trailing: qsTr("optional")
            TextField {
                id: argsField
                Layout.fillWidth: true
                placeholderText: qsTr("e.g. --verbose \"my file.txt\"")
                selectByMouse: true
                onAccepted: dialog.accept()
            }
        }
    }
}
