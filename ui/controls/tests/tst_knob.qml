import QtQuick
import QtTest
import MyBench.Controls

// Knob v2 (UI redesign round 2): stepped/continuous input by keys, drag and wheel,
// clockwiseIncreases, defaultIndex reset, bipolar arc origin, v1 property names.
Item {
    id: root
    width: 400
    height: 300

    Component {
        id: steppedComponent
        Knob { values: [1, 2, 5, 10, 20, 50]; currentIndex: 2; onIndexMoved: index => currentIndex = index }
    }
    Component {
        id: continuousComponent
        Knob { from: -5; to: 5; value: 0; stepSize: 0.1; onMoved: v => value = v }
    }

    SignalSpy { id: indexSpy; signalName: "indexMoved" }
    SignalSpy { id: movedSpy; signalName: "moved" }

    TestCase {
        name: "Knob"
        when: windowShown

        function center(knob: Knob): point {
            return Qt.point(knob.width / 2, knob.size / 2)
        }

        function test_steppedKeys() {
            const knob = createTemporaryObject(steppedComponent, root)
            knob.forceActiveFocus()
            keyClick(Qt.Key_Up)
            compare(knob.currentIndex, 3)
            keyClick(Qt.Key_Left)
            compare(knob.currentIndex, 2)
            keyClick(Qt.Key_PageUp)
            compare(knob.currentIndex, 5)
            keyClick(Qt.Key_Home)
            compare(knob.currentIndex, 0)
            keyClick(Qt.Key_Down)          // already at the stop: no signal
            compare(knob.currentIndex, 0)
            keyClick(Qt.Key_End)
            compare(knob.currentIndex, 5)
        }

        function test_counterClockwise() {
            const knob = createTemporaryObject(steppedComponent, root, { clockwiseIncreases: false })
            knob.forceActiveFocus()
            keyClick(Qt.Key_Up)            // a clockwise click: smaller value
            compare(knob.currentIndex, 1)
            compare(knob.turn, 1 - 1 / 5)
            knob.step(1, false)            // step() is value-based
            compare(knob.currentIndex, 2)
            keyClick(Qt.Key_Home)          // counter-clockwise stop = largest value
            compare(knob.currentIndex, 5)
        }

        function test_continuousStep() {
            const knob = createTemporaryObject(continuousComponent, root)
            knob.forceActiveFocus()
            keyClick(Qt.Key_Right)
            compare(knob.value, 0.1)
            keyClick(Qt.Key_PageDown)
            compare(knob.value, -0.9)
            keyClick(Qt.Key_End)
            compare(knob.value, 5)
        }

        function test_dragUpIncreases() {
            const knob = createTemporaryObject(continuousComponent, root, { stepSize: 0 })
            const p = center(knob)
            mousePress(knob, p.x, p.y)
            mouseMove(knob, p.x, p.y - 20)
            mouseMove(knob, p.x, p.y - 40)
            mouseRelease(knob, p.x, p.y - 40)
            verify(knob.value > 0, "drag up turns clockwise: " + knob.value)
            const v = knob.value
            mousePress(knob, p.x, p.y)
            mouseMove(knob, p.x, p.y + 30)
            mouseMove(knob, p.x, p.y + 60)
            mouseRelease(knob, p.x, p.y + 60)
            verify(knob.value < v)
        }

        function test_dragStepped() {
            const knob = createTemporaryObject(steppedComponent, root)
            const p = center(knob)
            mousePress(knob, p.x, p.y)
            mouseMove(knob, p.x, p.y - 10)
            mouseMove(knob, p.x, p.y - 25)
            mouseRelease(knob, p.x, p.y - 25)
            compare(knob.currentIndex, 4, "one detent per 14 px")
        }

        function test_wheelWithFocus() {
            const knob = createTemporaryObject(steppedComponent, root)
            knob.forceActiveFocus()
            const p = center(knob)
            mouseWheel(knob, p.x, p.y, 0, 120)
            compare(knob.currentIndex, 3)
            mouseWheel(knob, p.x, p.y, 0, -120)
            mouseWheel(knob, p.x, p.y, 0, -120)
            compare(knob.currentIndex, 1)
        }

        function test_wheelGestureOwnership() {
            const knob = createTemporaryObject(steppedComponent, root)
            const p = center(knob)
            knob.wheelOwner = false
            WheelGuard.gestureStarted()
            verify(!knob.wheelOwner)
            knob.wheelOwner = true             // a gesture that started over the knob
            mouseWheel(knob, p.x, p.y, 0, 120)
            compare(knob.currentIndex, 3, "the owner keeps turning without focus")
            WheelGuard.gestureStarted()        // the next gesture: ownership ends
            verify(!knob.wheelOwner)
        }

        function test_defaultIndex() {
            const knob = createTemporaryObject(steppedComponent, root, { defaultIndex: 4 })
            verify(knob.hasDefault)
            const p = center(knob)
            mouseDoubleClickSequence(knob, p.x, p.y)
            compare(knob.currentIndex, 4)
        }

        function test_bipolarArc() {
            const knob = createTemporaryObject(continuousComponent, root)
            verify(knob.bipolarArc)
            compare(knob.arcOrigin, 0.5)
            const unipolar = createTemporaryObject(continuousComponent, root, { from: 0, to: 10 })
            verify(!unipolar.bipolarArc)
            compare(unipolar.arcOrigin, 0)
        }

        function test_v1Names() {
            const knob = createTemporaryObject(continuousComponent, root, { diameter: 60, color: "red" })
            compare(knob.size, 60)
            compare(String(knob.accentColor), "#ff0000")
            verify(!knob.showValue)
        }
    }
}
