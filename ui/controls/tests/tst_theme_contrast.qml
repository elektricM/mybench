import QtQuick
import QtTest
import MyBench.Controls

// Theme v2 (docs/UI.md §2): effective palette (system palette when it matches the mode, otherwise
// the reference palettes), forced modes, displays following the theme, and readable contrast on
// the reference palettes and on played platform palettes (Breeze, macOS), light and dark.
TestCase {
    id: test
    name: "ThemeContrast"

    // window, base, text, accent, button, highlight
    readonly property var palettes: [
        { name: "breeze-light", dark: false, colors: ["#eff0f1", "#fcfcfc", "#232629", "#3daee9", "#fcfcfc", "#3daee9"] },
        { name: "breeze-dark", dark: true, colors: ["#31363b", "#232629", "#eff0f1", "#3daee9", "#3b4045", "#3daee9"] },
        { name: "macos-light", dark: false, colors: ["#ececec", "#ffffff", "#1d1d1f", "#007aff", "#ffffff", "#0064e1"] },
        { name: "macos-dark", dark: true, colors: ["#323232", "#1e1e1e", "#ffffff", "#0a84ff", "#5a5a5a", "#0058d0"] }
    ]

    function play(p: var) {
        Theme.platformProvided = true
        Theme.systemWindow = p.colors[0]
        Theme.systemBase = p.colors[1]
        Theme.systemText = p.colors[2]
        Theme.systemAccent = p.colors[3]
        Theme.systemButton = p.colors[4]
        Theme.systemHighlight = p.colors[5]
        Theme.systemHighlightedText = "#ffffff"
        Theme.systemDisabledText = Theme.mix(p.colors[2], p.colors[0], 0.6)
        Theme.mode = p.dark ? "dark" : "light"
    }
    function reference(dark: bool) {
        Theme.platformProvided = false
        Theme.mode = dark ? "dark" : "light"
    }

    function init() {
        Theme.alwaysDarkDisplays = false
        Theme.mode = "system"
    }
    function cleanup() {
        Theme.alwaysDarkDisplays = false
        Theme.platformProvided = false
        Theme.mode = "system"
    }

    function ratio(fg: color, bg: color): real {
        return Theme.contrast(Theme.composite(fg, bg), bg)
    }
    // CIE76 distance in L*a*b* (D65).
    function lab(c: color): var {
        const lin = v => v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        const r = lin(c.r), g = lin(c.g), b = lin(c.b)
        const x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
        const y = 0.2126 * r + 0.7152 * g + 0.0722 * b
        const z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
        const f = t => t > 0.008856 ? Math.cbrt(t) : 7.787 * t + 16 / 116
        return [116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))]
    }
    function distance(a: color, b: color): real {
        const la = lab(a), lb = lab(b)
        return Math.hypot(la[0] - lb[0], la[1] - lb[1], la[2] - lb[2])
    }

    // The contrast rules of docs/UI.md §2 for the current effective palette.
    function checkChrome(label: string) {
        for (const surface of [Theme.window, Theme.base]) {
            verify(ratio(Theme.text, surface) >= 7, label + ": text " + ratio(Theme.text, surface).toFixed(2))
            verify(ratio(Theme.textSecondary, surface) >= 4.5, label + ": textSecondary " + ratio(Theme.textSecondary, surface).toFixed(2))
            verify(ratio(Theme.textTertiary, surface) >= 3, label + ": textTertiary " + ratio(Theme.textTertiary, surface).toFixed(2))
        }
        verify(ratio(Theme.accent, Theme.window) >= 3, label + ": accent " + ratio(Theme.accent, Theme.window).toFixed(2))
        verify(ratio(Theme.accentText, Theme.accent) >= 4.5, label + ": accentText " + ratio(Theme.accentText, Theme.accent).toFixed(2))
        verify(ratio(Theme.focusRing, Theme.window) >= 3, label + ": focus ring")
        verify(ratio(Theme.focusRing, Theme.base) >= 3, label + ": focus ring on base")
        verify(ratio(Theme.separator, Theme.window) >= 1.2, label + ": separator " + ratio(Theme.separator, Theme.window).toFixed(2))
        verify(ratio(Theme.separator, Theme.base) >= 1.2, label + ": separator on base")
        verify(ratio(Theme.controlBorder, Theme.base) >= 1.5, label + ": control border")
        verify(ratio(Theme.selectionText, Theme.selection) >= 4.5, label + ": selection text")
        // The sidebar is a distinct but close shade of the window.
        verify(!Qt.colorEqual(Theme.sidebar, Theme.window), label + ": sidebar")
        // Display text and axis labels on the display.
        verify(ratio(Theme.displayText, Theme.display) >= 7, label + ": display text")
        verify(ratio(Theme.displayTextSecondary, Theme.display) >= 4.5, label + ": display text secondary")
        verify(ratio(Theme.plotGridMajor, Theme.display) > ratio(Theme.plotGridMinor, Theme.display), label + ": grid")
        verify(ratio(Theme.plotGridMinor, Theme.display) >= 1.05, label + ": minor grid visible")
    }
    function checkSignals(label: string) {
        const signals = [Theme.signalCh0, Theme.signalCh1, Theme.signalAo0, Theme.signalAo1, Theme.signalMath, Theme.signalRef]
        for (let i = 0; i < signals.length; ++i) {
            verify(ratio(signals[i], Theme.display) >= 3, label + ": signal " + i + " " + ratio(signals[i], Theme.display).toFixed(2))
            for (let j = i + 1; j < signals.length; ++j)
                verify(distance(signals[i], signals[j]) >= 25, label + ": signals " + i + "/" + j + " too close")
        }
        verify(ratio(Theme.signalCursor, Theme.display) >= 4.5, label + ": cursor")
        // The two cursors of a pair are told apart by color, and from every trace.
        const cursors = [Theme.cursor1, Theme.cursor2]
        verify(distance(cursors[0], cursors[1]) >= 25, label + ": cursor1/cursor2 too close")
        for (let c = 0; c < cursors.length; ++c) {
            verify(ratio(cursors[c], Theme.display) >= 3, label + ": cursor " + (c + 1) + " contrast")
            for (let i = 0; i < signals.length; ++i)
                verify(distance(cursors[c], signals[i]) >= 25, label + ": cursor " + (c + 1) + " / signal " + i + " too close")
        }
    }

    function test_referencePalettes_data() {
        return [{ tag: "light", dark: false }, { tag: "dark", dark: true }]
    }
    function test_referencePalettes(data) {
        reference(data.dark)
        compare(Theme.dark, data.dark)
        verify(!Theme.followSystem)
        checkChrome("reference " + data.tag)
        checkSignals("reference " + data.tag)
        // Light displays are the base color; dark ones are darker than the base.
        if (data.dark)
            verify(Theme.luminance(Theme.display) < Theme.luminance(Theme.base))
        else
            compare(Theme.display, Theme.base)
    }

    function test_playedPalettes_data() {
        return test.palettes.map(p => ({ tag: p.name, palette: p }))
    }
    function test_playedPalettes(data) {
        play(data.palette)
        verify(Theme.followSystem, data.tag)
        compare(Theme.window, Qt.color(data.palette.colors[0]))
        compare(Theme.base, Qt.color(data.palette.colors[1]))
        compare(Theme.text, Qt.color(data.palette.colors[2]))
        checkChrome(data.tag)
        checkSignals(data.tag)
    }

    function test_alwaysDarkDisplays_data() {
        return [{ tag: "reference", palette: null }, { tag: "macos-light", palette: test.palettes[2] },
                { tag: "breeze-light", palette: test.palettes[0] }]
    }
    function test_alwaysDarkDisplays(data) {
        if (data.palette)
            play(data.palette)
        else
            reference(false)
        const lightDisplay = Theme.display.toString()   // a copy, not a live reference
        verify(!Theme.displayDark)
        Theme.alwaysDarkDisplays = true
        verify(Theme.displayDark)
        verify(!Theme.dark)                      // the chrome stays light
        verify(Theme.luminance(Theme.display) < 0.05)
        verify(!Qt.colorEqual(Theme.display, lightDisplay))
        verify(Theme.luminance(Theme.displayText) > 0.5)
        checkSignals(data.tag + " dark displays")
        verify(ratio(Theme.text, Theme.window) >= 7)
    }

    // A forced mode opposite to the desktop uses the reference palette (never dark text on dark).
    function test_forcedOppositeUsesReference() {
        play(test.palettes[1])                   // Breeze dark desktop
        verify(Theme.followSystem)
        Theme.mode = "light"
        verify(!Theme.followSystem)
        verify(!Theme.dark)
        verify(Theme.luminance(Theme.window) > 0.6)
        checkChrome("breeze-dark forced light")
        // The user's accent color is kept, made readable.
        verify(ratio(Theme.accent, Theme.window) >= 3)
    }

    // Without a platform color scheme the reference palettes are used, also in "system" mode.
    function test_noPlatformUsesReference() {
        Theme.platformProvided = false
        Theme.systemWindow = "#31363b"
        Theme.mode = "dark"
        verify(!Theme.followSystem)
        compare(Theme.window, Qt.color("#282828"))
        Theme.mode = "light"
        compare(Theme.window, Qt.color("#E8E9EC"))
    }

    function test_systemModeFollowsPalette() {
        play(test.palettes[3])                   // macOS dark
        Theme.mode = "system"
        if (Application.styleHints.colorScheme === Qt.Unknown)
            verify(Theme.dark)                   // no reported scheme: the palette's lightness decides
        else
            compare(Theme.dark, Application.styleHints.colorScheme === Qt.Dark)
    }

    function test_displaysFollowTheme() {
        reference(false)
        verify(!Theme.displayDark)
        verify(Theme.luminance(Theme.display) > 0.8)
        compare(Theme.signalCh0, Qt.color("#1E8E5A"))
        reference(true)
        verify(Theme.displayDark)
        compare(Theme.signalCh0, Qt.color("#4CC38A"))
        compare(Theme.ch0, Theme.signalCh0)                   // alias
    }

    function test_signalColorNames() {
        reference(true)
        compare(Theme.signalColor("ai0"), Theme.signalCh0)
        compare(Theme.signalColor("audioInL"), Theme.signalCh0)
        compare(Theme.signalColor("ai1"), Theme.signalCh1)
        compare(Theme.signalColor("audioInR"), Theme.signalCh1)
        compare(Theme.signalColor("ao0"), Theme.signalAo0)
        compare(Theme.signalColor("audioOutL"), Theme.signalAo0)
        compare(Theme.signalColor("ao1"), Theme.signalAo1)
        compare(Theme.signalColor("audioOutR"), Theme.signalAo1)
        compare(Theme.signalColor("math"), Theme.signalMath)
        compare(Theme.signalColor("ref"), Theme.signalRef)
        compare(Theme.signalColor("cursor"), Theme.displayText)
        compare(Theme.seriesColors.length, 6)
        compare(Theme.seriesColors[0], Theme.signalCh0)
        compare(Theme.seriesColors[1], Theme.signalCh1)
    }

    function test_statusColors() {
        reference(true)
        compare(Theme.positive, Qt.color("#3FB950"))
        compare(Theme.negative, Qt.color("#F85149"))
        compare(Theme.danger, Theme.negative)                 // alias
        reference(false)
        compare(Theme.positive, Qt.color("#1A7F37"))
        compare(Theme.caution, Qt.color("#9A6700"))
        compare(Theme.warning, Theme.caution)
    }

    function test_metricsFromLineHeight() {
        verify(Theme.lineHeight >= Theme.fontSizeBody)
        compare(Theme.controlHeightS, Theme.lineHeight + 6)
        compare(Theme.controlHeight, Theme.lineHeight + 10)
        compare(Theme.controlHeightL, Theme.lineHeight + 16)
        compare(Theme.fontSizeCaption, Math.max(10, Theme.fontSizeBase - 2))
        compare(Theme.fontSizeHeading, Theme.fontSizeBase + 1)
        compare(Theme.fontSizeReadoutL, 2 * Theme.fontSizeBase)
        compare(Theme.fontSizeS, Theme.fontSizeCaption)       // alias
        compare(Theme.radiusM, Theme.radiusControl)
        compare(Theme.radiusL, Theme.radiusPopup)
    }

    function test_layoutClass() {
        compare(Theme.layoutClass(1440), "wide")
        compare(Theme.layoutClass(1280), "wide")
        compare(Theme.layoutClass(1279), "medium")
        compare(Theme.layoutClass(1024), "medium")
        compare(Theme.layoutClass(1023), "narrow")
        compare(Theme.layoutClass(760), "narrow")
        compare(Theme.layoutClass(759), "compact")
        compare(Theme.layoutClass(640), "compact")
    }

    function test_animationsOff() {
        const saved = Theme.animations
        Theme.animations = false
        compare(Theme.durationFast, 0)
        compare(Theme.durationNormal, 0)
        Theme.animations = true
        compare(Theme.durationFast, 120)
        compare(Theme.durationNormal, 200)
        Theme.animations = saved
    }
}
