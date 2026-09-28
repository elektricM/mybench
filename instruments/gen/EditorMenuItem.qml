import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Menu item of the Waveform Editor's File / Edit / Help menus with its keyboard shortcut shown on
// the right (the editor handles the keys itself while it has the focus, see EditorView).
MenuItem {
    id: control

    property string keys       // "Ctrl+S" (⌘S on macOS)
    property string iconName   // Theme icon name

    contentItem: RowLayout {
        spacing: Theme.spacingM
        Icon {
            Layout.preferredWidth: Theme.iconSize
            name: control.iconName
            size: Theme.iconSize
            color: control.enabled ? Theme.textSecondary : Theme.textDisabled
        }
        Text {
            Layout.fillWidth: true
            text: control.text
            color: control.enabled ? Theme.text : Theme.textDisabled
            font: control.font
            elide: Text.ElideRight
        }
        Text {
            visible: control.keys.length > 0
            text: KeyText.of(control.keys)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
        }
    }
}
