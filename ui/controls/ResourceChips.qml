pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import MyBench.Framework

// "[AI] Scope  [AO] Gen  [DMM]  [CTR · DIO 1]" — who holds which part of the device
// (ResourceArbiter), one chip per instrument. Clicking a chip emits instrumentClicked(id) (the
// shell focuses that panel); right-click offers "Stop <panel>".
Row {
    id: root

    property bool showIdle: true
    // [{ instrumentId, title, shortTitle, resources: "AI · AO", names: "analog input, ..." }]
    readonly property var groups: {
        const list = []
        for (const h of ResourceArbiter.holders) {
            let g = list.find(e => e.instrumentId === h.instrumentId)
            if (!g) {
                g = { instrumentId: h.instrumentId, title: h.title, shortTitle: h.shortTitle, labels: [], names: [] }
                list.push(g)
            }
            g.labels.push(h.lines.length > 0 ? h.lines : h.resource)
            g.names.push(h.lines.length > 0 ? h.lines : h.name)
        }
        return list.map(g => ({ instrumentId: g.instrumentId, title: g.title,
                                shortTitle: g.labels.indexOf(g.shortTitle) >= 0 ? "" : g.shortTitle,
                                resources: g.labels.join(" · "), names: g.names.join(", ") }))
    }
    signal instrumentClicked(string instrumentId)

    // Stops the instrument of a chip (context menu); like the run bar's Stop.
    function stopInstrument(instrumentId: string) {
        const controller = InstrumentRegistry.controller(instrumentId)
        if (controller)
            controller.stop()
    }

    spacing: 6

    Text {
        visible: root.showIdle && ResourceArbiter.holders.length === 0
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Device idle")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeM
    }

    Repeater {
        model: root.groups
        delegate: T.AbstractButton {
            id: chip
            required property var modelData
            readonly property string resource: modelData.resources
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            implicitHeight: 24
            implicitWidth: content.implicitWidth + 6
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            onClicked: root.instrumentClicked(modelData.instrumentId)
            Accessible.name: qsTr("%1: %2").arg(modelData.title).arg(modelData.names)

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: chipMenu.popup()
            }
            Menu {
                id: chipMenu
                MenuItem {
                    text: qsTr("Stop %1").arg(chip.modelData.title)
                    icon.source: Theme.icon("stop-filled")
                    onTriggered: root.stopInstrument(chip.modelData.instrumentId)
                }
                MenuItem {
                    text: qsTr("Show %1").arg(chip.modelData.title)
                    onTriggered: root.instrumentClicked(chip.modelData.instrumentId)
                }
            }

            background: Rectangle {
                radius: Theme.radiusM
                color: chip.hovered ? Theme.surfaceHover : Theme.surfaceSunken
                border.width: 1
                border.color: Theme.border
            }
            contentItem: Item {
                Row {
                    id: content
                    anchors.verticalCenter: parent.verticalCenter
                    x: 3
                    spacing: 6
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: resourceText.implicitWidth + 10
                        height: 18
                        radius: Theme.radiusS
                        color: Theme.accentMuted
                        Text {
                            id: resourceText
                            anchors.centerIn: parent
                            text: chip.resource
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Bold
                            font.letterSpacing: 0.5
                        }
                    }
                    Text {
                        visible: text.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        rightPadding: 4
                        text: chip.modelData.shortTitle
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeM
                        font.weight: Font.Medium
                    }
                }
            }
            ToolTip.visible: hovered
            ToolTip.delay: 500
            ToolTip.text: qsTr("%1: %2").arg(modelData.title).arg(modelData.names)
        }
    }
}
