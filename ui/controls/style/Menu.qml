import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Menus are native (Popup.Native: the platform's menus on macOS, Windows and KDE) except in
// screenshot/test mode (Theme.inlinePopups), where this in-window rendering is used. Use only
// MenuItem, MenuSeparator, Menu and Action inside, so the native path can show every entry.
T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)

    popupType: Theme.inlinePopups ? T.Popup.Item : T.Popup.Native
    margins: 0
    padding: Theme.spacingXs
    overlap: Theme.spacingXs
    font.pixelSize: Theme.fontSizeBody

    delegate: MenuItem {}

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > control.height : false
        clip: true
        currentIndex: control.currentIndex
        T.ScrollIndicator.vertical: ScrollIndicator {}
    }

    background: PopupFrame {
        implicitWidth: 200
        implicitHeight: Theme.controlHeight
        shadow: control.popupType === T.Popup.Item
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
