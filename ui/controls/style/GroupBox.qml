import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

T.GroupBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding,
                            implicitLabelWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    spacing: 8
    padding: 12
    topPadding: padding + (implicitLabelWidth > 0 ? implicitLabelHeight + spacing : 0)

    label: Text {
        x: control.leftPadding
        width: control.availableWidth
        text: control.title
        font.pixelSize: Theme.fontSizeBody
        font.weight: Font.DemiBold
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        topPadding: 10
    }

    background: Rectangle {
        radius: Theme.radiusL
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.border
    }
}
