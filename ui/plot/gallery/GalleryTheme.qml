// Colors of the gallery (a stand-in for MyBench.Controls' Theme).
import QtQuick
import MyBench.Plot

QtObject {
    property bool dark: true
    readonly property color window: dark ? "#0a0c10" : "#eceef2"
    readonly property color sidebar: dark ? "#0e1116" : "#f7f8fa"
    readonly property color card: dark ? "#12151b" : "#ffffff"
    readonly property color raised: dark ? "#181c23" : "#f3f5f8"
    readonly property color border: dark ? "#1f242d" : "#dfe3e9"
    readonly property color text: dark ? "#e8ebf0" : "#161a20"
    readonly property color muted: dark ? "#838d9a" : "#5f6875"
    readonly property color accent: dark ? "#4c8dff" : "#2563eb"
    readonly property color ch0: dark ? "#3ddc84" : "#16a34a"
    readonly property color ch1: dark ? "#22d3ee" : "#0891b2"
    readonly property color amber: dark ? "#fbbf24" : "#d97706"
    readonly property color violet: dark ? "#a78bfa" : "#7c3aed"
    readonly property color cursor1: dark ? "#ff8a65" : "#ea580c"
    readonly property color cursor2: dark ? "#f472b6" : "#db2777"
    readonly property int plotScheme: dark ? Plot.Dark : Plot.Light
}
