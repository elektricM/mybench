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
            compare(fresh.wheelEnabled, !Theme.macShortcuts)
            fresh.destroy()
        }

        function test_wheelChangesValue() {
            combo.wheelEnabled = true
            mouseWheel(combo, 40, 10, 0, -120)          // wheel down: next entry
            compare(combo.currentIndex, 1)
            mouseWheel(combo, 40, 10, 0, -120)
            compare(combo.currentIndex, 2)
            mouseWheel(combo, 40, 10, 0, 120)           // wheel up: previous
            compare(combo.currentIndex, 1)
            verify(activatedSpy.count >= 3)
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
