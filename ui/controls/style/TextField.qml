import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T
import MyBench.Controls

// Text field: `base` fill, hairline border, accent border while focused, the focus ring for
// keyboard focus.
T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    padding: 0
    leftPadding: Theme.spacingS
    rightPadding: Theme.spacingS
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody
    color: enabled ? Theme.text : Theme.textDisabled
    selectionColor: Theme.selection
    selectedTextColor: Theme.selectionText
    placeholderTextColor: Theme.textTertiary
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    // Unfocused fields show the start of their text.
    autoScroll: activeFocus

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
        implicitWidth: 120
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl
        color: Theme.base
        opacity: control.enabled ? 1 : 0.6
        border.width: Theme.borderWidth
        border.color: control.activeFocus && !control.readOnly ? Theme.accent : Theme.controlBorder
        FocusRing { shown: control.activeFocus && (control.focusReason === Qt.TabFocusReason || control.focusReason === Qt.BacktabFocusReason) }
    }
}
