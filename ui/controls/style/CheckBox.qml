import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 2
    spacing: 8
    hoverEnabled: true

    indicator: Rectangle {
        implicitWidth: 16
        implicitHeight: 16
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: Theme.radiusXs + 1
        color: control.checkState !== Qt.Unchecked ? (control.hovered ? Theme.accentHover : Theme.accent)
                                                   : (control.hovered ? Theme.surfaceHover : Theme.surfaceSunken)
        border.width: control.checkState !== Qt.Unchecked ? 0 : 1
        border.color: control.hovered ? Theme.textMuted : Theme.borderStrong
        opacity: control.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Icon {
            anchors.centerIn: parent
            visible: control.checkState === Qt.Checked
            name: "check"
            size: 12
            color: Theme.textOnAccent
        }
        Rectangle {
            anchors.centerIn: parent
            visible: control.checkState === Qt.PartiallyChecked
            width: 8
            height: 2
            radius: 1
            color: Theme.textOnAccent
        }
        FocusRing { shown: control.visualFocus; radius: Theme.radiusXs + 4 }
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
