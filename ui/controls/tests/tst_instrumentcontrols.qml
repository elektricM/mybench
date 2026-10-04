import QtQuick
import QtTest
import MyBench.Controls

// Instrument controls of the UI redesign round 2: KnobField (typing, snapping, steppers),
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
        id: measurementComponent
        MeasurementCard { title: "CH 0 · RMS"; value: "707.1 mV" }
    }
    Component {
        id: runComponent
        RunButton { onToggled: running = !running }
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

        function test_channelCardCollapses() {
            const card = createTemporaryObject(cardComponent, root)
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
            compare(panel.width, 280)
        }

        function test_pictureButton() {
            const button = createTemporaryObject(pictureComponent, root)
            verify(!button.checked)
            mouseClick(button)
            verify(button.checked)
            verify(button.height >= 40)
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

        function test_darkByDefault() {
            const sync = createTemporaryObject(syncComponent, root)
            compare(sync.mode, "dark")
        }
    }
}
