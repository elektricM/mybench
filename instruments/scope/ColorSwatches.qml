pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import MyBench.Controls

// Color choice for Graph Properties: "Auto" (the theme color), a palette of scope colors and a
// custom color dialog. `colorName` "" = Auto.
//     ColorSwatches { colorName: style.lineColor; autoColor: Theme.ch0; onPicked: n => style.lineColor = n }
Flow {
    id: root

    property string colorName
    property color autoColor: Theme.accent
    property string dialogTitle: qsTr("Color")
    readonly property var swatchColors: ["#64FF00", "#11CBFF", "#FFB000", "#FF5AFF", "#FFFF00", "#FF4D4D", "#4D7CFF",
                                    "#B388FF", "#FFFFFF", "#9AA0A6"]

    signal picked(string name)

    spacing: 4

    component Swatch: AbstractButton {
        id: swatch
        property color fill
        property bool selected: false
        implicitWidth: 18
        implicitHeight: 18
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        background: Rectangle {
            radius: 5
            color: swatch.fill
            border.width: swatch.selected ? 2 : 1
            border.color: swatch.selected ? Theme.text : (swatch.hovered ? Theme.textSecondary : Theme.borderStrong)
            FocusRing { shown: swatch.visualFocus; radius: 7 }
        }
        contentItem: Item {}
    }

    Swatch {
        fill: root.autoColor
        selected: root.colorName.length === 0
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Auto (theme color)")
        Accessible.name: qsTr("Auto color")
        onClicked: root.picked("")
        Text {
            anchors.centerIn: parent
            text: "A"
            font.pixelSize: 10
            font.weight: Font.Bold
            color: Qt.colorEqual(root.autoColor, "white") || root.autoColor.hslLightness > 0.6 ? "black" : "white"
        }
    }
    Repeater {
        model: root.swatchColors
        delegate: Swatch {
            required property string modelData
            fill: modelData
            selected: root.colorName.toUpperCase() === modelData
            ToolTip.visible: hovered
            ToolTip.text: modelData
            Accessible.name: modelData
            onClicked: root.picked(modelData)
        }
    }
    Swatch {
        readonly property bool custom: root.colorName.length > 0 && root.swatchColors.indexOf(root.colorName.toUpperCase()) < 0
        fill: custom ? root.colorName : "transparent"
        selected: custom
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Custom color…")
        Accessible.name: qsTr("Custom color")
        onClicked: dialog.open()
        Icon {
            anchors.centerIn: parent
            visible: !parent.custom
            name: "plus"
            size: 12
            color: Theme.textSecondary
        }
    }
    ColorDialog {
        id: dialog
        title: root.dialogTitle
        selectedColor: root.colorName.length > 0 ? root.colorName : root.autoColor
        onAccepted: root.picked(selectedColor.toString().toUpperCase())
    }
}
