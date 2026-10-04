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

    function applyColorScheme() {
        if (!writesColorScheme)
            return
        Application.styleHints.colorScheme = mode === "dark" ? Qt.Dark : mode === "light" ? Qt.Light : Qt.Unknown
    }
    onModeChanged: applyColorScheme()
    Component.onCompleted: applyColorScheme()

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
