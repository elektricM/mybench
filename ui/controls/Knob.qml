pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Templates as T
import MyBench.Framework

// Rotary knob (docs/UI.md §13): flat face, tick ring, value arc, pointer; 270° travel (minimum at
// 7:30, maximum at 4:30). Use it through KnobField (knob + typeable value + − / +) for the main
// parameters of an instrument (Time/Div, Volts/Div, trigger level, frequency...).
//
// Stepped knob: `values` (+ optional `labels`) and `currentIndex`; emits indexMoved(index).
// Continuous knob: `from`/`to` (optionally `logarithmic`, `stepSize`) and `value`; emits moved(value).
// Controlled component: bind the value and apply the signal.
// Input: vertical drag (up = clockwise; Shift = fine), rotary drag when the press starts on the
// ring, wheel (WheelGuard rule: only with focus or at the start of a gesture), arrow keys,
// PageUp/PageDown, Home/End. Clockwise turns increase the value unless `clockwiseIncreases` is
// false (e.g. a Time/Div knob turned clockwise for a faster timebase). Double-click resets to
// `defaultIndex` (stepped) or `defaultValue` (continuous; for a stepped knob: the nearest entry).
//     Knob { values: ctl.timeDivValues; currentIndex: ctl.timeDivIndex
//            accentColor: Theme.signalCh0; onIndexMoved: i => ctl.timeDivIndex = i }
T.Control {
    id: control

    // Stepped
    property var values: []
    property var labels: []
    property int currentIndex: 0
    property int defaultIndex: -1
    // Continuous
    property real value: 0
    property real from: 0
    property real to: 1
    property bool logarithmic: false
    property real stepSize: 0          // keys/wheel; 0 = 1/100 of the travel (1/500 with Shift)
    // Double-click resets to this: a value for a continuous knob (coerced to [from, to]), the
    // nearest entry of `values` for a stepped knob without `defaultIndex`. Unset: no reset.
    property var defaultValue: undefined
    readonly property bool hasDefault: (discrete && defaultIndex >= 0 && defaultIndex < count)
                                       || (defaultValue !== undefined && defaultValue !== null
                                           && isFinite(Number(defaultValue)))
    property bool clockwiseIncreases: true

    // Presentation
    property real size: 48
    property alias diameter: control.size                  // v1 name
    property color accentColor: Theme.accent
    property alias color: control.accentColor              // v1 name
    property string label                                  // optional caption above the dial
    property string unit
    property int digits: 3
    property bool showValue: false                         // value text below the dial
    property string valueText: discrete ? (labels.length > currentIndex && currentIndex >= 0 ? String(labels[currentIndex])
                                                                   : Units.format(Number(values[currentIndex] ?? 0), unit, digits))
                                        : Units.format(value, unit, digits)
    property bool showRangeLabels: true                    // minLabel/maxLabel, when set
    property string minLabel
    property string maxLabel
    property bool ticks: true

    readonly property bool discrete: values.length > 0
    readonly property int count: values.length
    readonly property real position: discrete ? (count > 1 ? currentIndex / (count - 1) : 0)
                                              : positionOf(value)
    // Fraction of the travel turned clockwise from the 7:30 stop.
    readonly property real turn: Math.max(0, Math.min(1, clockwiseIncreases ? position : 1 - position))
    readonly property real angle: -135 + 270 * turn   // 0 = straight up
    // Where the value arc starts (clockwise fraction): the zero of a bipolar range (-5..5 V: the
    // arc grows from 12 o'clock both ways), else the minimum stop.
    property bool bipolarArc: !discrete && !logarithmic && from < 0 && to > 0
    readonly property real arcOrigin: {
        if (!bipolarArc)
            return clockwiseIncreases ? 0 : 1
        const p = Math.max(0, Math.min(1, positionOf(0)))
        return clockwiseIncreases ? p : 1 - p
    }
    readonly property bool dragging: dragArea.pressed
    property bool wheelOwner: false
    property Connections wheelGestures: Connections {
        target: WheelGuard
        function onGestureStarted() {
            control.wheelOwner = false
        }
    }

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
    function snapped(v: real): real {
        if (stepSize > 0 && !logarithmic)
            v = from + Math.round((v - from) / stepSize) * stepSize
        return Number(Math.max(Math.min(from, to), Math.min(Math.max(from, to), v)).toPrecision(10))
    }
    function setPosition(p: real) {
        p = Math.max(0, Math.min(1, p))
        if (discrete) {
            const i = Math.round(p * (count - 1))
            if (i !== currentIndex)
                indexMoved(i)
        } else {
            const v = snapped(valueAt(p))
            if (v !== value)
                moved(v)
        }
    }
    // Sets the clockwise fraction of the travel.
    function setTurn(t: real) {
        setPosition(clockwiseIncreases ? t : 1 - t)
    }
    // n steps towards larger values (negative: smaller).
    function step(n: int, fine: bool) {
        if (!enabled || n === 0)
            return
        if (discrete) {
            const i = Math.max(0, Math.min(count - 1, currentIndex + n))
            if (i !== currentIndex)
                indexMoved(i)
        } else if (stepSize > 0 && !logarithmic) {
            const v = snapped(value + n * stepSize * (fine ? 0.1 : 1))
            if (v !== value)
                moved(v)
        } else {
            setPosition(position + n * (fine ? 0.002 : 0.01))
        }
    }
    // n clicks clockwise (negative: counter-clockwise).
    function rotate(n: int, fine: bool) {
        step(clockwiseIncreases ? n : -n, fine)
    }

    // Emits moved()/indexMoved() for the default; false when there is none.
    function resetToDefault(): bool {
        if (!enabled || !hasDefault)
            return false
        if (discrete) {
            let best = defaultIndex >= 0 && defaultIndex < count ? defaultIndex : -1
            if (best < 0) {
                const d = Number(defaultValue)
                let bestDistance = Infinity
                for (let i = 0; i < count; ++i) {
                    const distance = Math.abs(Number(values[i]) - d)
                    if (distance < bestDistance) {
                        bestDistance = distance
                        best = i
                    }
                }
            }
            if (best >= 0 && best !== currentIndex)
                indexMoved(best)
        } else {
            const d = Number(defaultValue)
            const v = Math.max(Math.min(from, to), Math.min(Math.max(from, to), d))
            if (v !== value)
                moved(v)
        }
        return true
    }

    readonly property bool hasRangeLabels: showRangeLabels && (minLabel.length > 0 || maxLabel.length > 0)
    implicitWidth: Math.max(size + (hasRangeLabels ? 2 * Math.max(minText.implicitWidth, maxText.implicitWidth) - size * 0.3 : 0),
                            label.length > 0 ? captionText.implicitWidth : 0,
                            showValue ? valueLabel.implicitWidth : 0)
    implicitHeight: (label.length > 0 ? captionText.implicitHeight + 4 : 0) + size
                    + (showValue ? valueLabel.implicitHeight + 2 : 0)
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.role: Accessible.Dial
    Accessible.name: label
    Accessible.description: valueText

    Keys.onUpPressed: event => rotate(1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onRightPressed: event => rotate(1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onDownPressed: event => rotate(-1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onLeftPressed: event => rotate(-1, (event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp) { rotate(discrete ? 3 : 10, false); event.accepted = true }
        else if (event.key === Qt.Key_PageDown) { rotate(discrete ? -3 : -10, false); event.accepted = true }
        else if (event.key === Qt.Key_Home) { setTurn(0); event.accepted = true }
        else if (event.key === Qt.Key_End) { setTurn(1); event.accepted = true }
    }

    contentItem: Item {
        Text {
            id: captionText
            visible: control.label.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: control.label
            color: control.enabled ? Theme.textSecondary : Theme.textDisabled
            font.pixelSize: Theme.fontSizeCaption
        }

        Item {
            id: dial
            width: control.size
            height: control.size
            anchors.horizontalCenter: parent.horizontalCenter
            y: captionText.visible ? captionText.height + 4 : 0
            opacity: control.enabled ? 1 : 0.45

            // Geometry on a 52 px design grid, scaled.
            readonly property real k: width / 52
            readonly property real c: width / 2
            readonly property real faceRadius: 19 * k
            readonly property real arcRadius: 21.6 * k
            readonly property real arcWidth: Math.max(2, 2.5 * k)
            readonly property real tickInner: 23.2 * k
            readonly property real tickOuter: 25.6 * k
            readonly property int tickCount: control.discrete ? (control.count <= 24 ? control.count : 0) : 11

            function polar(deg: real, r: real): point {
                const a = deg * Math.PI / 180
                return Qt.point(c + r * Math.cos(a), c + r * Math.sin(a))
            }
            readonly property string tickPath: {
                if (!control.ticks || tickCount < 2)
                    return ""
                let d = ""
                for (let i = 0; i < tickCount; ++i) {
                    const deg = 135 + 270 * i / (tickCount - 1)
                    const p = polar(deg, tickInner)
                    const q = polar(deg, tickOuter)
                    d += "M" + p.x.toFixed(2) + " " + p.y.toFixed(2) + "L" + q.x.toFixed(2) + " " + q.y.toFixed(2)
                }
                return d
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {   // tick ring
                    strokeColor: Theme.knobTick
                    strokeWidth: 1
                    fillColor: "transparent"
                    capStyle: ShapePath.FlatCap
                    PathSvg { path: dial.tickPath }
                }
                ShapePath {   // face
                    strokeColor: dragArea.containsMouse || control.activeFocus ? Theme.knobRingHover : Theme.knobRing
                    strokeWidth: 1
                    fillColor: dragArea.pressed ? Theme.knobFacePressed : dragArea.containsMouse ? Theme.knobFaceHover : Theme.knobFace
                    PathAngleArc {
                        centerX: dial.c
                        centerY: dial.c
                        radiusX: dial.faceRadius
                        radiusY: dial.faceRadius
                        startAngle: 0
                        sweepAngle: 360
                    }
                }
                ShapePath {   // value arc, from arcOrigin to the pointer
                    strokeColor: Math.abs(control.turn - control.arcOrigin) > 0.002 ? control.accentColor : "transparent"
                    strokeWidth: dial.arcWidth
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: dial.c
                        centerY: dial.c
                        radiusX: dial.arcRadius
                        radiusY: dial.arcRadius
                        startAngle: 135 + 270 * control.arcOrigin
                        sweepAngle: {
                            const sweep = 270 * (control.turn - control.arcOrigin)
                            return Math.abs(sweep) < 0.01 ? 0.01 : sweep
                        }
                    }
                }
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
                    readonly property real w: Math.max(2, 2.4 * dial.k)
                    x: dial.c - w / 2
                    y: dial.c - dial.faceRadius + 4 * dial.k
                    width: w
                    height: dial.faceRadius - 4 * dial.k - 6 * dial.k
                    radius: w / 2
                    color: Theme.text
                    antialiasing: true
                }
            }

            FocusRing {
                shown: control.visualFocus
                anchors.margins: -1
                radius: width / 2
            }

            MouseArea {
                id: dragArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                preventStealing: true
                property real startX: 0
                property real startY: 0
                property real startTurn: 0
                property bool rotary: false
                property real lastAngle: 0
                property real accumulated: 0
                property real wheelAccumulator: 0

                function angleAt(x: real, y: real): real {
                    return Math.atan2(y - dial.c, x - dial.c) * 180 / Math.PI
                }
                onPressed: mouse => {
                    control.forceActiveFocus(Qt.MouseFocusReason)
                    startX = mouse.x
                    startY = mouse.y
                    startTurn = control.turn
                    accumulated = 0
                    const dx = mouse.x - dial.c
                    const dy = mouse.y - dial.c
                    rotary = Math.sqrt(dx * dx + dy * dy) >= dial.faceRadius   // on the ring and ticks
                    lastAngle = angleAt(mouse.x, mouse.y)
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const fine = (mouse.modifiers & Qt.ShiftModifier) !== 0
                    if (rotary) {
                        const a = angleAt(mouse.x, mouse.y)
                        let d = a - lastAngle
                        if (d > 180) d -= 360
                        if (d < -180) d += 360
                        lastAngle = a
                        accumulated += d / 270 * (fine ? 0.2 : 1)
                    } else {
                        // Up or right turns clockwise; a full turn is ~160 px (800 px with Shift).
                        const travel = fine ? 800 : 160
                        accumulated = ((startY - mouse.y) + (mouse.x - startX) * 0.5) / travel
                    }
                    if (control.discrete && control.count > 1) {
                        // Vertical: one detent per 14 px (70 px with Shift) whatever the number
                        // of entries; rotary: the detents of the ring.
                        const steps = rotary ? Math.round(accumulated * (control.count - 1))
                                             : Math.round(accumulated * (fine ? 800 : 160) / (fine ? 70 : 14))
                        const i = Math.round(startTurn * (control.count - 1)) + steps
                        control.setTurn(Math.max(0, Math.min(control.count - 1, i)) / (control.count - 1))
                    } else {
                        control.setTurn(startTurn + accumulated)
                    }
                }
                onWheel: wheel => {
                    // Native etiquette (as SiField): a scroll passing over the knob keeps
                    // scrolling its view; a gesture that starts over it keeps turning it.
                    if (!WheelGuard.allows(control.activeFocus) && !control.wheelOwner) {
                        wheel.accepted = false
                        return
                    }
                    control.wheelOwner = true
                    const fine = (wheel.modifiers & Qt.ShiftModifier) !== 0
                    const delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : -wheel.angleDelta.x
                    wheelAccumulator += delta
                    const threshold = control.discrete ? 120 : 40
                    while (Math.abs(wheelAccumulator) >= threshold) {
                        const n = wheelAccumulator > 0 ? 1 : -1
                        wheelAccumulator -= n * threshold
                        control.rotate(n, fine)
                    }
                }
                onDoubleClicked: control.resetToDefault()
            }
        }

        Text {
            id: minText
            visible: control.hasRangeLabels && control.minLabel.length > 0
            x: dial.x + dial.width * 0.2 - width
            y: dial.y + dial.height - height + 1
            text: control.minLabel
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.features: Theme.tabularFigures
        }
        Text {
            id: maxText
            visible: control.hasRangeLabels && control.maxLabel.length > 0
            x: dial.x + dial.width * 0.8
            y: dial.y + dial.height - height + 1
            text: control.maxLabel
            color: Theme.textTertiary
            font.pixelSize: Theme.fontSizeCaption
            font.features: Theme.tabularFigures
        }

        Text {
            id: valueLabel
            visible: control.showValue
            anchors.horizontalCenter: parent.horizontalCenter
            y: dial.y + dial.height + 2
            text: control.valueText
            color: control.enabled ? Theme.text : Theme.textDisabled
            font.pixelSize: Theme.fontSizeBody
            font.weight: Font.DemiBold
            font.features: Theme.tabularFigures
        }
    }

    ToolTip.visible: dragArea.pressed && !showValue
    ToolTip.text: valueText
}
