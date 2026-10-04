import QtQuick
import QtQuick.Controls

// v1 compatibility (use Switch): the style Switch with the label on its right.
//     ToggleSwitch { text: qsTr("Null offset"); checked: ctl.relative; onToggled: ctl.relative = checked }
Switch {
    property color accentColor: Theme.accent   // v1, ignored (switches use the accent)
    property bool labelFirst: false            // v1, ignored (no caller uses it; the label is always on the right)
}
