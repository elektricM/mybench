pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

// A grid of instrument tools (PicoScope style): icon (20 px) over a small label; the open tool
// is drawn pressed (accent soft fill). Clicking a tool emits activated(id), also for the open one
// (the owner toggles its ToolPanel: currentId = currentId === id ? "" : id).
//     ToolPalette { model: [{ id: "measure", text: qsTr("Measure"), icon: "ruler" }, ...]
//                   currentId: pane.tool; onActivated: id => pane.tool = pane.tool === id ? "" : id }
Item {
    id: palette

    property var model: []
    property string currentId
    property int columns: 4
    property real cellHeight: 52
    property real spacing: 4

    signal activated(string id)

    implicitWidth: columns * 64 + (columns - 1) * spacing
    implicitHeight: grid.implicitHeight
    Accessible.role: Accessible.ToolBar

    Grid {
        id: grid
        width: parent.width
        columns: Math.max(1, palette.columns)
        spacing: palette.spacing
        readonly property real cellWidth: (width - (columns - 1) * spacing) / columns

        Repeater {
            model: palette.model
            delegate: T.AbstractButton {
                id: tool
                required property var modelData
                required property int index
                readonly property string toolId: String(modelData.id ?? "")
                readonly property bool current: toolId.length > 0 && toolId === palette.currentId
                objectName: "tool_" + toolId
                width: grid.cellWidth
                height: palette.cellHeight
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                enabled: modelData.enabled ?? true
                text: String(modelData.text ?? "")
                checkable: false
                checked: current
                Accessible.role: Accessible.Button
                Accessible.name: text
                Accessible.checkable: true
                Accessible.checked: current
                onClicked: palette.activated(toolId)

                readonly property color ink: !enabled ? Theme.textDisabled
                                           : current ? Theme.accent
                                           : hovered ? Theme.text : Theme.textSecondary

                background: Rectangle {
                    radius: Theme.radiusControl + 1
                    color: tool.current ? (tool.pressed ? Theme.alpha(Theme.accent, Theme.dark ? 0.28 : 0.18) : Theme.accentSoft)
                         : tool.pressed ? Theme.pressedFill : tool.hovered ? Theme.hoverFill : "transparent"
                    FocusRing { shown: tool.visualFocus }
                }
                contentItem: Item {
                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Icon {
                            anchors.horizontalCenter: parent.horizontalCenter
                            name: String(tool.modelData.icon ?? "")
                            size: Theme.iconSizeL
                            color: tool.ink
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(implicitWidth, tool.width - 4)
                            text: tool.text
                            color: tool.ink
                            font.pixelSize: Theme.fontSizeCaption
                            font.weight: tool.current ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }
                ToolTip.visible: hovered && (modelData.toolTip ?? "").length > 0
                ToolTip.delay: Theme.toolTipDelay
                ToolTip.text: String(modelData.toolTip ?? "")
            }
        }
    }
}
