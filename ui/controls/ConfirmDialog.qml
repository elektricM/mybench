import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modal confirmation with a message and a (possibly destructive) accept button.
//     ConfirmDialog { id: confirm; title: qsTr("Reset device?"); text: ...; acceptText: qsTr("Reset")
//                     destructive: true; onAccepted: DeviceHub.reset() }
Dialog {
    id: dialog

    property string text
    property string acceptText: qsTr("OK")
    property bool destructive: false

    anchors.centerIn: Overlay.overlay
    width: 440
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape

    contentItem: Text {
        text: dialog.text
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
        wrapMode: Text.WordWrap
        lineHeight: 1.2
    }

    footer: RowLayout {
        spacing: Theme.spacingS
        Item { Layout.fillWidth: true }
        ActionButton {
            Layout.bottomMargin: Theme.spacingL
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
        ActionButton {
            Layout.bottomMargin: Theme.spacingL
            Layout.rightMargin: Theme.spacingL
            text: dialog.acceptText
            tone: dialog.destructive ? "danger" : "accent"
            onClicked: dialog.accept()
        }
    }
}
