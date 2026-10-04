import QtQuick
import QtQuick.Templates as T
import MyBench.Controls

// A titled group: heading text over the content, no box (docs/UI.md: no cards).
T.GroupBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding,
                            implicitLabelWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    spacing: Theme.spacingS
    padding: 0
    topPadding: implicitLabelWidth > 0 ? implicitLabelHeight + spacing : 0

    label: Text {
        width: control.availableWidth
        text: control.title
        font.pixelSize: Theme.fontSizeHeading
        font.weight: Font.DemiBold
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {}
}
