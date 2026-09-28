import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// Integer entry in a chosen radix (the Digital Writer's "Pattern Number"): the radix prefix
// button, the value ("A", "1010", "10"), and ±1 steppers. Values are coerced to 0 ... maximum.
// Controlled: bind `value`, handle valueModified(value).
RowLayout {
    id: root

    property int value: 0
    property int maximum: 255
    property int radix: DioController.Hex
    property int bits: 8           // bits (binary padding)
    signal valueModified(int value)
    signal radixSelected(int radix)

    readonly property var bases: [10, 16, 8, 2, 10]

    spacing: Theme.spacingXs

    function commit() {
        const text = field.text.trim().replace(/^0x/i, "")
        const parsed = parseInt(text, root.bases[root.radix])
        if (!isNaN(parsed))
            root.valueModified(Math.max(0, Math.min(root.maximum, parsed)))
        field.text = Qt.binding(() => field.formatted)
    }

    DioRadixButton {
        radix: root.radix
        onRadixSelected: r => root.radixSelected(r)
    }
    TextField {
        id: field
        readonly property string formatted: {
            const v = Math.max(0, root.value)
            switch (root.radix) {
            case DioController.Hex: return v.toString(16).toUpperCase()
            case DioController.Octal: return v.toString(8)
            case DioController.Binary: return v.toString(2).padStart(root.bits, "0")
            default: return v.toString(10)
            }
        }
        Layout.fillWidth: true
        text: formatted
        font.family: Theme.monoFamily
        font.features: Theme.tabularFigures
        horizontalAlignment: Text.AlignRight
        selectByMouse: true
        onEditingFinished: root.commit()
        Keys.onUpPressed: root.valueModified(Math.min(root.maximum, root.value + 1))
        Keys.onDownPressed: root.valueModified(Math.max(0, root.value - 1))
        Accessible.name: qsTr("Pattern Number")
    }
    IconButton {
        iconName: "minus"
        size: Theme.controlHeightS
        iconSize: Theme.iconSizeS
        enabled: root.enabled && root.value > 0
        toolTip: qsTr("−1")
        onClicked: root.valueModified(Math.max(0, root.value - 1))
    }
    IconButton {
        iconName: "plus"
        size: Theme.controlHeightS
        iconSize: Theme.iconSizeS
        enabled: root.enabled && root.value < root.maximum
        toolTip: qsTr("+1")
        onClicked: root.valueModified(Math.min(root.maximum, root.value + 1))
    }
}
