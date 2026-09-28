pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ComboBox whose items can be disabled (ELVISmx lists items that the myDAQ cannot use, e.g. the
// Trigger Type "Digital", the TRIG/SYNC sources, and the source list separator).
//     ScopeCombo { model: ["Immediate", "Digital", "Edge"]; disabledIndices: [1]
//                  disabledToolTip: qsTr("Not available on NI myDAQ") }
ComboBox {
    id: control

    property var disabledIndices: []
    property string disabledToolTip: qsTr("Not available on NI myDAQ")
    property string toolTip

    function isDisabled(i: int): bool {
        return control.disabledIndices.indexOf(i) >= 0
    }

    Layout.fillWidth: true
    ToolTip.visible: hovered && toolTip.length > 0
    ToolTip.text: toolTip
    ToolTip.delay: 600

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index

        width: ListView.view.width
        text: typeof item.modelData === "object" ? item.modelData[control.textRole] : String(item.modelData)
        enabled: !control.isDisabled(item.index)
        font.weight: control.currentIndex === item.index ? Font.DemiBold : Font.Normal
        highlighted: control.highlightedIndex === item.index
        hoverEnabled: true
        checkable: true
        checked: control.currentIndex === item.index
        ToolTip.visible: hovered && !enabled && control.disabledToolTip.length > 0 && item.text.indexOf("---") < 0
        ToolTip.text: control.disabledToolTip
    }
}
