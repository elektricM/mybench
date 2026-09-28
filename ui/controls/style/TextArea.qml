import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

T.TextArea {
    id: control

    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding,
                            implicitBackgroundWidth + leftInset + rightInset,
                            placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight + topInset + bottomInset,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    padding: 8
    hoverEnabled: true
    color: enabled ? Theme.text : Theme.textDisabled
    selectionColor: Theme.alpha(Theme.accent, 0.35)
    selectedTextColor: Theme.text
    placeholderTextColor: Theme.textMuted
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
        implicitWidth: 200
        implicitHeight: 60
        radius: Theme.radiusM
        color: Theme.surfaceSunken
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.borderStrong : Theme.border
    }
}
