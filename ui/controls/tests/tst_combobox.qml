import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Controls

// The styled ComboBox behaves like the native one: wheel over the closed combo, keyboard
// type-ahead, press-drag-release selection.
Item {
    id: root
    width: 400
    height: 300

    ComboBox {
        id: combo
        x: 20
        y: 20
        width: 180
        model: ["Alpha", "Bravo", "Charlie", "Delta", "Echo"]
    }
    Component {
        id: scrollComponent
        ScrollView {
            id: sv
            x: 220
            width: 170
            height: 200
            property alias combo: inner
            contentHeight: 900
            contentWidth: width
            Item {
                width: 170
                height: 900
                ComboBox {
                    id: inner
                    x: 10
                    y: 60
                    width: 150
                    model: ["One", "Two", "Three", "Four"]
                }
            }
        }
    }
    SignalSpy { id: activatedSpy; target: combo; signalName: "activated" }

    TestCase {
        name: "ComboBoxNative"
        when: windowShown

        function init() {
            combo.currentIndex = 0
            combo.popup.close()
            activatedSpy.clear()
        }

        function test_wheelDefaultFollowsPlatform() {
            const fresh = Qt.createQmlObject('import QtQuick.Controls; ComboBox { model: ["a"] }', root)
            compare(fresh.wheelChanges, !Theme.macShortcuts)
            verify(!fresh.wheelEnabled)          // the native always-on wheel handling stays off
            fresh.destroy()
        }

        function test_wheelChangesValue() {
            combo.wheelChanges = true
            combo.forceActiveFocus()
            mouseWheel(combo, 40, 10, 0, -120)          // wheel down: next entry
            compare(combo.currentIndex, 1)
            mouseWheel(combo, 40, 10, 0, -120)
            compare(combo.currentIndex, 2)
            mouseWheel(combo, 40, 10, 0, 120)           // wheel up: previous
            compare(combo.currentIndex, 1)
            verify(activatedSpy.count >= 3)
            combo.wheelChanges = !Theme.macShortcuts
        }

        function test_wheelStartingOverUnfocusedComboChanges() {
            combo.wheelChanges = true
            root.forceActiveFocus()
            verify(!combo.activeFocus)
            wait(WheelGuard.gestureGap + 100)           // no scroll in progress
            mouseWheel(combo, 40, 10, 0, -120)
            compare(combo.currentIndex, 1)
            combo.wheelChanges = !Theme.macShortcuts
        }

        function test_macNeverChangesOnWheel() {
            combo.wheelChanges = false
            combo.forceActiveFocus()
            wait(WheelGuard.gestureGap + 100)
            mouseWheel(combo, 40, 10, 0, -120)
            compare(combo.currentIndex, 0)
            combo.wheelChanges = !Theme.macShortcuts
        }

        // A scroll gesture that started elsewhere keeps scrolling the view when it crosses a combo
        // and leaves the combo alone (native Qt/KDE); once the combo has focus it does take the wheel.
        function test_scrollAcrossComboKeepsScrolling() {
            combo.wheelChanges = true
            const flick = createTemporaryObject(scrollComponent, root)
            const inner = flick.combo
            const view = flick.contentItem
            root.forceActiveFocus()
            verify(!inner.activeFocus)
            inner.wheelChanges = true
            wait(WheelGuard.gestureGap + 100)
            // A continuous scroll (events well inside the gesture gap) starting over empty content
            // while the combo (y 60..100 in the content) moves under the fixed pointer.
            let crossed = false
            for (let i = 0; i < 40; ++i) {
                mouseWheel(flick, 60, 20, 0, -120)
                wait(25)
                const p = inner.mapFromItem(flick, 60, 20)
                if (inner.contains(p))
                    crossed = true
            }
            verify(crossed)
            verify(view.contentY > 100)                 // the view kept scrolling past the combo
            compare(inner.currentIndex, 0)
        }

        function test_scrollOverFocusedComboChanges() {
            const flick = createTemporaryObject(scrollComponent, root)
            const inner = flick.combo
            inner.wheelChanges = true
            inner.forceActiveFocus()
            wait(50)
            mouseWheel(flick, 10, 10, 0, -60)           // scroll in progress, started elsewhere
            const y = inner.mapToItem(flick, 0, inner.height / 2).y
            mouseWheel(flick, 60, y, 0, -120)
            compare(inner.currentIndex, 1)
        }

        function test_typeAhead() {
            combo.forceActiveFocus()
            keyClick(Qt.Key_D)
            compare(combo.currentIndex, 3)
            keyClick(Qt.Key_B)
            compare(combo.currentIndex, 1)
        }

        function test_pressDragReleaseSelects() {
            mousePress(combo, 40, combo.height / 2, Qt.LeftButton)
            mouseMove(combo, 40, combo.height + 30, 30, Qt.LeftButton)
            tryCompare(combo.popup, "visible", true)
            const list = combo.popup.contentItem
            const third = list.itemAtIndex(2)
            verify(third)
            const p = third.mapToItem(combo, third.width / 2, third.height / 2)
            mouseMove(combo, p.x, p.y, 30, Qt.LeftButton)
            compare(combo.dragIndex, 2)
            mouseRelease(combo, p.x, p.y, Qt.LeftButton)
            compare(combo.currentIndex, 2)
            compare(activatedSpy.count, 1)
            tryCompare(combo.popup, "visible", false)
        }

        function test_clickStillOpensAndPicks() {
            mouseClick(combo, 40, combo.height / 2)
            tryCompare(combo.popup, "opened", true)
            const item = combo.popup.contentItem.itemAtIndex(4)
            mouseClick(item, item.width / 2, item.height / 2)
            compare(combo.currentIndex, 4)
            tryCompare(combo.popup, "visible", false)
        }

        function test_dragOutsideClosesWithoutChange() {
            mousePress(combo, 40, combo.height / 2, Qt.LeftButton)
            mouseMove(combo, 40, combo.height + 30, 30, Qt.LeftButton)
            tryCompare(combo.popup, "visible", true)
            mouseMove(combo, 300, 250, 30, Qt.LeftButton)
            mouseRelease(combo, 300, 250, Qt.LeftButton)
            tryCompare(combo.popup, "visible", false)
            compare(combo.currentIndex, 0)
        }
    }
}
