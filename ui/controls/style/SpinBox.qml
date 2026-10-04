import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Integer spin box: the text field look with ▲/▼ steppers at the right edge (as SiField).
T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             up.implicitIndicatorHeight, down.implicitIndicatorHeight)

    padding: 0
    leftPadding: Theme.spacingS
    rightPadding: Theme.spacingS + 14
    hoverEnabled: true
    editable: true
    font.pixelSize: Theme.fontSizeBody

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        clip: width < implicitWidth
        font.family: control.font.family
        font.pixelSize: control.font.pixelSize
        font.features: Theme.tabularFigures
        color: control.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.selection
        selectedTextColor: Theme.selectionText
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: true
    }

    up.indicator: Item {
        x: control.width - width - 2
        y: 2
        implicitWidth: 14
        implicitHeight: Math.floor((Theme.controlHeight - 4) / 2)
        height: Math.floor((control.height - 4) / 2)
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: control.up.pressed ? Theme.pressedFill : control.up.hovered ? Theme.hoverFill : "transparent"
        }
        Icon {
            anchors.centerIn: parent
            name: "chevron-up"
            size: 10
            color: control.up.indicator.enabled ? (control.up.hovered ? Theme.text : Theme.textSecondary) : Theme.textDisabled
        }
    }
    down.indicator: Item {
        x: control.width - width - 2
        y: control.height / 2
        implicitWidth: 14
        implicitHeight: Math.floor((Theme.controlHeight - 4) / 2)
        height: Math.floor((control.height - 4) / 2)
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: control.down.pressed ? Theme.pressedFill : control.down.hovered ? Theme.hoverFill : "transparent"
        }
        Icon {
            anchors.centerIn: parent
            name: "chevron-down"
            size: 10
            color: control.down.indicator.enabled ? (control.down.hovered ? Theme.text : Theme.textSecondary) : Theme.textDisabled
        }
    }

    background: Rectangle {
        implicitWidth: 96
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusControl
        color: Theme.base
        opacity: control.enabled ? 1 : 0.6
        border.width: Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent : Theme.controlBorder
        FocusRing { shown: control.visualFocus }
    }
}
