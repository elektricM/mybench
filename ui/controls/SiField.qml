import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import MyBench.Framework

// Numeric entry in engineering units, like a bench instrument.
//
// - Shows the value with an SI prefix and unit ("1.000 kHz"); accepts typed prefixes ("1.5k",
//   "20m", "4,7 k", "1e3") — Enter or focus loss commits, Escape reverts.
// - Per-digit tuning: Up/Down change the digit left of the cursor (highlighted), Left/Right pick
//   the digit, PageUp/PageDown step ten times; the wheel over a digit (hovered, focus not needed)
//   changes that digit; the small arrows step by `stepSize` (or the selected digit).
// - `label` is the control's name (accessible name, app.md 7.4). Without a `toolTip`, the tooltip
//   is "<label> (from <from> to <to>)" built from the range and unit (app.md 7.1).
// - Out-of-range values are coerced to [from, to] (LabVIEW behaviour), with a short flash.
// - Controlled component: bind `value` and apply valueModified(value):
//       SiField { unit: "Hz"; from: 0.2; to: 20e3; value: ctl.frequency
//                 onValueModified: v => ctl.frequency = v }
T.TextField {
    id: control

    property real value: 0
    property real from: -1e300
    property real to: 1e300
    property string unit
    property int digits: 4               // significant digits shown (engineering mode)
    property bool engineering: true      // false: fixed `decimals`, no prefix
    property int decimals: 2
    // Engineering display without trailing zeros ("100 Hz", "1 kHz", "123.46 Hz").
    property bool trimZeros: false   // also for engineering: false ("0", "-1.5", "0.25 V")
    // >= 0: ELVISmx-style mantissa with this many decimals and an SI prefix letter, without the
    // unit ("100.00", "1.50k", "10.00k"); the unit is shown outside the field.
    property int prefixDecimals: -1
    // Ascending list of allowed values (e.g. a 1-2-5 sequence): arrows, Up/Down and the wheel
    // move to the next/previous entry instead of tuning a digit. Typed values are still accepted.
    property var steps: []
    property real stepSize: 0            // arrows / keys without a selected digit; 0 = digit
    property real resolution: 0          // smallest tuning step (0 = none)
    property bool showArrows: true
    property color accentColor: Theme.accent
    property string toolTip              // "" = automatic: label and allowed range
    property string label                // name of the value (accessible name), e.g. "Frequency"
    // "from 200 mHz to 20 kHz" (empty when the range is unbounded).
    readonly property string rangeText: {
        const low = Math.abs(from) < 1e299
        const high = Math.abs(to) < 1e299
        const f = v => Units.format(v, unit, Math.max(3, digits), true)
        if (low && high)
            return qsTr("from %1 to %2").arg(f(from)).arg(f(to))
        if (low)
            return qsTr("at least %1").arg(f(from))
        if (high)
            return qsTr("at most %1").arg(f(to))
        return ""
    }
    readonly property string effectiveToolTip: toolTip.length > 0 ? toolTip
                                             : label.length > 0 ? (rangeText.length > 0 ? label + " (" + rangeText + ")" : label)
                                             : rangeText.length > 0 ? rangeText.charAt(0).toUpperCase() + rangeText.slice(1) : ""
    readonly property bool valid: !dirty || !isNaN(parseText(text))
    readonly property string formattedText: formatValue(value)

    signal valueModified(real value)

    property bool dirty: false
    property bool coercedFlash: false
    property real wheelAccumulator: 0

    function formatValue(v: real): string {
        if (!isFinite(v))
            return "---" + (unit.length > 0 ? " " + unit : "")
        if (engineering && prefixDecimals >= 0)
            return prefixed(v)
        if (engineering)
            return Units.format(v, unit, digits, trimZeros)
        const fixedText = Units.fixed(v, unit, decimals)
        if (!trimZeros)
            return fixedText
        const trimmed = fixedText.replace(/^([-\u2212]?\d+)\.(\d*?)0+(?=\D|$)/, (m, whole, fraction) => fraction.length > 0 ? whole + "." + fraction : whole)
        return trimmed.replace(/^[-\u2212]0(?=\D|$)/, "0")
    }
    function prefixed(v: real): string {
        if (v === 0)
            return (0).toFixed(prefixDecimals)
        const letters = { "-12": "p", "-9": "n", "-6": "µ", "-3": "m", "0": "", "3": "k", "6": "M", "9": "G" }
        let exponent = Math.max(-12, Math.min(9, Math.floor(Math.log10(Math.abs(v)) / 3) * 3))
        if (Math.abs(Number((v / Math.pow(10, exponent)).toFixed(prefixDecimals))) >= 1000 && exponent < 9)
            exponent += 3
        return (v / Math.pow(10, exponent)).toFixed(prefixDecimals) + letters[String(exponent)]
    }
    function parseText(t: string): real {
        let v = Units.parse(t, unit)
        if (isNaN(v) && unit.length > 0)
            v = Units.parse(t, "")
        return v
    }
    function clampValue(v: real): real {
        return Math.min(Math.max(v, from), to)
    }
    function refresh() {
        if (!dirty)
            text = formatValue(value)
    }
    function revert() {
        dirty = false
        refresh()
    }
    function commit() {
        if (!dirty)
            return
        const parsed = parseText(text)
        dirty = false
        if (isNaN(parsed)) {
            refresh()
            invalidFlash.restart()
            return
        }
        apply(parsed)
    }
    function apply(v: real) {
        const c = clampValue(v)
        if (c !== v)
            coerceFlash.restart()
        if (c !== value)
            valueModified(c)
        Qt.callLater(control.refresh)
    }

    // --- digit geometry of the displayed text ---
    function layoutOf(str: string): var {
        const m = /^([-+]?)(\d+)(?:\.(\d+))?/.exec(str)
        if (!m)
            return null
        const rest = str.slice(m[0].length)
        let mult = engineering ? Units.prefixMultiplier(rest, unit) : 1
        if (isNaN(mult))
            mult = 1
        return { sign: m[1].length, intDigits: m[2].length, fracDigits: m[3] ? m[3].length : 0,
                 end: m[0].length, mult: mult }
    }
    function weightAt(str: string, index: int): real {
        const l = layoutOf(str)
        if (!l)
            return NaN
        const intEnd = l.sign + l.intDigits
        if (index >= l.sign && index < intEnd)
            return Math.pow(10, intEnd - 1 - index) * l.mult
        const fracStart = intEnd + 1
        if (l.fracDigits > 0 && index >= fracStart && index < fracStart + l.fracDigits)
            return Math.pow(10, -(index - fracStart + 1)) * l.mult
        return NaN
    }
    function digitIndices(str: string): var {
        const l = layoutOf(str)
        const list = []
        if (!l)
            return list
        for (let i = l.sign; i < l.end; ++i)
            if (str[i] !== ".")
                list.push(i)
        return list
    }
    // Digit left of the cursor (the one Up/Down change).
    function activeDigit(): int {
        const list = digitIndices(text)
        if (list.length === 0)
            return -1
        let best = list[0]
        for (const i of list)
            if (i < cursorPosition)
                best = i
        return best
    }
    function indexOfWeight(str: string, weight: real): int {
        const list = digitIndices(str)
        let best = -1
        let bestDistance = Infinity
        for (const i of list) {
            const d = Math.abs(Math.log10(weightAt(str, i)) - Math.log10(weight))
            if (d < bestDistance - 1e-9) {
                bestDistance = d
                best = i
            }
        }
        return best
    }
    // Next entry of `steps` from the current value, `count` entries up (> 0) or down.
    function stepListValue(count: int): real {
        const list = control.steps
        const eps = Math.abs(value) * 1e-9
        let i
        if (count > 0) {
            i = list.findIndex(v => v > value + eps) - 1 + count
            if (list.findIndex(v => v > value + eps) < 0)
                return value
        } else {
            let below = -1
            for (let k = 0; k < list.length; ++k)
                if (list[k] < value - eps)
                    below = k
            if (below < 0)
                return value
            i = below + 1 + count
        }
        return list[Math.max(0, Math.min(list.length - 1, i))]
    }
    function tune(steps: int, digitIndex: int) {
        if (readOnly || !enabled || steps === 0)
            return
        if (dirty)
            commit()
        if (control.steps && control.steps.length > 0) {
            const target = clampValue(stepListValue(steps))
            if (target !== value)
                valueModified(target)
            return
        }
        let weight = digitIndex >= 0 ? weightAt(text, digitIndex) : NaN
        if (isNaN(weight))
            weight = stepSize > 0 ? stepSize : weightAt(text, activeDigit())
        if (isNaN(weight) || weight <= 0)
            weight = 1
        if (resolution > 0 && weight < resolution)
            weight = resolution
        const next = clampValue(Number((value + steps * weight).toPrecision(12)))
        if (next === value)
            return
        valueModified(next)
        const keep = weight
        Qt.callLater(function() {
            control.refresh()
            const i = control.indexOfWeight(control.text, keep)
            if (i >= 0)
                control.cursorPosition = i + 1
        })
    }

    implicitWidth: Math.max(96, contentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: 9
    rightPadding: showArrows ? 22 : 9
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    horizontalAlignment: TextInput.AlignLeft
    font.pixelSize: Theme.fontSizeBody
    font.features: Theme.tabularFigures
    color: !enabled ? Theme.textDisabled : !valid ? Theme.danger : Theme.text
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text
    selectByMouse: true
    hoverEnabled: true
    inputMethodHints: Qt.ImhFormattedNumbersOnly
    Accessible.role: Accessible.SpinBox
    Accessible.name: label.length > 0 ? label : effectiveToolTip.length > 0 ? effectiveToolTip
                                                                           : unit.length > 0 ? qsTr("Value in %1").arg(unit) : qsTr("Value")
    Accessible.description: effectiveToolTip

    Component.onCompleted: refresh()
    onValueChanged: refresh()
    onUnitChanged: refresh()
    onDigitsChanged: refresh()
    onTrimZerosChanged: refresh()
    onPrefixDecimalsChanged: refresh()
    onEngineeringChanged: refresh()
    onDecimalsChanged: refresh()
    onTextEdited: dirty = true
    onEditingFinished: commit()
    onActiveFocusChanged: {
        if (activeFocus && focusReason === Qt.TabFocusReason)
            selectAll()
        if (!activeFocus)
            commit()
    }

    Keys.onEscapePressed: event => {
        if (dirty) {
            revert()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    Keys.onUpPressed: tune(1, activeDigit())
    Keys.onDownPressed: tune(-1, activeDigit())
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp) {
            tune(10, activeDigit())
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            tune(-10, activeDigit())
            event.accepted = true
        }
    }

    WheelHandler {
        enabled: (control.hovered || control.activeFocus) && control.enabled && !control.readOnly
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            control.wheelAccumulator += event.angleDelta.y
            const steps = Math.trunc(control.wheelAccumulator / 120)
            if (steps === 0)
                return
            control.wheelAccumulator -= steps * 120
            const position = control.positionAt(event.x, control.height / 2)
            const list = control.digitIndices(control.text)
            let index = -1
            for (const i of list)
                if (i === position || i === position - 1)
                    index = i
            control.tune(steps, index >= 0 ? index : control.activeDigit())
        }
    }

    Timer {
        id: coerceFlash
        interval: 700
        onRunningChanged: control.coercedFlash = running
    }
    Timer {
        id: invalidFlash
        interval: 900
    }

    background: Rectangle {
        implicitWidth: 96
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusM
        color: control.enabled ? Theme.surfaceSunken : Theme.alpha(Theme.surfaceSunken, 0.5)
        border.width: 1
        border.color: (!control.valid || invalidFlash.running) ? Theme.danger
                    : control.coercedFlash ? Theme.warning
                    : control.activeFocus ? control.accentColor
                    : control.hovered ? Theme.borderStrong : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

        // Tuned-digit marker.
        Rectangle {
            readonly property int digit: control.activeFocus && !control.dirty && !control.readOnly ? control.activeDigit() : -1
            readonly property rect first: digit >= 0 ? control.positionToRectangle(digit) : Qt.rect(0, 0, 0, 0)
            readonly property rect next: digit >= 0 ? control.positionToRectangle(digit + 1) : Qt.rect(0, 0, 0, 0)
            visible: digit >= 0 && control.selectedText.length === 0
            x: control.leftPadding + first.x - 1
            y: parent.height - 6
            width: Math.max(4, next.x - first.x + 2)
            height: 2
            radius: 1
            color: control.accentColor
        }

        // Step arrows.
        Column {
            visible: control.showArrows
            anchors.right: parent.right
            anchors.rightMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Repeater {
                model: [1, -1]
                delegate: T.AbstractButton {
                    id: arrow
                    required property int modelData
                    width: 16
                    height: (Theme.controlHeight - 6) / 2
                    enabled: control.enabled && !control.readOnly
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    autoRepeat: true
                    autoRepeatDelay: 350
                    autoRepeatInterval: 70
                    onClicked: control.tune(modelData, control.stepSize > 0 ? -1 : control.activeDigit())
                    background: Rectangle {
                        radius: Theme.radiusXs
                        color: arrow.down ? Theme.surfacePressed : arrow.hovered ? Theme.surfaceHover : "transparent"
                    }
                    contentItem: Item {
                        Icon {
                            anchors.centerIn: parent
                            name: "chevron-down-line"
                            size: 10
                            rotation: arrow.modelData > 0 ? 180 : 0
                            color: arrow.enabled ? (arrow.hovered ? Theme.text : Theme.textMuted) : Theme.textDisabled
                        }
                    }
                }
            }
        }
    }

    ToolTip.visible: hovered && !activeFocus && (effectiveToolTip.length > 0 || !valid)
    ToolTip.delay: 600
    ToolTip.text: !valid ? qsTr("Enter a number, e.g. 1.5k or 20m") : effectiveToolTip
}
