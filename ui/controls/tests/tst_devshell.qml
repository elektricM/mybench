import QtQuick
import QtQuick.Controls
import QtTest
import MyBench.Controls

// DevShell (the standalone instrument window): no inspector column while the panel loads or for a
// panel without inspector, and the toolbar's inspector toggle shows whether the column is on
// screen (it stays bound: clicking it never leaves it checked while the dock is hidden).
Item {
    id: root
    width: 400
    height: 300

    Component {
        id: shellComponent
        DevShell { width: 1100; height: 700; visible: true }
    }
    Component {
        id: panelComponent
        InstrumentPanel {
            property bool withInspector: false
            hasInspector: withInspector
        }
    }

    TestCase {
        name: "DevShell"
        when: windowShown

        function test_dockFollowsThePanel() {
            const shell = createTemporaryObject(shellComponent, root)
            verify(shell)
            const toggle = findChild(shell.contentItem, "inspectorToggle")
                           ?? findChild(shell.header, "inspectorToggle")
            verify(toggle)
            // Loading (no panel yet): no dock, toggle off and disabled.
            compare(shell.pane, null)
            verify(!shell.dockAvailable)
            verify(!toggle.checked)
            verify(!toggle.enabled)
            // A panel without inspector: still no dock.
            shell.pane = createTemporaryObject(panelComponent, root)
            verify(!shell.dockAvailable)
            verify(!toggle.checked)
            // With an inspector: dock shown, toggle checked; the toggle hides and shows it.
            shell.pane = createTemporaryObject(panelComponent, root, { withInspector: true })
            verify(shell.dockAvailable)
            verify(toggle.enabled)
            verify(toggle.checked)
            toggle.clicked()
            verify(!shell.inspectorShown)
            verify(!toggle.checked)
            toggle.clicked()
            verify(shell.inspectorShown)
            verify(toggle.checked)
            // Back to a panel without inspector while "shown": the toggle is not left checked.
            shell.pane = createTemporaryObject(panelComponent, root)
            verify(shell.inspectorShown)
            verify(!toggle.checked)
        }
    }
}
