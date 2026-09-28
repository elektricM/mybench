import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    padding: 6
    leftPadding: 9
    rightPadding: 9
    hoverEnabled: true
    color: enabled ? Theme.text : Theme.textDisabled
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text
    placeholderTextColor: Theme.textMuted
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true

    ContextMenu.menu: TextEditMenu {
        editor: control
    }

    PlaceholderText {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        elide: Text.ElideRight
        renderType: control.renderType
    }

    background: Rectangle {
        implicitWidth: 160
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusM
        color: control.enabled ? Theme.surfaceSunken : Theme.alpha(Theme.surfaceSunken, 0.5)
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.borderStrong : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
    }
}
