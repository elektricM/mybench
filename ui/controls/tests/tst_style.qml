import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Controls

// Style v2 rules (docs/UI.md §4, §6, §13.9): one tooltip delay, in-window popups in test mode,
// the primary button, desktop metrics.
Item {
    id: root
    width: 400
    height: 300

    Rectangle {
        id: target
        x: 20
        y: 60
        width: 80
        height: 30
        ToolTip.text: "Hint"
    }
    Button { id: primary; x: 150; y: 20; text: "Run"; highlighted: true }
    Button { id: plain; x: 150; y: 60; text: "Single" }
    ComboBox { id: combo; x: 150; y: 100; model: ["a", "b"] }
    Menu { id: menu; MenuItem { text: "Copy Image" } }

    TestCase {
        name: "StyleV2"
        when: windowShown

        function test_oneTooltipDelay() {
            target.ToolTip.delay = 250                 // any positive delay becomes the standard one
            target.ToolTip.visible = true
            const tip = target.ToolTip.toolTip
            verify(tip)
            compare(tip.delay, Theme.toolTipDelay)
            tryCompare(target.ToolTip, "visible", true, Theme.toolTipDelay + 1000)
            target.ToolTip.visible = false
            tryCompare(target.ToolTip, "visible", false)
            // 0 still means "show now" (validation messages).
            target.ToolTip.delay = 0
            target.ToolTip.visible = true
            compare(tip.delay, 0)
            tryCompare(target.ToolTip, "visible", true, 200)
            target.ToolTip.visible = false
        }
        function test_inlinePopupsInTestMode() {
            verify(Theme.inlinePopups)
            compare(menu.popupType, Popup.Item)
            compare(combo.popup.popupType, Popup.Item)
        }
        function test_metrics() {
            compare(plain.height, Theme.controlHeight)
            compare(combo.height, Theme.controlHeight)
            compare(plain.background.radius, Theme.radiusControl)
        }
        function test_primaryButton() {
            compare(primary.background.color, Theme.accent)
            compare(primary.contentItem.color, Theme.accentText)
            primary.enabled = false                    // a disabled primary looks like any disabled button
            compare(primary.background.color, Theme.controlFill)
            primary.enabled = true
            compare(plain.background.color, Theme.controlFill)
        }
    }
}
