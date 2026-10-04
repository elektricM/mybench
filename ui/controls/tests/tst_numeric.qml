import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Framework
import MyBench.Controls

// Numeric entry without knobs (docs/UI.md §6): SiField steppers, invalid state, clamping notice,
// scrubbing; StepSelector arrows, keys, wheel, list.
Item {
    id: root
    width: 600
    height: 400

    Component {
        id: fieldComponent
        SiField {
            width: 140
            unit: "V"
            from: -10
            to: 10
            value: 1
            onValueModified: v => value = v
        }
    }
    Component {
        id: selectorComponent
        StepSelector {
            x: 100
            y: 100
            model: [0.001, 0.002, 0.005, 0.01, 0.02, 0.05]
            unit: "s"
            currentIndex: 2
            onActivated: index => currentIndex = index
        }
    }
    SignalSpy { id: activatedSpy; signalName: "activated" }

    TestCase {
        name: "SiFieldV2"
        when: windowShown

        function test_steppersAlias() {
            const field = createTemporaryObject(fieldComponent, root)
            verify(field.steppers)
            verify(field.showArrows)
            verify(field.scrubbable)
            field.showArrows = false
            verify(!field.steppers)
            verify(!field.scrubbable)                 // follows steppers by default
            const plain = createTemporaryObject(fieldComponent, root, { steppers: false })
            verify(plain.rightPadding < field.rightPadding + 1)
        }
        function test_stepperButtonsStep() {
            const field = createTemporaryObject(fieldComponent, root, { stepSize: 0.5 })
            const up = findChild(field, "stepUp")
            verify(up)
            mouseClick(up)
            compare(field.value, 1.5)
            const down = findChild(field, "stepDown")
            mouseClick(down)
            mouseClick(down)
            compare(field.value, 0.5)
        }
        function test_invalidInput() {
            const field = createTemporaryObject(fieldComponent, root)
            field.forceActiveFocus()
            field.selectAll()
            keyClick(Qt.Key_A)
            keyClick(Qt.Key_B)
            verify(field.invalid)
            compare(field.background.border.color, Theme.negative)
            verify(field.notice.visible)
            keyClick(Qt.Key_Escape)                   // Esc reverts
            verify(!field.invalid)
            compare(field.value, 1)
            // Committing invalid text keeps the value and flags the field for a moment.
            field.selectAll()
            keyClick(Qt.Key_X)
            keyClick(Qt.Key_Return)
            compare(field.value, 1)
            verify(field.invalid)
        }
        function test_clampIsAnnounced() {
            const field = createTemporaryObject(fieldComponent, root)
            field.forceActiveFocus()
            field.selectAll()
            keyClick(Qt.Key_5)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            compare(field.value, 10)
            verify(field.clampedText.length > 0)
            verify(field.notice.visible)
            verify(field.notice.text.indexOf(Units.format(10, "V", 4, false)) >= 0)
        }
        function test_scrub() {
            const field = createTemporaryObject(fieldComponent, root, { value: 2.5 })
            field.beginScrub()
            verify(field.scrubbing)
            field.scrubBy(3, false)                   // second significant digit: 0.1 V
            compare(field.value, 2.8)
            field.scrubBy(-1, true)                   // fine: the last digit (2.800 V → 1 mV)
            compare(field.value, 2.799)
            field.endScrub()
            verify(!field.scrubbing)
            field.scrubBy(1000, false)
            compare(field.value, 10)                  // clamped
        }
    }

    TestCase {
        name: "StepSelector"
        when: windowShown

        function init() {
            activatedSpy.clear()
        }
        function test_textAndArrows() {
            const s = createTemporaryObject(selectorComponent, root)
            activatedSpy.target = s
            compare(s.currentText, Units.format(0.005, "s", 3, true))
            verify(s.implicitWidth > 2 * s.arrowWidth)
            mouseClick(s, s.width - 5, s.height / 2)          // ›
            compare(s.currentIndex, 3)
            mouseClick(s, 5, s.height / 2)                    // ‹
            mouseClick(s, 5, s.height / 2)
            compare(s.currentIndex, 1)
            compare(activatedSpy.count, 3)
        }
        function test_ends() {
            const s = createTemporaryObject(selectorComponent, root, { currentIndex: 0 })
            activatedSpy.target = s
            mouseClick(s, 5, s.height / 2)
            compare(activatedSpy.count, 0)
            verify(s.atStart)
            s.currentIndex = 5
            verify(s.atEnd)
            mouseClick(s, s.width - 5, s.height / 2)
            compare(activatedSpy.count, 0)
        }
        function test_keys() {
            const s = createTemporaryObject(selectorComponent, root)
            s.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(s.currentIndex, 3)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Down)
            compare(s.currentIndex, 1)
            keyClick(Qt.Key_End)
            compare(s.currentIndex, 5)
            keyClick(Qt.Key_Home)
            compare(s.currentIndex, 0)
        }
        function test_wheelFollowsWheelGuard() {
            const s = createTemporaryObject(selectorComponent, root)
            root.forceActiveFocus()
            wait(WheelGuard.gestureGap + 100)
            mouseWheel(s, s.width / 2, s.height / 2, 0, 120)  // a gesture starting over it
            compare(s.currentIndex, 3)
            mouseWheel(s, s.width / 2, s.height / 2, 0, 120)  // same gesture: still its own
            compare(s.currentIndex, 4)
            // A scroll that started elsewhere and passes over it leaves it alone.
            wait(WheelGuard.gestureGap + 100)
            mouseWheel(root, 5, 5, 0, -120)
            mouseWheel(s, s.width / 2, s.height / 2, 0, -120)
            compare(s.currentIndex, 4)
            s.forceActiveFocus()
            mouseWheel(s, s.width / 2, s.height / 2, 0, -120)  // focused: always
            compare(s.currentIndex, 3)
        }
        // A gesture that started over the selector owns it only until the next gesture starts,
        // even when that gesture is a single notch over the view.
        function test_ownershipEndsWithTheGesture() {
            const s = createTemporaryObject(selectorComponent, root)
            root.forceActiveFocus()
            wait(WheelGuard.gestureGap + 100)
            mouseWheel(s, s.width / 2, s.height / 2, 0, 120)  // one notch over the selector
            compare(s.currentIndex, 3)
            wait(WheelGuard.gestureGap + 100)
            mouseWheel(root, 5, 5, 0, -120)                    // a new gesture over the view
            mouseWheel(s, s.width / 2, s.height / 2, 0, -120)  // continues over the selector
            compare(s.currentIndex, 3)
        }
        function test_listPicks() {
            const s = createTemporaryObject(selectorComponent, root)
            mouseClick(s, s.width / 2, s.height / 2)
            tryCompare(s.popup, "opened", true)
            const list = s.popup.contentItem
            const item = list.itemAtIndex(4)
            verify(item)
            mouseClick(item, item.width / 2, item.height / 2)
            compare(s.currentIndex, 4)
            tryCompare(s.popup, "visible", false)
        }
        function test_objectModelAndFormatter() {
            const s = createTemporaryObject(selectorComponent, root, {
                model: [{ value: 1, text: "Slow" }, { value: 2, text: "Fast" }], currentIndex: 1 })
            compare(s.currentText, "Fast")
            compare(s.valueAt(0), 1)
            const f = createTemporaryObject(selectorComponent, root, { formatter: v => (v * 1000) + " ms" })
            compare(f.currentText, "5 ms")
        }
    }
}
