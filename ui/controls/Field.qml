import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A labelled control: caption above, the control(s) below. Children should set
// Layout.fillWidth: true.
//     Field { label: qsTr("Volts/Div"); ComboBox { Layout.fillWidth: true } }
ColumnLayout {
    id: root

    property string label
    property string hint                 // tooltip on the caption
    property string trailing             // small text right of the caption (e.g. a unit or range)
    property bool disabled: false        // dims the caption (the control handles its own state)

    Layout.fillWidth: true
    spacing: 5

    RowLayout {
        Layout.fillWidth: true
        visible: root.label.length > 0
        spacing: Theme.spacingXs
        Text {
            id: caption
            Layout.fillWidth: true
            text: root.label
            color: root.disabled ? Theme.textDisabled : Theme.textSecondary
            font.pixelSize: Theme.fontSizeS + 1
            font.weight: Font.Medium
            elide: Text.ElideRight
            HoverHandler { id: captionHover }
            ToolTip.visible: captionHover.hovered && root.hint.length > 0
            ToolTip.delay: 400
            ToolTip.text: root.hint
        }
        Text {
            visible: root.trailing.length > 0
            text: root.trailing
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeS
            font.features: Theme.tabularFigures
        }
    }
}
