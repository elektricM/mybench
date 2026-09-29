import QtQuick
import QtTest
import MyBench.Controls

// Platform shortcuts: the standard keys (Close, Print) and Ctrl+... work on their own, are never
// ambiguous, and their hints read Ctrl on Linux/Windows and the Command symbol only on macOS.
Item {
    id: host
    width: 200
    height: 100
    focus: true

    property int printed: 0
    property int closed: 0
    property int ambiguous: 0

    Shortcut {
        context: Qt.ApplicationShortcut
        sequences: [StandardKey.Print]
        onActivated: host.printed++
        onActivatedAmbiguously: host.ambiguous++
    }
    Shortcut {
        context: Qt.ApplicationShortcut
        sequences: [StandardKey.Close]
        onActivated: host.closed++
        onActivatedAmbiguously: host.ambiguous++
    }

    TestCase {
        name: "PlatformShortcuts"
        when: windowShown

        function test_standardKeysActivate() {
            keyClick(Qt.Key_P, Qt.ControlModifier)
            compare(host.printed, 1)
            keyClick(Qt.Key_W, Qt.ControlModifier)
            compare(host.closed, 1)
            compare(host.ambiguous, 0)
        }

        function test_hintsFollowPlatform() {
            const saved = Theme.macShortcuts
            Theme.macShortcuts = false
            compare(Theme.keys("Ctrl+P"), "Ctrl+P")
            compare(Theme.keys("Ctrl+Shift+S"), "Ctrl+Shift+S")
            verify(Theme.keys("Ctrl+K").indexOf("⌘") < 0)
            Theme.macShortcuts = true
            compare(Theme.keys("Ctrl+P"), "⌘P")
            Theme.macShortcuts = saved
        }

        function test_macFlagFollowsPlatform() {
            compare(Theme.macShortcuts, Qt.platform.os === "osx" || Qt.platform.os === "macos")
        }
    }
}
