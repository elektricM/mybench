pragma Singleton
import QtQuick

// MYBENCH design tokens. Dark by default (instrument displays), light mirrors it.
// Every color, size and duration used by the UI comes from here; never hard-code them.
QtObject {
    id: theme

    // --- Appearance ------------------------------------------------------------------------
    // "system" (default: follow the platform's light/dark setting), "light" or "dark".
    property string mode: "system"
    // The platform's palette (KDE, macOS, Windows, GNOME through QPalette). Overridable so tests
    // can play any platform. `platformProvided` is false where Qt reports no color scheme
    // (offscreen): the built-in colors are used there.
    property color systemWindow: sysPalette.window
    property color systemBase: sysPalette.base
    property color systemText: sysPalette.windowText
    property color systemAccent: sysPalette.accent
    property bool platformProvided: Application.styleHints.colorScheme !== Qt.Unknown
    readonly property bool systemDark: Application.styleHints.colorScheme !== Qt.Light
    readonly property bool dark: mode === "dark" || (mode === "system" && systemDark)
    readonly property bool light: !dark
    // The platform palette is used only when it matches the chosen light/dark look (a "Dark"
    // choice on a light desktop uses the built-in dark colors, keeping every color readable).
    readonly property bool followSystem: platformProvided && (luminance(systemWindow) < 0.5) === dark
    // Animations can be turned off globally (screenshots, reduced motion).
    property bool animations: true

    property SystemPalette sysPalette: SystemPalette { colorGroup: SystemPalette.Active }

    // --- Surfaces --------------------------------------------------------------------------
    // With the platform palette: window = the system window color; surfaces are shades of it.
    readonly property color window: followSystem ? (dark ? mix(systemWindow, "#000000", 0.25) : systemWindow)
                                                 : (dark ? "#0A0B0E" : "#EEF0F3")          // behind everything
    readonly property color chrome: followSystem ? (dark ? mix(systemWindow, "#000000", 0.12) : mix(systemWindow, "#FFFFFF", 0.45))
                                                 : (dark ? "#0F1014" : "#F7F8FA")          // nav rail, top/status bar
    readonly property color surface: followSystem ? (dark ? systemWindow : mix(systemWindow, systemBase, 0.75))
                                                  : (dark ? "#131519" : "#FFFFFF")         // panels, inspector
    readonly property color surfaceRaised: followSystem ? (dark ? mix(systemWindow, "#FFFFFF", 0.05) : systemBase)
                                                        : (dark ? "#191B21" : "#FFFFFF")   // cards, popups, menus
    readonly property color surfaceSunken: followSystem ? (dark ? mix(systemWindow, "#000000", 0.2) : mix(systemWindow, systemBase, 0.5))
                                                        : (dark ? "#0E0F13" : "#F4F5F7")   // input wells, tracks
    readonly property color surfaceHover: followSystem ? mix(surface, systemText, dark ? 0.08 : 0.06)
                                                       : (dark ? "#1F2229" : "#EFF1F4")
    readonly property color surfacePressed: followSystem ? mix(surface, systemText, dark ? 0.14 : 0.11)
                                                         : (dark ? "#262A32" : "#E5E8EC")
    readonly property color surfaceSelected: followSystem ? mix(surface, accent, dark ? 0.2 : 0.14)
                                                          : (dark ? "#232735" : "#E8EDFD")
    readonly property color display: dark ? "#060709" : "#FCFCFD"         // instrument display areas (fixed)
    readonly property color displayBorder: dark ? "#1B1E25" : "#DFE2E8"
    readonly property color displayGrid: dark ? "#1A1F24" : "#E7EAEE"
    readonly property color border: followSystem ? mix(surface, systemText, dark ? 0.14 : 0.14)
                                                 : (dark ? "#23262E" : "#E1E4E9")
    readonly property color borderStrong: followSystem ? mix(surface, systemText, dark ? 0.26 : 0.26)
                                                       : (dark ? "#323641" : "#C9CED6")
    readonly property color divider: followSystem ? mix(surface, systemText, 0.09)
                                                  : (dark ? "#1C1F25" : "#E8EAEE")
    readonly property color overlay: dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(0.07, 0.08, 0.11, 0.25)
    readonly property color shadow: dark ? Qt.rgba(0, 0, 0, 0.65) : Qt.rgba(0.09, 0.11, 0.16, 0.16)

    // --- Text ------------------------------------------------------------------------------
    readonly property color text: followSystem ? systemText : (dark ? "#E7E9EE" : "#14161B")
    readonly property color textSecondary: followSystem ? mix(systemText, surface, 0.25) : (dark ? "#A1A7B3" : "#4B5261")
    readonly property color textMuted: followSystem ? mix(systemText, surface, 0.46) : (dark ? "#6B717E" : "#7F8695")
    readonly property color textDisabled: followSystem ? mix(systemText, surface, 0.68) : (dark ? "#454A55" : "#B4B9C3")
    readonly property color textOnAccent: onColor(accent)

    // --- Accent and status -------------------------------------------------------------------
    // The platform accent (or highlight) color, nudged until it is readable on the surfaces.
    readonly property color accent: platformProvided ? readable(systemAccent, surface, 3) : (dark ? "#6D8BFF" : "#3A5BD9")
    readonly property color accentHover: dark ? mix(accent, "#FFFFFF", 0.2) : mix(accent, "#000000", 0.14)
    readonly property color accentPressed: dark ? mix(accent, "#000000", 0.14) : mix(accent, "#000000", 0.28)
    readonly property color accentMuted: alpha(accent, dark ? 0.18 : 0.11)
    readonly property color focusRing: accentHover
    readonly property color success: dark ? "#3DD68C" : "#0F9D58"
    readonly property color warning: dark ? "#F2B340" : "#B86E00"
    readonly property color danger: dark ? "#F0625D" : "#D7372F"
    readonly property color info: accent
    readonly property color simulated: dark ? "#E9C46A" : "#9A6B00"      // SIMULATED tag

    // Run / stop buttons.
    readonly property color run: dark ? "#2FBF71" : "#13A05A"
    readonly property color runHover: dark ? "#3AD383" : "#0E8F4F"
    readonly property color stop: dark ? "#E5534B" : "#D33A31"
    readonly property color stopHover: dark ? "#F0655D" : "#BC2F27"

    // --- Channel colors (consistent everywhere: traces, chips, inspector sections) -----------
    readonly property color ch0: dark ? "#4ADE80" : "#169C4A"     // CH 0 / AI 0 / AO 0: green
    readonly property color ch1: dark ? "#22D3EE" : "#0888A8"     // CH 1 / AI 1 / AO 1: cyan
    readonly property color math: dark ? "#FBBF24" : "#C27803"    // math: amber
    readonly property color ref: dark ? "#A78BFA" : "#7646E0"     // reference: violet
    readonly property color cursor1: dark ? "#FDE047" : "#A98300"
    readonly property color cursor2: dark ? "#F472B6" : "#C8277A"
    readonly property var channelColors: [ch0, ch1, math, ref]
    function channelColor(index: int): color {
        return channelColors[Math.max(0, index) % channelColors.length]
    }

    // --- Spacing, radii, sizes ---------------------------------------------------------------
    readonly property int spacingXxs: 2
    readonly property int spacingXs: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXl: 24
    readonly property int spacingXxl: 32

    readonly property int radiusXs: 3
    readonly property int radiusS: 5
    readonly property int radiusM: 7
    readonly property int radiusL: 10
    readonly property int radiusXl: 14

    readonly property int controlHeight: 28      // buttons, fields, combos
    readonly property int controlHeightS: 22     // compact
    readonly property int controlHeightL: 34     // run button, prominent actions
    readonly property int iconSize: 16
    readonly property int iconSizeS: 14
    readonly property int iconSizeL: 20
    readonly property int borderWidth: 1
    readonly property int focusWidth: 2

    readonly property int navRailWidth: 68
    readonly property int topBarHeight: 48
    readonly property int statusBarHeight: 26
    readonly property int inspectorWidth: 320

    // --- Typography --------------------------------------------------------------------------
    readonly property string fontFamily: Fonts.uiFamily
    readonly property string monoFamily: Fonts.monoFamily
    readonly property int fontSizeXs: 10      // micro labels, badges
    readonly property int fontSizeS: 11       // captions, field labels
    readonly property int fontSizeM: 12       // secondary text
    readonly property int fontSizeBody: 13    // default text and controls
    readonly property int fontSizeL: 14       // section titles
    readonly property int fontSizeXl: 16      // panel titles
    readonly property int fontSizeTitle: 20   // page titles
    readonly property int fontSizeHeadline: 26
    readonly property int fontSizeDisplay: 48 // big readouts
    readonly property var tabularFigures: ({ "tnum": 1 })

    // --- Motion ------------------------------------------------------------------------------
    readonly property int durationFast: animations ? 110 : 0
    readonly property int durationNormal: animations ? 180 : 0
    readonly property int durationSlow: animations ? 280 : 0
    readonly property int easing: Easing.OutCubic

    // --- Helpers -----------------------------------------------------------------------------
    // Shortcut text for tooltips and key hints, from a portable sequence ("Ctrl+Shift+F"): on
    // macOS the native symbols ("⇧⌘F", Ctrl = Command in Qt), elsewhere the sequence itself.
    property bool macShortcuts: Qt.platform.os === "osx" || Qt.platform.os === "macos"
    function keys(sequence: string): string {
        if (!macShortcuts || sequence.length === 0)
            return sequence
        const parts = sequence.split("+")
        let key = parts.pop()
        if (key === "" && parts.length > 0 && parts[parts.length - 1] === "") {   // "Ctrl++"
            parts.pop()
            key = "+"
        }
        let mods = ""
        if (parts.indexOf("Meta") >= 0)
            mods += "⌃"
        if (parts.indexOf("Alt") >= 0)
            mods += "⌥"
        if (parts.indexOf("Shift") >= 0)
            mods += "⇧"
        if (parts.indexOf("Ctrl") >= 0)
            mods += "⌘"
        return mods + key
    }
    // Icon URL from a name in assets/icons (or a URL passed through).
    function icon(name: string): url {
        if (name.length === 0)
            return ""
        if (name.indexOf(":") >= 0 || name.indexOf("/") >= 0)
            return name
        return "qrc:/qt/qml/MyBench/Controls/icons/" + name + ".svg"
    }
    // Relative luminance (WCAG) and contrast ratio of two colors.
    function luminance(c: color): real {
        const lin = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
    }
    function contrast(a: color, b: color): real {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    // `c` moved toward white (dark) / black (light) until it has `ratio` contrast on `bg`.
    function readable(c: color, bg: color, ratio: real): color {
        const target = luminance(bg) < 0.4 ? "#FFFFFF" : "#000000"
        let result = c
        for (let i = 0; i < 12 && contrast(result, bg) < ratio; ++i)
            result = mix(result, target, 0.12)
        return result
    }
    function alpha(c: color, a: real): color {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
    function mix(a: color, b: color, t: real): color {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
    }
    // Background tint of a colored element (chips, badges, section headers).
    function tint(c: color): color {
        return alpha(c, dark ? 0.16 : 0.12)
    }
    // Readable color on top of `c` (white or near-black).
    function onColor(c: color): color {
        const luminance = 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b
        return luminance > 0.6 ? "#0B0C0F" : "#FFFFFF"
    }
}
