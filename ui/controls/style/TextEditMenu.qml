import QtQuick
import QtQuick.Templates as T

// Context menu of text fields: Undo/Redo, Cut/Copy/Paste/Delete, Select All. Native (the style's
// Menu rule: in-window only in screenshot/test mode).
// The actions are defined here rather than taken from QtQuick.Controls.impl: RedoAction (and
// possibly others) only exist from Qt 6.10, and the minimum is 6.9.
Menu {
    id: menu

    required property var editor   // TextField or TextArea (TextInput/TextEdit API)


    T.Action {
        text: qsTr("Undo")
        icon.name: "edit-undo"
        shortcut: StandardKey.Undo
        enabled: menu.editor.canUndo
        onTriggered: menu.editor.undo()
    }
    T.Action {
        text: qsTr("Redo")
        icon.name: "edit-redo"
        shortcut: StandardKey.Redo
        enabled: menu.editor.canRedo
        onTriggered: menu.editor.redo()
    }
    MenuSeparator {}
    T.Action {
        text: qsTr("Cut")
        icon.name: "edit-cut"
        shortcut: StandardKey.Cut
        enabled: !menu.editor.readOnly && menu.editor.selectedText.length > 0 && menu.editor.hasOwnProperty("cut")
        onTriggered: menu.editor.cut()
    }
    T.Action {
        text: qsTr("Copy")
        icon.name: "edit-copy"
        shortcut: StandardKey.Copy
        enabled: menu.editor.selectedText.length > 0
        onTriggered: menu.editor.copy()
    }
    T.Action {
        text: qsTr("Paste")
        icon.name: "edit-paste"
        shortcut: StandardKey.Paste
        enabled: !menu.editor.readOnly && menu.editor.hasOwnProperty("paste")
        onTriggered: menu.editor.paste()
    }
    T.Action {
        text: qsTr("Delete")
        icon.name: "edit-delete"
        shortcut: StandardKey.Delete
        enabled: menu.editor.selectedText.length > 0 && !menu.editor.readOnly
        onTriggered: menu.editor.remove(menu.editor.selectionStart, menu.editor.selectionEnd)
    }
    MenuSeparator {}
    T.Action {
        text: qsTr("Select All")
        icon.name: "edit-select-all"
        shortcut: StandardKey.SelectAll
        onTriggered: menu.editor.selectAll()
    }
}
