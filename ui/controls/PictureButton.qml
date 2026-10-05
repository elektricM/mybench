import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T

// Checkable picture button: a small pictogram above a label, for choices that read better as
// pictures than as a dropdown (measurement types, trigger types, waveform shapes). The pictogram
// is `glyph` (an SVG path on a 32 × 18 box, stroked with the text color) or `iconName` (an icon
// of assets/icons). Checked = accent border and soft accent fill. Controlled: bind `checked` to the
// state and change the state in onClicked; a click never touches `checked`, so the binding holds.
// `autoToggle: true` makes it a standard checkable button (a click flips `checked`, toggled()).
//     PictureButton { text: qsTr("RMS"); glyph: "M3 9c3-7 5-7 8 0s5 7 8 0"
//                     checked: ctl.kind === 2; onClicked: ctl.kind = 2 }
T.AbstractButton {
    id: control

    property string glyph
    property string iconName
    property color accentColor: Theme.accent
    property bool autoToggle: false

    checkable: autoToggle
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Math.max(72, implicitContentWidth + 12)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 6
    horizontalPadding: 3      // narrow grids (5 per row in a 250 px card) keep their labels
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked

    readonly property color ink: !enabled ? Theme.textDisabled
                                : checked ? accentColor
                                : hovered ? Theme.text : Theme.textSecondary

    background: Rectangle {
        radius: Theme.radiusControl + 1
        color: control.checked ? (control.pressed ? Theme.alpha(control.accentColor, Theme.dark ? 0.28 : 0.18) : Theme.alpha(control.accentColor, Theme.dark ? 0.20 : 0.12))
             : control.pressed ? Theme.pressedFill : control.hovered ? Theme.hoverFill : Theme.cardFill
        border.width: 1
        border.color: control.checked ? control.accentColor : control.hovered ? Theme.controlBorder : Theme.separator
        FocusRing { shown: control.visualFocus }
    }

    contentItem: Item {
        implicitHeight: picture.height + 3 + label.implicitHeight
        implicitWidth: Math.max(picture.width, label.implicitWidth)
        Item {
            id: picture
            width: 32
            height: 18
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.round((parent.height - height - 3 - label.implicitHeight) / 2)
            Shape {
                anchors.fill: parent
                visible: control.glyph.length > 0
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: control.ink
                    strokeWidth: 1.4
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: control.glyph }
                }
            }
            Icon {
                visible: control.glyph.length === 0 && control.iconName.length > 0
                anchors.centerIn: parent
                name: control.iconName
                size: 18
                color: control.ink
            }
        }
        Text {
            id: label
            anchors.horizontalCenter: parent.horizontalCenter
            y: picture.y + picture.height + 3
            width: Math.min(implicitWidth, parent.width)
            text: control.text
            color: control.ink
            font.pixelSize: Theme.fontSizeCaption
            font.weight: control.checked ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
