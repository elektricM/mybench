pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// One DIO line in the Digital I/O display, the one place for everything about that line: name
// (+ user label), its state as a big HIGH / LOW tile in the direction's color (inputs = input
// green, outputs = output orange), the direction (In | Out | –), the owner of the line when
// another instrument declares it and the output readback warning. An output tile is a button:
// a click (or key 0-7) flips the line in the Manual pattern.
Rectangle {
    id: root

    required property DioController controller
    required property int line
    // Direction colors, shared with the panel (values, lanes, cards).
    property color inputColor: Theme.signalCh0
    property color outputColor: Theme.signalAo0

    readonly property int direction: (controller.outputMask >> line) & 1 ? DioController.Out
                                   : (controller.inputMask >> line) & 1 ? DioController.In : DioController.Unused
    readonly property bool isOut: direction === DioController.Out
    readonly property bool isIn: direction === DioController.In
    readonly property color accentColor: isOut ? outputColor : isIn ? inputColor : Theme.textMuted
    // OUT: while running the written level (ELVISmx semantics); stopped in the Manual pattern the
    // value Run will write (so a click shows at once); IN: the level read (none before the first
    // reading); unused: no state.
    readonly property bool showsWritten: controller.hasWritten && (controller.running || controller.pattern !== DioController.Manual)
    readonly property bool known: isOut || (isIn && controller.hasReading)
    readonly property bool level: isOut ? (showsWritten ? (controller.writtenLevels >> line) & 1
                                                        : controller.pattern === DioController.Manual && ((controller.manualValue >> Math.max(0, bitIndex)) & 1))
                                : isIn ? (controller.lineStates >> line) & 1 : false
    readonly property bool togglable: isOut && controller.pattern === DioController.Manual
    readonly property var owner: controller.lineOwners[line] ?? ({})
    readonly property bool owned: (owner.owner ?? "").length > 0
    readonly property bool conflict: owned && owner.active && direction !== DioController.Unused
    readonly property bool readbackError: (controller.readbackErrors >> line) & 1
    readonly property string label: controller.lineLabels[line] ?? ""
    readonly property bool compact: height < 150
    // The state tile's click: flips an output line (Manual pattern; the controller ignores others).
    function activate() {
        root.controller.toggleLine(root.line)
    }
    // Readable text on a filled state tile.
    function inkOn(c: color): color {
        return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b > 0.55 ? "#141414" : "#FFFFFF"
    }

    implicitWidth: 96
    implicitHeight: 176
    radius: Theme.radiusL
    color: Theme.cardFill
    border.width: 1
    border.color: conflict ? Theme.warning : direction === DioController.Unused ? Theme.cardBorder : Theme.alpha(accentColor, 0.55)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingS
        spacing: root.compact ? 3 : Theme.spacingXs

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: "DIO " + root.line
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            // Readback mismatch (short circuit / overload on an output).
            Icon {
                visible: root.readbackError
                name: "warning"
                size: Theme.iconSizeS
                color: Theme.warning
                HoverHandler { id: warningHover }
                ToolTip.visible: warningHover.hovered
                ToolTip.text: qsTr("Output DIO %1 reads %2 while driven %3 — short circuit or overload?")
                              .arg(root.line).arg(root.level ? qsTr("low") : qsTr("high")).arg(root.level ? qsTr("high") : qsTr("low"))
            }
        }
        Text {
            Layout.fillWidth: true
            visible: root.label.length > 0 || root.owned
            text: root.label.length > 0 ? root.label : root.owner.owner ?? ""
            elide: Text.ElideRight
            color: root.conflict ? Theme.warning : Theme.textSecondary
            font.pixelSize: Theme.fontSizeCaption
            HoverHandler { id: ownerHover; enabled: root.owned }
            ToolTip.visible: ownerHover.hovered
            ToolTip.text: (root.owner.text ?? "") + (root.conflict ? " " + qsTr("Running Digital I/O on it pauses that instrument.") : "")
        }

        // The state: filled = HIGH, outlined = LOW, in the direction's color. Outputs: a button.
        Rectangle {
            id: stateTile
            objectName: "dioState" + root.line
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 28
            radius: Theme.radiusM
            readonly property bool lit: root.known && root.level
            // Stopped: the level Run will write, lighter than a level on the pin.
            readonly property bool pending: root.isOut && !root.controller.running
            color: lit ? (pending ? Theme.alpha(root.accentColor, 0.5) : root.accentColor)
                       : root.togglable && stateArea.containsMouse ? Theme.alpha(root.accentColor, 0.14)
                       : root.direction === DioController.Unused ? "transparent" : Theme.alpha(root.accentColor, 0.06)
            border.width: 1
            border.color: root.direction === DioController.Unused ? Theme.cardBorder : root.accentColor
            Text {
                anchors.centerIn: parent
                text: root.direction === DioController.Unused || !root.known ? "–" : root.level ? qsTr("HIGH") : qsTr("LOW")
                color: stateTile.lit ? (stateTile.pending ? Theme.text : root.inkOn(root.accentColor)) : root.direction === DioController.Unused ? Theme.textMuted : root.accentColor
                font.pixelSize: Math.max(Theme.fontSizeS, Math.min(20, stateTile.width / 5))
                font.weight: Font.DemiBold
                font.features: Theme.tabularFigures
            }
            MouseArea {
                id: stateArea
                anchors.fill: parent
                enabled: root.isOut
                hoverEnabled: true
                cursorShape: root.togglable ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.activate()
                ToolTip.visible: containsMouse
                ToolTip.delay: 600
                ToolTip.text: root.togglable ? qsTr("Click (or key %1) to set DIO %1 %2").arg(root.line).arg(root.level ? qsTr("low") : qsTr("high"))
                                             : qsTr("Choose the Manual pattern to set the lines by hand")
            }
            Accessible.role: root.isOut ? Accessible.Button : Accessible.StaticText
            Accessible.name: qsTr("DIO %1 %2").arg(root.line).arg(root.level ? qsTr("high") : qsTr("low"))
        }

        // Direction: the only place to set it.
        DioChoice {
            objectName: "dioDirection" + root.line
            Layout.fillWidth: true
            model: [{ text: qsTr("In"), toolTip: qsTr("Input (read)"), color: root.inputColor },
                    { text: qsTr("Out"), toolTip: qsTr("Output (write)"), color: root.outputColor },
                    { text: "–", toolTip: qsTr("Unused: free for other instruments"), color: Theme.textMuted }]
            currentIndex: root.isOut ? 1 : root.isIn ? 0 : 2
            onActivated: i => root.controller.setLineDirection(root.line, i)
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
