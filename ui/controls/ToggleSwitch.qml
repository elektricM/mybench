import QtQuick
import QtQuick.Templates as T

// On/off switch with an animated knob and an optional label (left or right).
//     ToggleSwitch { text: qsTr("Null offset"); checked: ctl.relative; onToggled: ctl.relative = checked }
T.Switch {
    id: control

    property color accentColor: Theme.accent
    property bool labelFirst: false

    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding
                            + (text.length > 0 ? spacing : 0) + implicitIndicatorWidth)
    implicitHeight: Math.max(Theme.controlHeightS, implicitContentHeight, implicitIndicatorHeight)
    padding: 0
    spacing: 8
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.pixelSize: Theme.fontSizeBody

    indicator: Rectangle {
        implicitWidth: 30
        implicitHeight: 18
        x: control.labelFirst ? control.width - width : 0
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? (control.hovered ? Qt.lighter(control.accentColor, 1.1) : control.accentColor)
                               : (control.hovered ? Theme.surfacePressed : Theme.surfaceHover)
        border.width: control.checked ? 0 : 1
        border.color: Theme.borderStrong
        opacity: control.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }

        Rectangle {
            width: 14
            height: 14
            radius: 7
            y: 2
            x: control.checked ? parent.width - width - 2 : 2
            color: control.checked ? "#FFFFFF" : (Theme.dark ? "#C9CDD6" : "#FFFFFF")
            border.width: control.checked || Theme.dark ? 0 : 1
            border.color: Theme.borderStrong
            Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        }
        FocusRing { shown: control.visualFocus; radius: height / 2 + 3 }
    }

    contentItem: Text {
        leftPadding: control.labelFirst ? 0 : control.indicator.width + (control.text.length > 0 ? control.spacing : 0)
        rightPadding: control.labelFirst ? control.indicator.width + control.spacing : 0
        verticalAlignment: Text.AlignVCenter
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
    }
}
