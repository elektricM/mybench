pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import MyBench.Controls

// A tight segmented choice for the narrow places of the Digital I/O panel (a line tile's
// direction, the Read / Write groups): equal segments, small padding, the current segment filled
// in its color (`model` entries { text, toolTip, color }). Never elides "In" / "Out" / "0–3" in an
// 80 px tile or a 220 px column, where the platform SegmentedControl would. A custom part built
// on QtQuick.Templates (docs/UI.md: no native style overridden).
//     DioChoice { model: [{ text: "In", color: green }, { text: "Out", color: orange }]
//                 currentIndex: dir; onActivated: index => setDirection(index) }
Item {
    id: root

    property var model: []
    property int currentIndex: -1
    property real spacing: 2
    signal activated(int index)

    implicitWidth: 40 * Math.max(1, model.length)
    implicitHeight: Theme.controlHeightS

    Row {
        anchors.fill: parent
        spacing: root.spacing
        Repeater {
            model: root.model
            delegate: T.AbstractButton {
                id: segment
                required property var modelData
                required property int index
                readonly property bool current: index === root.currentIndex
                readonly property color tint: modelData.color ?? Theme.accent
                objectName: "choice" + index
                width: (root.width - (root.model.length - 1) * root.spacing) / Math.max(1, root.model.length)
                height: root.height
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                text: modelData.text ?? ""
                Accessible.role: Accessible.RadioButton
                Accessible.checked: current
                ToolTip.visible: hovered && (modelData.toolTip ?? "").length > 0
                ToolTip.delay: 500
                ToolTip.text: modelData.toolTip ?? ""
                onClicked: root.activated(index)

                background: Rectangle {
                    radius: Theme.radiusS
                    color: segment.current ? segment.tint
                         : segment.hovered ? Theme.surfaceHover : Theme.alpha(Theme.text, Theme.dark ? 0.06 : 0.05)
                    border.width: segment.visualFocus ? 2 : 0
                    border.color: Theme.focusRing
                }
                contentItem: Text {
                    text: segment.text
                    color: segment.current ? (0.299 * segment.tint.r + 0.587 * segment.tint.g + 0.114 * segment.tint.b > 0.55 ? "#141414" : "#FFFFFF")
                                           : Theme.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fontSizeCaption
                    font.weight: segment.current ? Font.DemiBold : Font.Normal
                    font.features: Theme.tabularFigures
                }
            }
        }
    }
}
