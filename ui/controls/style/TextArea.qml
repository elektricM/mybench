import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// Multi-line text: the text field look.
T.TextArea {
    id: control

    implicitWidth: Math.max(contentWidth + leftPadding + rightPadding,
                            implicitBackgroundWidth + leftInset + rightInset,
                            placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             implicitBackgroundHeight + topInset + bottomInset,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    padding: Theme.spacingS
    topPadding: Theme.spacingXs + 2
    bottomPadding: Theme.spacingXs + 2
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody
    color: enabled ? Theme.text : Theme.textDisabled
    selectionColor: Theme.selection
    selectedTextColor: Theme.selectionText
    placeholderTextColor: Theme.textTertiary
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
        implicitHeight: 3 * Theme.lineHeight
        radius: Theme.radiusControl
        color: Theme.base
        opacity: control.enabled ? 1 : 0.6
        border.width: Theme.borderWidth
        border.color: control.activeFocus && !control.readOnly ? Theme.accent : Theme.controlBorder
    }
}
