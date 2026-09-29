import QtQuick
import MyBench.Framework

// Feeds the shell's appearance into Theme: the light/dark mode (`mode`), animations, and the
// developer override --system-palette "window,base,text,accent" (colors) that plays a platform palette
// (KDE Breeze, macOS...) for screenshots and tests. One per window (app shell, dev shells).
QtObject {
    id: sync

    property string mode: "system"
    readonly property var paletteColors: AppInfo.value("system-palette").split(",")
    readonly property bool hasPalette: paletteColors.length === 4

    property QtObject modeBinding: Binding { target: Theme; property: "mode"; value: sync.mode }
    property QtObject animationsBinding: Binding { target: Theme; property: "animations"; value: AppInfo.animations }
    property QtObject providedBinding: Binding { target: Theme; property: "platformProvided"; value: true; when: sync.hasPalette }
    property QtObject windowBinding: Binding { target: Theme; property: "systemWindow"; value: sync.paletteColors[0]; when: sync.hasPalette }
    property QtObject baseBinding: Binding { target: Theme; property: "systemBase"; value: sync.paletteColors[1]; when: sync.hasPalette }
    property QtObject textBinding: Binding { target: Theme; property: "systemText"; value: sync.paletteColors[2]; when: sync.hasPalette }
    property QtObject accentBinding: Binding { target: Theme; property: "systemAccent"; value: sync.paletteColors[3]; when: sync.hasPalette }
}
