import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.Label {
    id: control

    color: enabled ? Theme.text : Theme.textDisabled
    linkColor: Theme.accent
    font.pixelSize: Theme.fontSizeBody
}
