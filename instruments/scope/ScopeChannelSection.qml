pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// "Channel 0 Settings" / "Channel 1 Settings": Source, Enabled, Scale Volts/Div (knob + list)
// and Vertical Position (Div) (knob + field). Coupling and probe are fixed facts of the myDAQ and
// shown as information.
Section {
    id: root

    required property ScopeController controller
    required property ScopeChannel channel
    required property color channelColor

    // Source combo rows: AI 0, AI 1, separator, AudioInput Left, AudioInput Right.
    readonly property var sourceRows: [0, 1, 3, 4]
    function sourceForRow(row: int): int {
        return row < 2 ? row : row - 1
    }

    title: qsTr("Channel %1 Settings").arg(channel.index)
    accentColor: channelColor
    summary: channel.enabled ? channel.sourceName + " · " + channel.scaleLabel + "/div" : qsTr("Off")
    headerTools: ToggleSwitch {
        accentColor: root.channelColor
        checked: root.channel.enabled
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Enabled")
        Accessible.name: qsTr("Channel %1 Enabled").arg(root.channel.index)
        onToggled: {
            if (!root.controller.setChannelEnabled(root.channel.index, checked))
                checked = Qt.binding(() => root.channel.enabled)
        }
    }

    Field {
        label: qsTr("Source")
        Layout.fillWidth: true
        ScopeCombo {
            id: sourceCombo
            model: ["AI 0", "AI 1", "───────────", "AudioInput Left", "AudioInput Right"]
            disabledIndices: [2]
            disabledToolTip: ""
            currentIndex: root.sourceRows[root.channel.source]
            Accessible.name: qsTr("Channel %1 Source").arg(root.channel.index)
            onActivated: row => {
                if (row !== 2)
                    root.controller.setChannelSource(root.channel.index, root.sourceForRow(row))
                currentIndex = Qt.binding(() => root.sourceRows[root.channel.source])
            }
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.channel.audio ? qsTr("Coupling: AC (mean removed) · 1× probe · ±2 V")
                                 : qsTr("Coupling: DC · 1× probe · ±%1 V").arg(root.channel.voltsPerDiv >= 0.5 ? "10" : "2")
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeS
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingM
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Theme.spacingXs
            Knob {
                Layout.alignment: Qt.AlignHCenter
                // Bench-scope direction: clockwise = smaller Volts/Div (more sensitive, "zoom in"),
                // as the mouse wheel over the graph. The knob ring therefore runs from large to small.
                readonly property int last: root.channel.scaleLabels.length - 1
                label: qsTr("Scale Volts/Div")
                values: root.channel.scaleValues.slice().reverse()
                labels: root.channel.scaleLabels.slice().reverse()
                currentIndex: last - root.channel.scaleIndex
                color: root.channelColor
                showValue: false
                diameter: 54
                onIndexMoved: index => root.channel.scaleIndex = last - index
            }
            ScopeCombo {
                // ELVISmx lists the largest scale first.
                readonly property int last: root.channel.scaleLabels.length - 1
                model: root.channel.scaleLabels.slice().reverse()
                currentIndex: last - root.channel.scaleIndex
                Accessible.name: qsTr("Channel %1 Volts/Div").arg(root.channel.index)
                onActivated: index => root.channel.scaleIndex = last - index
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Theme.spacingXs
            Knob {
                Layout.alignment: Qt.AlignHCenter
                label: qsTr("Vertical Position (Div)")
                from: -5
                to: 5
                stepSize: 0.1
                value: root.channel.position
                color: root.channelColor
                showValue: false
                showRangeLabels: true
                minLabel: "-5"
                maxLabel: "5"
                diameter: 54
                onMoved: v => root.channel.position = Math.round(v * 10) / 10
            }
            SiField {
                Layout.fillWidth: true
                engineering: false
                decimals: 2
                from: -5
                to: 5
                stepSize: 0.1
                value: root.channel.position
                toolTip: qsTr("Vertical Position (Div): -5 … 5, moves the trace only")
                Accessible.name: qsTr("Channel %1 Vertical Position").arg(root.channel.index)
                onValueModified: v => root.channel.position = v
            }
        }
    }
}
