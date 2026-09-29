import QtQuick
import QtTest
import MyBench.Controls

// Theme follows the platform: mode System / Light / Dark, palette-derived surfaces and accent,
// readable accents, fixed instrument display colors.
TestCase {
    name: "ThemeSystem"

    function init() {
        Theme.mode = "system"
        Theme.platformProvided = true
    }
    function cleanup() {
        Theme.mode = "system"
    }

    function test_defaultIsSystem() {
        compare(Theme.mode, "system")
    }

    function test_modeChoosesLook() {
        Theme.mode = "dark"
        compare(Theme.dark, true)
        Theme.mode = "light"
        compare(Theme.dark, false)
        Theme.mode = "system"
        compare(Theme.dark, Application.styleHints.colorScheme !== Qt.Light)
    }

    function test_windowFollowsSystemPalette() {
        Theme.mode = "dark"
        Theme.systemWindow = "#31363b"
        Theme.systemBase = "#1b1e20"
        Theme.systemText = "#fcfcfc"
        verify(Theme.followSystem)
        compare(Theme.surface, Qt.color("#31363b"))
        compare(Theme.text, Qt.color("#fcfcfc"))
        verify(Theme.window.r < Theme.surface.r)          // behind the panels
        Theme.mode = "light"
        // A "Light" choice on a dark desktop: the built-in light colors, never dark-on-dark.
        verify(!Theme.followSystem)
        compare(Theme.window, Qt.color("#EEF0F3"))
        Theme.systemWindow = "#eff0f1"
        Theme.systemBase = "#ffffff"
        Theme.systemText = "#232629"
        verify(Theme.followSystem)
        compare(Theme.window, Qt.color("#eff0f1"))
        compare(Theme.text, Qt.color("#232629"))
    }

    function test_accentFollowsSystemAndStaysReadable() {
        Theme.mode = "dark"
        Theme.systemWindow = "#31363b"
        Theme.systemAccent = "#3daee9"
        verify(Theme.contrast(Theme.accent, Theme.surface) >= 3)
        // A dark accent on a dark surface is lightened, not used as is.
        Theme.systemAccent = "#101820"
        verify(Theme.contrast(Theme.accent, Theme.surface) >= 3)
        verify(Theme.accent !== Qt.color("#101820"))
        Theme.mode = "light"
        Theme.systemWindow = "#eff0f1"
        Theme.systemAccent = "#f5e050"                       // yellow on light: darkened
        verify(Theme.contrast(Theme.accent, Theme.surface) >= 3)
        // The text on the accent stays readable.
        verify(Theme.contrast(Theme.textOnAccent, Theme.accent) >= 3)
    }

    function test_displayColorsStayFixed() {
        Theme.mode = "dark"
        Theme.systemWindow = "#7f7f7f"
        compare(Theme.display, Qt.color("#060709"))
        compare(Theme.ch0, Qt.color("#4ADE80"))
        Theme.systemWindow = "#31363b"
    }

    function test_noPlatformKeepsBuiltInColors() {
        Theme.platformProvided = false
        Theme.mode = "dark"
        compare(Theme.window, Qt.color("#0A0B0E"))
        compare(Theme.accent, Qt.color("#6D8BFF"))
        Theme.mode = "light"
        compare(Theme.accent, Qt.color("#3A5BD9"))
    }
}
