pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Framework
import MyBench.Controls

// A small window of the shell (Preferences, About, Device Info, Simulator Settings) around a
// reusable pane (docs/UI.md §10). Offscreen (screenshots, tests: AppInfo.offscreen) the pane is
// shown in a popup inside the main window instead, so it can be captured (§13.9).
//     PaneWindow { id: about; title: qsTr("About"); pane: Component { AboutPane {} } }
//     about.show()
Item {
    id: root

    property string title
    property Component pane
    readonly property bool inline: AppInfo.offscreen
    readonly property bool shown: inline ? popup.opened : window.visible
    // The pane instance while shown (tests).
    readonly property Item paneItem: inline ? popupPane : windowPane
    readonly property Item windowPane: windowLoader.item as Item
    readonly property Item popupPane: popupLoader.item as Item

    function show() {
        if (inline) {
            popup.open()
            return
        }
        window.show()
        window.raise()
        window.requestActivate()
    }
    function close() {
        popup.close()
        window.close()
    }

    Window {
        id: window
        title: root.title
        flags: Qt.Dialog
        transientParent: root.Window.window
        color: Theme.window
        width: root.windowPane ? root.windowPane.implicitWidth : 0
        height: root.windowPane ? root.windowPane.implicitHeight : 0
        minimumWidth: root.windowPane ? root.windowPane.implicitWidth : 0
        minimumHeight: root.windowPane ? Math.min(root.windowPane.implicitHeight, Theme.minWindowHeight) : 0

        Loader {
            id: windowLoader
            anchors.fill: parent
            active: window.visible
            sourceComponent: root.pane
            focus: true
        }
        Shortcut {
            sequences: [StandardKey.Cancel, StandardKey.Close]
            onActivated: window.close()
        }
    }

    Popup {
        id: popup
        parent: root.Overlay.overlay      // attached to the Item: on the Popup it resolves to null
        x: Math.round(((parent ? parent.width : 0) - width) / 2)
        y: Math.round(((parent ? parent.height : 0) - height) / 3)
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        width: root.popupPane ? Math.min(root.popupPane.implicitWidth, (parent ? parent.width : 0) - 2 * Theme.spacingXl) : 0
        height: root.popupPane ? Math.min(root.popupPane.implicitHeight, (parent ? parent.height : 0) - 2 * Theme.spacingXl) : 0
        // The pane draws the window surface; the frame adds the hairline and the shadow.
        background: PopupFrame {
            color: Theme.window
            radius: 0
        }
        contentItem: Loader {
            id: popupLoader
            active: popup.visible
            sourceComponent: root.pane
            clip: true
        }
    }
}
