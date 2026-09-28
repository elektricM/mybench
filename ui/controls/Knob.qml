pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Shapes
import QtQuick.Templates as T
import MyBench.Framework

// Rotary knob, 270° travel (minimum at 7:30, maximum at 4:30), for the controls ELVISmx had as
// knobs (Volts/Div, Time/Div, frequency, amplitude...). Pair it with an SiField or a ComboBox
// bound to the same controller property.
//
// Discrete ("ring") knob: `values` (+ optional `labels`) and `currentIndex`, emits indexMoved().
// Continuous knob: `from`/`to` (optionally `logarithmic`) and `value`, emits moved().
// Drag vertically (Shift = fine), wheel, arrow keys, PageUp/PageDown, Home/End; double-click
// resets to `defaultValue` when it is set. Controlled component: bind the value and apply the
// signal.
//     Knob { label: qsTr("Time/Div"); values: ctl.timeDivValues; currentIndex: ctl.timeDivIndex
//            labels: ctl.timeDivLabels; onIndexMoved: i => ctl.timeDivIndex = i }
T.Control {
    id: control

    // Discrete
    property var values: []
    property var labels: []
    property int currentIndex: 0
    // Continuous
    property real value: 0
    property real from: 0
    property real to: 1
    property bool logarithmic: false
    property real stepSize: 0          // keys/wheel; 0 = 1/100 of the travel (1/200 with Shift)
    // Double-click resets to this (app.md 7.2): a value for a continuous knob (coerced to
    // [from, to]), an entry of `values` for a ring knob (nearest one). Unset: double-click does
    // nothing.
    property var defaultValue: undefined
    readonly property bool hasDefault: defaultValue !== undefined && defaultValue !== null
                                       && isFinite(Number(defaultValue))
    // Presentation
    property string label              // caption above
    property string unit
    property int digits: 3
    property bool showValue: true
    property string valueText: discrete ? (labels.length > currentIndex && currentIndex >= 0 ? String(labels[currentIndex])
                                                                   : Units.format(Number(values[currentIndex] ?? 0), unit, digits))
                                        : Units.format(value, unit, digits)
    property bool showRangeLabels: true
    property string minLabel: discrete ? (labels.length > 0 ? String(labels[0]) : Units.format(Number(values[0] ?? 0), "", 2))
                                       : Units.format(from, "", 2)
    property string maxLabel: discrete ? (labels.length > 0 ? String(labels[labels.length - 1]) : Units.format(Number(values[values.length - 1] ?? 0), "", 2))
                                       : Units.format(to, "", 2)
    property color color: Theme.accent
    property real diameter: 58
    property bool ticks: true

    readonly property bool discrete: values.length > 0
    readonly property int count: values.length
    readonly property real position: discrete ? (count > 1 ? currentIndex / (count - 1) : 0)
                                              : positionOf(value)
    readonly property real angle: -135 + 270 * Math.max(0, Math.min(1, position))   // 0 = straight up

    signal moved(real value)
    signal indexMoved(int index)

    function positionOf(v: real): real {
        if (to === from)
            return 0
        if (logarithmic && from > 0 && to > 0 && v > 0)
            return Math.log(v / from) / Math.log(to / from)
        return (v - from) / (to - from)
    }
    function valueAt(p: real): real {
        p = Math.max(0, Math.min(1, p))
        if (logarithmic && from > 0 && to > 0)
            return from * Math.pow(to / from, p)
        return from + (to - from) * p
    }
    function setPosition(p: real) {
        p = Math.max(0, Math.min(1, p))
        if (discrete) {
            const i = Math.round(p * (count - 1))
            if (i !== currentIndex)
                indexMoved(i)
        } else {
            const v = Number(valueAt(p).toPrecision(10))
            if (v !== value)
                moved(v)
        }
    }
    function step(n: int, fine: bool) {
        if (!enabled)
            return
        if (discrete) {
            const i = Math.max(0, Math.min(count - 1, currentIndex + n))
            if (i !== currentIndex)
                indexMoved(i)
        } else if (stepSize > 0 && !logarithmic) {
            const v = Math.max(from, Math.min(to, Number((value + n * stepSize).toPrecision(10))))
            if (v !== value)
                moved(v)
        } else {
            setPosition(position + n * (fine ? 0.005 : 0.01))
        }
    }

    // Emits moved()/indexMoved() for the default value; false when there is none.
    function resetToDefault(): bool {
        if (!enabled || !hasDefault)
            return false
        const d = Number(defaultValue)
        if (discrete) {
            let best = -1
            let bestDistance = Infinity
            for (let i = 0; i < count; ++i) {
                const distance = Math.abs(Number(values[i]) - d)
                if (distance < bestDistance) {
                    bestDistance = distance
                    best = i
                }
            }
            if (best >= 0 && best !== currentIndex)
                indexMoved(best)
        } else {
            const v = Math.max(Math.min(from, to), Math.min(Math.max(from, to), d))
            if (v !== value)
                moved(v)
        }
        return true
    }

    implicitWidth: Math.max(diameter + 16, captionText.implicitWidth, valueLabel.implicitWidth)
    implicitHeight: (label.length > 0 ? captionText.implicitHeight + 4 : 0) + diameter
                    + (showValue ? valueLabel.implicitHeight + 2 : 0)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.Dial
    Accessible.name: label
    Accessible.description: valueText

    Keys.onUpPressed: event => step(1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onRightPressed: event => step(1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onDownPressed: event => step(-1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onLeftPressed: event => step(-1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp) { step(discrete ? 3 : 10, false); event.accepted = true }
        else if (event.key === Qt.Key_PageDown) { step(discrete ? -3 : -10, false); event.accepted = true }
        else if (event.key === Qt.Key_Home) { setPosition(0); event.accepted = true }
        else if (event.key === Qt.Key_End) { setPosition(1); event.accepted = true }
    }

    contentItem: Item {
        Text {
            id: captionText
            visible: control.label.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.label
            color: control.enabled ? Theme.textSecondary : Theme.textDisabled
            font.pixelSize: Theme.fontSizeS + 1
            font.weight: Font.Medium
        }

        Item {
            id: dial
            width: control.diameter
            height: control.diameter
            anchors.horizontalCenter: parent.horizontalCenter
            y: captionText.visible ? captionText.height + 4 : 0
            opacity: control.enabled ? 1 : 0.45

            readonly property real stroke: 3
            readonly property real arcRadius: width / 2 - stroke

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                // Track.
                ShapePath {
                    strokeColor: Theme.dark ? "#2A2E37" : "#DADDE3"
                    strokeWidth: dial.stroke
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: dial.width / 2
                        centerY: dial.height / 2
                        radiusX: dial.arcRadius
                        radiusY: dial.arcRadius
                        startAngle: 135
                        sweepAngle: 270
                    }
                }
                // Value.
                ShapePath {
                    strokeColor: control.color
                    strokeWidth: dial.stroke
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: dial.width / 2
                        centerY: dial.height / 2
                        radiusX: dial.arcRadius
                        radiusY: dial.arcRadius
                        startAngle: 135
                        sweepAngle: Math.max(0.01, 270 * Math.max(0, Math.min(1, control.position)))
                    }
                }
            }

            // Detents of a ring knob.
            Repeater {
                model: control.discrete && control.ticks && control.count <= 32 ? control.count : 0
                delegate: Rectangle {
                    required property int index
                    readonly property real a: (135 + 270 * (control.count > 1 ? index / (control.count - 1) : 0)) * Math.PI / 180
                    readonly property real r: dial.arcRadius - 6
                    width: 2
                    height: 2
                    radius: 1
                    x: dial.width / 2 + r * Math.cos(a) - 1
                    y: dial.height / 2 + r * Math.sin(a) - 1
                    color: index <= control.currentIndex ? Theme.alpha(control.color, 0.8) : Theme.textMuted
                    opacity: 0.8
                }
            }

            // Knob body.
            Rectangle {
                id: body
                anchors.centerIn: parent
                width: dial.width - 20
                height: width
                radius: width / 2
                border.width: 1
                border.color: control.activeFocus ? Theme.alpha(control.color, 0.7) : (Theme.dark ? "#3A3F4B" : "#C9CED6")
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.dark ? (dragArea.containsMouse ? "#363B47" : "#2E323C") : "#FFFFFF" }
                    GradientStop { position: 1; color: Theme.dark ? "#1C1F26" : "#E9ECF0" }
                }
                layer.enabled: true
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: Theme.shadow
                    shadowBlur: 0.6
                    shadowVerticalOffset: 2
                    shadowOpacity: Theme.dark ? 0.8 : 0.35
                    blurMax: 16
                }

                // Pointer.
                Item {
                    anchors.fill: parent
                    rotation: control.angle
                    Behavior on rotation {
                        enabled: !dragArea.pressed && Theme.animations
                        NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutCubic }
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 4
                        width: 3
                        height: parent.height * 0.32
                        radius: 1.5
                        color: control.color
                    }
                }
            }

            FocusRing {
                shown: control.visualFocus
                anchors.margins: -2
                radius: width / 2
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SizeVerCursor
                preventStealing: true
                property real startY: 0
                property real startPosition: 0
                onPressed: mouse => {
                    control.forceActiveFocus(Qt.MouseFocusReason)
                    startY = mouse.y
                    startPosition = control.position
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const travel = (mouse.modifiers & Qt.ShiftModifier) ? 900 : 180
                    control.setPosition(startPosition + (startY - mouse.y) / travel)
                }
                onWheel: wheel => {
                    const n = wheel.angleDelta.y > 0 ? 1 : wheel.angleDelta.y < 0 ? -1 : 0
                    control.step(n, (wheel.modifiers & Qt.ShiftModifier) !== 0)
                }
                onDoubleClicked: control.resetToDefault()
            }
        }

        Text {
            visible: control.showRangeLabels && control.minLabel.length > 0
            x: dial.x - 2
            y: dial.y + dial.height - height + 2
            text: control.minLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeXs
        }
        Text {
            visible: control.showRangeLabels && control.maxLabel.length > 0
            x: dial.x + dial.width - width + 2
            y: dial.y + dial.height - height + 2
            text: control.maxLabel
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeXs
        }

        Text {
            id: valueLabel
            visible: control.showValue
            anchors.horizontalCenter: parent.horizontalCenter
            y: dial.y + dial.height + 2
            text: control.valueText
            color: control.enabled ? Theme.text : Theme.textDisabled
            font.pixelSize: Theme.fontSizeM
            font.weight: Font.DemiBold
            font.features: Theme.tabularFigures
        }
    }

    ToolTip.visible: dragArea.pressed
    ToolTip.text: valueText
}
