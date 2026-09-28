import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             up.implicitIndicatorHeight, down.implicitIndicatorHeight)

    padding: 0
    leftPadding: 8
    rightPadding: 22
    hoverEnabled: true
    editable: true

    validator: IntValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        clip: width < implicitWidth
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.alpha(Theme.accent, 0.35)
        selectedTextColor: Theme.text
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
        selectByMouse: true
    }

    up.indicator: Item {
        x: control.width - width - 3
        y: 3
        implicitWidth: 16
        implicitHeight: (Theme.controlHeight - 6) / 2
        height: (control.height - 6) / 2
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusXs
            color: control.up.pressed ? Theme.surfacePressed : control.up.hovered ? Theme.surfaceHover : "transparent"
        }
        Icon {
            anchors.centerIn: parent
            name: "chevron-down-line"
            rotation: 180
            size: 10
            color: control.up.indicator.enabled ? (control.up.hovered ? Theme.text : Theme.textMuted) : Theme.textDisabled
        }
    }

    down.indicator: Item {
        x: control.width - width - 3
        y: control.height / 2
        implicitWidth: 16
        implicitHeight: (Theme.controlHeight - 6) / 2
        height: (control.height - 6) / 2
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusXs
            color: control.down.pressed ? Theme.surfacePressed : control.down.hovered ? Theme.surfaceHover : "transparent"
        }
        Icon {
            anchors.centerIn: parent
            name: "chevron-down-line"
            size: 10
            color: control.down.indicator.enabled ? (control.down.hovered ? Theme.text : Theme.textMuted) : Theme.textDisabled
        }
    }

    background: Rectangle {
        implicitWidth: 96
        implicitHeight: Theme.controlHeight
        radius: Theme.radiusM
        color: Theme.surfaceSunken
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.borderStrong : Theme.border
        opacity: control.enabled ? 1 : 0.55
        Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusM + 3 }
    }
}
