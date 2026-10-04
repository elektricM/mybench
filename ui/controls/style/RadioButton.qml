import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Radio button (dialogs; 2–4 short options in forms use SegmentedControl).
T.RadioButton {
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
        implicitWidth: 14
        implicitHeight: 14
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: width / 2
        color: control.checked ? (control.down ? Theme.accentPressed : Theme.accent) : Theme.base
        border.width: control.checked ? 0 : Theme.borderWidth
        border.color: control.hovered ? Theme.alpha(Theme.text, 0.36) : Theme.controlBorder
        opacity: control.enabled ? 1 : 0.5

        Rectangle {
            anchors.centerIn: parent
            visible: control.checked
            width: 6
            height: 6
            radius: 3
            color: Theme.accentText
        }
        FocusRing { shown: control.visualFocus; radius: width / 2 }
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
