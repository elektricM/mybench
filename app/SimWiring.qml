pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Framework
import MyBench.Controls

// The simulated myDAQ's wiring (core/src/sim/sim_core.cpp): AO 0 drives AI 0 directly and AI 1
// through an RC low-pass (corner = DeviceHub.simulatorRcCutoff); AO 1 is not connected. An input
// no output drives shows its test signal (Simulator pane). `showAudio` adds Audio Out → Audio In.
GridLayout {
    id: wiring
    objectName: "simWiring"

    property bool showAudio: false
    // Behind the wire labels: the colour of the surface the wiring sits on.
    property color background: Theme.base

    columns: 3
    columnSpacing: Theme.spacingS
    rowSpacing: Theme.spacingXxs

    // "1 kHz", "1.59 kHz", "159 Hz"
    function cornerText(hz: real): string {
        const kilo = hz >= 1000
        const value = kilo ? hz / 1000 : hz
        return qsTr("RC %1 %2").arg(Number(value.toPrecision(3)).toString()).arg(kilo ? "kHz" : "Hz")
    }

    readonly property var links: [
        { from: "AO 0", link: qsTr("direct"), to: "AI 0", connected: true },
        { from: "AO 0", link: cornerText(DeviceHub.simulatorRcCutoff), to: "AI 1", connected: true },
        { from: "AO 1", link: qsTr("not connected"), to: "", connected: false }
    ].concat(showAudio ? [{ from: qsTr("Audio Out"), link: qsTr("high-pass"), to: qsTr("Audio In"), connected: true }] : [])

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered
    ToolTip.delay: Theme.toolTipDelay
    ToolTip.text: qsTr("Simulated wiring. An input no output drives shows its test signal.")

    Repeater {
        model: wiring.links.length * 3
        delegate: Item {
            id: part
            required property int index
            readonly property var entry: wiring.links[Math.floor(index / 3)]
            readonly property int column: index % 3
            objectName: column === 1 ? "simWire-" + Math.floor(index / 3) : ""
            Layout.fillWidth: column === 1
            Layout.minimumWidth: column === 1 ? 90 : 0
            implicitWidth: column === 1 ? wireLabel.implicitWidth + 3 * Theme.spacingS : endLabel.implicitWidth
            implicitHeight: Math.max(wireLabel.implicitHeight, endLabel.implicitHeight)

            Text {
                id: endLabel
                visible: part.column !== 1
                anchors.verticalCenter: parent.verticalCenter
                text: part.column === 0 ? part.entry.from : part.entry.to
                color: part.entry.connected ? Theme.text : Theme.textTertiary
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.Medium
            }
            // The wire: a line with an arrow head and its label on it; dashed look when open.
            Rectangle {
                visible: part.column === 1 && part.entry.connected
                anchors.verticalCenter: parent.verticalCenter
                x: 0
                width: parent.width - arrow.width / 2
                height: 1
                color: Theme.textSecondary
            }
            Text {
                id: arrow
                visible: part.column === 1 && part.entry.connected
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                text: "▸"
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            Rectangle {
                visible: part.column === 1
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: part.entry.connected ? -arrow.width / 2 : 0
                width: wireLabel.implicitWidth + Theme.spacingS
                height: wireLabel.implicitHeight
                color: wiring.background
                Text {
                    id: wireLabel
                    anchors.centerIn: parent
                    text: part.entry.link
                    color: part.entry.connected ? Theme.textSecondary : Theme.textTertiary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
    }
}
