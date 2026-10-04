import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// Check box (lists and dialogs; forms use Switch): a 14 px box, accent when checked.
T.CheckBox {
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
        readonly property bool on: control.checkState !== Qt.Unchecked
        implicitWidth: 14
        implicitHeight: 14
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: on ? (control.down ? Theme.accentPressed : Theme.accent) : Theme.base
        border.width: on ? 0 : Theme.borderWidth
        border.color: control.hovered ? Theme.alpha(Theme.text, 0.36) : Theme.controlBorder
        opacity: control.enabled ? 1 : 0.5

        Icon {
            anchors.centerIn: parent
            visible: control.checkState === Qt.Checked
            name: "check"
            size: 12
            color: Theme.accentText
        }
        Rectangle {
            anchors.centerIn: parent
            visible: control.checkState === Qt.PartiallyChecked
            width: 8
            height: 2
            radius: 1
            color: Theme.accentText
        }
        FocusRing { shown: control.visualFocus; radius: 5 }
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
