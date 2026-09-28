import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// One DIO line in the Digital I/O display: name (+ user label), a big LED, the direction badge,
// the manual-pattern switch for OUT lines, the owner of the line when another instrument
// declares it (Counter/Timer terminal, FREQOUT) and the output readback warning.
Rectangle {
    id: root

    required property DioController controller
    required property int line

    readonly property int direction: (controller.outputMask >> line) & 1 ? DioController.Out
                                   : (controller.inputMask >> line) & 1 ? DioController.In : DioController.Unused
    readonly property bool isOut: direction === DioController.Out
    readonly property bool isIn: direction === DioController.In
    readonly property color accentColor: isOut ? Theme.math : isIn ? Theme.ch1 : Theme.textMuted
    // OUT: the written level (ELVISmx semantics); IN: the level read; unused: never lit.
    readonly property bool level: isOut ? ((controller.hasWritten ? controller.writtenLevels : 0) >> line) & 1
                                : isIn ? (controller.lineStates >> line) & 1 : false
    readonly property var owner: controller.lineOwners[line] ?? ({})
    readonly property bool owned: (owner.owner ?? "").length > 0
    readonly property bool conflict: owned && owner.active && direction !== DioController.Unused
    readonly property bool readbackError: (controller.readbackErrors >> line) & 1
    readonly property string label: controller.lineLabels[line] ?? ""
    readonly property bool compact: height < 150

    implicitWidth: 96
    implicitHeight: 176
    radius: Theme.radiusL
    color: direction === DioController.Unused ? "transparent" : Theme.alpha(accentColor, Theme.dark ? 0.06 : 0.05)
    border.width: 1
    border.color: conflict ? Theme.warning : direction === DioController.Unused ? Theme.border : Theme.alpha(accentColor, 0.55)
    opacity: direction === DioController.Unused ? 0.7 : 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingS
        spacing: root.compact ? 2 : Theme.spacingXs

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "DIO " + root.line
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.label.length > 0 ? root.label : " "
            elide: Text.ElideRight
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeXs
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Led {
                anchors.centerIn: parent
                size: Math.max(16, Math.min(36, parent.height - 6, parent.width - 16))
                on: root.level
                color: Theme.ch1
                opacity: root.direction === DioController.Unused ? 0.4 : 1
            }
            // Readback mismatch (short circuit / overload on an output).
            Icon {
                visible: root.readbackError
                anchors.right: parent.right
                anchors.top: parent.top
                name: "warning"
                size: Theme.iconSizeS
                color: Theme.warning
                HoverHandler { id: warningHover }
                ToolTip.visible: warningHover.hovered
                ToolTip.text: qsTr("Output DIO %1 reads %2 while driven %3 — short circuit or overload?")
                              .arg(root.line).arg(root.level ? qsTr("LO") : qsTr("HI")).arg(root.level ? qsTr("HI") : qsTr("LO"))
            }
        }
        Badge {
            Layout.alignment: Qt.AlignHCenter
            text: root.isOut ? qsTr("Out") : root.isIn ? qsTr("In") : "–"
            color: root.accentColor
            solid: root.isOut
        }
        // Manual pattern switch (OUT lines).
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.compact ? 0 : 40
            visible: !root.compact
            DioSwitch {
                anchors.centerIn: parent
                visible: root.isOut
                enabled: root.controller.pattern === DioController.Manual
                checked: (root.controller.manualValue >> Math.max(0, root.bitIndex)) & 1
                onToggled: root.controller.toggleLine(root.line)
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: enabled ? qsTr("DIO %1: HI (up) / LO (down) — key %1").arg(root.line)
                                      : qsTr("Switch to the Manual pattern to set the lines by hand")
            }
            Text {
                anchors.centerIn: parent
                visible: !root.isOut
                text: root.isIn ? qsTr("read") : qsTr("unused")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
            }
        }
        // Owner of the line (another instrument).
        Badge {
            Layout.alignment: Qt.AlignHCenter
            visible: root.owned
            text: root.owner.owner ?? ""
            caps: false
            color: root.owner.active ? Theme.warning : Theme.textSecondary
            HoverHandler { id: ownerHover }
            ToolTip.visible: ownerHover.hovered
            ToolTip.text: (root.owner.text ?? "") + (root.conflict ? " " + qsTr("Running Digital I/O on it pauses that instrument.") : "")
        }
    }

    // Bit of this line in the right-justified output value.
    readonly property int bitIndex: {
        let bit = 0
        for (let l = 0; l < root.line; ++l)
            if ((controller.outputMask >> l) & 1)
                ++bit
        return (controller.outputMask >> root.line) & 1 ? bit : -1
    }
}
