import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

// Context menu of text fields: Undo/Redo, Cut/Copy/Paste/Delete, Select All. Native where the
// platform has native menus (macOS, Windows), our Menu otherwise.
Menu {
    id: menu

    required property Item editor

    popupType: T.Popup.Native

    UndoAction { editor: menu.editor }
    RedoAction { editor: menu.editor }
    MenuSeparator {}
    CutAction { editor: menu.editor }
    CopyAction { editor: menu.editor }
    PasteAction { editor: menu.editor }
    DeleteAction { editor: menu.editor }
    MenuSeparator {}
    SelectAllAction { editor: menu.editor }
}
