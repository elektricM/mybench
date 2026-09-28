import QtQuick
import QtTest
import MyBench.Framework
import MyBench.Controls

// docs/audit.md "Application shell" rows 20 (Knob default), 21 (SiField wheel/tooltip/name),
// 24 (resource chip menu), 25 (shortcut hints, workspace tab moves).
Item {
    id: root
    width: 640
    height: 480

    Component {
        id: knobComponent
        Knob { from: 0; to: 10 }
    }
    Component {
        id: fieldComponent
        SiField { width: 140 }
    }
    Component {
        id: chipsComponent
        ResourceChips {}
    }

    SignalSpy { id: movedSpy; signalName: "moved" }
    SignalSpy { id: indexSpy; signalName: "indexMoved" }
    SignalSpy { id: modifiedSpy; signalName: "valueModified" }

    TestCase {
        name: "ThemeKeys"

        function test_keys() {
            const saved = Theme.macShortcuts
            Theme.macShortcuts = true
            compare(Theme.keys("Ctrl+Shift+F"), "⇧⌘F")
            compare(Theme.keys("Ctrl+."), "⌘.")
            compare(Theme.keys("Ctrl+3"), "⌘3")
            compare(Theme.keys("Ctrl++"), "⌘+")
            compare(Theme.keys("Alt+Shift+Ctrl+X"), "⌥⇧⌘X")
            compare(Theme.keys("F1"), "F1")
            Theme.macShortcuts = false
            compare(Theme.keys("Ctrl+Shift+F"), "Ctrl+Shift+F")
            compare(Theme.keys("Ctrl+,"), "Ctrl+,")
            Theme.macShortcuts = saved
        }
    }

    TestCase {
        name: "KnobDefault"
        when: windowShown

        function dial(knob: Item): point {
            return Qt.point(knob.width / 2, knob.diameter / 2)
        }
        function test_continuousReset() {
            const knob = createTemporaryObject(knobComponent, root, { value: 7, defaultValue: 2.5 })
            movedSpy.target = knob
            movedSpy.clear()
            const p = dial(knob)
            mouseDoubleClickSequence(knob, p.x, p.y)
            compare(movedSpy.count, 1)
            compare(movedSpy.signalArguments[0][0], 2.5)
        }
        function test_coercedToRange() {
            const knob = createTemporaryObject(knobComponent, root, { value: 7, defaultValue: 50 })
            movedSpy.target = knob
            movedSpy.clear()
            verify(knob.resetToDefault())
            compare(movedSpy.signalArguments[0][0], 10)
        }
        function test_noDefaultNoReset() {
            const knob = createTemporaryObject(knobComponent, root, { value: 7 })
            movedSpy.target = knob
            movedSpy.clear()
            const p = dial(knob)
            mouseDoubleClickSequence(knob, p.x, p.y)
            compare(movedSpy.count, 0)
            verify(!knob.resetToDefault())
        }
        function test_ringReset() {
            const knob = createTemporaryObject(knobComponent, root, { values: [1, 2, 5, 10], currentIndex: 3, defaultValue: 2 })
            indexSpy.target = knob
            indexSpy.clear()
            const p = dial(knob)
            mouseDoubleClickSequence(knob, p.x, p.y)
            compare(indexSpy.count, 1)
            compare(indexSpy.signalArguments[0][0], 1)
        }
    }

    TestCase {
        name: "SiFieldDetails"
        when: windowShown

        function test_autoToolTip() {
            const field = createTemporaryObject(fieldComponent, root, { label: "Frequency", unit: "Hz", from: 0.2, to: 20000, value: 1000 })
            compare(field.effectiveToolTip, "Frequency (from " + Units.format(0.2, "Hz", 4, true) + " to " + Units.format(20000, "Hz", 4, true) + ")")
            verify(field.effectiveToolTip.indexOf("kHz") > 0)
            compare(field.Accessible.name, "Frequency")
        }
        function test_callerToolTipWins() {
            const field = createTemporaryObject(fieldComponent, root, { toolTip: "Custom", unit: "V", from: 0, to: 1 })
            compare(field.effectiveToolTip, "Custom")
            compare(field.Accessible.name, "Custom")
        }
        function test_accessibleFallback() {
            const field = createTemporaryObject(fieldComponent, root, { unit: "V" })
            compare(field.effectiveToolTip, "")
            compare(field.Accessible.name, "Value in V")
            const ranged = createTemporaryObject(fieldComponent, root, { unit: "V", from: 0 })
            verify(ranged.effectiveToolTip.startsWith("At least"))
        }
        // gen request: trimZeros display ("100 Hz", "1 kHz") and a step list (1-2-5 rates).
        function test_trimZeros() {
            const field = createTemporaryObject(fieldComponent, root, { unit: "Hz", value: 100, trimZeros: true })
            compare(field.text, Units.format(100, "Hz", 4, true))
            compare(field.text, "100 Hz")
            const plain = createTemporaryObject(fieldComponent, root, { unit: "Hz", value: 100 })
            compare(plain.text, Units.format(100, "Hz", 4, false))
        }
        function test_trimZerosFixed() {   // par1 #2: scope Vertical Position / Level
            const field = createTemporaryObject(fieldComponent, root, { engineering: false, decimals: 2, trimZeros: true, value: 0 })
            compare(field.text, "0")
            field.value = -1.5
            compare(field.text, "-1.5")
            field.value = 0.25
            compare(field.text, "0.25")
            field.unit = "V"
            compare(field.text, "0.25 V")
        }
        function test_prefixDecimals() {   // par1 #3: bode Start/Stop "100.00", "1.50k", "10.00k"
            const field = createTemporaryObject(fieldComponent, root, { unit: "Hz", prefixDecimals: 2, value: 100 })
            compare(field.text, "100.00")
            field.value = 1500
            compare(field.text, "1.50k")
            field.value = 10000
            compare(field.text, "10.00k")
            field.value = 0.5
            compare(field.text, "500.00m")
            // Typed text still parses with the unit implied.
            compare(field.parseText("2.5k"), 2500)
        }
        function test_stepList() {
            const field = createTemporaryObject(fieldComponent, root, { unit: "S/s", value: 2, steps: [0.1, 0.2, 0.5, 1, 2, 5, 10] })
            modifiedSpy.target = field
            modifiedSpy.clear()
            field.tune(1, -1)
            compare(modifiedSpy.signalArguments[0][0], 5)
            field.value = 5
            field.tune(-2, -1)
            compare(modifiedSpy.signalArguments[1][0], 1)
            field.value = 3            // between entries: up goes to 5, down to 2
            field.tune(1, -1)
            compare(modifiedSpy.signalArguments[2][0], 5)
            field.tune(-1, -1)
            compare(modifiedSpy.signalArguments[3][0], 2)
            field.value = 10
            modifiedSpy.clear()
            field.tune(1, -1)          // top of the list: no change
            compare(modifiedSpy.count, 0)
        }
        function test_wheelOnHoverWithoutFocus() {
            const field = createTemporaryObject(fieldComponent, root, { unit: "V", from: 0, to: 100, value: 12 })
            field.y = 200
            modifiedSpy.target = field
            modifiedSpy.clear()
            verify(!field.activeFocus)
            mouseMove(field, 20, field.height / 2)
            tryVerify(() => field.hovered)
            mouseWheel(field, 20, field.height / 2, 0, 120)
            compare(modifiedSpy.count, 1)
            verify(!field.activeFocus)
        }
    }

    TestCase {
        name: "ResourceChips"
        when: windowShown

        function test_idleAndStop() {
            const chips = createTemporaryObject(chipsComponent, root)
            compare(chips.groups.length, 0)
            const alpha = InstrumentRegistry.controller("alpha")
            verify(alpha !== null)
            chips.stopInstrument("alpha")   // idle: stays idle, no error
            compare(alpha.running, false)
            chips.stopInstrument("nope")    // unknown id: ignored
        }
    }

    // Workspace tab moves (app/WorkspaceState.qml; the functions come from row 25).
    TestCase {
        name: "WorkspaceTabs"

        function workspace(): var {
            const component = Qt.createComponent(Qt.resolvedUrl("../../../app/WorkspaceState.qml"))
            verify(component.status === Component.Ready, component.errorString())
            const ws = component.createObject(root)
            if (typeof ws.moveTab !== "function")
                skip("WorkspaceState.moveTab/openInPane not merged yet")
            return ws
        }
        function ids(ws: var): string {
            return ws.tabs.map(t => t.primary + (t.secondary.length > 0 ? "|" + t.secondary : "")).join(",")
        }
        function test_moveTab() {
            const ws = workspace()
            ws.open("alpha"); ws.open("beta"); ws.open("gamma")
            ws.select(0)
            ws.moveTab(0, 2)
            compare(ids(ws), "beta,gamma,alpha")
            compare(ws.currentIndex, 2)   // the current tab follows
            ws.moveTab(2, 0)
            compare(ids(ws), "alpha,beta,gamma")
            ws.moveTab(1, 1)
            compare(ids(ws), "alpha,beta,gamma")
            ws.destroy()
        }
        function test_openInPane() {
            const ws = workspace()
            ws.open("alpha"); ws.open("beta")
            ws.select(0)
            ws.openInPane("beta", "right")          // tab dropped on the right half
            compare(ids(ws), "alpha|beta")
            compare(ws.focused, "beta")
            ws.open("gamma")
            ws.select(0)
            ws.focused = "beta"
            ws.openInOtherPane("gamma")             // focused = beta (right): gamma replaces alpha on the left
            compare(ids(ws), "gamma|beta,alpha")
            compare(ws.focused, "gamma")
            ws.openInOtherPane("alpha")             // focused left: alpha goes right, beta to its own tab
            compare(ids(ws), "gamma|alpha,beta")
            ws.destroy()
        }
    }
}
