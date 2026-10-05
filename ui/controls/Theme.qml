pragma Singleton
import QtQuick
import MyBench.Framework

// MYBENCH design tokens (docs/UI.md §2–§4, contract §13.1). Every color, size and duration used by
// the UI comes from here; never hard-code them.
//
// Colors derive from the *effective palette*: the system palette when its lightness matches the
// chosen mode, otherwise a built-in light or dark reference palette (offscreen, CI, KDE schemes
// that report no color scheme, a forced mode opposite to the desktop). Displays (plots, readouts)
// follow the theme; `alwaysDarkDisplays` keeps them dark in light mode.
//
// Names marked "alias" exist only so code that has not migrated to v2 keeps building.
QtObject {
    id: theme

    // --- Mode ----------------------------------------------------------------------------------
    // "system" (follow the platform), "light" or "dark" (the default). ThemeSync feeds it and writes
    // Application.styleHints.colorScheme for a forced mode.
    property string mode: "dark"
    property bool alwaysDarkDisplays: false
    // Animations can be turned off globally (screenshots, reduced motion).
    property bool animations: true
    // macOS unified title bar (the toolbar then sits beside the traffic lights).
    property bool unifiedTitleBar: false
    // Screenshot and test mode (docs/UI.md §13.9): ComboBox lists, menus and context menus stay
    // in-window popups (Popup.Item); otherwise menus are native and lists are popup windows.
    property bool inlinePopups: AppInfo.offscreen || AppInfo.screenshot || Qt.platform.pluginName === "offscreen"
    // Standard controls are drawn by a platform style (macOS, FluentWinUI3, Fusion, KDE) rather than
    // MyBench.Style (Preferences > Appearance > Controls, --style). Custom parts (knobs, cards,
    // picture buttons) then take their colors from the system palette, like the native controls.
    readonly property bool nativeControls: AppInfo.nativeControls

    // The platform palette (QPalette through SystemPalette). Overridable, so tests and
    // `--system-palette` can play any platform. `platformProvided` is false where Qt reports no
    // color scheme (offscreen, CI, some KDE schemes): the reference palettes are used there.
    property color systemWindow: sysPalette.window
    property color systemBase: sysPalette.base
    property color systemText: sysPalette.windowText
    property color systemAccent: sysPalette.accent
    property color systemButton: sysPalette.button
    property color systemHighlight: sysPalette.highlight
    property color systemHighlightedText: sysPalette.highlightedText
    property color systemDisabledText: sysDisabledPalette.windowText
    property bool platformProvided: Qt.platform.pluginName !== "offscreen"
                                    && Application.styleHints.colorScheme !== Qt.Unknown
    // The desktop's own look: its color scheme, or the palette's lightness when it reports none.
    readonly property bool systemDark: Application.styleHints.colorScheme === Qt.Dark
                                       || (Application.styleHints.colorScheme === Qt.Unknown && luminance(systemWindow) < 0.5)
    readonly property bool dark: mode === "dark" || (mode !== "light" && systemDark)
    readonly property bool light: !dark
    // The system palette is used only when it matches the chosen light/dark look.
    readonly property bool followSystem: platformProvided && (luminance(systemWindow) < 0.5) === dark
    readonly property bool displayDark: dark || alwaysDarkDisplays

    property SystemPalette sysPalette: SystemPalette { colorGroup: SystemPalette.Active }
    property SystemPalette sysDisabledPalette: SystemPalette { colorGroup: SystemPalette.Disabled }

    // --- Reference palettes (what screenshots show; neutral, close to macOS and Breeze) --------
    readonly property color referenceDarkBase: "#1E1E1E"
    readonly property color referenceDarkText: "#E8E8E8"
    readonly property color referenceWindow: dark ? "#282828" : "#E8E9EC"
    readonly property color referenceBase: dark ? referenceDarkBase : "#FFFFFF"
    readonly property color referenceText: dark ? referenceDarkText : "#1F1F1F"
    readonly property color referenceButton: dark ? "#3A3A3A" : "#FFFFFF"
    readonly property color referenceAccent: dark ? "#2A72D9" : "#0A64D8"

    // --- Effective palette ---------------------------------------------------------------------
    readonly property color paletteWindow: followSystem ? systemWindow : referenceWindow
    readonly property color paletteBase: followSystem ? systemBase : referenceBase
    readonly property color paletteText: followSystem ? systemText : referenceText
    readonly property color paletteButton: followSystem ? systemButton : referenceButton
    // The user's accent color is kept even when the reference palette is shown.
    readonly property color paletteAccent: platformProvided ? systemAccent : referenceAccent
    readonly property color paletteHighlight: followSystem ? systemHighlight : accent
    readonly property color paletteHighlightedText: followSystem ? systemHighlightedText : accentText

    // --- Chrome --------------------------------------------------------------------------------
    readonly property color window: paletteWindow                    // toolbar, pane headers, inspector, status bar
    readonly property color sidebar: dark ? mix(window, "#000000", 0.04) : mix(window, paletteText, 0.04)
    readonly property color base: paletteBase                        // fields, tables, Overview, popups
    readonly property color separator: alpha(text, dark ? 0.16 : 0.15)   // every 1 px hairline
    readonly property color controlFill: paletteButton
    readonly property color controlBorder: alpha(text, dark ? 0.22 : 0.26)
    readonly property color hoverFill: alpha(text, 0.05)
    readonly property color pressedFill: alpha(text, 0.09)
    // Instrument controls (Knob, ChannelCard, MeasurementCard, ToolPanel).
    readonly property color cardFill: base
    readonly property color cardBorder: separator
    readonly property color accentSoft: alpha(accent, dark ? 0.20 : 0.12)     // pressed tool, picked item
    readonly property color knobFace: dark ? mix(base, text, 0.13) : mix(base, text, 0.015)
    readonly property color knobFaceHover: dark ? mix(base, text, 0.17) : mix(base, text, 0.04)
    readonly property color knobFacePressed: dark ? mix(base, text, 0.21) : mix(base, text, 0.07)
    readonly property color knobRing: alpha(text, dark ? 0.30 : 0.38)
    readonly property color knobRingHover: alpha(text, dark ? 0.45 : 0.48)
    readonly property color knobTick: textTertiary
    readonly property color selection: paletteHighlight
    readonly property color selectionText: contrast(paletteHighlightedText, selection) >= 4.5 ? paletteHighlightedText
                                                                                              : onColor(selection)
    readonly property color selectionInactive: alpha(text, 0.10)
    readonly property color overlay: Qt.rgba(0, 0, 0, dark ? 0.45 : 0.22)   // behind modal dialogs
    readonly property color shadowColor: Qt.rgba(0, 0, 0, dark ? 0.22 : 0.12)

    // --- Text ----------------------------------------------------------------------------------
    readonly property color text: paletteText
    readonly property color textSecondary: readable(mix(text, window, 0.30), window, 4.5)
    readonly property color textTertiary: readable(mix(text, window, 0.5), window, 3)
    readonly property color textDisabled: followSystem && contrast(systemDisabledText, window) < 0.7 * contrast(text, window)
                                          ? systemDisabledText : mix(text, window, 0.62)

    // --- Accent --------------------------------------------------------------------------------
    // The platform accent, adjusted to ≥ 3:1 on the window; where possible also darkened so white
    // text on it reaches 4.5:1 (otherwise the text on it is near-black).
    readonly property color accent: accentFor(paletteAccent, window)
    readonly property color accentText: contrast("#FFFFFF", accent) >= 4.5 ? "#FFFFFF"
                                      : contrast("#0B0B0D", accent) >= contrast("#FFFFFF", accent) ? "#0B0B0D" : "#FFFFFF"
    readonly property color accentHover: dark ? mix(accent, "#FFFFFF", 0.12) : mix(accent, "#000000", 0.10)
    readonly property color accentPressed: dark ? mix(accent, "#000000", 0.15) : mix(accent, "#000000", 0.20)
    readonly property color accentMuted: alpha(accent, dark ? 0.22 : 0.14)
    readonly property color focusRing: accent
    readonly property color link: accent

    // --- Status (state dots, banner icons, invalid field borders; never fills) ------------------
    readonly property color positive: dark ? "#3FB950" : "#1A7F37"
    readonly property color caution: dark ? "#D29922" : "#9A6700"
    readonly property color negative: dark ? "#F85149" : "#CF222E"
    // The Run / Stop button fills (round 5): green to start, red to stop, white text ≥ 4.5:1.
    readonly property color runFill: "#1F883D"
    readonly property color stopFill: "#CF222E"

    // --- Displays (plots, readout surfaces) -----------------------------------------------------
    readonly property color display: !displayDark ? base
                                   : dark ? mix(base, "#000000", 0.35) : mix(referenceDarkBase, "#000000", 0.35)
    readonly property color displayText: displayDark && !dark ? referenceDarkText : text
    readonly property color displayTextSecondary: readable(mix(displayText, display, 0.30), display, 4.5)
    readonly property color plotGridMajor: alpha(displayText, 0.14)
    readonly property color plotGridMinor: alpha(displayText, 0.06)
    readonly property color plotFrame: alpha(displayText, 0.22)

    // --- Signal palette (channel identity only; on displays) ------------------------------------
    readonly property color signalCh0: displayDark ? "#4CC38A" : "#1E8E5A"    // CH0 / AI0 / Audio L in
    readonly property color signalCh1: displayDark ? "#3FB8E0" : "#0B7FAB"    // CH1 / AI1 / Audio R in
    readonly property color signalAo0: displayDark ? "#F2994A" : "#C25E0A"    // AO0 / Audio L out
    readonly property color signalAo1: displayDark ? "#E26FC0" : "#B4318F"    // AO1 / Audio R out
    readonly property color signalMath: displayDark ? "#E8C547" : "#9A7A00"
    readonly property color signalRef: displayDark ? "#A78BFA" : "#6D4FD8"
    readonly property color signalCursor: displayText                          // dashed and labelled
    // Cursor pairs (C1/C2, A/B): two hues no channel uses, so the pair reads apart at a glance.
    readonly property color signalCursor1: displayDark ? "#FF7A7A" : "#C62F3A"
    readonly property color signalCursor2: displayDark ? "#7FA7FF" : "#183F8C"
    // Default trace colors of plots (Plot.seriesColors), in order.
    readonly property var seriesColors: [signalCh0, signalCh1, signalMath, signalRef, signalAo1, signalAo0]

    function signalColor(name: string): color {
        switch (name) {
        case "ai0": case "ch0": case "audioInL": return signalCh0
        case "ai1": case "ch1": case "audioInR": return signalCh1
        case "ao0": case "audioOutL": return signalAo0
        case "ao1": case "audioOutR": return signalAo1
        case "math": return signalMath
        case "ref": return signalRef
        case "cursor": return signalCursor
        case "cursor1": return signalCursor1
        case "cursor2": return signalCursor2
        }
        return displayText
    }

    // --- Typography ----------------------------------------------------------------------------
    readonly property string fontFamily: Fonts.uiFamily
    readonly property string monoFamily: Fonts.monoFamily
    // The platform UI font size (only offscreen screenshots force 13 px).
    readonly property int fontSizeBase: Fonts.uiPixelSize
    readonly property int fontSizeCaption: Math.max(10, fontSizeBase - 2)
    readonly property int fontSizeSmall: fontSizeBase - 1
    readonly property int fontSizeBody: fontSizeBase
    readonly property int fontSizeHeading: fontSizeBase + 1           // DemiBold
    readonly property int fontSizeTitle: fontSizeBase + 5             // DemiBold
    readonly property int fontSizeReadoutM: fontSizeBase + 7
    readonly property int fontSizeReadoutL: 2 * fontSizeBase
    readonly property int fontSizeReadoutXL: Math.round(4.5 * fontSizeBase)   // Light
    readonly property var tabularFigures: ({ "tnum": 1 })
    property FontMetrics bodyMetrics: FontMetrics {
        font.family: theme.fontFamily
        font.pixelSize: theme.fontSizeBody
    }
    // Height of a line of body text; control heights derive from it.
    readonly property int lineHeight: Math.ceil(bodyMetrics.height)

    // --- Metrics -------------------------------------------------------------------------------
    readonly property int spacingXs: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXl: 24
    readonly property int formRowSpacing: 6        // between the rows of a form (Section content)

    readonly property int controlHeightS: lineHeight + 6     // plot toolbars, dense tables
    // Our fields, steppers and segmented controls match the standard controls beside them: the
    // platform style's regular control height when it draws them (macOS 24), else lineHeight + 10.
    readonly property int nativeControlHeight: nativeControls && AppInfo.controlsStyle === "macOS" ? 24 : 0
    readonly property int controlHeight: nativeControlHeight > 0 ? nativeControlHeight : lineHeight + 10
    readonly property int controlHeightL: lineHeight + 16    // the Run button, dialog default buttons
    readonly property int iconSize: 16
    readonly property int iconSizeL: 20
    readonly property int radiusControl: 5
    readonly property int radiusPopup: 8          // popups, menus, dialogs, Overview tiles
    readonly property int borderWidth: 1
    readonly property int focusWidth: 2

    readonly property int toolbarHeight: unifiedTitleBar && macShortcuts ? 52 : 44
    readonly property int paneHeaderHeight: 40
    readonly property int statusBarHeight: 24
    readonly property int sidebarWidth: 220
    readonly property int sidebarCompactWidth: 56
    readonly property int inspectorWidth: 300
    readonly property int inspectorMinWidth: 260
    readonly property int inspectorMaxWidth: 420
    readonly property int minWindowWidth: 640
    readonly property int minWindowHeight: 480
    readonly property int splitMinWidth: 900
    readonly property int paneMinWidth: 450
    readonly property int shortHeight: 600
    // Instrument panels (round 2, docs/UI.md §13.8b): the column of cards and knobs right of the
    // display, its width below `instrumentCompactWidth` (pane width), and the floating tool panels.
    readonly property int instrumentColumnWidth: 260
    readonly property int instrumentColumnCompactWidth: 220
    readonly property int instrumentCompactWidth: 1000
    readonly property int toolPanelWidth: 300

    // Adaptive layout class of a window width (docs/UI.md §8).
    function layoutClass(width: real): string {
        if (width >= 1280)
            return "wide"
        if (width >= 1024)
            return "medium"
        if (width >= 760)
            return "narrow"
        return "compact"
    }

    // --- Elevation and motion ------------------------------------------------------------------
    // One shadow for popups, menus, dialogs and the overlay inspector (MultiEffect: blurMax =
    // shadowBlur, shadowBlur 1.0, shadowVerticalOffset = shadowOffsetY, shadowColor).
    readonly property int shadowBlur: 16
    readonly property int shadowOffsetY: 4
    readonly property int durationFast: animations ? 120 : 0      // state changes
    readonly property int durationNormal: animations ? 200 : 0    // layout
    readonly property int easing: Easing.OutCubic
    readonly property int toolTipDelay: 600                        // every tooltip

    // --- Aliases (v1 names; removed in the cleanup phase) ---------------------------------------
    readonly property color chrome: window
    readonly property color surface: window
    readonly property color surfaceRaised: base
    readonly property color surfaceSunken: base
    readonly property color surfaceHover: hoverFill
    readonly property color surfacePressed: pressedFill
    readonly property color surfaceSelected: selectionInactive
    readonly property color border: separator
    readonly property color divider: separator
    readonly property color displayBorder: plotFrame
    readonly property color borderStrong: controlBorder
    readonly property color displayGrid: plotGridMajor
    readonly property color textMuted: textTertiary
    readonly property color textOnAccent: accentText
    readonly property color shadow: shadowColor
    readonly property color success: positive
    readonly property color warning: caution
    readonly property color danger: negative
    readonly property color info: accent
    readonly property color simulated: textSecondary
    readonly property color run: accent
    readonly property color runHover: accentHover
    readonly property color stop: textSecondary
    readonly property color stopHover: text
    readonly property color ch0: signalCh0
    readonly property color ch1: signalCh1
    readonly property color math: signalMath
    readonly property color ref: signalRef
    readonly property color cursor1: signalCursor1
    readonly property color cursor2: signalCursor2
    readonly property var channelColors: [ch0, ch1, math, ref]
    function channelColor(index: int): color {
        return channelColors[Math.max(0, index) % channelColors.length]
    }
    readonly property int spacingXxs: 2
    readonly property int spacingXxl: 24
    readonly property int radiusXs: 3
    readonly property int radiusS: radiusControl
    readonly property int radiusM: radiusControl
    readonly property int radiusL: radiusPopup
    readonly property int radiusXl: radiusPopup
    readonly property int iconSizeS: 14
    readonly property int navRailWidth: sidebarCompactWidth
    readonly property int topBarHeight: toolbarHeight
    readonly property int fontSizeXs: fontSizeCaption
    readonly property int fontSizeS: fontSizeCaption
    readonly property int fontSizeM: fontSizeSmall
    readonly property int fontSizeL: fontSizeHeading
    readonly property int fontSizeXl: fontSizeHeading
    readonly property int fontSizeHeadline: fontSizeReadoutM
    readonly property int fontSizeDisplay: fontSizeReadoutXL
    readonly property int durationSlow: durationNormal

    // --- Helpers -------------------------------------------------------------------------------
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
    // Relative luminance (WCAG) and contrast ratio of two opaque colors.
    function luminance(c: color): real {
        const lin = v => v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4)
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
    }
    function contrast(a: color, b: color): real {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
    // A translucent color painted over an opaque one.
    function composite(c: color, background: color): color {
        return mix(Qt.rgba(background.r, background.g, background.b, 1), Qt.rgba(c.r, c.g, c.b, 1), c.a)
    }
    // `c` moved toward white (dark) / black (light) until it has `ratio` contrast on `bg`.
    function readable(c: color, bg: color, ratio: real): color {
        const target = luminance(bg) < 0.4 ? "#FFFFFF" : "#000000"
        let result = c
        for (let i = 0; i < 16 && contrast(result, bg) < ratio; ++i)
            result = mix(result, target, 0.12)
        return result
    }
    function accentFor(c: color, bg: color): color {
        const visible = readable(c, bg, 3)
        if (contrast("#FFFFFF", visible) >= 4.5)
            return visible
        let deeper = visible
        for (let i = 0; i < 24 && contrast("#FFFFFF", deeper) < 4.5; ++i)
            deeper = mix(deeper, "#000000", 0.05)
        return contrast(deeper, bg) >= 3 ? deeper : visible
    }
    function alpha(c: color, a: real): color {
        return Qt.rgba(c.r, c.g, c.b, a)
    }
    function mix(a: color, b: color, t: real): color {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t)
    }
    // Subtle background of a colored element.
    function tint(c: color): color {
        return alpha(c, dark ? 0.16 : 0.12)
    }
    // Readable color on top of `c` (white or near-black).
    function onColor(c: color): color {
        return contrast("#FFFFFF", c) >= contrast("#0B0B0D", c) ? "#FFFFFF" : "#0B0B0D"
    }
}
