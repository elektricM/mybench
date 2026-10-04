import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// In-window dialog (native MessageDialog/FileDialog/ColorDialog are preferred where they fit):
// the popup surface, a title in the title size, the content, and a right-aligned button row.
T.Dialog {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding,
                            implicitHeaderWidth,
                            implicitFooterWidth)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: Theme.spacingL + 4
    topPadding: Theme.spacingS
    bottomPadding: Theme.spacingS

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
    }

    background: PopupFrame {
        shadow: control.popupType === T.Popup.Item
    }

    header: Text {
        text: control.title
        visible: control.title.length > 0
        elide: Text.ElideRight
        color: Theme.text
        font.pixelSize: Theme.fontSizeTitle - 2
        font.weight: Font.DemiBold
        leftPadding: control.leftPadding
        rightPadding: control.rightPadding
        topPadding: Theme.spacingL + 2
        bottomPadding: Theme.spacingXs
    }

    footer: DialogButtonBox {
        visible: count > 0
    }

    T.Overlay.modal: Rectangle { color: Theme.overlay }
    T.Overlay.modeless: Rectangle { color: "transparent" }
}
