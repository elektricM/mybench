pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import MyBench.Controls

// The LabVIEW-style radix prefix of a numeric value ("x", "d", "b"...): click it to choose
// Decimal, Hex, Octal, Binary or SI notation. `radix` is a DioController.Radix.
AbstractButton {
    id: root

    property int radix: DioController.Hex
    signal radixSelected(int radix)

    readonly property var names: [qsTr("Decimal"), qsTr("Hex"), qsTr("Octal"), qsTr("Binary"), qsTr("SI Notation")]
    readonly property var letters: ["d", "x", "o", "b", "p"]   // LabVIEW radix letters

    implicitWidth: 22
    implicitHeight: 20
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.role: Accessible.ButtonMenu
    Accessible.name: qsTr("Radix: %1").arg(names[radix])
    ToolTip.visible: hovered
    ToolTip.delay: 500
    ToolTip.text: qsTr("Display format: %1").arg(names[radix])

    background: Rectangle {
        radius: Theme.radiusXs
        color: root.hovered || menu.visible ? Theme.surfaceHover : "transparent"
        border.width: 1
        border.color: root.hovered || menu.visible ? Theme.borderStrong : Theme.border
    }
    contentItem: Text {
        text: root.letters[root.radix] ?? ""
        color: Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fontSizeM
        font.family: Theme.monoFamily
        font.weight: Font.DemiBold
    }
    onClicked: menu.popup(root, 0, root.height + 2)

    Menu {
        id: menu
        Instantiator {
            model: root.names
            onObjectAdded: (index, object) => menu.insertItem(index, object as MenuItem)
            onObjectRemoved: (index, object) => menu.removeItem(object as MenuItem)
            delegate: MenuItem {
                required property string modelData
                required property int index
                text: root.letters[index] + "   " + modelData
                checkable: true
                checked: index === root.radix
                onTriggered: root.radixSelected(index)
            }
        }
    }
}
