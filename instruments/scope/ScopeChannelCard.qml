pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MyBench.Controls

// One input channel in the right column (PicoScope style), on the shared ChannelCard: colored
// header with the on/off switch (the only place to switch a channel), Scale (1-2-5 V/div knob),
// Position knob, Source and Input range (Auto's tooltip names the range in effect; coupling and
// wiring are in the Source tooltip). Audio sources have no range choice: the card states ±2 V AC.
ChannelCard {
    id: card

    required property ScopeController controller
    required property ScopeChannel channel
    required property color channelColor

    // Source entries in ScopeChannel.Source order.
    readonly property var sourceNames: [qsTr("AI 0"), qsTr("AI 1"), qsTr("Audio L"), qsTr("Audio R")]

    objectName: "channelCard" + channel.index
    title: "CH " + channel.index
    // The source shows once: in the Source box while on, here while the card is collapsed.
    subtitle: channel.enabled ? "" : sourceNames[channel.source] ?? ""
    color: channelColor
    checked: channel.enabled
    // Off = header only (the source in the subtitle): the column keeps the trigger in view.
    collapseWhenOff: true
    // The range is shown by the range buttons; audio inputs have none, so it is stated once here.
    facts: channel.audio ? [qsTr("±%1 V · AC").arg(channel.effectiveRange)] : []
    // The controller refuses to switch off the last channel while running.
    onToggled: on => {
        if (!controller.setChannelEnabled(channel.index, on))
            checked = Qt.binding(() => card.channel.enabled)
    }

    KnobField {
        objectName: "scale" + card.channel.index
        Layout.fillWidth: true
        label: qsTr("Scale")
        unit: "V/div"
        compact: card.compact
        knobSize: 44
        accentColor: card.channelColor
        values: card.channel.scaleValues
        labels: card.channel.scaleLabels.map(l => l + "/div")
        currentIndex: card.channel.scaleIndex
        onIndexActivated: index => card.channel.scaleIndex = index
    }
    KnobField {
        objectName: "position" + card.channel.index
        Layout.fillWidth: true
        label: qsTr("Position")
        unit: "div"
        compact: card.compact
        knobSize: 34
        accentColor: card.channelColor
        from: -5
        to: 5
        stepSize: 0.1
        defaultValue: 0
        digits: 3
        formatter: v => Number(v.toFixed(2)) + " div"
        value: card.channel.position
        toolTip: qsTr("Moves the trace on the screen only (−5 to 5 div); drag the channel marker at the left edge")
        onValueModified: v => card.channel.position = Math.round(v * 100) / 100
    }
    GridLayout {
        Layout.fillWidth: true
        columns: card.compact ? 1 : 2
        rowSpacing: Theme.spacingXs
        columnSpacing: Theme.spacingXs
        ComboBox {
            objectName: "source" + card.channel.index
            Layout.fillWidth: card.compact
            Layout.preferredWidth: card.compact ? -1 : 96
            model: card.sourceNames
            currentIndex: card.channel.source
            Accessible.name: qsTr("Channel %1 source").arg(card.channel.index)
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: card.channel.audio ? qsTr("Audio input: AC coupled, single-ended")
                                             : qsTr("Analog input: DC coupled, differential")
            // AI and audio inputs cannot be mixed: the other channel follows (toast).
            onActivated: index => {
                card.controller.setChannelSource(card.channel.index, index)
                currentIndex = Qt.binding(() => card.channel.source)
            }
        }
        SegmentedControl {
            objectName: "inputRange" + card.channel.index
            // Audio inputs always use ±2 V: nothing to choose.
            visible: !card.channel.audio
            Layout.fillWidth: true
            compact: true
            // ScopeChannel.InputRange order. Auto: ±10 V from 500 mV/div, ±2 V below (ELVISmx).
            model: [{ text: qsTr("Auto"), toolTip: qsTr("Input range ±10 V from 500 mV/div, ±2 V below (now ±%1 V)").arg(card.channel.effectiveRange) },
                    { text: "±10", toolTip: qsTr("Input range ±10 V") }, { text: "±2", toolTip: qsTr("Input range ±2 V") }]
            currentIndex: card.channel.inputRange
            onActivated: index => card.channel.inputRange = index
        }
    }
}
