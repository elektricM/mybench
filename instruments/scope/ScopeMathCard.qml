pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import MyBench.Controls

// The math trace as a channel card in the right column (round 5, the only math control): the
// switch in the header, then the operation (picture buttons), Scale and Position knobs. Off, the
// card folds to its header and names the operation.
ChannelCard {
    id: card

    required property ScopeController controller
    required property color channelColor
    readonly property ScopeMath math: controller.math

    // ScopeMath.Operation order (no FFT: that is the FFT view). Glyphs: SVG paths on 32 × 18.
    readonly property var operations: [
        { text: "CH0 + CH1", glyph: "M16 2 V16 M7 9 H25" },
        { text: "CH0 − CH1", glyph: "M7 9 H25" },
        { text: "CH0 × CH1", glyph: "M10 3 L22 15 M22 3 L10 15" },
        { text: "−CH0", glyph: "M2 9 C6 17 10 17 14 9 S22 1 26 9 30 13 30 13" },
        { text: "−CH1", glyph: "M2 9 C6 17 10 17 14 9 S22 1 26 9 30 13 30 13" }
    ]

    objectName: "mathCard"
    title: qsTr("Math")
    subtitle: math.enabled ? "" : math.functionName
    color: channelColor
    checked: math.enabled
    collapseWhenOff: true
    onToggled: on => card.math.enabled = on

    GridLayout {
        objectName: "mathOperations"
        Layout.fillWidth: true
        columns: 3
        rowSpacing: Theme.spacingXs
        columnSpacing: Theme.spacingXs
        Repeater {
            model: card.operations
            delegate: PictureButton {
                required property var modelData
                required property int index
                objectName: "mathOperation" + index
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: modelData.text
                glyph: modelData.glyph
                accentColor: card.channelColor
                checked: card.math.operation === index
                focusPolicy: Qt.TabFocus
                onClicked: card.math.operation = index
            }
        }
    }
    KnobField {
        objectName: "mathScale"
        Layout.fillWidth: true
        label: qsTr("Scale")
        compact: card.compact
        accentColor: card.channelColor
        knobSize: 34
        values: card.math.scaleValues
        labels: card.math.scaleLabels.map(l => l + "/div")
        currentIndex: card.math.scaleIndex
        onIndexActivated: index => card.math.scaleIndex = index
    }
    KnobField {
        objectName: "mathPosition"
        Layout.fillWidth: true
        label: qsTr("Position")
        unit: "div"
        compact: card.compact
        accentColor: card.channelColor
        knobSize: 34
        from: -5
        to: 5
        stepSize: 0.1
        defaultValue: 0
        formatter: v => Number(v.toFixed(2)) + " div"
        value: card.math.position
        onValueModified: v => card.math.position = Math.round(v * 100) / 100
    }
    Text {
        readonly property bool needsBoth: card.math.operation <= ScopeMath.Product
        readonly property bool missing: needsBoth ? !(card.controller.channel0.enabled && card.controller.channel1.enabled)
                                                  : !(card.math.operation === ScopeMath.InvertA ? card.controller.channel0.enabled
                                                                                               : card.controller.channel1.enabled)
        Layout.fillWidth: true
        visible: missing
        text: needsBoth ? qsTr("Needs CH0 and CH1 on.")
                        : qsTr("Needs CH%1 on.").arg(card.math.operation === ScopeMath.InvertA ? 0 : 1)
        color: Theme.caution
        font.pixelSize: Theme.fontSizeCaption
        wrapMode: Text.WordWrap
    }
}
