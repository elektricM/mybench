import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// On/off switch (section headers, forms): a 28×16 track, accent when on, a white knob.
T.Switch {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 0
    spacing: Theme.spacingS
    hoverEnabled: true
    font.pixelSize: Theme.fontSizeBody

    indicator: Rectangle {
        implicitWidth: 28
        implicitHeight: 16
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? (control.down ? Theme.accentPressed : Theme.accent)
                               : Theme.alpha(Theme.text, control.down ? 0.26 : control.hovered ? 0.2 : 0.15)
        opacity: control.enabled ? 1 : 0.5
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            x: Math.max(2, Math.min(parent.width - width - 2, control.visualPosition * parent.width - width / 2))
            y: 2
            width: 12
            height: 12
            radius: 6
            color: "#FFFFFF"
            Behavior on x {
                enabled: !control.down && Theme.animations
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }
        FocusRing { shown: control.visualFocus; radius: height / 2 + 3 }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
