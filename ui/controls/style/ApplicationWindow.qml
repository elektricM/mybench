import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Window of the MYBENCH style: the Theme window color and UI font, and a palette from the Theme so
// Qt's own items and dialogs follow light/dark.
T.ApplicationWindow {
    id: window

    color: Theme.window
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeBody

    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.base: Theme.base
    palette.alternateBase: Theme.composite(Theme.hoverFill, Theme.base)
    palette.text: Theme.text
    palette.button: Theme.controlFill
    palette.buttonText: Theme.text
    palette.brightText: Theme.accentText
    palette.highlight: Theme.selection
    palette.highlightedText: Theme.selectionText
    palette.link: Theme.link
    palette.linkVisited: Theme.accentPressed
    palette.placeholderText: Theme.textTertiary
    palette.toolTipBase: Theme.base
    palette.toolTipText: Theme.text
    palette.light: Theme.composite(Theme.hoverFill, Theme.window)
    palette.midlight: Theme.composite(Theme.pressedFill, Theme.window)
    palette.mid: Theme.composite(Theme.controlBorder, Theme.window)
    palette.dark: Theme.composite(Theme.separator, Theme.window)
    palette.shadow: Theme.shadowColor
    palette.accent: Theme.accent
    palette.disabled.text: Theme.textDisabled
    palette.disabled.windowText: Theme.textDisabled
    palette.disabled.buttonText: Theme.textDisabled
}
