import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Window of the MYBENCH style: Theme background, font and a palette derived from the Theme so
// that every control (including Qt's own dialogs) follows light/dark.
T.ApplicationWindow {
    id: window

    color: Theme.window
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeBody

    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.base: Theme.surfaceSunken
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surfaceRaised
    palette.buttonText: Theme.text
    palette.brightText: "#FFFFFF"
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.textOnAccent
    palette.link: Theme.accent
    palette.linkVisited: Theme.accentPressed
    palette.placeholderText: Theme.textMuted
    palette.toolTipBase: Theme.surfaceRaised
    palette.toolTipText: Theme.text
    palette.light: Theme.surfaceHover
    palette.midlight: Theme.surfacePressed
    palette.mid: Theme.borderStrong
    palette.dark: Theme.border
    palette.shadow: Theme.shadow
    palette.accent: Theme.accent
    palette.disabled.text: Theme.textDisabled
    palette.disabled.windowText: Theme.textDisabled
    palette.disabled.buttonText: Theme.textDisabled

    Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
}
