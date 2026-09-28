pragma Singleton
import QtQuick

// Shortcut text of the Waveform Editor's menus and tooltips: "Ctrl+Shift+S" shows as "⇧⌘S" on
// macOS (Qt maps Ctrl to the Command key there).
QtObject {
    readonly property bool mac: Qt.platform.os === "osx" || Qt.platform.os === "macos"

    function of(sequence: string): string {
        if (!mac || sequence.length === 0)
            return sequence
        return sequence.replace("Ctrl+Shift+", "⇧⌘").replace("Ctrl+", "⌘").replace("Shift+", "⇧")
    }
}
