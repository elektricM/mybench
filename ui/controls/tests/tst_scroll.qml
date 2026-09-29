import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Controls

// Native wheel/touchpad scrolling (WheelScroll): pixel-exact for pixel deltas, a fixed step per
// notch, immediate (no kinetic animation), stops at the bounds; the styled ScrollView uses it.
Item {
    id: root
    width: 400
    height: 300

    Flickable {
        id: flick
        width: 200
        height: 200
        contentWidth: 200
        contentHeight: 1000
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll { flickable: flick }
    }

    ScrollView {
        id: sv
        x: 220
        width: 160
        height: 200
        contentHeight: 1000
        contentWidth: width
        Item { width: 160; height: 1000 }
    }

    ScrollView {
        id: sv2
        x: 220
        y: 210
        width: 160
        height: 90
        contentHeight: 900
        contentWidth: width
        Item {
            width: 160
            height: 900
            SiField { id: field; x: 10; y: 60; width: 120; unit: "V"; from: 0; to: 100; value: 12; onValueModified: v => sv2.modified++ }
        }
        property int modified: 0
    }

    TestCase {
        name: "NativeScroll"
        when: windowShown

        function init() {
            flick.contentY = 0
            sv.contentItem.contentY = 0
        }

        function test_notchStepIsImmediate() {
            mouseWheel(flick, 50, 50, 0, -120)
            compare(flick.contentY, (Application.styleHints.wheelScrollLines ?? 3) * 20)   // no animation: exact and instant
            verify(!flick.moving)
            mouseWheel(flick, 50, 50, 0, 120)
            compare(flick.contentY, 0)
        }

        function test_stopsAtBounds() {
            mouseWheel(flick, 50, 50, 0, 120)
            compare(flick.contentY, 0)
            for (let i = 0; i < 100; ++i)
                mouseWheel(flick, 50, 50, 0, -120)
            compare(flick.contentY, 800)
        }

        // A scroll that passes over an unfocused SiField keeps scrolling and leaves its value alone.
        function test_scrollPassesOverSiField() {
            sv2.contentItem.contentY = 0
            root.forceActiveFocus()
            wait(WheelGuard.gestureGap + 100)
            sv2.modified = 0
            for (let i = 0; i < 12; ++i) {
                mouseWheel(sv2, 60, 20, 0, -120)
                wait(20)
            }
            compare(sv2.modified, 0)
            compare(field.value, 12)
            verify(sv2.contentItem.contentY > 300)
        }

        function test_styledScrollViewScrollsAndStops() {
            mouseWheel(sv, 50, 50, 0, -120)
            const step = sv.contentItem.contentY
            verify(step > 0)
            compare(step, (Application.styleHints.wheelScrollLines ?? 3) * 20)
            compare(sv.contentItem.boundsBehavior, Flickable.StopAtBounds)
        }
    }
}
