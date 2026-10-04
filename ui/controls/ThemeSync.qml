import QtQuick
import MyBench.Framework

// Feeds the shell's appearance into Theme (one per window: app shell, dev shells, gallery):
// - `mode` ("system" | "light" | "dark"). A forced Light/Dark also sets
//   Application.styleHints.colorScheme so native dialogs, menus and the title bar follow; "system"
//   resets it. Not written offscreen (screenshots and tests show the reference palettes).
// - `alwaysDarkDisplays` (the "Always Use Dark Displays" preference; `--dark-displays`).
// - animations (off for screenshots and --no-animations).
// - the developer override `--system-palette "window,base,text,accent"`, which plays a platform
//   palette (KDE Breeze, macOS…) for screenshots and tests; button, highlight and the disabled text
//   are derived from those four colors.
QtObject {
    id: sync

    property string mode: "dark"
    property bool alwaysDarkDisplays: AppInfo.flag("dark-displays")
    readonly property var paletteColors: AppInfo.value("system-palette").split(",")
    readonly property bool hasPalette: paletteColors.length === 4
    readonly property bool paletteDark: hasPalette && Theme.luminance(paletteColors[0]) < 0.5
    readonly property bool writesColorScheme: Qt.platform.pluginName !== "offscreen" && !AppInfo.screenshot

    // Offscreen (screenshots, tests) the platform provides no dark palette: with a platform style
    // (Fusion there) the application palette is set from the theme so the standard controls match.
    readonly property bool pushesPalette: Theme.nativeControls
                                          && (Qt.platform.pluginName === "offscreen" || Qt.platform.pluginName === "minimal")
    readonly property var themePalette: ({
        window: Theme.window, windowText: Theme.text, base: Theme.base, alternateBase: Theme.mix(Theme.base, Theme.text, 0.04),
        text: Theme.text, button: Theme.controlFill, buttonText: Theme.text,
        highlight: Theme.selection, highlightedText: Theme.paletteHighlightedText, accent: Theme.accent,
        placeholderText: Theme.textSecondary, toolTipBase: Theme.base, toolTipText: Theme.text,
        light: Theme.mix(Theme.controlFill, "#FFFFFF", Theme.dark ? 0.12 : 0.6),
        midlight: Theme.mix(Theme.controlFill, "#FFFFFF", Theme.dark ? 0.06 : 0.3),
        mid: Theme.mix(Theme.controlFill, "#000000", Theme.dark ? 0.25 : 0.2),
        dark: Theme.mix(Theme.controlFill, "#000000", Theme.dark ? 0.45 : 0.35),
        shadow: "#000000", disabledText: Theme.textDisabled
    })
    function applyPalette() {
        if (pushesPalette)
            AppInfo.setApplicationPalette(themePalette)
    }
    onThemePaletteChanged: applyPalette()

    function applyColorScheme() {
        if (!writesColorScheme)
            return
        Application.styleHints.colorScheme = mode === "dark" ? Qt.Dark : mode === "light" ? Qt.Light : Qt.Unknown
    }
    onModeChanged: applyColorScheme()
    Component.onCompleted: { applyColorScheme(); applyPalette() }

    property QtObject modeBinding: Binding { target: Theme; property: "mode"; value: sync.mode }
    property QtObject darkDisplaysBinding: Binding { target: Theme; property: "alwaysDarkDisplays"; value: sync.alwaysDarkDisplays }
    property QtObject animationsBinding: Binding { target: Theme; property: "animations"; value: AppInfo.animations }
    property QtObject titleBarBinding: Binding { target: Theme; property: "unifiedTitleBar"; value: AppInfo.flag("unified-titlebar") }
    property QtObject providedBinding: Binding { target: Theme; property: "platformProvided"; value: true; when: sync.hasPalette }
    property QtObject windowBinding: Binding { target: Theme; property: "systemWindow"; value: sync.paletteColors[0]; when: sync.hasPalette }
    property QtObject baseBinding: Binding { target: Theme; property: "systemBase"; value: sync.paletteColors[1]; when: sync.hasPalette }
    property QtObject textBinding: Binding { target: Theme; property: "systemText"; value: sync.paletteColors[2]; when: sync.hasPalette }
    property QtObject accentBinding: Binding { target: Theme; property: "systemAccent"; value: sync.paletteColors[3]; when: sync.hasPalette }
    property QtObject buttonBinding: Binding {
        target: Theme; property: "systemButton"; when: sync.hasPalette
        value: !sync.hasPalette ? "transparent"
             : sync.paletteDark ? Theme.mix(sync.paletteColors[0], sync.paletteColors[2], 0.1) : sync.paletteColors[1]
    }
    property QtObject highlightBinding: Binding { target: Theme; property: "systemHighlight"; value: sync.paletteColors[3]; when: sync.hasPalette }
    property QtObject highlightedTextBinding: Binding {
        target: Theme; property: "systemHighlightedText"; when: sync.hasPalette
        value: sync.hasPalette ? Theme.onColor(sync.paletteColors[3]) : "transparent"
    }
    property QtObject disabledTextBinding: Binding {
        target: Theme; property: "systemDisabledText"; when: sync.hasPalette
        value: sync.hasPalette ? Theme.mix(sync.paletteColors[2], sync.paletteColors[0], 0.6) : "transparent"
    }
}
