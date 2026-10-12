import QtQuick
import QtQuick.Layouts
import QtTest
import MyBench.Controls

// Instrument controls of the UI redesign: KnobField (typing, snapping, steppers),
// ChannelCard, ToolPalette, ToolPanel, PictureButton, MeasurementCard, RunButton, dark default.
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: steppedField
        KnobField {
            label: "Time/Div"
            unit: "s"
            values: [1e-3, 2e-3, 5e-3, 1e-2]
            currentIndex: 0
            onIndexActivated: index => currentIndex = index
        }
    }
    Component {
        id: continuousField
        KnobField {
            label: "Level"
            unit: "V"
            from: -10
            to: 10
            stepSize: 0.5
            value: 1
            onValueModified: v => value = v
        }
    }
    Component {
        id: cardComponent
        ChannelCard {
            width: 260
            title: "CH 0"
            subtitle: "AI 0"
            facts: ["DC", "±10 V range"]
            onToggled: on => checked = on
            Rectangle { implicitWidth: 100; implicitHeight: 80; color: "gray" }
        }
    }
    Component {
        id: paletteComponent
        ToolPalette {
            width: 300
            model: [{ id: "measure", text: "Measure", icon: "ruler" }, { id: "cursors", text: "Cursors", icon: "crosshair" }]
        }
    }
    Component {
        id: panelComponent
        ToolPanel { title: "Measurements"; Rectangle { implicitWidth: 50; implicitHeight: 40 } }
    }
    Component {
        id: pictureComponent
        PictureButton { text: "RMS"; glyph: "M3 9c3-7 5-7 8 0s5 7 8 0" }
    }
    Component {
        id: boundPictureComponent
        PictureButton {
            property bool chosen: false
            text: "Edge"
            checked: chosen
        }
    }
    Component {
        id: autoPictureComponent
        PictureButton { text: "Hold"; autoToggle: true }
    }
    Component {
        id: measurementComponent
        MeasurementCard { title: "CH 0 · RMS"; value: "707.1 mV" }
    }
    Component {
        id: runComponent
        RunButton { onToggled: running = !running }
    }
    Component {
        id: columnComponent
        InstrumentColumn {
            height: 400
            property alias wide: wideCard
            Rectangle { id: wideCard; Layout.fillWidth: true; implicitWidth: 120; implicitHeight: 150; color: "gray" }
            Rectangle { Layout.fillWidth: true; implicitHeight: 150; color: "gray" }
            tools: ToolPalette {
                columns: 3
                model: [{ id: "a", text: "A", icon: "ruler" }, { id: "b", text: "B", icon: "grid" }]
            }
        }
    }
    Component {
        id: syncComponent
        ThemeSync {}
    }

    SignalSpy { id: spy }

    TestCase {
        name: "InstrumentControls"
        when: windowShown

        function watch(target: QtObject, signalName: string) {
            spy.target = null
            spy.signalName = signalName
            spy.target = target
            spy.clear()
        }

        function typeInto(field: Item, text: string) {
            const input = findChild(field, "valueInput")
            verify(input)
            mouseClick(input)
            wait(20)
            input.selectAll()
            for (const c of text)
                keyClick(c)
            keyClick(Qt.Key_Return)
        }

        function test_steppedTypingSnaps() {
            const field = createTemporaryObject(steppedField, root)
            typeInto(field, "2m")
            compare(field.currentIndex, 1)
            typeInto(field, "6m")          // snaps to the nearest entry (5 ms)
            compare(field.currentIndex, 2)
            typeInto(field, "1")           // 1 s: clamps to the largest
            compare(field.currentIndex, 3)
            compare(findChild(field, "valueInput").text, field.valueText)
        }

        function test_continuousTyping() {
            const field = createTemporaryObject(continuousField, root)
            typeInto(field, "250m")
            compare(field.value, 0.25)
            typeInto(field, "1.5k")        // clamped to `to`
            compare(field.value, 10)
            watch(field, "valueModified")
            typeInto(field, "abc")
            compare(spy.count, 0)
            verify(field.invalid)
            compare(field.value, 10)
        }

        function test_steppers() {
            const field = createTemporaryObject(steppedField, root)
            const plus = findChild(field, "plus")
            const minus = findChild(field, "minus")
            mouseClick(plus)
            compare(field.currentIndex, 1)
            mouseClick(plus)
            compare(field.currentIndex, 2)
            mouseClick(minus)
            compare(field.currentIndex, 1)
            const level = createTemporaryObject(continuousField, root)
            mouseClick(findChild(level, "plus"))
            compare(level.value, 1.5)
        }

        function test_keysInValue() {
            const field = createTemporaryObject(continuousField, root)
            const input = findChild(field, "valueInput")
            mouseClick(input)
            keyClick(Qt.Key_Up)
            compare(field.value, 1.5)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(field.value, 0.5)
        }

        function test_compact() {
            const field = createTemporaryObject(steppedField, root, { compact: true })
            verify(!field.knob.visible)
            const full = createTemporaryObject(steppedField, root)
            verify(full.knob.visible)
            verify(full.implicitWidth > field.implicitWidth)
        }

        // Default: an off channel keeps its body (dimmed, usable) so it can be set up first.
        function test_channelCardKeepsBodyWhenOff() {
            const card = createTemporaryObject(cardComponent, root)
            waitForRendering(card)
            const open = card.height
            const body = findChild(card, "channelBody")
            compare(body.opacity, 1)
            mouseClick(findChild(card, "channelSwitch"))
            verify(!card.checked)
            verify(card.expanded)
            verify(!card.active)
            compare(card.height, open)
            verify(body.visible && body.enabled)
            verify(body.opacity < 1)
        }

        function test_channelCardCollapses() {
            const card = createTemporaryObject(cardComponent, root, { collapseWhenOff: true })
            waitForRendering(card)
            const open = card.height
            verify(open > 100)
            const sw = findChild(card, "channelSwitch")
            mouseClick(sw)
            verify(!card.checked)
            tryVerify(() => card.height < 60)
            mouseClick(sw)
            verify(card.checked)
            tryCompare(card, "height", open)
        }

        function test_toolPalette() {
            const palette = createTemporaryObject(paletteComponent, root)
            watch(palette, "activated")
            const tool = findChild(palette, "tool_cursors")
            mouseClick(tool)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], "cursors")
            palette.currentId = "cursors"
            verify(tool.current)
            mouseClick(tool)               // the open tool again: same id (owner toggles)
            compare(spy.signalArguments[1][0], "cursors")
        }

        function test_toolPanelCloses() {
            const panel = createTemporaryObject(panelComponent, root)
            watch(panel, "closeRequested")
            panel.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            compare(spy.count, 1)
            mouseClick(findChild(panel, "closeButton"))
            compare(spy.count, 2)
            compare(panel.width, Theme.toolPanelWidth)
        }

        function test_pictureButton() {
            const button = createTemporaryObject(pictureComponent, root)
            verify(!button.checked)
            verify(!button.checkable)
            mouseClick(button)
            verify(!button.checked)          // no auto-toggle: the caller decides
            verify(button.height >= 40)
        }

        // A click never breaks the caller's `checked` binding, also while checked.
        function test_pictureButtonKeepsBinding() {
            const button = createTemporaryObject(boundPictureComponent, root)
            watch(button, "clicked")
            mouseClick(button)
            compare(spy.count, 1)
            verify(!button.checked)
            button.chosen = true
            verify(button.checked)
            mouseClick(button)
            mouseClick(button)
            compare(spy.count, 3)
            verify(button.checked)
            button.chosen = false
            verify(!button.checked)
            button.chosen = true
            verify(button.checked)
        }

        // One fixed width whatever the cards hold (a setting never resizes the display); the palette stays at the bottom while the cards scroll.
        function test_instrumentColumn() {
            const column = createTemporaryObject(columnComponent, root)
            compare(column.width, Theme.instrumentColumnWidth)
            column.wide.implicitWidth = 900
            compare(column.width, Theme.instrumentColumnWidth)
            column.compact = true
            compare(column.width, Theme.instrumentColumnCompactWidth)
            column.compact = false
            const slot = findChild(column, "instrumentColumnTools")
            compare(column.tools.parent, slot)
            verify(slot.height > 0 && column.tools.visible, "the palette shows (R5: visibility loop)")
            compare(slot.y + slot.height, column.height)
            verify(column.tools.width > 0 && column.tools.width < column.width)
            verify(!column.scrolls)
            column.wide.implicitHeight = 600
            tryVerify(() => column.scrolls)
            compare(slot.y + slot.height, column.height, "palette pinned while the cards scroll")
        }

        // Without a pictogram: a compact text button, no empty picture slot .
        function test_pictureButtonTextOnly() {
            const plain = createTemporaryObject(autoPictureComponent, root)
            const picture = createTemporaryObject(pictureComponent, root)
            verify(!plain.hasPicture)
            verify(picture.hasPicture)
            verify(plain.height < picture.height - 10, "text-only " + plain.height + " vs " + picture.height)
            verify(plain.height >= Theme.controlHeight)
        }

        function test_pictureButtonAutoToggle() {
            const button = createTemporaryObject(autoPictureComponent, root)
            watch(button, "toggled")
            verify(button.checkable)
            mouseClick(button)
            verify(button.checked)
            compare(spy.count, 1)
            mouseClick(button)
            verify(!button.checked)
        }

        function test_measurementCard() {
            const card = createTemporaryObject(measurementComponent, root)
            verify(!card.hasStats)
            verify(card.width >= 180)
            verify(card.height >= 64)
            card.stats = { min: "1", max: "2", mean: "", stdDev: "" }
            verify(card.hasStats)
            watch(card, "removeRequested")
            mouseMove(card, card.width / 2, card.height / 2)
            mouseClick(findChild(card, "removeButton"))
            compare(spy.count, 1)
        }

        // Numeric API: value and stats formatted with Units.format (σ one digit less).
        function test_measurementCardNumeric() {
            const card = createTemporaryObject(measurementComponent, root)
            compare(card.valueText, "707.1 mV")          // string API unchanged
            card.unit = "Hz"
            card.numericValue = 1000.0412
            compare(card.valueText, "1 kHz")
            card.numericValue = 201.8512e-6
            compare(card.valueText, "201.9 µHz")
            card.stats = { min: 0.00123456, max: "2 Hz", mean: 1234.56, stdDev: 201.85e-6 }
            verify(card.hasStats)
            compare(card.statText("min"), "1.235 mHz")
            compare(card.statText("max"), "2 Hz")
            compare(card.statText("mean"), "1.235 kHz")
            compare(card.statText("stdDev"), "202 µHz")
            card.significantDigits = 3
            compare(card.valueText, "202 µHz")
            compare(card.statText("stdDev"), "202 µHz")     // integer digits are kept
            card.stats = { stdDev: 1.2345e-3 }
            compare(card.statText("stdDev"), "1.2 mHz")
            compare(card.statText("min"), "—")
            card.numericValue = NaN
            compare(card.valueText, "707.1 mV")
        }

        function test_runButton() {
            const button = createTemporaryObject(runComponent, root)
            verify(button.width >= 96 && button.height >= 52)
            mouseClick(button)
            verify(button.running)
            button.forceActiveFocus()
            keyClick(Qt.Key_Space)
            verify(!button.running)
            keyClick(Qt.Key_Return)
            verify(button.running)
            button.busy = true
            mouseClick(button)
            verify(button.running, "busy ignores clicks")
        }

        // Green Run, red Stop (the state reads from across the bench).
        function test_runButtonColors() {
            const button = createTemporaryObject(runComponent, root)
            compare(button.baseFill, Theme.runFill)
            button.running = true
            compare(button.baseFill, Theme.stopFill)
            verify(Theme.contrast("#FFFFFF", Theme.runFill) >= 4.5)
            verify(Theme.contrast("#FFFFFF", Theme.stopFill) >= 4.5)
        }

        function test_darkByDefault() {
            const sync = createTemporaryObject(syncComponent, root)
            compare(sync.mode, "dark")
        }
    }
}
