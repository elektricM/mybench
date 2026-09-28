pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// The eight DIO screw terminals with the counter signal each one carries (CTR 0 SOURCE/GATE/AUX/
// OUT, FREQOUT) for the current settings, and the lines other instruments (Digital I/O) hold.
//     PinMap { pins: controller.pins }   // [{ line, terminal, role, signal, owner, heldBy, conflict }]
Item {
    id: root

    property var pins: []
    property bool compact: width < 520

    implicitHeight: row.implicitHeight

    function ownerColor(owner: string): color {
        switch (owner) {
        case "input": return Theme.ch0
        case "pulse": return Theme.math
        case "freqout": return Theme.ch1
        default: return Theme.textMuted
        }
    }

    RowLayout {
        id: row
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: root.compact ? 3 : Theme.spacingXs

        Repeater {
            model: root.pins
            delegate: Rectangle {
                id: pin
                required property var modelData
                readonly property bool used: modelData.owner.length > 0
                readonly property bool held: modelData.heldBy.length > 0
                readonly property color tone: modelData.conflict ? Theme.warning
                                             : used ? root.ownerColor(modelData.owner)
                                             : Theme.textMuted

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: column.implicitHeight + 2 * Theme.spacingS
                radius: Theme.radiusM
                color: used || modelData.conflict ? Theme.alpha(tone, Theme.dark ? 0.12 : 0.08) : "transparent"
                border.width: 1
                border.color: modelData.conflict ? Theme.warning
                             : used ? Theme.alpha(tone, 0.55)
                             : Theme.displayBorder

                ColumnLayout {
                    id: column
                    anchors.fill: parent
                    anchors.margins: Theme.spacingS
                    spacing: 2

                    RowLayout {
                        spacing: 5
                        Rectangle {
                            implicitWidth: 7
                            implicitHeight: 7
                            radius: 3.5
                            color: pin.used || pin.modelData.conflict ? pin.tone : "transparent"
                            border.width: 1
                            border.color: pin.used || pin.modelData.conflict ? pin.tone : Theme.textMuted
                        }
                        Text {
                            text: root.compact ? String(pin.modelData.line) : "DIO " + pin.modelData.line
                            color: pin.used ? Theme.text : Theme.textSecondary
                            font.pixelSize: Theme.fontSizeM
                            font.weight: Font.DemiBold
                            font.features: Theme.tabularFigures
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: pin.used ? pin.modelData.role : pin.held ? pin.modelData.heldBy : "—"
                        color: pin.used || pin.modelData.conflict ? pin.tone : Theme.textMuted
                        font.pixelSize: Theme.fontSizeS
                        font.weight: pin.used ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: !root.compact
                        Layout.fillWidth: true
                        text: pin.modelData.conflict ? qsTr("held by %1").arg(pin.modelData.heldBy)
                                                     : pin.modelData.terminal.length > 0 ? pin.modelData.terminal : " "
                        color: pin.modelData.conflict ? Theme.warning : Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                    }
                }

                HoverHandler { id: hover }
                ToolTip.visible: hover.hovered
                ToolTip.delay: 400
                ToolTip.text: {
                    let text = "DIO " + pin.modelData.line
                    if (pin.modelData.terminal.length > 0)
                        text += " (" + pin.modelData.terminal + ")"
                    if (pin.used)
                        text += "\n" + pin.modelData.signal
                    if (pin.held)
                        text += "\n" + qsTr("Held by %1").arg(pin.modelData.heldBy)
                    return text
                }
            }
        }
    }
}
